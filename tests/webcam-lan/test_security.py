import json
import secrets
import unittest
from cryptography.hazmat.primitives.ciphers.aead import AESGCM
from cryptography.exceptions import InvalidTag
from endpoint import Invitation, b64, unb64, key_for, create_app
from aiohttp.test_utils import TestClient, TestServer
from aiohttp import WSMsgType


def offer(invitation, secret=None):
    nonce = secrets.token_bytes(12)
    data = AESGCM(key_for(secret or invitation.secret, invitation.session, "offer")).encrypt(
        nonce, json.dumps(dict(type="offer", sdp="test-only")).encode(), invitation.session.encode())
    return dict(session=invitation.session, nonce=b64(nonce), ciphertext=b64(data))


class Security(unittest.TestCase):
    def test_accept_and_replay(self):
        invitation = Invitation()
        envelope = offer(invitation)
        self.assertEqual(invitation.accept(envelope)["type"], "offer")
        with self.assertRaises(ValueError):
            invitation.accept(envelope)

    def test_expired(self):
        invitation = Invitation(-1)
        with self.assertRaises(ValueError):
            invitation.accept(offer(invitation))

    def test_revoked(self):
        invitation = Invitation()
        invitation.revoked = True
        with self.assertRaises(ValueError):
            invitation.accept(offer(invitation))

    def test_wrong_secret_does_not_consume(self):
        invitation = Invitation()
        with self.assertRaises(InvalidTag):
            invitation.accept(offer(invitation, secrets.token_bytes(32)))
        self.assertFalse(invitation.used)
        invitation.accept(offer(invitation))

    def test_wrong_session(self):
        invitation = Invitation()
        envelope = offer(invitation)
        envelope["session"] = Invitation().session
        with self.assertRaises(ValueError):
            invitation.accept(envelope)
        self.assertFalse(invitation.used)

    def test_tamper(self):
        invitation = Invitation()
        envelope = offer(invitation)
        data = bytearray(unb64(envelope["ciphertext"]))
        data[0] ^= 1
        envelope["ciphertext"] = b64(data)
        with self.assertRaises(InvalidTag):
            invitation.accept(envelope)
        self.assertFalse(invitation.used)

    def test_answer_direction_and_authentication(self):
        invitation = Invitation()
        envelope = invitation.reply(dict(type="answer", sdp="test-only"))
        data = unb64(envelope["ciphertext"])
        aad = invitation.session.encode()
        self.assertEqual(json.loads(AESGCM(key_for(invitation.secret, invitation.session, "answer"))
            .decrypt(unb64(envelope["nonce"]), data, aad))["type"], "answer")
        with self.assertRaises(InvalidTag):
            AESGCM(key_for(invitation.secret, invitation.session, "offer")).decrypt(unb64(envelope["nonce"]), data, aad)


class EndpointSecurity(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.invitation = Invitation()
        self.client = TestClient(TestServer(create_app(self.invitation, "https://webcam.example")))
        await self.client.start_server()

    async def asyncTearDown(self):
        await self.client.close()

    async def test_wrong_origin(self):
        response = await self.client.get("/signal", headers={"Origin": "https://untrusted.example"})
        self.assertEqual(response.status, 403)
        self.assertFalse(self.invitation.used)

    async def test_unauthenticated_message(self):
        socket = await self.client.ws_connect("/signal", headers={"Origin": "https://webcam.example"})
        await socket.send_json({})
        message = await socket.receive(timeout=2)
        self.assertEqual(message.type, WSMsgType.CLOSE)
        self.assertEqual(message.data, 1008)
        self.assertFalse(self.invitation.used)


if __name__ == "__main__":
    unittest.main()
