"""Isolated feasibility endpoint. Not a Movie receiver or production service."""
import argparse
import asyncio
import base64
import json
import os
import secrets
import time
from pathlib import Path

from aiohttp import web, WSMsgType
from aiortc import RTCPeerConnection, RTCConfiguration, RTCSessionDescription
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.kdf.hkdf import HKDF
from cryptography.hazmat.primitives.ciphers.aead import AESGCM

PROTOCOL = "djehuti-webcam-feasibility-v1"


def b64(value):
    return base64.urlsafe_b64encode(value).decode().rstrip("=")


def unb64(value):
    return base64.urlsafe_b64decode(value + "=" * (-len(value) % 4))


def key_for(secret, session, direction):
    return HKDF(algorithm=hashes.SHA256(), length=32, salt=session.encode(),
                info=(PROTOCOL + ":" + direction).encode()).derive(secret)


class Invitation:
    def __init__(self, lifetime=120):
        self.session = secrets.token_hex(16)
        self.secret = secrets.token_bytes(32)
        self.deadline = time.monotonic() + lifetime
        self.used = False
        self.revoked = False

    def accept(self, envelope):
        if self.revoked or self.used or time.monotonic() >= self.deadline:
            raise ValueError("invitation unavailable")
        if envelope.get("session") != self.session:
            raise ValueError("wrong session")
        nonce = unb64(envelope["nonce"])
        if len(nonce) != 12:
            raise ValueError("wrong nonce length")
        data = AESGCM(key_for(self.secret, self.session, "offer")).decrypt(
            nonce, unb64(envelope["ciphertext"]), self.session.encode())
        offer = json.loads(data)
        if offer.get("type") != "offer" or not isinstance(offer.get("sdp"), str):
            raise ValueError("invalid offer")
        # Consume synchronously before any network/SDP await: concurrent replays lose.
        self.used = True
        return offer

    def reply(self, answer):
        nonce = secrets.token_bytes(12)
        payload = AESGCM(key_for(self.secret, self.session, "answer")).encrypt(
            nonce, json.dumps(answer).encode(), self.session.encode())
        return dict(session=self.session, nonce=b64(nonce), ciphertext=b64(payload))


def create_app(invitation, origin, results=None):
    app = web.Application(client_max_size=128 * 1024)
    peers = set()
    totals = dict(videoFrames=0, audioFrames=0, audioSamples=0, connections=0)

    async def receive(track):
        try:
            while True:
                frame = await track.recv()
                totals[track.kind + "Frames"] += 1
                if track.kind == "audio":
                    totals["audioSamples"] += frame.samples
                if results and (totals[track.kind + "Frames"] % 30 == 0):
                    results.write_text(json.dumps(totals), encoding="utf-8")
        except Exception:
            # End-of-stream is expected when the bounded test closes its peer.
            pass

    async def signal(request):
        if request.headers.get("Origin") != origin:
            raise web.HTTPForbidden(text="origin rejected")
        # No permissive CORS; browser WebSockets use explicit Origin validation.
        if invitation.used or invitation.revoked or time.monotonic() >= invitation.deadline:
            raise web.HTTPForbidden(text="invitation unavailable")
        ws = web.WebSocketResponse(max_msg_size=128 * 1024, heartbeat=10)
        await ws.prepare(request)
        pc = None
        tasks = []
        try:
            message = await asyncio.wait_for(ws.receive(), timeout=10)
            if message.type != WSMsgType.TEXT:
                raise ValueError("text envelope required")
            offer = invitation.accept(json.loads(message.data))
            pc = RTCPeerConnection(RTCConfiguration(iceServers=[]))
            peers.add(pc)
            totals["connections"] += 1

            @pc.on("track")
            def on_track(track):
                tasks.append(asyncio.create_task(receive(track)))

            @pc.on("datachannel")
            def on_channel(channel):
                @channel.on("message")
                def on_message(message):
                    if message == "stats":
                        channel.send(json.dumps(totals))

            await pc.setRemoteDescription(RTCSessionDescription(**offer))
            await pc.setLocalDescription(await pc.createAnswer())
            await ws.send_json(invitation.reply(dict(type="answer", sdp=pc.localDescription.sdp)))
            # Keep signaling alive; bounded session, no persistence or auto-reconnect.
            async with asyncio.timeout(90):
                async for message in ws:
                    if message.type == WSMsgType.TEXT and message.data == "stop":
                        break
        except (ValueError, KeyError, TimeoutError, json.JSONDecodeError):
            await ws.close(code=1008, message=b"session rejected")
        except Exception:
            # Never include SDP, invitation material, or credentials in logs/replies.
            await ws.close(code=1008, message=b"negotiation rejected")
        finally:
            if pc:
                await pc.close()
                peers.discard(pc)
            for task in tasks:
                task.cancel()
            if tasks:
                await asyncio.gather(*tasks, return_exceptions=True)
        return ws

    async def cleanup(_):
        await asyncio.gather(*(pc.close() for pc in list(peers)))

    app.router.add_get("/signal", signal)
    app.on_shutdown.append(cleanup)
    return app


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bind", required=True, help="Explicit LAN interface IPv4")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--origin", default="https://lagdaemon.com")
    parser.add_argument("--invitation", type=Path, required=True, help="Private local test file; never commit")
    parser.add_argument("--lifetime", type=int, default=120)
    parser.add_argument("--results", type=Path, help="Explicit private development results file (counts only)")
    parser.add_argument("--qr", type=Path, help="Private invitation QR SVG; explicitly requested test output only")
    args = parser.parse_args()
    invitation = Invitation(args.lifetime)
    args.invitation.write_text(json.dumps(dict(protocol=PROTOCOL, session=invitation.session,
        secret=b64(invitation.secret), endpoint=f"ws://{args.bind}:{args.port}/signal")), encoding="utf-8")
    if args.qr:
        import qrcode
        import qrcode.image.svg
        qrcode.make(args.invitation.read_text(encoding="utf-8"), image_factory=qrcode.image.svg.SvgPathImage).save(args.qr)
    print("Feasibility endpoint ready; invitation written locally. No media files are saved.", flush=True)
    web.run_app(create_app(invitation, args.origin, args.results), host=args.bind, port=args.port, access_log=None)
