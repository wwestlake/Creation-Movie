# Djehuti Movie implementation handoff

## October 24 - Milestone 3: functional effects

Implemented on `feature/shared-track-document`; local commit only, no merge or
push. Station and shared sources are unchanged. See `docs/EFFECTS.md` for reuse,
processing order, catalog, command contracts, routing limits and validation.

Movie now has persistent ordered track effects, metadata-generated controls in a
docked browser/inspector, enable/bypass, reorder/removal and transactional undo.
Audio gain, stereo balance, track fades and low/high-pass filtering process real
samples. Brightness, contrast, saturation, RGB balance, opacity, crop, scale and
position process real frames. Preview and export share the production Renderer
and effect algorithms. JUCE IIR (also used by Station), Image/Graphics transforms,
Movie docking, worker scheduling and existing codecs are reused. Station's host
and shared GPU nodes use different processing surfaces; neither was copied.

Schema 4 persists stable effect/owner/type/parameter identities and explicit
parameter values. Versions 2 and 3 remain readable without writing on open.
Future automation can supply evaluated values through the same parameter map;
automation playback/editor is deferred. Existing clip gain/opacity remain intact;
there was no general clip chain to integrate. Effects tracks have no established
routing semantics: insertion is rejected with an explanation, while ordinary
compatible track chains work. No routing model was invented.

Focused validation passed: Debug app/tests build, DSP/pixel/order/bypass checks,
buffer continuity, parameter validation, undo/redo, v3/v4 persistence and existing
v2 migration/ownership checks. Representative Debug timings: 720p colour/opacity/
transform 58.15 ms/frame, non-neutral saturation 154.62 ms/frame; one second stereo
48 kHz filter 1.16 ms. These are local indicative measurements, not release-mode
performance claims. Filter output across block sizes matched exactly.

Limitations: CPU 8-bit SDR processing can miss real-time frame budgets, especially
saturation and multiple layers. Manual edits use existing renderer revision
invalidation and are not guaranteed click-free; no smoothing is claimed. Stateful
filters replay up to one second on seek. No GPU/proxy redesign, external plugin
host, capture or automation runtime was introduced. Compiler shadow warnings in
shared/UI code remain; the build is not claimed warning-free. Native pointer
interaction remains unverified; the opt-in real-window check uses mounted JUCE
controls and synthesized drag events.

Final validation evidence:

- `build/effects-build.log` and `build/effects-unit-results.log`: successful
  single-worker Debug build and focused tests after the final DSP optimization.
- `build/effects-check-verified/track-ui-results.json`: all 11 mounted-window
  groups passed, including effect controls, track regression and save/reopen.
  Saved VFS demo project: `ac6b2597587a46c4b156cdf29062d03b`. Window content was
  inspected in `effects-ui-demo.png`; both processed exports are in that folder.
- `effects-media-results.json`: sampled RGB difference sum 3,147,105; isolated
  dry/processed audio RMS 0.050000/0.012597; decoded FFV1 pixel error 0; PCM maximum
  sample error 0.000015247; active-audio filter seek error 0. MP4 starts both
  streams at zero, with video/audio durations 4.972/4.971 seconds (AAC tolerance).
  This checks encoded timing, not a subjective lip-sync or listening session.
- `build/effects-regression.log`: all ten lifecycle/track groups passed,
  including advancing playback, v2 migration, decoded audio/video equivalence,
  MP4 export and failed-save protection. Original project restored; Movie closed
  normally and the shared VFS service retained.
- One earlier full-recording fixture had no useful visual difference at its
  first frame and failed the pixel-change assertion. The final check used the
  existing short visible excerpt. An initial lifecycle launch missed its startup
  deadline; that instance was closed through its API before the successful run.

Changed files: `CMakeLists.txt`, `HANDOFF.md`, `Source/EffectsPanel.inc`,
`Source/MovieEffects.h/.cpp`, `Source/MovieDocument.h/.cpp`,
`Source/MovieMedia.h/.cpp`, `Source/MovieTracks.cpp`, `Source/MainComponent.h/.cpp`,
`Source/TrackInspector.inc`, `Source/TrackUiCheck.inc`, `tests/EffectsTests.inc`,
`tests/MovieDocumentTests.cpp`, `scripts/Check-MovieLifecycle.py`,
`docs/EFFECTS.md`, `docs/CAPABILITIES.md`, `docs/TRACK_DOCUMENT.md` and
`docs/TRACK_INTERFACE.md`. Build evidence and generated media remain untracked
ignored local artifacts; no umbrella gitlink commit was made.

