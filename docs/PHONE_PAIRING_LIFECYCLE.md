# Phone pairing setup lifecycle — Order 2 completion

Date: November 2, 2026. Movie-only local implementation; no push or merge.
Base: local Order 1 commit ac64eef. The Executive Director reported successful
Galaxy S23 Ultra QR connection with that build; this new lifecycle behavior has
not been physically verified by Codex.

## Delivered behavior

- Pairing uses an owned, modeless DocumentWindow, replacing LaunchOptions' modal
  launcher. It never enters JUCE's modal manager and does not block the workspace.
- A new connection dismisses setup automatically at the existing receiver's
  connected state. Deferred, lifetime-guarded deletion avoids deleting a panel
  inside its own button/timer callback. Movie's main window is brought forward.
- Close is available while waiting/connecting and on error. It cancels an unused
  setup invitation. Failed setup offers Retry and Close. Escape/title-bar close
  use the same path; neither is required for normal operation.
- Reopening Pair Phone while already connected shows a Done summary without
  issuing another invitation or replacing the peer. Closing that summary leaves
  the phone connected. Successful setup deletion also preserves the live peer.
- The workspace footer reports phone status and exposes Disconnect Phone at all
  times. Disconnect invokes the existing movie.webcam.stop handler, independently
  of setup visibility, and closes any open setup window. Pair Phone remains the
  normal reconnect entry point.
- The existing VisualCapture/WebCamCapture owns the connection. No second manager,
  authentication changes, discovery changes, new routing, or Android changes were
  introduced. Track source polling/assignment and recording remain unchanged.

## Changed files

- Source/MainComponent.cpp / .h: owned setup window, workspace status/footer,
  Disconnect callback, main-window activation after dismissal and shutdown cleanup.
- Source/MovieWebCamPairing.h: modeless window, Close/Done/Retry controls,
  established-connection preservation and deferred safe auto-dismissal.
- tests/WebCamPairingLifecycleTests.h: focused presentation/ownership checks.
- tests/WebCamReceiverTests.cpp: lifecycle entry point and live receiver assertions
  after setup auto-dismissal, source availability and explicit disconnect.
- docs/WEBCAM_QR_PAIRING.md and this report: current operating instructions/results.

## Verification

Debug application and receiver fixture builds passed using canonical build/ and
one MSBuild worker. Logs: build/pairing-lifecycle-final-build.log and
build/pairing-lifecycle-workspace-build.log (final workspace-focus build).

    build/Debug/WebCamReceiverTests.exe --lifecycle

Passed: a modeless setup window does not block an owner component; waiting Close
revokes pending setup; successful pairing auto-dismisses without revocation;
reopening keeps the connection and does not issue another invitation; Done leaves
the connection alone; failed setup supports Retry/Close; late queued close is safe
after owner destruction; an explicit receiver stop still permits normal dismissal.
Evidence: build/pairing-lifecycle-check.log.

    python tests/webcam-lan/run-native.py --fixture <native-fixture> --output <private-output> --bind auto

Passed with synthetic aiortc video/audio, encrypted authentication and direct
private transport. The production pairing panel/window auto-dismissed while the
actual receiver stayed connected. Both webcam:video and webcam:audio remained in
the existing source catalog. Shared subscribers continued receiving (61/30 video,
102 audio blocks). Explicit stop independently changed receiver state to stopped.
Monotonic timestamp checks also passed on this run. Logs/results:
build/pairing-lifecycle-media.log, native-fixture.log and native-results.json in
build/pairing-lifecycle-media/. Test-only monitor drops are not a physical audio
output measurement.

Movie launched and responded normally, then was closed through its existing API.
Desktop automation could not obtain a foreground process/accessibility snapshot,
so end-to-end pointer/keyboard workspace operation is not claimed as verified.
All app/test instances launched here were closed; private invitations were removed.
Pre-existing Suite services and unrelated application repositories were preserved.

## Remaining verification / integration

Executive physical check: click Pair Phone, scan on Galaxy S23 Ultra, confirm setup
disappears while camera/audio keep streaming; assign the existing WebCam sources;
open Pair Phone again and use Done; disconnect from the footer; repeat with an
expired/failed attempt and verify Retry/Close. No Android update is required by
this order. The prior intermittent audio timestamp observation remains outside
this UI fix; a passing synthetic run does not certify production synchronization.

Changes are committed locally on fix/phone-pairing-lifecycle, dependent on the
unmerged local Order 1 branch. Commit identifier accompanies the completion
message. Nothing was pushed/merged, no PR was created/updated, and umbrella pins
were left unchanged. Stop after this order.
