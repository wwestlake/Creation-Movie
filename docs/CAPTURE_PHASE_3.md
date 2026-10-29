# Djehuti Movie — Multisource Capture Phase 3

Date: October 28, 2026
Status: Implementation and validation complete; ready for PR review, not integrated.
Branch: `feat/movie-camera-microphone-capture`
Base: Movie master `91598ba`, merged Phase 2 PR #39.
Tracking: Movie issue #40 / Creation Suite Road Map.

## Delivered workflow

Create/select an existing VIDEO or AUDIO track. In Track Inspector choose Camera
or Microphone, select a discovered device and a supported format, then Apply
Source. Camera video appears in the existing Source Monitor. Microphone input
levels appear there and as numeric dBFS in Track Inspector. Enable the track to
acquire microphone samples; listening is a separate per-track checkbox. Newly
assigned microphones do not enable speaker monitoring. Use headphones before
explicitly enabling listening. Monitoring uses 12 dB headroom and the existing
program playback output device; no separate output subsystem is created.

The source identity and exact requested native format are stored in the existing
track source configuration. Changes use the established track commands and undo
manager. Existing placements, media, effects and Program Monitor are preserved.
No tracks are automatically created by acquisition. Unavailable saved bindings
remain in the project and never fall back to another physical device.

## Capture technologies and responsibilities

`MovieVisualCapture` remains the single application-facing acquisition manager.
Its existing screen/window worker is retained. New `MovieDeviceCapture` native
adapters are owned by that manager and receive its track subscriptions; they do
not own or create document tracks. Device discovery, preparation, polling, image
conversion and destruction run on an MTA worker, independently of the GUI thread.

Camera discovery uses Media Foundation device activation objects and persistent
video symbolic links. Native video types provide actual dimensions, rational
frame rates and subtype identities. `IMFSourceReaderEx::SetNativeMediaType`
selects the physical source format before the asynchronous source reader converts
to RGB32 preview frames. Output dimensions and frame rate are checked against
the request. Callback state retains at most one pending sample; the worker owns
image copies and publishes immutable latest snapshots. Camera frame timestamps
are native Media Foundation sample times in 100 ns units, not a claimed
cross-device synchronization clock.

Microphone discovery uses Windows capture endpoints and persistent endpoint IDs.
WASAPI shared-mode `IsFormatSupported` probes exact float32 sample-rate/channel
configurations; closest-match responses are not advertised as supported.
Acquisition uses `IAudioCaptureClient`, preserving acquired frame counts,
endpoint QPC timestamps and discontinuity counts. No loopback flag is used.
Source meters measure actual acquired samples even while listening is off.

Listening consumes fixed-capacity SPSC queues from the existing `Playback`
audio callback. That callback does not acquire capture locks, allocate memory,
call device APIs or edit documents. Queue generations invalidate muted/replaced
subscriptions; overflow is bounded and counted. Linear sample-rate conversion
adapts monitor audio to the existing output rate. This is preview listening,
not Phase 4 drift correction or synchronized recording. Multichannel inputs
retain their full native acquisition format; stereo monitoring uses the first
two channels (mono is duplicated).

## Shared acquisition and lifecycle

Active subscriptions are grouped by physical device identity. Compatible requests
share one camera reader or WASAPI capture client and the same latest snapshot.
Each microphone track independently controls listening; the physical input is
mixed once when any subscriber listens, avoiding doubled gain from duplicate
track assignments. Removing one subscriber does not close another's device.

A group must have at least one advertised format satisfying all active requested
settings. Otherwise all subscribers receive `formatConflict` and that device is
closed, rather than reopening duplicate sessions or silently changing format.
A single invalid saved format receives `unsupportedFormat`. Changing formats
restarts only that physical source. The last unsubscribe releases it.

Discovery refreshes device presence every two seconds. Existing camera
capabilities are reused without reopening a streaming camera. Device removal,
access/configuration errors, reader failure, missing samples and source switching
clear stale snapshots and report failure/unavailability. Explicit refresh retries
failed acquisition. Application destruction joins native workers, stops audio,
flushes readers and releases sources. Camera callback state has an independent
lifetime and never calls a destroyed GUI/source owner.

## Formats and command integration

The existing `movie.capture.sources` catalog now includes `camera` and
`microphone` entries and their `formats` arrays. Camera format objects contain
`width`, `height`, `fpsNum`, `fpsDen`, and a persistent Media Foundation subtype
GUID in `format`; microphone objects contain `sampleRate` and `channels`.
The picker presents complete supported combinations rather than independently
invented width/rate/channel choices. Unsupported preview conversion or device
contention produces an explicit acquisition error.

Assign the selected catalog identity and format using `movie.track.source`.
New device assignments are checked against the current catalog; project loading
retains missing devices/configurations. The closed document settings vocabulary
now permits the subtype string, without serializing transient handles.
`movie.track.preferences.monitoring` remains the independent per-track listen or
video preview preference; `enabled` releases the corresponding subscription.

`movie.capture.status` includes camera/microphone source IDs, availability,
state/error, subscribers, active format, video frames or audio sample frames,
timestamps, peaks, discontinuities, monitor subscribers and output/drop counts.
These extend existing agent commands; no parallel API is added.

## Validation evidence

- Single-worker Debug builds of CreationMovieApp, MovieDocumentTests and
  MovieVisualCaptureTests passed. Existing compiler warnings remain. Application/test build log: `build/capture-phase3-build-validated.log`; final
  application rebuild after command-description corrections:
  `build/capture-phase3-build-published.log`.
