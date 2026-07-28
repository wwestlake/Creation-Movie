# Creation Movie Vision

Creation Movie is not just a video editor. It is the video production sibling to Creation Station: a full Creation Suite environment for recording, editing, composing, processing, previewing, and rendering moving-image work inside the same integrated system of systems.

This document captures the intended product direction as of July 28, 2026.

## Core Identity

Creation Movie should be treated as the video suite counterpart to the audio suite:

- Creation Station is the sound studio
- Creation Movie is the picture studio
- Creation Engine is the source of rendered world/media output
- Creation Live is the performance and broadcast layer

The goal is a unified creative platform where projects, assets, automation, language tooling, and helper systems can move across apps without feeling like exports between strangers.

## Product Vision

Creation Movie should support the full video workflow, not only timeline cutting:

- video recording and ingest
- non-linear editing
- audio and video effect processing
- title, caption, motion, and compositing workflows
- render orchestration
- helper-assisted editing through the suite's BYOK AI system
- control-surface-driven transport and editing workflows, including X-Touch support

It should feel to video creators the way Creation Station feels to audio creators: deep, integrated, tool-rich, and friendly to both manual craft and assisted workflows.

## Shared Suite Principles

Creation Movie should follow the same platform rules as the other Creation apps:

- use the shared suite VFS and asset model rather than ad hoc local file handling
- use the shared Creation language with movie-safe domains only
- use shared suite settings, shared identity, and shared helper plumbing
- participate in suite-wide asset references, metadata, versioning, and provenance
- preserve original creator media and avoid destructive mutation of source assets

This means Creation Movie should not be planned as a standalone editor first and integrated later. It should be designed from the beginning as a suite-native application.

## BYOK Helper

Creation Movie should have the same kind of BYOK helper presence as Creation Station, adapted to video work.

The helper should eventually assist with:

- edit planning
- shot organization
- rough-cut suggestions
- dialogue and caption workflows
- timeline search and navigation
- render setup guidance
- conform and relink assistance
- scene, take, and asset tagging
- cross-app workflows such as pulling audio from Station or rendered footage from Engine

The helper should understand the project, timeline, assets, sequence structure, and render targets in the same general way the audio helper should understand sessions, tracks, clips, and tools.

## Video Suite Scope

Creation Movie should become a full video suite with these major capability areas.

### 1. Capture And Ingest

- import camera media
- import generated renders
- import image sequences
- import stills, graphics, overlays, and titles
- import audio assets from Creation Station
- record video and synchronized audio where supported
- preserve original media as source assets

### 2. Timeline And Editing

- multi-track video timeline
- linked and unlinked audio/video clip handling
- trim, split, slip, slide, ripple, overwrite, and insert editing
- markers, regions, and sequence-level metadata
- nested sequences and reusable editorial structures
- timeline zooming and navigation that can scale to long-form work

### 3. Monitoring And Review

- source/program monitoring
- playhead-locked navigation
- real transport behavior with scrubbing and shuttle concepts
- editor-local preview workflows
- visual/audio sync confidence across playback paths

### 4. Effects And Processing

- video effect chain support
- audio effect chain support
- automation for effect parameters over time
- plugin hosting where appropriate for both sound and picture workflows
- shared internal Creation plugins first, with external plugin support where practical

### 5. Render And Delivery

- render queue
- intermediate and delivery targets
- image sequence export
- audio stem or mixdown coordination where needed
- structured handoff to Creation Live and other suite tools

## Reuse From Creation Station

Creation Movie should deliberately reuse proven ideas from Creation Station wherever that makes sense:

- transport concepts
- timeline navigation patterns
- automation patterns
- session awareness and helper context plumbing
- control-surface mapping patterns
- shared plugin-management ideas
- asset-library presentation and metadata blocks
- suite settings entry points

This does not mean the UI should become an audio UI painted over a movie app. It means the suite should reuse the best interaction models and infrastructure where the concepts are equivalent.

## Audio Integration With Creation Station

Creation Movie must be able to use audio assets and results coming from Creation Station.

Examples:

- use rendered music cues from Station in Movie sequences
- use dialogue cleanup or processed stems from Station
- relink to updated shared assets when the user intentionally updates an asset version
- retain references to approved asset versions so edits remain stable and reproducible

