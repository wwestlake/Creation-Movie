# Track management interface

The timeline reads the versioned track document directly. No UI track list or
separate persistence exists. Selection, scroll position and inline rename state
are transient; names, types, heights, sources and preferences use the existing
undoable `movie.track.*` commands.

## Editing

- Choose **Add Track** above the timeline, then Video, Audio, Image, Effects or
  Automation. Creation works when the document has no tracks.
- Click a header to select its track and open the docked **Track Inspector**.
  **View > Track Inspector** restores that panel when another tab is active.
- Double-click a header or press F2 to rename inline. Return commits; Escape
  cancels. The inspector name field also commits on Return/focus loss.
- Drag a header onto another lane to move it to that position. The orange marker
  shows the target position; dragging near the viewport edges scrolls the list.
  Right-click > Move Up/Down or Alt+Up/Down provides a discrete alternative.
- The green/grey header indicator toggles Enabled. Disabled tracks and clips are
  visibly dimmed and retain the existing preview/export enable semantics.
- Arm/Mon preferences are shown on audio/video headers of at least 64 pixels.
  Their inspector captions explicitly state that recording/monitoring is inactive.
- Remove Track is available above the timeline, in the inspector and in the
  header context menu. Configured/occupied tracks require confirmation. Imported
  media is kept. Existing target-reference validation can reject removal.
- Ctrl+Z / Ctrl+Shift+Z undo/redo the same document operations as the API.

Header and clip layout use the same document heights and vertical scroll offset.
The wheel scrolls vertically; Shift+wheel scrolls time. Empty lanes remain
selectable. Existing mixed-media tracks are labeled explicitly.

Library Place, double-click, context-menu placement and Timeline menu placement
use the selected track. Invalid media/type combinations are rejected by existing
document validation. Clip drag/drop resolves destination ownership by stable ID.

## Inspector and sources

The scrollable Track Inspector exposes name, type, Enabled and lane height;
audio/video tracks also expose arm and monitor preferences. Changing type uses
the document's complete-content validation; incompatible contents/source settings
must be handled first.

Imported project sources are selected by media name from the compatible project
catalog. There is no second filesystem picker. Import external media through the
existing Media Library first.

Display/window/camera and microphone/system-audio entries are labeled **saved
configuration only**. A source label and optional requested format can be saved;
no device is enumerated, opened, monitored or recorded. Apply Source commits the
descriptor; Clear Source removes it. Neither changes existing clip contents.

Effects tracks explain that their routing is undefined; ordinary audio, video
and image tracks have functional effects through the docked [Effects browser](EFFECTS.md).
Automation tracks show
the stored target's track/clip names, parameter, effect-presence indication and
point count. Curves, bindings and effect IDs are preserved; no automation runtime
or curve editor is introduced.

## Reuse and validation

Station's TrackerPanel was inspected. Its headers are coupled to Station's
timeline/audio-engine callbacks and are not a standalone shared UI component.
Movie uses the existing shared TimelineTrack/automation representation and its
existing document commands; it does not copy Station's panel or change Station.
Docking, controls, scrollbars, text editing and popup anchoring use the existing
JUCE/shared suite infrastructure.

Debug builds accept:

```
"Djehuti Movie.exe" --track-ui-check "<output-directory>" "<existing-video>"
```

This opt-in integration check opens the actual application window and creates a
uniquely named VFS demonstration project. It exercises mounted control callbacks,
inline editing and timeline drag handlers, prepares imported WAV/PNG fixtures,
places real video/audio/image media, checks undo/redo and save/reopen, renders
window-content PNGs, then restores the initially opened project and requests
normal quit. Results identify native pointer testing separately; synthesized
JUCE events do not establish OS-level pointer delivery. It is excluded from
Release builds. Existing user projects are not used as test fixtures.

The document tests and `scripts/Check-MovieLifecycle.py --tracks` remain focused
regression checks for migration, persistence, playback and export.

Milestone 4 adds automation keyframe lanes and editing through the Effects dock;
see [Automation](AUTOMATION.md). Historical "stored only" limitations above
are superseded for resolved effect-parameter targets; capture/source monitoring
and effects-track routing remain deferred.
