# WebCam Order 1 feasibility harness

This is an isolated Windows endpoint and browser test. It is not a Movie receiver,
Blazor application, deployment package, or production authentication library.
Nothing here changes website files, the production service, or Movie runtime code.

## What it exercises

The endpoint listens on one explicitly supplied LAN interface. A genuine HTTPS
document is loaded in a disposable browser with normal certificate validation.
The test replaces only that browser document with its fixture and injects
`client.js`. The fixture therefore has the real HTTPS origin/security context,
but **is not actually served by lagdaemon.com**. This distinction matters: this
does not test deployed Blazor assets, production CSP integration, or Android.

A default-permission WebSocket probe is followed by a test-only CDP permission
grant in the disposable context. This tests the browser enforcement boundary;
it does not demonstrate a human accepting the Android permission prompt.
No certificate exceptions, insecure-origin overrides, disabled web security, or
disabled LNA enforcement flags are used. The Chromium sandbox is enabled.

The browser generates canvas video and oscillator audio (no physical camera or
microphone) and sends them to aiortc on Windows. A data channel returns decoded
frame/sample counts. ICE servers are empty on both sides. Selected candidate
types must both be `host`. The run is on one PC using its LAN interface, not a
two-device Wi-Fi or physical-latency test.

## Reproduce

Use Python 3.12 and Node.js with Playwright available. All outputs and dependency
installs must go on an explicitly selected development drive. Do not install into
the shared LLVM/vcpkg tree or a production environment. Only binary wheels are
needed; no native dependency build is required.

1. In Movie's existing `build/webcam-feasibility/` directory, install dependencies:
   `python -m pip install --only-binary=:all: --target <test-python-packages> -r tests/webcam-lan/requirements.txt`.
2. Set `PYTHONPATH` to that package directory and `PYTHONDONTWRITEBYTECODE=1`.
   Redirect `TEMP`, `TMP`, and pip cache to the development drive.
3. Run `python tests/webcam-lan/test_security.py`.
4. Set `PYTHON_EXECUTABLE` to Python 3.12, `PLAYWRIGHT_MODULE` to the installed
   Playwright module (or use normal Node resolution), and `BROWSER_EXECUTABLE`
   to an installed Chromium browser. No browser download is required.
5. Set `WEBCAM_TEST_BIND` to this PC's actual LAN IPv4 address. Optionally set
   `WEBCAM_TEST_OUTPUT` to a private development output folder and
   `WEBCAM_TEST_ORIGIN` to a trusted HTTPS origin under your control.
6. Run `node tests/webcam-lan/run-browser.cjs`, redirecting its output to a log.
   The script starts/stops its own endpoint and browser. Read `browser-results.json`.

For a manual phone test, run `endpoint.py --bind <LAN-IP> --invitation <private-file>`
and serve the test client/UI from an approved trusted HTTPS origin. The current
order does not authorize publishing these files to production. Use the invitation
only locally, never in query strings, logs, issues, or commits. A phone needs LAN
reachability and browser permission; firewall/client isolation may prevent access.
No firewall settings are changed by this harness.

## Prototype security model

- The invitation contains a cryptographically random 256-bit secret and random
  session identifier. This is a high-entropy out-of-band secret, **not** a short PIN.
- HKDF-SHA256 derives separate AES-256-GCM keys for offer and answer. Random
  96-bit nonces and the session identifier as authenticated data bind each envelope.
- The encrypted SDP includes WebRTC DTLS fingerprints. The secret is never sent
  on the WebSocket. Answer authentication prevents accepting substituted SDP.
- A valid offer consumes the invitation synchronously before SDP processing;
  expiry/revocation/replay fail closed. Invalid authentication does not consume it.
- Origin is allowlisted. Origin checking is defense in depth, not authentication:
  native clients can forge it. No website cookies or bearer tokens are reused.
- Signaling has bounded message size and timeouts. The endpoint retains no media.
  WebRTC supplies DTLS/SRTP media protection independently of `ws://` signaling.

The small two-message protocol is a feasibility construction, not an audited
production protocol. A LAN attacker can still observe connection metadata or
block traffic. Production needs explicit user confirmation, invitation delivery
UI, rate/connection limits, revocation UI, resource accounting, and review of
the complete protocol. Never reuse this one-off invitation for reconnect.
The harness intentionally uses non-trickle ICE and does not implement renewal,
clock synchronization, recording, device identity persistence, or multi-phone use.
