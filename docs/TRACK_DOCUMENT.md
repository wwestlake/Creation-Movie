# Movie track document (Milestone 1)

Schema `djehuti-movie-sequence`, version 4, keeps an ordered `tracks` array.
Milestone 3 adds persistent effects chains; version-3 documents remain readable
with empty chains. See [Effects](EFFECTS.md) for the added model and commands.
Array position is display/compositing order: later tracks paint over earlier
tracks, preserving the old renderer. Clips persist `trackId`; the shared clip's
`trackIndex` is a derived adapter for existing rendering and edit helpers.
Track IDs survive reorder, rename, source changes, undo and save/reopen.

## Reuse

Movie tracks extend the existing `creation::timeline::TimelineTrack` identity,
name and display representation used by Station. Automation points, shapes,
storage tokens and curve mathematics come directly from shared Timeline.
Shared split/delete helpers remain in use. Movie does not copy Station's
index-permutation/folder remapping algorithm: stable ownership resolves indices
after changing array order. Station and shared source are unchanged.

Movie adds the video/audio/image/effects/automation type vocabulary without
changing Station's enums or processing. Track types are persisted as tokens;
unrecognized future tokens fail loading rather than silently becoming audio.
Effects and automation tracks cannot hold media placements in this milestone.

## Commands

`movie.track.list` returns tracks in order; `movie.track.get` takes `trackId`.
All mutations use the same document undo snapshots as timeline edits:

| Command suffix | Arguments beyond trackId |
| --- | --- |
| create | type, optional name; no trackId required; ID returned in sequence |
| remove | optional removePlacements=true; otherwise occupied tracks reject removal |
| rename | name |
| reorder | order: final zero-based array position |
| type | type; contents, source and preferences must remain compatible |
| enabled | enabled: boolean |
| source | source: descriptor or null to clear |
| preferences | optional armed, monitoring, height (44–512) |
| automation | automation: target IDs and points as described below |

Track removal never deletes imported media. Removing an automation target rejects
until the binding is cleared. Removing placements targeted by automation likewise
fails validation. No operation silently retargets a binding.
`movie.timeline.place`, `title` and `move` accept stable `trackId` as well as the
existing index argument. Default media placement chooses a compatible track.

Sources contain only `category`, `identity`, and optional `settings`. Categories:
video: media/display/window/camera; audio: media/microphone/systemAudio;
image: media. `media` identities must refer to compatible imported media.
Other identities are persistent source selection intent, never live HWNDs,
COM pointers or device handles. Settings allow width, height, fpsNum, fpsDen,
sampleRate, channels and channel. No discovery/activation occurs. Source changes
do not change existing clips. Arm/monitor preferences only apply to audio/video.

Automation contains `trackId`, `clipId`, `effectId`, `parameterId`, and `points`.
Each point has id, seconds, normalized value, shape and tension, using the shared
Station representation. Existing track/clip bindings are validated; effect and
parameter tokens are reserved opaque identities until those systems exist.
An empty points array and empty target IDs clear a binding. Evaluation and editor
UI are deferred; the shared curve evaluator is tested, not installed in playback.

## Version-2 compatibility

Loading is transactional: parse/validate into a new Sequence and replace only on
success. There are no migration writes. A successful explicit save writes v3.
Failed conversion leaves the existing persisted entry and active document intact.

All eight old indices map deterministically to `legacy-track-0` through
`legacy-track-7`, including empty lanes. They are flagged `legacyMixed` so audio,
video, stills and titles remain legal together. Their initial type is video as a
compatibility container, not an inference from the old lane labels. Existing
source offsets, frame timing, media versions, parameters and ascending layer
order are retained. Changing a legacy track's type removes the exception only if
the complete document validates. Unsupported clip kinds or invalid track indices
reject conversion explicitly rather than dropping content.

## Boundaries

Milestone 2 supplies dynamic headers, management controls, vertical scrolling
and the Track Inspector; see [Track interface](TRACK_INTERFACE.md). Effect
processing, device enumeration, recording and automation evaluation remain later milestones.
Disabled tracks contribute neither picture nor sound in preview/export; duration
continues to include their placements. Sources do not replace the media library.

Milestone 4 evaluates the existing effect-parameter automation targets/points.
The optional `automationVisible` flag defaults to true. Schema remains 4;
see [Automation](AUTOMATION.md) for editing, compatibility and runtime behavior.
