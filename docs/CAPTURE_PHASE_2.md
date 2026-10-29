# Djehuti Movie — Multisource Capture Phase 2

Date: October 28, 2026  
Status: Implementation and validation complete; ready for PR review, not yet integrated.  
Branch: `feat/movie-screen-window-capture`  
Base: Movie master `8c8ac14` (merged transitions); Suite integration PR #208 is merged.

## Delivered behavior

Create/select an existing VIDEO track, choose Screen / individual display or
Application window in Track Inspector, select an available source, and apply it.
The existing Source Monitor shows actual live capture. Assignment enables that
track's live preview preference. Clear Source removes the binding; the Live
capture preview checkbox and Source Monitor Preview/Pause/Stop controls manage
its subscription. Capture assignment does not replace imported clips or modify
the program timeline.

Discovery lists individual monitors with device labels, dimensions, coordinates
and primary-display indication. Each selected screen is one `HMONITOR`, never
the virtual desktop. Window choices include title, process ID, geometry and a
short opaque selector to distinguish otherwise identical windows. Availability
refreshes in the background; Refresh available sources also retries failed
acquisition. Selection is explicit, never an automatic fallback to another item.

Multiple tracks may select the same source. They share one physical capture
session and immutable latest-frame snapshots. Stopping/clearing/removing one
track does not stop another subscriber. The final unsubscribe releases the
capture session. Source assignment and preview preference use existing document
transactions, undo/redo and project save/reopen. No tracks are automatically
created by acquisition.

## Technology and component responsibilities

- `Source/MovieVisualCapture.h/.cpp`: Windows Graphics Capture using Win32
  monitor/window interop, D3D11 and a free-threaded two-buffer frame pool.
  One MTA worker owns discovery, physical sessions, frame readback and cleanup;
  the GUI never performs GPU readback. The manager tracks subscriptions by
  stable Movie track ID and physical source selector, not by track profile.
- The worker retains one current software image per active physical source and
  publishes QPC-based timestamps in 100 ns units. This is preview infrastructure,
  not a recording queue, encoder or synchronization engine.
- A fresh bounded staging readback is used per acquired frame. Reusing the
  staging resource produced stale pixel readback on the validation machine even
  while capture timestamps advanced. The changing-pixel regression detects this.
  Immediate-context multithread protection is enabled.
- Window lifetime events retire selectors on destruction. Closed/minimized or
  missing sources clear the preview and expose unavailable/failed status; they
  are not replaced silently. Size changes recreate the capture frame pool.
- `Source/TrackInspector.inc` integrates discovery/assignment with existing
  controls; `Source/MainComponent.cpp` reuses Source Monitor and existing command
  registration. Program Monitor and the existing media renderer remain intact.
- `Source/MovieDocument.cpp` makes visual assignment and preview activation one
  undoable edit. Clearing a visual assignment removes its active subscription.
  Source configuration retains the existing schema; no media recording or
  subframe-timing schema change is introduced in this phase.

The installed Windows SDK 19041 C++/WinRT projection uses legacy coroutine
headers. Only the capture adapter translation unit uses C++17 `/await`; the
application and remaining sources retain C++20. No dependencies were installed,
LLVM changed, or shared Suite source modified.

## Agent access

- `movie.capture.sources`: read-only current display/window descriptors.
- `movie.capture.status`: read-only acquisition states, active session count,
  subscriber counts, frame counters, image dimensions and QPC timestamps.
- Assign through existing `movie.track.source` with an existing video `trackId`
  and `{category: display|window, identity: <catalog ID>}`. The application checks
  that the requested ID/kind is currently available before accepting a new binding.
- Existing `movie.track.preferences` controls `monitoring`; clearing the source
  uses the established source edit. Saved unavailable bindings are retained on
  project load rather than discarded.

These tools use the same document operations as the GUI. No LAN listener,
camera/microphone source, capture recording command or new project model is added.

## Validation evidence

- Single-worker Debug application, MovieDocumentTests and MovieVisualCaptureTests
  builds passed. Existing compiler/SDK warnings remain; not a zero-warning claim.
- MovieDocumentTests passed existing document/effects/automation/transition
  regressions and new visual-assignment preview activation, persistence,
  clear/undo/redo checks. Log: `build/capture-phase2-document-tests.log`.
- Actual WGC validation passed on **all three available physical displays** and
  a changing application window: live pixels, increasing QPC timestamps, exact
  individual monitor extents, one acquisition for two subscribers, independent
  stop/switch, disappearance on window close and final resource release.
  Evidence: `build/capture-phase2-native-final/results.json` and
  `build/capture-phase2-native-final.log`.
- Authenticated external API discovery/catalog/status checks passed with unique
  selectors and three display descriptors. The check was read-only.
  Evidence: `build/capture-phase2-api-results.json`.
- Mounted Movie interface checks passed: display/window assignment and preview,
  shared acquisition, independent clearing, source switching, save/reopen,
  undo/redo and closed-window handling. Evidence:
  `build/capture-phase2-ui-d/track-ui-results.json`.
- All 18 existing editing, effects, automation, transitions, playback and export
  regression groups passed with an empty error field. Evidence:
  `build/capture-phase2-regression/track-ui-results.json`.

The native test uses an owned JUCE window that changes its actual rendered
pixels. The Movie interface test uses synthesized component events in the actual
application, not native pointer input. Validation images are local ignored build
artifacts and are not committed or published.

## Limits and Phase 1 conformance

- Requires Windows 10 version 1903 or later with supported WGC/D3D hardware.
  Protected content, permission/platform restrictions and inaccessible windows
  may fail or be blank; there is no capture-protection bypass.
- Preview is BGRA/8-bit SDR. HDR tone mapping is not implemented. Full-resolution
  readback has a resource cost; this phase does not establish production recording
  throughput or a guaranteed source-count/frame-rate envelope.
- Own-process Movie windows are omitted from the window list. Display capture
  still contains the actual desktop on that selected display, including Movie
  when it is visible there.
- Window selectors deliberately expire when Movie restarts. Their assignments
  persist, but users must explicitly select the current window again. No HWND is
  serialized or silently rebound to a different window. Display identity uses the
  monitor device interface rather than its renumberable desktop slot.
- Physical monitor unplug/replug, protected-window rejection, HDR, device/driver
  failure and every third-party window type have not been exhaustively tested.
  Window disappearance and individual display capture were tested directly.
- No Phase 1 architectural deviation: track-owned assignments, shared physical
  acquisition and independent subscription lifetimes are retained. Phase 2's
  narrower work order explicitly requires preview, not recording. VFS streaming
  writes are not needed for this phase and remain approved infrastructure for the
  later phase that needs them.
- Camera/microphone capture, synchronized multitrack recording and LAN phone
  capture remain Phases 3, 4 and 5 respectively. Phase 3 is not authorized here.

## Integration

Validation is complete. Publication follows the required
PR to master; do not bypass Executive Director review. The approved Phase 1
report is included with this work so the authoritative reference is versioned.
Update the umbrella reference only after Movie merges, through its own PR.
