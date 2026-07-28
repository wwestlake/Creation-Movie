# Creation Movie Board Plan

This document turns the Creation Movie product vision into a practical board plan.

It reflects the intended direction as of July 28, 2026.

## Planning Intent

Creation Movie is not a generic editor. It is a suite-native picture workstation that must:

- edit video and synchronized audio
- reuse suite asset, VFS, helper, and control infrastructure
- exchange media cleanly with Creation Station and Creation Engine
- preserve original creator media without destructive mutation

The board should therefore be organized around systems first, then workflows, then polish.

## Recommended Board Structure

Use one set of epics on the suite board for Creation Movie, with `Movie` labels on all related issues.

Recommended labels:

- `Movie`
- `Movie-Core`
- `Movie-Timeline`
- `Movie-Playback`
- `Movie-Assets`
- `Movie-Plugins`
- `Movie-Controls`
- `Movie-UI`
- `Movie-Render`

## Epic 1: Movie Workspace Shell

Goal:
Establish the main editor shell and visual structure so the app feels like a Creation Suite tool from day one.

Key requirements:

- top-level workspace layout consistent with the suite
- dockable or pop-out panels
- dedicated pop-out video preview window
- suite-level settings access from the app header
- clear separation of source, timeline, inspector, library, and preview areas

Why this comes first:
The rest of the app needs a stable workspace model before we wire real editing behavior into it.

## Epic 2: Timeline Model And Video Tracker

Goal:
Build the core editorial timeline model.

Key requirements:

- a video tracker that behaves like the audio tracker in overall interaction philosophy
- timeline displayed in real time with frame granularity
- visible timecode and frame position at the playhead
- multiple video and audio lanes
- clip placement, trim, split, move, ripple-ready foundations
- markers and regions
- horizontal zoom and navigation that scale to long edits

Important rule:
This is not a music timeline. There is no MIDI piano-roll or instrument workflow here. The time model should center on seconds, timecode, and exact frame position.

## Epic 3: Playback, Preview, And Scrub

Goal:
Provide trustworthy editor playback.

Key requirements:

- synchronized audio and video playback
- editor-local transport
- playhead stays visible during playback
- source and program preview concepts
- pop-out preview window that stays in sync
- scrub and shuttle behavior
- audible scrubbing where practical

Important note:
Scrub behavior should align with the suite control mapping system so hardware control can drive it cleanly.

## Epic 4: Media Ingest And Asset Binding

Goal:
Make importing and managing picture media suite-native instead of file-chaotic.

Key requirements:

- accept video files
- accept audio files
- accept still image formats
- preserve originals as source assets
- create suite asset records with metadata and provenance
- support relink, version awareness, and non-destructive derived media
- allow media from Creation Station and Creation Engine to appear as normal project assets

Examples of metadata:

- duration
- resolution
- frame rate
- audio channels
- sample rate
- codec or container summary
- source app
- asset version

## Epic 5: Shared VFS Integration

Goal:
Use the same suite-standard VFS and asset access pattern as the other apps.

Key requirements:

- project media referenced through suite VFS contracts
- app knows where its own projects live through suite config
- shared suite VFS can locate shared assets and cross-app outputs
- no destructive mutation of original imported media
- derived files tracked as first-class assets

This is where Movie must match the suite standard rather than inventing a separate media store.

## Epic 6: Control Surface And MIDI Mapping

Goal:
Let users drive editing functions with the same mapping philosophy used elsewhere in the suite.

Key requirements:

- no musical MIDI input workflow
- yes to MIDI as a control surface layer
- reuse or generalize the Creation Station MIDI control router into a shared library
- transport mapping
- jog and scrub wheel mapping
- zoom, marker, track focus, and selection controls
- clear on-screen indication for special transport modes

Architecture note:
This should become suite shared code, not a one-off Movie implementation.

## Epic 7: Audio Path And Audio Effects

Goal:
Treat sequence audio seriously.

Key requirements:

- audio lanes in the timeline
- linked and unlinked audio/video clip handling
- per-track and per-clip audio routing where appropriate
- accept VST3 plugins for direct audio effects
- support automation-ready audio effect parameters

Important note:
Movie should not pretend to be a DAW, but it should absolutely support professional audio handling inside the edit.

## Epic 8: Internal Video Effects And CEL Plugin System

Goal:
Build the native effect system for picture processing.

Key requirements:

- internal video effect plugin model
- CEL-driven effect logic where practical
- parameter model suitable for automation
- frame or clip level processing contracts
- standard utility effects first
- clear boundary between real-time preview evaluation and render-quality evaluation

Expected early effect classes:

- transform
- crop
- opacity
- color adjustment
- blur
- compositing utilities
- title or overlay helpers

## Epic 9: Render Pipeline

Goal:
Render finished work reliably.

Key requirements:

- render queue foundations
- preview-quality versus final-quality processing modes
- export to standard movie formats
- image sequence export path
- audio mix integration during video render
- asset publishing of final renders back into the suite

## Epic 10: Cross-App Media Workflows

Goal:
Make Creation Movie feel like part of one suite.

Key requirements:

- import rendered audio assets from Creation Station
- import rendered video or image sequences from Creation Engine
- preserve provenance back to the source app
- support stable version references and intentional upgrades
- allow helper-aware workflows across app boundaries

## Epic 11: Editor UX And Interaction Polish

Goal:
Make the editor pleasant and fast to use.

Key requirements:

- modern clip drag behavior
- smart scroll and zoom behavior
- visible current beat or frame equivalent in the edit grid
- better selection clarity
- flyover help for icon controls
- consistent suite styling instead of ad hoc panels

## Recommended Delivery Order

Recommended sequence:

1. Movie workspace shell
2. Timeline model and video tracker
3. Playback, preview, and scrub
4. Media ingest and asset binding
5. Shared VFS integration
6. Control surface and MIDI mapping shared layer
7. Audio path and VST3 audio effects
8. Internal CEL video effects system
9. Render pipeline
10. Cross-app media workflows
11. UX and workflow polish

## First Milestone

The first meaningful milestone should be:

"Open media, place it on a timeline, preview it in the editor and a pop-out monitor, scrub by frame, and save the edit as a suite-native project without damaging source media."

That milestone proves the shape of the app before we chase advanced effects.

## Suggested Initial Issues

Good first issue set:

1. Create Movie workspace shell with dock zones and pop-out preview host
2. Define timeline time model with seconds, timecode, and frame addressing
3. Implement first video tracker canvas with zoom and scrolling
4. Add synchronized playhead and keep-visible playback behavior
5. Define Movie asset types and metadata contract against suite VFS
6. Generalize MIDI control router into suite shared control library
7. Implement Movie transport mapping targets for X-Touch style devices
8. Define internal video effect plugin interface backed by CEL-capable contracts
9. Add VST3 audio effects support path for sequence audio
10. Define render queue and asset publish contract

## Summary

The board should not begin with "video effects" or "export presets" in isolation.

It should begin with the editing core:

- workspace
- tracker
- preview
- playback
- ingest
- suite-native assets
- shared control mapping

Once those are real, the plugin and render layers have a clean place to live.
