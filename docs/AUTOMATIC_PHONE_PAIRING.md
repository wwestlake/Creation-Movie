# Simplified Android WebCam pairing — completion report

Date: November 2, 2026. Authorized local implementation/testing only. No push,
merge, release, website deployment or Google Drive replacement under this order.

## Delivered behavior

Movie has a **Pair Phone** main-toolbar command, also available in Track Inspector.
One activation discovers endpoints, starts listeners and opens the QR invitation.
There is no address-selection menu. The dialog shows Waiting, Connecting,
Connected or Failed, with expiry, refresh and disconnect controls. Closing an
unconnected dialog revokes its invitation; a connected session survives closing.

Android 0.0.5 (5) scans the QR, waits for scanner release/preview setup and requests
any necessary camera/microphone permissions, then connects automatically. Camera
selection, switching and microphone controls remain. Manual invitation entry and
phone network addresses are optional details, not prerequisites.

## Discovery and protocol

Movie uses Windows GetAdaptersAddresses/GetIfEntry2: only operational,
preferred private IPv4 unicast addresses are eligible. Loopback, APIPA, public,
inactive and duplicate addresses are excluded. Physical adapters rank first;
routed virtual adapters and tunnel/PPP interfaces are fallbacks. Host-only
virtual adapters without a gateway are excluded. Addresses are ordered by class,
interface metric and address, with a 16-endpoint maximum. Each advertised address
has its own successfully bound listener; failed binds are omitted. No wildcard
public listener is introduced. Zero usable listeners gives an actionable failure.

The existing v1 invitation retains protocol/session/secret/endpoint. It adds an
ordered endpoints array and expiresAtUnixMs; endpoint remains for legacy readers.
The existing directional HKDF/AES-GCM SDP authentication, session binding,
120-second host-monotonic expiry and global single-use gate are unchanged.
Authenticated expired/consumed requests receive encrypted rejection messages.
Android accepts bounded private IPv4 candidates and tries them serially, with
3-second connect and 5-second attempt limits and a 90-second overall limit.
Stale callbacks cannot advance/cancel a later attempt. After verified answer
acceptance it proceeds to WebRTC; the receiver binds media to the interface that
accepted authentication. No subnet-prefix equality test is used. Routed private
networks can work when the OS provides the route and network policy permits it.

No cloud signaling, relay, STUN/TURN or Station dependency was added. Existing
VP8/Opus media and timing processing were not redesigned. No firewall/Norton or
router configuration was changed.

## Diagnostics and verification

Normal Android messages distinguish unreachable/refused-or-blocked endpoints,
unverifiable authentication, expired/consumed invitations and media failure.
They suggest local reachability/firewall/router-isolation checks, never diagnose
wrong Wi-Fi merely from a timeout. Android technical logs include attempt/stage
and exception class; desktop logs record bind/auth rejection categories. Neither
logs secrets, invitation payloads or SDP. A closed/malformed response may be
indistinguishable from an authentication failure caused by an intermediary.

- Movie Debug app and WebCamReceiverTests: passed, single worker;
  build/automatic-pairing-final-build.log.
- Native unit checks: passed, including physical/virtual/VPN adapter ordering,
  inactive/public/link-local/duplicate exclusion and existing timing/monitor
  boundaries; build/automatic-pairing-final-unit.log.
- Six native pairing groups passed: QR schema/bounds/expiry, independent ZXing
  decode of the exact desktop-rendered image, all advertised listeners reachable,
  invalid authentication rejected without consumption, authenticated expiry
  rejection, and refresh invalidating the old generation. This PC supplied one
  eligible physical endpoint; multiple-interface combinations were simulated.
  See build/automatic-pairing-final-check/results.json.
- Android Debug assembly and five JVM tests passed (zero failures/errors): legacy
  and multi-endpoint parser/security, crypto/rejection handling, unreachable-first
  failover to a different private range, stale callbacks and diagnostic categories.
  See Android build/automatic-pairing-final-build.log and app/build/test-results.
- Android apksigner verification passed with the existing development certificate.
- Synthetic media connected and decoded through auto-discovered listeners. The
  first run failed the audio-monotonicity assertion (62 video/103 audio subscriber
  blocks); a repeat passed (61/101, second subscriber 30). Both logs are retained
  under build/automatic-pairing-media*. This intermittent timestamp regression is
  unresolved; root cause is not established. Timing code is unchanged by this
  pairing work. Do not infer production recording reliability from the repeat.
- Movie launched and accessibility confirmed Pair Phone in the main toolbar.
  Automated window capture timed out, so visual button/dialog interaction is not
  claimed as passed. Movie was closed normally after inspection. Test fixtures
  exited and their private invitation JSON/PNG files were removed.

QR decoding used test-only zxing-cpp 2.3.0 installed under ignored development
build dependencies; it is not a Movie runtime dependency. Reproduction:

    WebCamReceiverTests --unit
    python tests/webcam-lan/verify-auto-pairing.py --fixture <fixture-exe> --output <private-test-folder>
    python tests/webcam-lan/run-native.py --fixture <fixture-exe> --output <test-folder> --bind auto

Use the existing isolated Python test dependencies plus zxing-cpp==2.3.0.

## Delivery, acceptance and remaining limits

Android test artifact: app/build/outputs/apk/debug/app-debug.apk in the Android
repository, version 0.0.5 (5), SHA256:
fde983362238d9aa7d1923bf4a4e11a78fd9f2a36f7173cf913af9a9a87bdcd7.
Package/signing identity remains compatible with prior WebCam development builds.
The previously unmerged recovery commit was preserved locally as 7376978 before
this work. Final app commit identifiers accompany the completion message.

No physical Galaxy S23 Ultra pairing was performed. Final acceptance: install
0.0.5, launch the matching Movie build, click Pair Phone, scan, grant permissions,
verify Connected on both sides and actual Movie source preview. Stop/disconnect
and repeat with a fresh code; verify expiry and unreachable-path messages. The
Executive Director performs this test. USB device logs were unavailable.

IPv6-only/public-address networks, guaranteed reachability through arbitrary VPNs,
router isolation, Norton/Windows firewall policy and physical scanner/permission
lifecycle behavior remain constraints or unverified device scenarios. Persisted
trusted-device reconnect is outside this single-use invitation order. Resolve
the observed intermittent audio timestamp regression before a production take.

Changes are committed on local feat/automatic-phone-pairing branches. Nothing is
pushed or merged, no PR was created/modified, and umbrella pins are unchanged.
Pre-existing Suite services and unrelated repositories were left alone.
