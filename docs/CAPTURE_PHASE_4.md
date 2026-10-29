# Capture Phase 4 — synchronized multitrack recording

Review branch: `feat/movie-synchronized-recording`, based on merged Phase 3
`71e5f337af8176e84d6fce12924c3e0943d99d74` (Movie PR #41).
Phase 5 phone capture is not included.

## Architecture and workflow

Arm existing audio/video tracks in Track Inspector, assign supported physical
sources, set the timeline insertion position, then Record. Stop finalizes the
take and publishes independent media assets and placements. Existing clips are
preserved. Arm state and source configuration are snapshotted when Record starts.
Seeking or changing preview/listening does not move the session clock. Project
switching is blocked until finalization/publication completes; normal application
close requests stop and finalize first.

`MovieRecording` owns one session coordinator. `MovieVisualCapture` and
`MovieDeviceCapture` retain physical acquisition ownership. Recording subscriptions
coexist with preview/listening subscriptions; compatible tracks share acquisition,
but receive independent recording outputs. Encoders and VFS calls run on recording
workers. Hardware callbacks only publish immutable samples to bounded SPSC queues.

`MovieCaptureStream` carries original source timestamps, clock/epoch identities,
host-correlated presentation time, discontinuity information, and immutable media.
Each subscription has a 64 MiB byte budget and 4,095 usable queue slots. Overflow
is reported and finalizes the retained prefix; it does not grow memory or silently
drop material while reporting success. Encoder buffers, AVIO buffers and timing
sidecar buffers are bounded independently of recording duration.

`MovieRecordingWriter` writes through the public suite VFS client only. Video uses
NUT with index writing disabled ([FFmpeg implementation](https://ffmpeg.org/doxygen/8.0/nutenc_8c_source.html)), H.264/OpenH264 for even dimensions and FFV1/BGRA
for odd dimensions. Audio uses native-rate/channel float PCM in RF64 WAV. Header
patching goes through the same streamed VFS operation. No recording/scratch media
is written to the host filesystem. Timing JSONL and take journals are VFS entries.
The existing media reader accepts NUT; small indexless takes without a probed
duration derive duration from packets using bounded memory.

## Timing and synchronization

All tracks use a common future QPC epoch, with exact session-relative first-sample
offsets. WGC frames carry their native QPC timestamps. A static visual source can
produce held-frame heartbeats with explicit repeated-frame diagnostics. Camera
device QPC timestamps are used when supplied; the fallback uses an arrival-envelope
correlation with reported uncertainty. This fallback is not a measured physical
camera latency guarantee.

WASAPI original capture data is recorded before optional monitor conversion.
Audio boundary blocks are trimmed at sample resolution. Clock residuals drive
gradual resampling, capped at 1,000 ppm, rather than adjusting the monitor mix.
Missing intervals over 100 ms insert explicitly counted silence; jumps over two
seconds, backward time, changed formats/epochs and invalid timestamps stop the
affected output with a retained partial result. Other sources continue when safe.

A two-second delivery watchdog also catches stale acquisition snapshots. These timestamps describe reported capture clocks. A physical flash/clap latency
and long-duration real-device synchronization measurement remains a separate
acceptance activity; synthetic clock tests do not substitute for it.

## Document and storage contracts

Schema 6 stores precise `startSeconds`/`durationSeconds` alongside compatibility
frame fields. Versions 2–5 remain readable. Effects, automation and transitions
remain intact. Recording publication is one undo transaction: undo removes the
new placements while retaining recorded media in the library. Redo restores exact
offsets. A changed/deleted target leaves its recording unplaced in the media
library instead of retargeting or recreating tracks.

The required shared dependency is the suite service's version-1 open-ended writer.
See `services/VfsService/STREAMING_WRITES.md` in the umbrella repository. Older
services fail recording preparation explicitly; there is no filesystem fallback.
Local integration builds enable `CREATION_MOVIE_BUILD_VFS_SERVICE=ON` in the
canonical Movie `build/` tree. Normal app builds do not acquire server-only
httplib/ixwebsocket build dependencies. The release workflow now checks out the
umbrella `master` instead of the obsolete development branch; no tag/release was
created or tested.
The shared/service PR must be integrated and its service deployed before using
Movie recording with a standard installed service.

Take journals under `Project/Capture/<takeId>.json` retain destination operation IDs,
source ownership, exact offsets and finalization results. Completed media remains
an ordinary project entry. `movie.recording.recover` republishes finalized usable
entries idempotently. It does not repair incomplete containers after a crash.
There is no claim of crash-proof or power-loss recovery, and no automatic deletion
of retained failed streams.

## Agent commands

`movie.recording.readiness`, `.start` (optional `position`), `.stop`, `.status`,
`.results`, and `.recover` (`takeId`) use the GUI coordinator and document path.
Start/stop acknowledge asynchronously; inspect state through preparation,
recording, stopping and final publication. A take may finish `completed`, `partial`
or `failed`; inspect per-track `usable`, `error`, and diagnostics. Command acceptance
does not establish successful recording or save completion.

## Validation and integration status

Validation is complete for the checks listed below. Debug service, client smoke test, Movie application and
document targets build. VFS streaming smoke tests pass, including open-ended
append, header patches, exact retries, stale writes, sparse gaps, finalization,
destination protection and byte-for-byte reads. Document/effects/automation/
transition tests pass after fixing the schema-6 transition loader regression found
by the existing transition test.

The mounted test has verified simultaneous physical C920 camera, C920 microphone
and individual-display recordings on four tracks with one shared camera acquisition,
preview independence, armed snapshots, precise placements, undo/redo and save/reopen.
The full mounted run in `build/capture-phase4-ui-r4/track-ui-results.json` passed
all 12 groups, including usable partial window output with continued microphone
capture, backward/end seeking, startup cancellation and unavailable-source
rejection. The 30-minute, 300 ppm synthetic fixture produced an expected duration
of 1799.460162 s and decoded duration of 1799.460250 s: 88 microseconds residual.
Both 64x48 H.264 and 65x49 FFV1 VFR outputs decoded. These are synthetic
measurements, not a 30-minute physical-device test.

The final mounted run in `build/capture-phase4-recording-buffered/track-ui-results.json`
passed all 12 groups. It recorded five tracks from four physical acquisitions:
C920 camera on two tracks, an individual display, C920 microphone (16 kHz stereo),
and USB Audio CODEC (48 kHz stereo). Both microphone outputs reported zero gaps
and discontinuities after increasing the bounded native WASAPI ring from 100 ms
to one second; the preceding five-track run reported three each. Available packets
are still drained immediately; the ring capacity is not a one-second monitor delay.
Peak recording queues remained below 64 MiB (about 31.64 MiB for the display and
2.64 MiB for the larger audio queue in this run).

Both the final native 37-check device regression (`build/capture-phase4-devices-final.log`)
and native display/window regression (`build/capture-phase4-visual-validated.log`)
passed. The former verifies physical and virtual endpoints, native format changes,
sharing, independent unsubscribe, monitor consumption/muting, conflict recovery and
cleanup; silent virtual inputs are not physical audio evidence. GUI checks use
mounted controls/handlers and application snapshots, not native pointer input.

All broader mounted regressions passed:

| Check | Evidence | Result |
| --- | --- | --- |
| Five-track recording and recovery/failure behavior | `build/capture-phase4-recording-buffered/track-ui-results.json` | 12 groups |
| Native devices/formats/sharing/monitoring | `build/capture-phase4-devices-final.log` | 37 checks |
| Native display/window pixels and lifecycle | `build/capture-phase4-visual-validated.log` | Passed |
| Editing, effects, automation, transitions, playback and exports | `build/capture-phase4-transitions-ui-verified/track-ui-results.json` | 18 groups |
| Camera/microphone GUI regression | `build/capture-phase4-devices-ui-verified/track-ui-results.json` | 10 groups |
| Display/window GUI regression | `build/capture-phase4-visual-ui-verified/track-ui-results.json` | 9 groups |
| Document/schema/effects/automation/transitions | `build/capture-phase4-document-tests-final.log` | Passed |
| VFS streaming plus legacy large upload/range behavior | `build/capture-phase4-vfs-tests-final.log` | All passed |
| Real recorded-media API/editing/playback/export workflow | `build/capture-phase4-recording-buffered/recorded-workflow-results.json` | 4 groups |

The final authenticated workflow reopened the recorded project, idempotently
recovered its five-track take journal, independently moved/trimmed/split recorded
media with exact undo, played the actual VFS recordings, and exported the recorded
timeline through the existing MP4 renderer. ffprobe accepted H.264/AAC output
(duration 10.911 s). No separate recording-only edit/export pipeline was used.
Test artifacts stay in the ignored canonical build tree and disposable VFS test
projects; captured media/screenshots are not committed.

Build evidence: `build/capture-phase4-build-r.log` (native buffer/application and
native test executable), `build/capture-phase4-build-t.log` (final application),
`build/capture-phase4-build-p.log` (VFS smoke/application), and
`build/capture-phase4-configure-final.log` (`CREATION_MOVIE_BUILD_VFS_SERVICE=ON`).
Existing compiler warnings remain. Existing dependency packages/LLVM were reused;
no vcpkg installation or LLVM rebuild was performed. Test harnesses now wait on
bounded real conditions for asynchronous monitor delivery and playback preroll,
and invoke checkbox handlers synchronously to avoid posted-click races.
The VFS failure test injects HTTP 507 and verifies explicit failure plus unchanged
acknowledged size/revision and readable finalized prefix. Actual disk exhaustion
and forced process/power loss are unverified.

## Limitations and review status

- Windows native capture only; at most 16 armed audio/video tracks per session.
- No phone capture, system loopback, effects routing or new capture-side effects.
- No physical flash/clap measurement, long physical-device drift run, hot-unplug
  stress test, actual full-disk test or forced-crash/power-loss recovery guarantee.
- Larger native buffering reduced the measured audio overruns; arbitrary hardware,
  drivers and sustained machine overload are not guaranteed to remain drop-free.
  Video `gaps` diagnostics count long VFR/held-frame intervals, not proven frame loss.
- Short indexless NUT files may require a packet scan for duration. Long-recording
  seek performance and multi-gigabyte RF64 outputs have not been benchmarked.
- Old installed VFS services reject recording preparation; installing/deploying an
  updated service is required. The validation service was run from the build tree.

The directly required VFS change is committed as
`c6e4dfd3c4b55acee7ef7da1b8a98d54e2363372` in
[Suite PR #210](https://github.com/wwestlake/Creation-Suite/pull/210). Its Movie
pin is the already merged Phase 3 commit, not this application's unmerged Phase 4
work. It supersedes Suite PR #209. PR #210's Requirements CI fails the same
pre-existing Station R3 checks (`PatchLiveVoice.cpp:193`, `CS_SIGNAL_LAB_RUNTIME`;
`AudioGraphSelfTest.cpp:46`, `CS_SIGNAL_LAB_NODE_LIBRARY`). No Station changes or
check bypass are included.

Movie Phase 4 is ready for review on `feat/movie-synchronized-recording`. After its PR actually merges, update
the umbrella gitlink through a separate PR and remove completed branches. Required
reviews and CI must not be bypassed. An open PR is not integrated work.
