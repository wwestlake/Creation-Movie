# WebCam / CreationMovie — Order 1 feasibility report

Date: October 29, 2026. Scope: isolated feasibility investigation only.

## Finding and review gate

**Direct, authenticated local WebRTC is technically demonstrated on Windows Edge
156 using the PC's LAN interface. Android Chrome operation is not demonstrated.**
The Android permission/version gate remains open before claiming this architecture
works on the intended phone. Do not treat this report as authorization to begin
Order 2 or Order 3, or as completion of phone-to-Movie recording.

The prototype is `tests/webcam-lan/`. It neither builds nor changes Movie's
production receiver, website, AWS, firewall configuration, or certificate stores.
No merge or deployment was performed. Implementation/production readiness of
the complete WebCam product remains 0%; this is feasibility evidence only.

## Environment and method

- Windows PC, Edge 156.0.4314.50 (Chromium 156), headless Playwright browser in a
  disposable profile under the existing Movie `build/` directory. Sandbox enabled.
- Python 3.12.14, aiortc 1.14.0, aiohttp 3.13.2. Binary wheels installed only beneath
  Movie `build/webcam-feasibility/`; no LLVM/vcpkg changes or application rebuild.
- Native Python WebRTC endpoint bound to the explicitly selected Ethernet LAN
  IPv4 address. Browser and endpoint run on **one PC**, not two Wi-Fi devices.
- The browser loads the real `https://lagdaemon.com` document with normal TLS
  validation. The fixture replaces only the disposable browser document and
  injects the test script. It uses that real secure origin, but **the harness is
  not served by production**. No production files or responses were modified.
- Default headless permission denial is observed. An origin/context-scoped CDP
  permission grant then represents the allowed case; no human prompt was tested.
  No certificate bypass, custom certificate, insecure-origin trust override,
  disabled web security, or disabled LNA checks are used.
- Canvas video (320 x 240, requested 15 fps) and oscillator audio are synthetic.
  No real camera/microphone permission or physical sensor accuracy is tested.
- ADB lists zero attached Android devices. The SDK client restarted an existing
  version-mismatched ADB daemon while querying devices; no device was modified.

## Connection architecture exercised

`HTTPS-origin JS -> permission-gated local ws://<LAN-IP>:8765/signal -> native
Windows aiortc endpoint -> authenticated SDP/ICE -> direct WebRTC media`.

`ws://` itself is not confidential. The feasibility exchange encrypts/authenticates
offer and answer with AES-256-GCM using separate directional HKDF-SHA256 keys
derived from a random 256-bit out-of-band invitation. The secret is never sent on
the socket. Authenticated SDP binds the DTLS fingerprints; WebRTC supplies its
own DTLS/SRTP protection for media. Do not replace the high-entropy invitation
with a guessable PIN. This construction needs production security review.

Both peers use empty ICE-server lists. No public signaling, external STUN, TURN,
AWS media path, or separate desktop service is required by this construction.
The Python process is only a stand-in endpoint; it is not a proposed production
service or proof of a particular C++ WebRTC library's capabilities.

## Test results

Observed successful sandboxed desktop run:

| Check | Result |
| --- | --- |
| Real HTTPS origin and normal TLS verification | Passed; secure context, Web Crypto, and media-device API present |
| LAN WebSocket without permission | Blocked: `ERR_BLOCKED_BY_LOCAL_NETWORK_ACCESS_CHECKS` |
| LAN WebSocket with test-only permission grant | Connected without certificate exception |
| Wrong invitation secret | Rejected; valid invitation remained usable |
| Authenticated SDP and ICE/DTLS negotiation | Passed |
| Native decoded video | 75 frames during approximately five seconds |
| Native decoded audio | 246 frames / 236,160 samples |
| ICE selected pair | Succeeded; host/host candidates, UDP |
| Negotiated codecs (final run) | VP8 video and Opus audio; H.264 was not tested |
| Time to data-channel open | 4,298 ms in final run (7,093 ms in earlier run), including non-trickle ICE gathering; not a latency benchmark |
| Reuse of consumed invitation | Rejected on a new connection (HTTP 403) |
| Python security tests | 9 passed: expiry, revocation, replay, wrong secret, wrong session, tamper, directional answer authentication, Origin rejection, unauthenticated message rejection |
| JS syntax checks | Client and browser runner passed |

The browser log also contains an unrelated unauthenticated website HTTP 401 from
loading the initial document, the expected denied-permission error, and the
expected consumed-invitation HTTP 403. These are not failed media checks.

Local raw evidence is under ignored `build/webcam-feasibility/`:
`browser-results.json`, `browser.log`, `security.log`, and `endpoint.log`.
The private invitation file is deleted by the runner on normal cleanup and is
never committed. See the harness README for reproduction and manual-phone limits.

