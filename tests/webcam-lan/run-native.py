"""Synthetic aiortc sender against the native Movie receiver fixture, not a phone test."""
import argparse
import asyncio
import json
import os
from pathlib import Path
import subprocess
import time
from aiohttp import ClientSession, ClientConnectionError, WSServerHandshakeError
from aiortc import RTCPeerConnection, RTCConfiguration, RTCSessionDescription, VideoStreamTrack, AudioStreamTrack
from endpoint import b64, unb64, key_for
from cryptography.hazmat.primitives.ciphers.aead import AESGCM


async def run(args):
    args.output.mkdir(parents=True, exist_ok=True)
    invitation_path = args.invitation_file or args.output / "invitation.private.json"
    if invitation_path.exists() and not args.invitation_file:
        invitation_path.unlink()
    log = (args.output / "native-fixture.log").open("w")
    process = None if args.invitation_file else subprocess.Popen([str(args.fixture), str(args.output.resolve()), args.bind], stdout=log, stderr=log,
                               creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
    peer = RTCPeerConnection(RTCConfiguration(iceServers=[]))
    try:
        deadline = time.monotonic() + 20
        while not invitation_path.exists():
            if (process and process.poll() is not None) or time.monotonic() > deadline:
                raise RuntimeError("Native fixture did not produce invitation; inspect native-fixture.log")
            await asyncio.sleep(.1)
        invitation = json.loads(invitation_path.read_text())
        secret, session = unb64(invitation["secret"]), invitation["session"]
        peer.addTrack(VideoStreamTrack())
        peer.addTrack(AudioStreamTrack())
        channel = peer.createDataChannel("webcam-timing-v1")

        @channel.on("message")
        def message(text):
            received = time.monotonic_ns()
            if not isinstance(text, str):
                return
            value = json.loads(text)
            if value.get("type") == "clock-probe":
                channel.send(json.dumps(dict(type="clock-reply", generation=session, id=value["id"],
                    hostSend100ns=value["hostSend100ns"], receiveNs=str(received), wallMs=str(time.time_ns() // 1000000),
                    cameraTimestampNs="0", sendNs=str(time.monotonic_ns()))))

        await peer.setLocalDescription(await peer.createOffer())
        nonce = os.urandom(12)
        encrypted = AESGCM(key_for(secret, session, "offer")).encrypt(nonce,
            json.dumps(dict(type="offer", sdp=peer.localDescription.sdp)).encode(), session.encode())
        async with ClientSession() as client:
            async with client.ws_connect(invitation["endpoint"], origin="https://lagdaemon.com") as rejected:
                wrong = os.urandom(32)
                bad = AESGCM(key_for(wrong, session, "offer")).encrypt(nonce,
                    json.dumps(dict(type="offer", sdp=peer.localDescription.sdp)).encode(), session.encode())
                await rejected.send_json(dict(session=session, nonce=b64(nonce), ciphertext=b64(bad)))
                failure = await rejected.receive(timeout=5)
                if failure.type.name not in ("CLOSE", "CLOSED", "ERROR"):
                    raise RuntimeError("Native endpoint accepted unauthenticated offer")
            async with client.ws_connect(invitation["endpoint"], origin="https://lagdaemon.com") as socket:
                await socket.send_json(dict(session=session, nonce=b64(nonce), ciphertext=b64(encrypted)))
                envelope = await socket.receive_json(timeout=20)
                answer = json.loads(AESGCM(key_for(secret, session, "answer")).decrypt(
                    unb64(envelope["nonce"]), unb64(envelope["ciphertext"]), session.encode()))
                await peer.setRemoteDescription(RTCSessionDescription(**answer))
                try:
                    async with client.ws_connect(invitation["endpoint"], origin="https://lagdaemon.com") as replay:
                        failure = await replay.receive(timeout=5)
                        if failure.type.name not in ("CLOSE", "CLOSED", "ERROR"):
                            raise RuntimeError("Consumed invitation accepted on a new connection")
                except (ClientConnectionError, WSServerHandshakeError):
                    pass  # The native listener may reject a consumed invitation before upgrading HTTP.
                deadline = time.monotonic() + 65
                while (not process or process.poll() is None) and time.monotonic() < deadline:
                    if args.invitation_file and (args.output / "stop-sender").exists():
                        break
                    await asyncio.sleep(.25)
        if process and process.poll() is None:
            raise RuntimeError("Native fixture did not complete within deadline")
        if process and process.returncode:
            raise RuntimeError("Native receiver checks failed; inspect native-results.json and native-fixture.log")
        print("Native receiver synthetic integration checks passed (not physical Android evidence)" if process else
              "External Movie synthetic sender finished; inspect the separate Movie workflow results")
    finally:
        await peer.close()
        if process and process.poll() is None:
            process.terminate()
        if process:
            process.wait(timeout=10)
        log.close()
        if invitation_path.exists():
            invitation_path.unlink()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fixture", type=Path)
    parser.add_argument("--invitation-file", type=Path, help="Use an already-running Movie; stop via output/stop-sender")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--bind", required=True)
    arguments = parser.parse_args()
    if not arguments.fixture and not arguments.invitation_file:
        parser.error("Supply --fixture or --invitation-file")
    asyncio.run(run(arguments))
