# Djehuti Movie capabilities

## WebCam Order 3 (implementation ready for physical testing)

The existing Android Remote project is repurposed as Djehuti WebCam. Movie has an
authenticated private-IPv4 WebRTC receiver for VP8 video and Opus audio, exposed
as ordinary camera/microphone sources with preview, input levels/listening and
shared recording subscriptions. Synthetic native and actual-Movie VFS recording,
undo/redo and save/reopen checks pass. Galaxy S23 Ultra operation, physical sync,
sustained drift and interruption acceptance remain unverified. See
[WebCam Order 3](WEBCAM_ORDER_3.md) and [timing contract](WEBCAM_TIMING_CONTRACT.md).
The implementation is under PR review; no merge, deployment or release is implied.

## Capture Phase 4 (merged)

Track-owned synchronized recording and incremental VFS persistence are implemented
in merged Phase 4, including its VFS dependency. Focused and broader validation
passed. See
[Capture Phase 4](CAPTURE_PHASE_4.md) for architecture, evidence and limitations.
Phone capture remains outside this phase.

## Capture Phase 3

Camera and microphone adapters extend the existing capture manager, with native
format catalog selection, camera Source Monitor preview, microphone input meters,
independent listening and shared physical acquisition. Source assignments persist
on tracks. See [Capture Phase 3](CAPTURE_PHASE_3.md) for measured validation and
hardware limitations; recording is described in Phase 4 above.

## Capture Phase 2

Existing video tracks can select an individual display or application window and
show actual live capture in Source Monitor. Tracks selecting the same source
share one physical Windows Graphics Capture acquisition. Assignment, preview
state, clear and undo/redo use the existing document/persistence paths. Recording
is described in Phase 4 above; phone capture remains a later phase. Camera/microphone adapters are described above. See
[Capture Phase 2](CAPTURE_PHASE_2.md) for validation and platform/identity limits.

## Milestone 5 (merged)

Native video/audio transitions, timeline editing, exact properties, persistence,
agent commands and shipped training cards are implemented and merged in PR #38 (`8c8ac14`). Build, console tests and all 18 focused
application check groups passed for that milestone.
See [Transitions](TRANSITIONS.md) and HANDOFF.md for evidence and limits.
Capture Phase 4 adds recording to this merged foundation.

## Implemented foundation

Milestone 4 adds persistent effect automation lanes, timeline keyframes and exact
time/value controls. Audio evaluates per sample and video per frame through the
same preview/export renderer. See [Automation](AUTOMATION.md).

Milestone 3 adds functional track effects through the common preview/export
renderer: gain, balance, fades, filtering, colour, opacity and crop/transform.
The docked Effects browser/inspector edits ordered, persistent, undoable chains.
See [Effects](EFFECTS.md) for schema-4 compatibility, processing order, validation
and the explicit effects-track routing/performance limitations.

- Docked media, source/program preview, timeline, properties and agent surfaces.
- FFmpeg-backed probing and decoding, including direct reads from project VFS entries.
- Source-aware clip placement, trimming and splitting; removal and ripple deletion.
- Title overlays, clip gain/opacity, document undo/redo and versioned VFS persistence.
- Playback and background exports using MP4, ProRes/MOV and FFV1/MKV presets.
- Shared Virtual Engineer tools and authenticated localhost automation commands.
- Dynamic track headers, Add/Remove Track, inline naming, drag/menu reordering,
  synchronized vertical scrolling and a docked Track Inspector. See
  [Track interface](TRACK_INTERFACE.md) for configuration and validation limits.
- Persistent user-managed track document, stable clip ownership, source/arm/monitor
  preferences, and automation curve/target storage. See
  [Track document](TRACK_DOCUMENT.md) for commands and version-2 migration policy.

These are implementation capabilities, not a claim that every format, workflow or UI path has passed acceptance testing.

## Project and job contract

Create, open, save, import and export execute their storage/media work in a background job. Only one such job runs at a time. A command's returned `jobId` identifies its job; the initial command result is acceptance, not completion.

The job endpoint reports `running`, then `completed`, `failed` or `cancelled`, with a result or error. Terminal completion is published on the message thread after applying the result. Workers do not write status/result strings while the API is reading them. Progress and cancellation are atomic.

While a job is running, state queries, transport and selection remain available. Conflicting edits, project changes and close requests are rejected with `job_busy`. Imports retain their originating project identity and preserve the existing undo history.

The UI asks Save / Discard / Cancel before replacing an unsaved project. Automation must save first or explicitly pass `discardChanges: true` to `movie.project.new` or `movie.project.open`; otherwise it receives `unsaved_changes`. Failed opens leave the active document intact.

Save writes a captured document and commits the project off the UI thread. Dirty state clears only after success and only if the active document still matches that snapshot. Autosave uses this same path after roughly one minute of idle, dirty time. Closing a dirty document waits for save success; a failed save retains both the application and unsaved edits. `movie.app.close` uses that same close path. Save-before-switch similarly proceeds only after success.

Cancellation is a request. Some storage calls cannot be interrupted. If an operation has already completed its durable work, it may finish successfully even if cancellation arrives late. Wait for the terminal status before issuing the next mutation. If the app disappears before a command's response can be read (clean close), verify process termination.

## Focused live check

After building and with Movie closed, run `scripts/Check-MovieLifecycle.py` using Python and explicit `--heartbeat <VfsHeartbeat.json>` and `--media <recording.mp4>` paths. Use a sufficiently substantial recording to exercise the active import guard.

The script launches the actual Debug application, creates a uniquely named disposable project, imports real media, checks job/unsaved guards, saves and reopens edits, and exports a short sequence. It then deletes only that run's disposable project after verifying its exact ID and name, deliberately causing save-on-close to fail. The check requires the app to stay open with dirty edits and an error, restores the originally opened project, and closes normally.

It does not restart the shared VFS, terminate someone else's Movie instance, or touch existing projects. Results and the short export are written under `build/`. A failure leaves the launched app available for inspection. This is a focused lifecycle check, not an exhaustive media or GUI regression suite.

## Current limits and further work

- The media path is currently SDR/8-bit. HDR and display-rotation metadata need deliberate handling rather than a blanket professional-format claim.
- Professional proxy/cache management, waveforms, advanced mixing, grading and richer titles remain further editing work.
- Startup project enumeration and project-menu enumeration still use synchronous service calls; this change moves the explicit create/open/save operations off the UI thread.
- This job lifecycle does not add crash recovery or forcibly interrupt blocking storage calls.
- Development DLL copying is not installer completion. Release codec deployment, attribution and corresponding-source obligations need a packaging pass.
- Milestone 5 includes shipped transition knowledge cards; complete user Help still needs authoring. Registered command descriptions and schemas describe the current automation behavior.
