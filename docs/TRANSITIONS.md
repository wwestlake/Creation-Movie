# Native Movie transitions (Milestone 5)

## Editing

Select the outgoing clip and choose **+ Transition**, the clip's **Add
transition...** context action, or **View > Transitions**. The docked editor
offers outgoing/incoming clips, type, exact start/duration, derived end,
curve/tension, wipe direction/softness and black midpoint. Choose **Create
transition** or **Apply exact properties**. Times are decimal seconds; numeric
entry is authoritative and is not silently rounded to video frames. Property
fields retain enough digits to reapply their displayed value without rounding.
Invalid or
non-finite text is rejected. Timeline mouse gestures snap to sequence frames.

Transitions have orange crossed indicators over their owning track; selection
is yellow. Video and audio occupy separate strips so both may share one interval.
Click/double-click opens properties. Drag the left/right boundary to resize or
the centre to move; the edit commits on release, matching clip movement. Use
Delete, the indicator's context menu or the editor's Delete transition button.
All changes are undoable. The saved-transition chooser can select indicators
that are outside the visible timeline. New from selected clip seeds a draft;
changing clip choices does not alter their placements or source ranges.

## Types and processing

| Type token | Behavior |
| --- | --- |
| `video.dissolve` | Premultiplied alpha/colour blend of outgoing and incoming frames |
| `video.black` | Outgoing fades into opaque black, then incoming fades from black; midpoint 0.01–0.99 |
| `video.wipe` | Left/right or top/bottom reveal; softness 0–1 of the frame axis, with exact endpoints |
| `audio.crossfade` | Linear gains (1-p,p) or equal-power gains (cos(p*pi/2),sin(p*pi/2)) |

Video progress uses the shared linear, S-curve, power or Bezier segment evaluator;
tension is -1 to 1. Audio crossfades evaluate per sample. Their curve is linear
or equalPower. Equal power preserves the sum of squared gains, not the peak of
correlated signals: the existing final output ceiling can still limit loud sums.

Order is source decode -> clip gain/opacity -> pair transition -> track mixing/
composition -> ordered track effects with automation -> normal output. Video
black explicitly contributes black rather than revealing a lower track at its
midpoint, while preserving clip opacity at its endpoints. Other video transitions
retain premultiplied alpha. Audio decoding uses
the engine's existing stereo conversion (including mono-to-stereo); no extra
channel-layout or codec engine is introduced. Video transitions do not implicitly
change sound: create an audio transition as well when wanted, including on video
clips that contain audio. Disabled tracks remain disabled.

Preview, arbitrary-position renders and export use the same Renderer, saved
definitions and blend/gain functions. Per-clip decoder instances prevent two
offsets of the same asset from fighting over one decoder during a transition;
the existing 16-reader bound is retained. Stateful track effects use their
existing preroll and evaluated automation after the crossfade.

## Overlap and source handles

Pairs must be on the same track with compatible media, outgoing earlier than
incoming. Adjacent cuts and existing overlaps are supported; timeline gaps,
still images and title sources are rejected. A transition must cover the entire
existing overlap and remain within the pair's outer placement bounds. A third
compatible clip or another same-domain transition may not intersect the interval.
Audio and video definitions may coexist over the same pair/time.

For each participating clip, the full interval maps to source time:

```
source time = clip.sourceIn + timeline time - clip.start
```

Both ends must lie in actual media duration. A centered transition at an adjacent
cut therefore needs outgoing postroll and incoming preroll. If either is absent,
the operation rejects with a clear error. It never repeats frames, invents
samples, extends the source, changes clips, or performs a ripple edit. One-sided
placement can be entered explicitly when its handle requirements are satisfiable.

For two four-second videos, a useful explicit setup is A at timeline 0, sourceIn
0.5, duration 1.5; B at timeline 1.5, sourceIn 1, duration 1.5. A transition from
1 to 2 seconds has sufficient handles without modifying either placement.

## Persistence and editing safety

Schema 5 adds an ordered `transitions` array with stable id, owning track,
outgoing/incoming clip IDs and all parameters. Times are saved as numeric seconds
without frame quantization using the existing document JSON serializer;
end is derived for UI/API queries rather than redundantly persisted, and any
supplied end is checked against start+duration. Versions 2/3/4 read with empty
transition lists. Unknown transition types/properties, malformed definitions or
transition data mislabeled as an older schema reject rather than disappear.
Failed loads/edits leave the active document intact.