- Document, effects, automation and transition console tests passed, including
  unavailable camera/microphone references, native subtype/rational frame-rate
  persistence, safe microphone listen defaults and source undo/redo. Evidence:
  `build/capture-phase3-document-tests-final.log`.
- Native discovery found the physical HD Pro Webcam C920 (455 native format
  descriptors), Animaze Virtual Camera (6), and nine Windows audio input
  endpoints. Virtual-camera/VoiceMeeter/cable endpoints are not physical-device
  evidence. C920 camera acquisition and C920/USB Audio CODEC nonzero audio input
  provide physical-device evidence; G935/Oculus/virtual endpoints delivered
  sample streams but several were silent during validation.
- The mounted Movie device check passed all 10 groups, including physical C920
  video in Source Monitor, C920 microphone levels, shared subscriptions, existing
  output callback consumption, mute controls, VFS reopen and undo/redo.
  Evidence: `build/capture-phase3-devices-ui-final/track-ui-results.json`.
- Authenticated external read-only catalog/status checks passed: unique IDs,
  3 displays, 2 cameras, 9 audio inputs and actual supported format arrays.
  Evidence: `build/capture-phase3-api-results.json` and
  `build/capture-phase3-api-final.log`; the final application was closed through
  the existing app-close command after inspection.
- All **37 expanded native checks passed**: both actual camera endpoints,
  selected alternative frame rates/resolutions, all nine audio endpoint streams,
  sharing, independent unsubscribe, bounded monitor consumption/muting, explicit
  incompatible-request rejection and recovery, unavailable IDs and final cleanup.
  Physical C920 video, its microphone and one actual display also ran concurrently;
  stopping the camera left the other sources active. Evidence:
  `build/capture-phase3-devices-validated/device-results.json`.
- Phase 2 native capture regression passed on all three physical displays and
  a changing owned window. Its mounted Movie interface regression passed all
  **9 groups**, including source switching, sharing, persistence, undo/redo and
  window disappearance. Evidence: `build/capture-phase3-visual-regression.log`
  and `build/capture-phase3-visual-ui-final/track-ui-results.json`.
- All **18** existing editing, effects, automation, transitions, playback/restart
  and decoded export regression groups passed. Evidence:
  `build/capture-phase3-regression-ui-final/track-ui-results.json`.
  The original project was restored and Movie exited normally after each GUI run.

Final device tests wait for `deviceDiscoveryComplete` before inspecting hardware.
No generated audio/video is treated as physical-device evidence. UI checks use
synthesized component events in the actual application; native pointer delivery
is untested. Local validation images include device/desktop content and are not
committed or published.

## Limits and architecture conformance

- Windows camera/microphone privacy permissions and device sharing policies apply.
  Protected/busy/denied devices are reported; permissions are not bypassed.
- Preview is 8-bit SDR. Native subtype support does not guarantee that every
  installed device/codec supports conversion to RGB32; failures are explicit.
- Camera capability enumeration includes an initial device activation. Later
  discovery reuses capabilities until the device disappears or inaccessible
  capabilities are explicitly refreshed.
- Microphone capability discovery probes the native mix rate and common rates
  (8/16/32/44.1/48/96/192 kHz), mono/stereo and the native channel count (up to 8).
  It does not claim exhaustive enumeration of every vendor-specific rate/layout.
- Up to 16 independently acquired microphone endpoints have bounded monitor
  queues. There is no recording throughput guarantee.
- Companion camera microphones appear as separately selectable audio endpoints.
  No ambiguous device-name matching silently associates/assigns audio tracks.
- Speaker feedback remains possible after intentionally enabling listening;
  listening is off for new microphone assignments and the interface calls for
  headphones. Acoustic echo cancellation is not implemented.
- Physical unplug/replug, permission-denial toggling, driver failure and human
  confirmation of speaker/headphone audibility remain unverified.
  Missing-endpoint/format-conflict reporting and callback consumption were tested;
  other native error handling was inspected in source.
  Do not infer acoustic fidelity, echo cancellation or all 455 camera formats
  from selected-format validation.
- No Phase 1 ownership deviation or shared-library dependency expansion. No VFS
  writer, recording, clock synchronization coordinator, system-audio loopback or
  LAN phone capture is included. Phase 4 has not begun.

## Platform contracts

- [Media Foundation native type selection](https://learn.microsoft.com/en-us/windows/win32/api/mfreadwrite/nf-mfreadwrite-imfsourcereaderex-setnativemediatype)
- [Media Foundation image stride](https://learn.microsoft.com/en-us/windows/win32/api/mfobjects/nf-mfobjects-imf2dbuffer-lock2d)

## Integration

Publish a Movie PR to master for Executive Director review. The prerequisite
Phase 2 umbrella reference can be updated to its merged Movie commit separately.
Phase 2's umbrella update is [Suite PR #209](https://github.com/wwestlake/Creation-Suite/pull/209),
commit `4ecf6bb`, pointing only to merged Movie `91598ba`. Its Requirements check
fails R3 in unchanged Station `PatchLiveVoice.cpp:193` and
`AudioGraphSelfTest.cpp:46`. The same failure is present on base umbrella master
`0769b21` (Actions run `40236734825`); PR run `40805313526` confirms it. No check
was bypassed and no Station source was modified. This blocks umbrella integration.

Phase 3's umbrella reference follows only after its Movie PR merges. An open PR
is ready for review, not integrated work.
