# WebCam Order 3 — receiver timing contract, version 1

The Android sender and Movie receiver use one versioned protocol and a fresh random
connection generation after every authenticated connection. The phone does not
choose project paths, owner tracks, recording origin, or placement offsets.

## Signaling and identity

The initial test-endpoint profile retains Order 1's authenticated encrypted
offer/answer: `djehuti-webcam-feasibility-v1`, 256-bit out-of-band invitation,
direction-separated HKDF-SHA256/AES-256-GCM keys, session-bound authenticated data,
single consumption, expiry and revocation. This permits real Android capture to
be tested before Movie integration. It is not a website account or cloud grant.
Production pairing must bind the negotiated DTLS fingerprints to this invitation;
never send a reusable plaintext bearer token on an unprotected LAN socket.

## Capture and transport timing

- Use native WebRTC capture timestamps and RTP audio sample/video clock rates.
  Do not timestamp a frame for recording simply when it arrives over Wi-Fi.
- Preserve original extended RTP timestamps, stream identity, SSRC, source clock
  rate, connection generation, sequence/discontinuity information, and timing
  anchors. Decode must not replace timestamps with the GUI/playback position.
- RTCP sender reports relate each stream's RTP clock to the sender's NTP clock.
  Receiver integration must verify the chosen Android build's sender-clock basis.
- Authenticated data-channel clock probes carry host send time, phone receive and
  send monotonic times, paired phone wall time, and host receive time. Retain low
  round-trip samples, report uncertainty, estimate offset/rate continuously, and
  invalidate anchors on wall-clock steps, long stalls or generation changes.
- Where RTCP NTP is tied to phone wall time, correlate it through those paired
  wall/monotonic observations to Movie's host monotonic clock. Do not assume NTP
  is synchronized across devices. Phone wall precision and capture pipeline
  latency contribute uncertainty and require physical measurement in Order 4.
- A missing/invalid source anchor is a readiness/timing error, not permission to
  record arrival-time estimates as synchronized capture. Explicit preview-only
  operation may display frames while timing readiness is pending.

## Bounded delivery and Movie integration

Use bounded packet reorder/jitter handling and bounded decode work. Drop late
video explicitly, request recovery keyframes, and count audio loss/concealment.
Never let network callbacks block encoders, VFS writes, or the GUI. Publish
immutable decoded samples through the existing capture subscription queues.
Populate source epoch/clock, original source time/position, mapped host time,
uncertainty and discontinuity fields in `CaptureSample`.

Movie's common recording epoch remains authoritative. Audio sample progression
and gradual drift correction remain in the existing writer; preview/seek does
not move capture time. Local sources continue if the phone fails. Format/epoch
changes finalize a useful segment; reconnect starts a newly timed segment and
does not shift previously recorded material. Existing writer epoch rejection is
preserved until explicit coordinator segment continuation is implemented.

## Validation gates

Stage B checks real media and signaling against the isolated endpoint. Stage C
must test RTP wraparound, clock-probe asymmetry/outliers, source-clock changes,
missing anchors, queue overflow, generation isolation and source fan-out before
claiming synchronized recording readiness. Stage D separates synthetic evidence
from physical Galaxy S23 Ultra results. Order 4 measures sustained drift and
physical flash/clap alignment; no numeric accuracy is inferred from connectivity.
