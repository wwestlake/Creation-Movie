# Djehuti Movie capabilities

## Implemented foundation

- Docked media, source/program preview, timeline, properties and agent surfaces.
- FFmpeg-backed probing and decoding, including direct reads from project VFS entries.
- Source-aware clip placement, trimming and splitting; removal and ripple deletion.
- Title overlays, clip gain/opacity, document undo/redo and versioned VFS persistence.
- Playback and background exports using MP4, ProRes/MOV and FFV1/MKV presets.
- Shared Virtual Engineer tools and authenticated localhost automation commands.

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
- Professional proxy/cache management, waveforms, advanced mixing, transitions, grading and richer titles remain further editing work.
- Startup project enumeration and project-menu enumeration still use synchronous service calls; this change moves the explicit create/open/save operations off the UI thread.
- This job lifecycle does not add crash recovery or forcibly interrupt blocking storage calls.
- Development DLL copying is not installer completion. Release codec deployment, attribution and corresponding-source obligations need a packaging pass.
- Movie-specific shipped knowledge cards and complete user Help still need authoring. Registered command descriptions and schemas describe the current automation behavior.
