# Android webcam readiness — November 15, 2026

The `develop` integration resumes the existing native client and receiver. It
includes automatic LAN/QR pairing, the modeless setup window, persistent phone
status/disconnect controls, and the existing source catalog and recording paths.

Clock refinements now follow RTP progression with bounded slew instead of
jumping a recording's host timestamps backwards. Timing uncertainty includes
the remaining mapping correction. Duplicate/late RTP samples cannot rewind the
decoder. The Android client waits for resource disposal before reconnecting and
ignores permission completions while backgrounded. Existing Osmo diagnostics
are preserved; they are not part of webcam acceptance.

## Verified software checks

- Movie Debug application and receiver fixture build passed, one build worker.
- Native timing/monitor/pairing lifecycle boundary checks passed, including
  deterministic backward/forward clock-refinement and late-packet regressions.
- Two independent synthetic WebRTC runs passed video/audio reception, mapped
  timestamp monotonicity, fan-out/unsubscribe and setup-window auto-dismiss.
- Six QR/authentication groups passed: actual QR decoding, invitation bounds,
  all advertised listeners, invalid authentication, expiry and revocation.
- Actual Movie workflow passed five groups: source catalog; two video tracks
  plus audio recorded to VFS; undo/redo; save/reopen; Stop and fresh reconnect.
  Media collection comparisons canonicalize catalog order without ignoring any
  media fields or track/clip order.

Evidence is in the canonical ignored `build/production-webcam-*` logs and
`build/webcam-movie-test/movie-results.json`. Synthetic media is not a physical
Android camera test.

## Physical acceptance still required

Bill must verify the installed Android build's scan/permissions, moving video
and microphone reception, camera switching, mute, Stop, background cleanup and
fresh reconnect on the actual phone/LAN. Sustained drift/flash-clap alignment,
Wi-Fi interruption and reconnect during an ongoing multitrack take remain
production acceptance gates. This report does not certify production deployment.

Tracking: https://github.com/wwestlake/Creation-Movie/issues/51