## October 24 - Milestone 2: track interface

Implemented on `feature/shared-track-document`, continuing Milestone 1. Local
commit only; no merge/push or umbrella gitlink commit. See
`docs/TRACK_INTERFACE.md` for user interactions and the opt-in GUI check.

The previous interface only painted track names/order. It now provides Add Track
with all five types, header selection, inline/inspector rename, header drag and
menu/keyboard reorder, enable state, arm/monitor preferences, safe removal and a
docked scrollable Track Inspector. Headers and clips share document-derived lane
heights and a vertical scroll offset. Library placement uses the selected stable
track identity. Sources use compatible project-media choices or explicitly
inactive saved live-source configurations; automation shows stored target names
and point count without evaluation or a curve editor.

Station's TrackerPanel was inspected but is not an independent shared UI widget:
it depends on Station's timeline/audio integration. No Station panel was copied.
Existing shared track/automation structures, Movie document commands, docking,
JUCE controls and popup conventions are reused. No shared/Station or media-engine
source changed. No schema change was needed.

Validation:

- Single-worker Debug application build passed (`build/track-ui-build.log`).
  Shadow warnings remain in the shared header and UI/check locals; this is not
  a zero-warning claim.
- Existing MovieDocumentTests passed (migration, commands, ownership, undo/redo,
  sources, automation persistence and atomic failure).
- The opt-in Debug GUI check ran the actual mounted application window. Eight
  groups passed: empty state; five types/type-specific inspector; name/enable/
  preferences/source/height controls; synthesized header-drag events and undo;
  inline rename/scroll alignment; removal/undo/redo; Library placement of real
  video plus generated WAV/PNG on selected tracks; identical VFS save/reopen.
- Final GUI results and rendered window content:
  `build/track-ui-final/track-ui-results.json`, `track-ui-demo.png` and
  `track-ui-empty.png`. The saved demonstration VFS project is
  `3f1da6f28c494bee8ecb85714e5c62c5`; it contains all five track types and three
  imported media assets. Only uniquely named test projects were changed.
- The ten-group live lifecycle/track regression passed, including version-2
  migration and mixed-content document tests, playback, MP4 export, decoded
  lossless audio/video equivalence and failed-save protection. Log:
  `build/track-ui-regression.log`. The initial project was restored and Movie
  exited normally after both checks. The shared VFS service remains available.
- Window-content renders were inspected; an unnecessary inspector horizontal
  scrollbar was removed and the final GUI check repeated after the repair.
- Native desktop pointer delivery is **not verified**. The Windows helper could
  read the real accessibility tree, but capture returned `FrameArrived timed out`
  / `window capture timed out`, and clicking returned `coordinate input geometry
  is unavailable`. GUI integration checks used mounted controls and synthesized
  JUCE drag events, not OS-level mouse input. Manual pointer/keyboard acceptance
  is still recommended when an interactive desktop is available.

Capture, device discovery, effects, automation playback/editor and codec work
remain deferred. Existing project/media formats and processing paths are retained.

Changed files: `CMakeLists.txt`, `Source/MainComponent.cpp`,
`Source/MainComponent.h`, `Source/TrackTimeline.inc`, `Source/TrackInspector.inc`,
`Source/TrackUiCheck.inc`, `HANDOFF.md`, `docs/TRACK_INTERFACE.md`,
`docs/TRACK_DOCUMENT.md`, and `docs/CAPABILITIES.md`.

## October 24 — Milestone 1: track document

Implemented on local branch `feature/shared-track-document`; no merge or push.
See `docs/TRACK_DOCUMENT.md` for the schema, command contract, shared reuse,
compatibility policy and deferred work. No Station or shared source changes.

Movie extends shared TimelineTrack and reuses shared AutomationPoint/curve
representations and split/delete helpers. Schema 3 persists ordered tracks and
stable clip ownership. Version 2 converts transactionally in memory into eight
deterministically identified legacy mixed-media tracks. Opening never writes the
conversion; only successful saving does. Existing source/timing/render order is
preserved. Effects/automation processing and capture remain deferred.

Validation completed:

- Single-worker Debug build of CreationMovieApp and MovieDocumentTests passed.
  Warnings remain, including a local-name shadow warning; not zero-warning.
- MovieDocumentTests passed: all eight legacy indices, deterministic conversion,
  media/version/timing preservation, track commands, undo/redo, removal protection,
  source configuration and runtime-handle rejection, stable automation bindings,
  schema round trip, atomic failed load, shared split ownership and shared curves.
