# Djehuti WebCam — Order 3 implementation and validation

Date: October 31, 2026. Authorized scope: native Android client and Movie receiver;
reviewable commits/PRs only. No merge, production deployment, or release.

## Android client

The existing Remote Android project is repurposed, retaining its package identity.
Kotlin/Compose, camera/QR permission infrastructure, CameraX/ML Kit scanner,
OkHttp, EGL and libwebrtc factories are reused. Obsolete website authentication,
project/session selection and asset transfer are removed. The web Remote is not
changed. The launcher opens WebCam with private invitation entry/QR scanning,
front/rear selection, one Camera2 capturer feeding preview and transmission,
microphone transmission control, explicit status and stop/background cleanup.

The maintained-distribution candidate remains Stream WebRTC Android 1.3.9, the
version documented upstream and resolved from Maven Central. No hosted Stream
SDK is used. VP8/Opus codec preference is explicit. An initially absent microphone
requires a fresh connection to add; an opened microphone can mute/unmute its
transmission. A failed session requires Stop and a new invitation. This initial
client stops rather than recording covertly when backgrounded.

Android implementation and exact Galaxy S23 Ultra procedure:
`apps/CreationRemoteAndroid/docs/WEBCAM_ORDER_3.md` in the Suite checkout.
Review: https://github.com/wwestlake/Creation-Remote-Android/pull/3.

## Pairing and transport

Movie Track Inspector offers Pair/disconnect. The user chooses a private IPv4
interface; Movie listens there on port 8765 and copies a private, expiring
invitation for transfer to the phone. The command API permits an explicit port.
The isolated endpoint can also generate a local QR SVG for Stage B testing.
Movie's UI currently transfers invitation text; it does not render a QR itself.

One-use 256-bit invitations retain Order 1's directional HKDF-SHA256/AES-256-GCM
offer/answer profile. The secret is not sent on the socket; authenticated SDP
binds DTLS fingerprints. Expiry, consumption, revocation by Stop/new invitation,
and generation checks gate peer creation. Invalid authentication does not consume
the invitation. Signaling messages, accepted sockets and media queues are bounded.
The handshake is not a substitute for a full production security review.

No public signaling, external STUN/TURN, cloud account, or media relay is used.
Movie binds the selected LAN interface and requires selected ICE candidates to
be private IPv4 hosts before recording readiness. The code uses pinned
libdatachannel v0.24.6, commit `d9617a53201615ef729e99dc39ad6736982d0346`,
with media enabled. VP8 RTP reassembly is bounded; Opus packets feed the existing
FFmpeg decoders. OpenSSL is a discovered dependency; the current machine build
uses its existing 3.3.1 installation. Dependency patch/support status must be
reviewed before distributing a production package; this build does not certify
the dependencies vulnerability-free. No installed dependency or service was
replaced, and LLVM/vcpkg were not modified.

## Timing and bounded media handling

The contract was established before receiver implementation in
[WEBCAM_TIMING_CONTRACT.md](WEBCAM_TIMING_CONTRACT.md).

Authenticated data-channel probes correlate phone monotonic and wall observations
to Movie's monotonic clock. RTCP sender reports anchor each stream's original RTP
clock; SSRC/generation changes are explicit. Original extended RTP positions and
converted source times are preserved in CaptureSample alongside mapped host time
and estimated transport-clock uncertainty. Missing anchors or failed plausibility
checks leave preview-only readiness; arrival time never replaces source timing.
Repeated probes update offset estimates, and the existing audio writer retains
gradual drift correction. Physical sensor latency is not included in the declared
transport-clock estimate and must be measured in Order 4.

Reception/decode run off the GUI thread. Network callbacks enqueue into a separate
bounded queue without waiting for decode or VFS. The pending/in-flight batches
are each bounded to 8 MiB; VP8 fragments have a separate 2 MiB/256-packet bound.
Lost video fragments are explicit discontinuities. Missing audio RTP positions
insert explicitly marked silence up to two seconds; larger gaps fail the affected
source. Late audio is dropped explicitly. Overflow/decoder failures latch until a
new authorized generation. Existing per-track 64 MiB recording queues remain.
Negotiated video-orientation rotation is applied before exposing decoded frames;
actual phone orientation/switch behavior remains a physical validation item.

## Movie source and recording integration

`MovieWebCam` owns transport/decode only. `MovieVisualCapture` merges WebCam video
and microphone endpoints into its existing catalog, validation, previews, status
and recording subscriptions. Multiple tracks share one reception/decoder but
retain independent queues/outputs. No tracks are auto-created or auto-armed.
Source assignments use existing camera/microphone categories and stable endpoint
selectors, so the document schema and existing editing/export paths are unchanged.

Microphone levels and optional listening use the existing bounded monitor design,
extracted unchanged into `MovieCaptureMonitor` for native and remote adapters.
Mute/reassignment invalidates queued sound by generation. The output callback
does not lock, allocate, decode, or perform network/VFS calls.

