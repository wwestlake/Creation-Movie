# Djehuti Movie implementation handoff

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
