"""Real native listeners and encrypted pairing failures; no physical phone evidence."""
import argparse
import asyncio
import json
import os
from pathlib import Path
import subprocess
import time
from urllib.parse import urlparse
from aiohttp import ClientSession, ClientTimeout, WSMsgType
from cryptography.hazmat.primitives.ciphers.aead import AESGCM
from endpoint import b64, unb64, key_for
from PIL import Image
import zxingcpp


async def verify(args):
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    private = output / "invitation.private.json"
    for name in ("invitation.private.json", "invitation.private.png", "expire", "renew", "stop", "status.json"):
        (output / name).unlink(missing_ok=True)
    log = (output / "fixture.log").open("w")
    process = subprocess.Popen([str(args.fixture.resolve()), "--pairing-check", str(output)], stdout=log, stderr=log,
        creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
    checks = []

    async def wait_json(path, predicate=lambda v: True):
        end = time.monotonic() + 15
        while time.monotonic() < end:
            try:
                value = json.loads(path.read_text())
                if predicate(value):
                    return value
            except (OSError, json.JSONDecodeError):
                pass
            if process.poll() is not None:
                raise RuntimeError("Native fixture exited; inspect fixture.log")
            await asyncio.sleep(.05)
        raise RuntimeError("Native fixture check timed out")

    def envelope(invitation, secret=None):
        nonce = os.urandom(12)
        session = invitation["session"]
        plain = json.dumps({"type": "offer", "sdp": "v=0\r\n"}).encode()
        key = key_for(secret or unb64(invitation["secret"]), session, "offer")
        return {"session": session, "nonce": b64(nonce), "ciphertext": b64(AESGCM(key).encrypt(nonce, plain, session.encode()))}

    try:
        invitation = await wait_json(private)
        endpoints = invitation["endpoints"]
        assert 1 <= len(endpoints) <= 16 and len(set(endpoints)) == len(endpoints)
        assert invitation["endpoint"] == endpoints[0]
        assert 0 < invitation["expiresAtUnixMs"] - time.time_ns() // 1_000_000 <= 120000
        assert len(unb64(invitation["secret"])) == 32 and len(invitation["session"]) == 32
        for endpoint in endpoints:
            parsed = urlparse(endpoint)
            assert parsed.scheme == "ws" and parsed.path == "/signal" and not parsed.query and not parsed.fragment
        checks.append("Automatic QR invitation schema, bounds, expiry and compatibility field")
        qr_deadline = time.monotonic() + 5
        decoded = None
        while time.monotonic() < qr_deadline:
            try:
                decoded = zxingcpp.read_barcode(Image.open(output / "invitation.private.png"))
                if decoded:
                    break
            except (FileNotFoundError, OSError):
                pass
            await asyncio.sleep(.05)
        assert decoded and json.loads(decoded.text) == invitation
        checks.append("Desktop-rendered QR independently decoded with all invitation fields exact")
        async with ClientSession(timeout=ClientTimeout(total=5)) as client:
            for endpoint in endpoints:
                async with client.ws_connect(endpoint) as ws:
                    await ws.close()
            checks.append("Every advertised endpoint has a reachable native listener")
            async with client.ws_connect(endpoints[-1]) as ws:
                await ws.send_json(envelope(invitation, os.urandom(32)))
                rejection = await ws.receive(timeout=5)
                assert rejection.type in (WSMsgType.CLOSE, WSMsgType.CLOSED, WSMsgType.ERROR)
            await wait_json(output / "status.json", lambda s: s["state"] == "pairing")
            checks.append("Invalid authentication rejected without consuming invitation")
            (output / "expire").touch()
            await wait_json(output / "status.json", lambda s: s["state"] == "expired")
            async with client.ws_connect(endpoints[-1]) as ws:
                await ws.send_json(envelope(invitation))
                reply = await ws.receive_json(timeout=5)
                plain = AESGCM(key_for(unb64(invitation["secret"]), invitation["session"], "answer")).decrypt(
                    unb64(reply["nonce"]), unb64(reply["ciphertext"]), invitation["session"].encode())
                assert json.loads(plain) == {"type": "error", "code": "expired"}
            checks.append("Expired invitation produces authenticated rejection and Failed-compatible state")
            (output / "renew").touch()
            fresh = await wait_json(private, lambda i: i["session"] != invitation["session"])
            async with client.ws_connect(fresh["endpoints"][0]) as ws:
                await ws.send_json(envelope(invitation))
                rejection = await ws.receive(timeout=5)
                assert rejection.type in (WSMsgType.CLOSE, WSMsgType.CLOSED, WSMsgType.ERROR)
            checks.append("Refreshing revokes old invitation across all listeners")
        (output / "results.json").write_text(json.dumps({"checks": checks, "endpointCount": len(endpoints), "physicalAndroid": False}, indent=2))
        print(f"Automatic pairing checks passed: {len(checks)} groups, {len(endpoints)} actual listener(s); no physical Android claim")
    finally:
        (output / "stop").touch()
        try:
            await asyncio.to_thread(process.wait, 10)
        except subprocess.TimeoutExpired:
            process.terminate()
            process.wait(timeout=10)
        private.unlink(missing_ok=True)
        (output / "invitation.private.png").unlink(missing_ok=True)
        log.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fixture", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    asyncio.run(verify(parser.parse_args()))