The existing coordinator writes ordinary per-track VFS media and publishes owner
placements. Source disconnect/format/epoch failure retains a usable prefix while
unrelated local tracks can continue. A fresh invitation is currently blocked
during an active take: stop/finalize before reconnecting. Automatic reconnect
segment continuation during a still-running multitrack take is unfinished
engineering work for the interruption acceptance work, not an accepted permanent
limitation. Existing media/placements must never be shifted to hide the gap.

API commands are `movie.webcam.invite`, `.status`, and `.stop`; UI and commands use
the same implementation. Invitation data is excluded from WebCam status and
project source assignments. Keep private invitation responses out of diagnostics.

## Validation and remaining acceptance

- Android Debug build and two invitation/crypto unit checks pass; development APK
  signing verification passes. Its final artifact fingerprint is recorded in the
  Android report after the final permission-flow update.
- Movie Debug builds of the application and isolated receiver fixture pass during
  development. Final source-version verification and native integration results
  are recorded in the closeout below.
- Nine Order 1 security tests pass after adding count-only physical endpoint
  diagnostics. The private QR SVG generation smoke check passes.
- No Galaxy S23 Ultra physical test has been performed: ADB lists only an offline
  emulator. No real camera/microphone, Wi-Fi, physical A/V alignment, cleanup,
  thermal behavior or reconnect result is inferred from synthetic media.

Order 4 must measure real phone/local A/V alignment and sustained drift, exercise
jitter/loss/reordering, Wi-Fi interruptions, device lifecycle and source changes,
and verify reconnect segmentation with continuing local tracks. Complete security
and dependency patch review before production distribution. Do not begin Order 4
without a separate work order.

## Integration state

Android and Movie changes remain independently reviewable. Do not merge either
PR, update umbrella pins to unmerged feature commits, deploy, or publish releases.
After approved app merges, update the umbrella gitlinks through its PR process.
The separate website and its existing uncommitted work are outside this change.

## Verified closeout evidence

| Check | Result |
| --- | --- |
| Movie Debug app + native fixture build | Passed, `build/webcam-boundaries-build.log`, exit 0 |
| Native authenticated synthetic sender | Passed; invalid secret and consumed-invitation connection rejected; direct private host transport verified |
| Source timing readiness | Both RTP/RTCP anchors present, six clock observations; declared transport uncertainty 27,850 x 100 ns (2.785 ms), not physical accuracy |
| Shared source / unsubscribe | First video subscriber 61 frames, second 30; removing second did not stop first; audio subscriber 102 blocks |
| Native decode | 131 video frames / 205 audio blocks observed during fixture run |
| Monitor and timing boundary checks | Passed, `build/webcam-timing-unit.log`: gain/mute/generation, probe outliers, wall-clock step, RTP wrap, missing anchors and bounded receive queue |
| Actual Movie synthetic workflow | Five groups passed: catalog, three VFS recordings, undo/redo, save/reopen, fresh authenticated reconnect |
| Recorded media | Two video outputs, 95 packets each; audio 158 blocks; three usable outputs, zero reported gaps/discontinuities |
| Recording queues | Peaks 11,059,200 / 11,059,200 / 122,880 bytes, each below 64 MiB |
| Document regression executable | Passed, `build/webcam-document-tests.log`, exit 0; document/effect source is unchanged |
| Order 1 security checks | Nine passed; `build/webcam-security.log` |
| Isolated endpoint QR | Local SVG generation smoke check passed |
| Android final build / signing | Debug build, two JVM tests, apksigner verification passed; Android report records final APK fingerprint |
| Physical phone tests | Not run; no connected S23 Ultra |

Raw native evidence is in ignored `build/webcam-native-test/native-results.json`;
Movie evidence is `build/webcam-movie-test/movie-results.json` and
`take-results.json`. Synthetic preview audio was drained by the fixture's
non-real-time message loop; its monitor-drop counter is visible and bounded, not
a real audio-output stability measurement. Record queues are independent.
`tests/webcam-lan/run-native.py` and `Verify-MovieWebCam.ps1` reproduce the focused
checks. The latter preserves the original open project, uses a new named test
project, and leaves ordinary recorded VFS assets as evidence.

Two implementation defects were found and corrected during these tests: path
validation initially ran before the WebSocket upgrade populated its path; and
incoming media handlers run tail-first, so the RTCP observer must run before the
receiving session consumes sender reports. The timing gate correctly prevented
recording while the latter defect was present. Final checks above use the fixes.

Android commits: `75a3e49` and `36bde91`, PR
https://github.com/wwestlake/Creation-Remote-Android/pull/3.
Movie commit/PR are recorded in its review submission and issue #46. Neither
application is merged under this order; umbrella integration awaits approved app
merges. The temporary development VFS service was used without installing it or
changing production configuration. No Order 4 work was started.