- `Check-MovieLifecycle.py --tracks` passed ten live check groups using a
  disposable VFS project and real video: track queries/commands, source and
  preferences persistence, migration/save/reopen, advancing playback, FFV1
  video/audio decoded SHA-256 equivalence across migration, MP4 export, and the
  existing failed-open/failed-save/close guards. The test app exited normally.
- Logs: `build/track-tests-build.log`, `build/track-app-build.log`,
  `build/track-live-check.log`, `build/track-lifecycle-results.json`.
- Initial live invocation found a stale heartbeat and no storage service; the
  existing installed service was started with its current configuration. It
  remains available. No existing service or user app was terminated.

Full track-management UI, long-list scrolling, device activation, recording,
effect processing and automation playback/editor remain later milestones.
Station was not rebuilt: its code and shared code were unchanged. This is focused
functional verification, not a codec matrix or complete visual acceptance pass.

Updated October 19, 2026. Task: [background job and project lifecycle reliability](https://github.com/wwestlake/Creation-Movie/issues/35).

## Current foundation

Gemini's media-engine work is integrated. The application has FFmpeg decoding/encoding, VFS media storage, source/program monitors, timeline editing, title overlays, gain/opacity, undo/redo, exports, Virtual Engineer tools and localhost automation. See `docs/CAPABILITIES.md` for the current boundaries; the earlier scaffold-only README was obsolete.

## This closeout

- Imports retain their originating project identity and preserve document undo history.
- Workers calculate results locally. The UI applies results before publishing job completion; workers no longer mutate the strings/objects read by the UI/API.
- Explicit project create/open/save storage calls run in the background. Failed opens preserve the active session/document.
- Running jobs block conflicting edits, project replacement and closing. Read-only inspection, selection and transport remain usable.
- Dirty project replacement requires Save / Discard / Cancel in the UI, or save / explicit `discardChanges` in automation.
- Close waits for successful saving. A failed save leaves the app running with dirty edits. Normal window close, system-requested quit and `movie.app.close` use the same path.
- Autosave uses the same asynchronous save operation after approximately one minute of idle, dirty time.

Automation clients must wait for the matching `jobId` to reach a terminal state. A successfully accepted command is not a completed import/open/save/export.

## Actual evidence

- Incremental Debug application build passed with one worker. The first attempt caught a const-qualified save-session capture; that was corrected before the successful build. Existing compiler warnings remain; this is not a zero-warning claim.
- `scripts/Check-MovieLifecycle.py` drove the actual Debug app through its authenticated localhost API, using a uniquely named disposable VFS project and a real CAD demo recording.
- Passed: completed create exposes the new document; active import rejects project switch and close; imported media is present when completion is published; dirty replacement is rejected; save clears dirty after success; failed open preserves the active project; saved edits reopen; real media exports.
- The check deleted only its own disposable project to force storage writes to fail. Save-on-close reported failure while the app remained alive with dirty edits. It then restored the initially opened project and exited normally through the close command.
- Export probe: H.264 640 x 360, AAC 48 kHz, both 1.001 seconds; output 117,714 bytes. This was a focused media smoke check, not a codec matrix.
- `git diff --check` passed. No broad regression suite or duplicate Release build was run.
- The initial check found no running storage service; the installed shared service was started using its existing configuration. No shared service was killed/restarted, and no existing project was deleted or rewritten by the check.

Local evidence: `build/lifecycle-build.log`, `build/lifecycle-results.json`, and the short MP4 path recorded in that JSON. The test instance exited normally. The shared storage service is left available.

Executable: `build/CreationMovieApp_artefacts/Debug/Djehuti Movie.exe`.

## Limits and next work

The API check exercises the production handlers and live document/storage state. It does not establish visual acceptance of the Save / Discard / Cancel dialog or OS shutdown interaction. Those paths were inspected in source; the close command exercised their shared save/close handler.

Startup and project-menu enumeration still perform synchronous service requests. Blocking storage operations are not forcibly interruptible. There is no new crash-recovery mechanism. Media editing remains the SDR/8-bit foundation documented in Capabilities; advanced editing, codec-aware installer packaging, Movie-specific knowledge cards and fuller user Help remain separate work.

Continue in the shared checkout and canonical build directory. Use a focused feature branch, one-worker incremental build and only relevant functional checks. Close each assignment by pushing/merging the app default branch, then the umbrella gitlink. Do not recreate per-agent workspaces or rebuild LLVM.