## Browser security and compatibility findings

Chrome's [147 release notes](https://developer.chrome.com/release-notes/147)
document Local Network Access restrictions for WebSockets. Current
[Chrome guidance](https://developer.chrome.com/docs/modern-web-guidance/guides/security/local-network-access)
describes permission-gated HTTPS-to-local `ws://` access when the URL identifies
a private IP or recognized local hostname. A private IPv4 literal is the narrow
initial recommendation; do not depend on public-looking DNS resolving privately.

This is an exception governed by browser policy, not a universal HTTPS/WebSocket
rule. Use explicit user interaction and explain the LAN permission before
connecting. Old PNA preflight advice alone is not a solution. CORS response
headers do not authenticate a WebSocket peer; the endpoint checks Origin and
cryptographic authorization separately.

The installed Playwright wrapper did not recognize `local-network` in its
high-level grant API. Its documented CDP `Browser.setPermission` descriptor did;
the final runner uses that normal permission test interface in its disposable
context. This is automated granted/denied-state testing, not browser permission
UX testing. It makes no changes to the user's regular browser profile.

Proposed Android baseline: Chrome 147 or newer **subject to physical verification
of the exact version/build**. Edge results do not establish Android support,
rollout state, enterprise policy, or other browsers. Camera/microphone permission
and secure-context rules still apply independently. iOS/Safari is not verified.

## Recommended eventual pairing/signaling contract

1. User enables pairing in Movie. Bind only the selected LAN interface and expose
   the listener for a bounded time. No automatic router forwarding or cloud relay.
2. Movie displays a QR invitation containing version, instance/session identifiers,
   explicit LAN endpoint, expiry information, and a high-entropy one-use secret.
   Keep secret material in the URL fragment, remove it after import, and exclude
   it from analytics, logs, referrers, and project persistence.
3. WebCam requests LAN permission from a user action. Authenticate and protect
   negotiation using a reviewed protocol binding the invitation, both peers,
   negotiated DTLS fingerprints, and connection generation. Movie should expose
   clear approval/revocation controls. Never send the invitation as a plaintext
   bearer token over `ws://`.
4. Negotiate video plus optional audio, send media directly over WebRTC, and report
   actual selected format and connection state. LAN permission is not pairing.
5. Stop closes the peer and releases tracks. Reconnect requires a new authorized
   connection generation; the consumed invitation must not authorize it again.

The prototype implements only a bounded, single-use, two-message encrypted
offer/answer exchange with non-trickle ICE. It does not implement QR UI, explicit
Movie confirmation, persistent device trust, grant renewal, production rate
limits, clock mapping, recording, or a finished authentication protocol. Origin
can be forged by non-browser clients; the secret is the authentication boundary.
LAN attackers can still block traffic or observe connection metadata.

## Movie integration boundary

The source review confirms `MovieVisualCapture`/`MovieCaptureStream` are suitable
entry points for immutable decoded samples and shared subscriptions. The existing
coordinator/writers/VFS remain the recording authority. No integration is added
by this order. Current writer behavior rejects source-epoch changes, so reconnect
segmentation and clock mapping still require Order 3 work. The existing Remote
Receiver's `NO_MEDIA` build is not a verified production receiver dependency.

## Remaining decisions and unverified work

- Obtain a physical Android Chrome run on phone and PC over Wi-Fi. Verify the real
  permission prompt, direct reachability, expiry/replay rejection, camera/mic
  permissions, and selected local candidate pair. Until then the Android gate is
  open; no architectural blocker was found on the tested desktop.
- Approve the protected local-WebSocket signaling direction and production
  authentication review before freezing Order 2's interface. Browser-blocked
  devices should fail clearly; do not silently introduce cloud signaling or
  certificate-warning bypasses.
- The test script's trusted-origin injection is not a deployed Blazor/CSP test.
  Decide an approved HTTPS test-delivery path if physical testing is needed before
  the website app is approved for deployment. No production change is authorized.
- A fresh load of the hosted app requires internet. Offline application loading
  requires separately agreed caching behavior; local signaling/media do not by
  themselves provide offline web assets.
- No physical latency/sync/drift, sustained Wi-Fi test, firewall/client isolation,
  camera switch, sleep/background lifecycle, or Movie recording was validated.
  No claim is made against the earlier 20 ms / one-frame proposed timing targets.

## Repository protection

Website `develop` and its existing uncommitted Remote/navigation/deployment work
were left unchanged. Only this report and the isolated harness are added on a
fresh Movie feature branch from merged Movie master. No app or shared-library
source files, production configuration, or Suite gitlinks are changed.
Issue: https://github.com/wwestlake/Creation-Movie/issues/44.
Commit and PR are recorded in the PR/issue closeout rather than self-referentially
embedding a commit hash in its own contents. Bill retains merge/deploy authority.