Important rule:

- the system should never destroy or silently overwrite the artist's original work

That means source recordings, original imports, and important derived renders need clear provenance and safe version behavior.

## Video Integration With Creation Engine

Creation Movie must also be able to pull rendered video output from Creation Engine.

This implies a Creation Engine subsystem that should be specified clearly:

- render scenes or sequences from the engine into suite-managed media assets
- output image sequences, video renders, alpha-capable renders, and supporting metadata
- publish those outputs into the suite asset/VFS system so Movie can discover and use them directly
- preserve source/render provenance so editorial users know what came from the engine and when

Creation Engine therefore needs a defined render-output and publish pipeline, not just ad hoc file export.

## Plugin Vision

Creation Movie should eventually support both audio and video processing chains.

Desired direction:

- audio plugins for dialogue, music, ambience, mastering, and cleanup
- video plugins for look, transform, correction, stylization, compositing, and utility work
- automation of plugin parameters on the timeline
- internal Creation plugins first, with external hosting where practical

This should be designed with the same seriousness as plugin work in Station, not as a minor accessory.

## Control Surface Vision

X-Touch support should matter here too.

Desired control areas include:

- transport
- jog/shuttle/scrub
- track or lane selection
- level and mix control for sequence audio
- automation arming and writing
- timeline navigation and zoom
- marker movement and placement

Creation Movie should reuse the suite mapping system pattern so hardware mappings feel consistent from app to app.

## Pipeline Direction

Creation Movie will need its own video pipeline. We should not assume the audio-side plumbing is enough.

Expected architectural needs:

- decode pipeline
- frame cache strategy
- preview pipeline
- timeline composition pipeline
- effect evaluation graph
- synchronized audio/video playback path
- render pipeline

Where possible, it should still reuse shared suite systems around:

- assets
- settings
- helper context
- plugin discovery/management concepts
- language integration
- control-surface mapping

## Language And Node System

Creation Movie should adopt the same shared Creation language and the same general node-programming philosophy used elsewhere in the suite, but with movie-safe domains.

Likely movie domains:

- `movie`
- `timeline`
- `media`
- `render`
- `caption`
- `color`
- `composite`

Likely shared domains:

- `shared`

Blocked domains should include anything app-specific that does not belong in Movie, such as direct game-engine or DAW-only execution domains unless mediated through explicit interoperable APIs.

## Asset And VFS Expectations

Creation Movie should be built against the suite asset/VFS direction already being established:

- assets are discoverable through suite-managed metadata
- apps can reference shared assets without copying them blindly
- assets can carry provenance, tags, type info, and version information
- original source media remains preserved
- derived outputs are first-class assets, not disposable temp files

For Movie specifically, important asset classes will include:

- source video
- source audio
- still images
- graphics
- titles/templates
- engine renders
- proxies
- conformed media
- intermediate renders
- final deliveries

## Integrated System Of Systems

Creation Movie only makes full sense as part of the integrated Creation Suite.

The long-term picture is:

- Creation Station produces audio assets and mixes
- Creation Engine produces rendered world footage and simulation-driven visual output
- Creation Movie assembles, processes, edits, and delivers finished video works
- Creation Live uses approved outputs for live presentation and broadcast

The suite VFS, shared language, BYOK helper model, control mapping model, and asset identity/version rules are what make this feel like one creative platform instead of four disconnected tools.

## Initial Planning Priorities

Suggested near-term planning sequence:

1. Define the Creation Engine render-publish contract for movie-ready outputs.
2. Define Movie asset classes and how they map onto the suite VFS.
3. Define the first real timeline model for picture plus synchronized audio.
4. Define the preview/render pipeline split.
5. Define the first Movie control-surface mapping targets.
6. Define BYOK helper context for editorial tasks.
7. Define which plugin standards are realistic for early audio and video effect hosting.

## Summary

Creation Movie should be developed as a suite-native video production system, not a basic editor.

It should:

- record and edit video
- integrate tightly with audio from Creation Station
- accept rendered media from Creation Engine
- support helper-assisted workflows
- support control surfaces like X-Touch
- host effect workflows for both audio and video
- share the suite's VFS, asset identity, language, and configuration model

That is the intended direction.