Track reorder retains stable ownership. Explicit clip/track removal cascades
associated transitions in the same undo transaction. Other timeline edits,
including a split, move, trim or ripple operation that would invalidate a pair,
reject atomically; remove/update the transition first when necessary. Compatible
edits preserve IDs. Opening old data does not write until a successful save.

## Agent interface

The integrated Virtual Engineer and authenticated external Agent API expose the
same registered tools/handlers as GUI operations:

| Command | Arguments/result |
| --- | --- |
| `movie.transition.catalog` | Types, names, defaults, units, ranges and enum values |
| `movie.transition.list` | All saved definitions |
| `movie.transition.get` | `transitionId`; returns exact properties |
| `movie.transition.validate` | Create properties, or `transitionId` with changes; read-only compatibility/handle dry run |
| `movie.transition.create` | `trackId`, `outgoing`, `incoming`, `type`, `start`, `duration`; optional curve/direction/softness/midpoint/tension; returns stable id/properties |
| `movie.transition.update` | `transitionId` plus changed writable properties |
| `movie.transition.remove` | `transitionId` |

Start >=0, duration >=1/48000 second, end <=86400 seconds. Video time is sampled at
export frame starts; audio uses sample time. End and id are read-only. Invalid
edits return edit_rejected; dry-run errors return invalid_transition. Query of a
missing id returns unknown_transition. No background job is needed for an edit.
Existing save/export commands remain asynchronous and must be awaited.

Two built-in read-only training cards in `Source/MovieTransitionCards.h` are
registered through `VirtualEngineer::setShippedCards`. They document types,
parameters, numerical units, source/overlap requirements, failure handling and
the complete dissolve + crossfade + duration update + save + export workflow.
They appear in Cards and in `/v1/cards/match` retrieval, with ordinary VFS suite,
app and project scopes still available. No OS card store or per-app AI path is
introduced.

Example agent workflow, substituting real stable IDs for V1/V2/VT and A1/A2/AT.
The pairs are adjacent at 2 seconds and each has at least 0.5 second handles:

1. Validate then call `movie.transition.create`:
   `{"trackId":"VT","outgoing":"V1","incoming":"V2","type":"video.dissolve","start":1.5,"duration":1,"curve":"linear"}`.
2. Retain returned id D. Create the audio crossfade:
   `{"trackId":"AT","outgoing":"A1","incoming":"A2","type":"audio.crossfade","start":1.5,"duration":1,"curve":"equalPower"}`.
3. Retain returned id F. Update each with
   `{"transitionId":"D-or-F","start":1.75,"duration":0.5}`.
4. Query both: end must be 2.25. Call `movie.project.save {}` and wait for its
   completed job. Then call `movie.export` with the user's selected absolute
   `path` and `preset:"mp4"`, and await successful job completion.

For a wipe elsewhere, use another valid pair, `video.wipe`, direction
leftToRight and softness 0.15. Do not reinterpret a validation error as permission
to move unrelated clips or manufacture handles.

## Verification

Focused document tests check exact pixels for all required types/directions,
softness/endpoints, audio gain laws and sample continuity, handle/conflict
rejection, unchanged clips, numerical edits, identity, cascade/undo and old/new
schema roundtrips. Existing effects and automation tests remain part of the target.

The opt-in Debug application check is:

```
"Djehuti Movie.exe" --track-ui-check "<new-output-directory>" "<four-second-video>" --transitions-ui-check
```

It extends the track/effects/automation checks with real transition controls,
numeric validation, timeline selection/boundary/move/delete events, agent
commands, shipped cards, save/reopen, actual playback/restart and FFV1/PCM + MP4
export. Generated WAV and imported video run through the production renderer;
both audio curves are compared with actual decoded source samples. The optional
read-only `scripts/Check-MovieTransitionApi.py` verifies external HTTP command
registration, query/dry-run behavior and training-card retrieval while it runs.
Native mouse input is not claimed; see HANDOFF.md for actual results and limits.

CPU/8-bit SDR throughput and decoder/track-filter preroll limitations remain.
Lossless WAV crossfade seeking matches continuous processing exactly in the
focused check. The AAC fixture retains a small seek residual (about 0.000127
peak sample amplitude), reproduced by the source-only comparison with identical
preroll. Transition processing does not remove this existing decoder limitation.
There is no transition GPU engine, automatic gap filling, still/title transition,
cross-track routing or implicit video/audio linkage in this milestone.
