# Multitrack acquisition and Central Viewer

Movie records inputs independently, like a DAW for video. Every enabled camera,
Android camera, display/window capture and microphone records continuously on its
own track using the same take origin. Viewer selection never changes the set of
recorders. Capture generates no composite/program take.

## Use

1. Pair the phone, then use **Central Viewer > Add input...** to select cameras,
   phone video/microphone, monitors and windows. Each input gets a named track;
   choosing an existing input reuses its track.
2. Enable each desired input using **REC on/off** in the timeline header or
   **Recording input enabled** in Track Inspector. There is no extra Arm step.
   Microphone listening remains optional and independent of recording.
3. **Record N inputs** or the main Record control starts one coordinated take.
   Change the viewer selector or a track's **View** control freely. **Timeline
   review** monitors edited material while source recording continues.
4. Stop finalizes each asset and its synchronized placement. Trim, split,
   transition, composite and mix placements afterward; originals are retained.

Enabled inputs stay acquired for monitoring between takes. Stop recording
finishes media writing. To release a local device, disable its recording inputs
and select Timeline review, or close Movie. A disabled input selected for
monitoring remains acquired and is labeled monitor only. Disconnect the phone
to release its capture session. Source assignments, enabling and Undo/Redo are
locked during a take. Unavailable
devices report errors rather than silently substituting a different source.
Window inputs must be selected again after restarting Movie.

## Implementation and evidence

Existing VisualCapture adapters, bounded subscription queues, per-track writers,
the common capture clock, VFS streaming and document publication are reused.
Central Viewer reads a selected capture snapshot directly. Viewer state is
transient; it never edits track/source configuration. Legacy `armed` flags remain
readable but no longer gate participation. Enabled sources stay acquired together.

Take/timing records preserve source identity/name, native clock/epoch, original
frame dimensions, mapped host time, uncertainty and exact placement offsets.
Window resize keeps recording at the file's initial dimensions with aspect fit.
Readback respects actual texture bounds during frame-pool resize.

Debug build, document and WebCam timing/lifecycle checks pass. Native WGC checks
pass for changing pixels, resize, displays, shared acquisition and cleanup.
`tests/webcam-lan/Verify-MultitrackViewer.ps1` passes using the physical C920,
synthetic Android/WebRTC video/audio, a screen and a resizing window: five
independent recordings around ten seconds, viewer switching, disabled-input
exclusion, common offsets, nondestructive trim/undo and save/reopen. Actual
mounted-viewer PNGs and results are in ignored `build/multitrack-viewer-check/`.
The final Undo/Redo lock builds successfully; its live-recording assertions are
included for the next hardware run. Physical phone/client acceptance is pending
with WebCam 0.0.12 visibly displayed in Movie. Camera testing is stopped and Movie
is shut down; resume it only with the operator. Synthetic timing does not measure
phone sensor latency or physical A/V drift.

Official OBS [sources](https://obsproject.com/kb/sources-guide),
[overview](https://obsproject.com/kb/obs-studio-overview), and
[audio-track guidance](https://obsproject.com/kb/multiple-audio-track-recording-guide)
were reviewed. Movie's capture contract is independent acquisition followed by
postproduction; live scene/program switching is outside that contract.

Tracking: https://github.com/wwestlake/Creation-Movie/issues/52
