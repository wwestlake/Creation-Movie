# Movie effects automation (Milestone 4)

## Editing

Select an ordinary media track, open the Effects browser/inspector and select
an effect. Each parameter has **Automate**: this creates or opens its existing
automation track and focuses the same Effects dock. A new curve starts at zero
seconds with the parameter's current manual value. The track header names the
effect/parameter; the inspector also identifies the owning media track.

Double-click the automation lane to add a keyframe. Click a point to select it
in the inspector, and drag it to change time/value (committed on release as one
undoable edit). Timeline gestures snap time to sequence frames. The inspector
provides exact seconds/value entry, Apply, Add at playhead, Remove keyframe,
interpolation and tension. Precision entry accepts milliseconds; the command
interface accepts finer finite times. Coincident new keys are rejected.

**Show lane in timeline** hides presentation only; evaluation continues. Open
the same parameter's Automate button to edit or show a hidden lane. The track's
enable control instead suspends its automation and restores manual parameters.
Remove automation lane deletes that lane through ordinary undoable track removal.
Removing its effect or owning media track is rejected until the binding lane is
removed, preventing silent curve loss. Bypass/off retains the curve and resumes
at the current absolute time when the effect is re-enabled.

## Model and evaluation

There is no second automation store. Existing Movie automation tracks retain
stable target track/effect/parameter identities and shared normalized
`AutomationPoint` values, IDs, shape and tension. Renderer snapshots resolve
those identities once. Effect/track reordering changes no target identities.
Manual values remain in the effect; automation supplies evaluated overrides.

Shared `evaluateAutomationSegment` provides linear, step, S-curve, power and
Bezier interpolation. Endpoints hold before/after the curve. Choice parameters
always hold/step, including exact transitions at keys. Numeric values map
linearly between metadata minimum/maximum; cutoff is linear Hz even though its
manual slider uses a perceptual skew. Values retain the shared float normalized
precision, exactly preserved on save/reopen. A newly bound parameter has one
lane; duplicate bindings are rejected by the new editing commands.

Audio gain/balance/filter cutoff and mode (plus existing fades) evaluate per
sample at absolute timeline time on the worker. Curves/default lookups resolve
outside sample loops; sample processing allocates no curve or map entries.
Filter coefficients update when the evaluated cutoff/mode changes. Histories
and coefficient objects use the existing per-instance, per-channel JUCE IIR
processors. Discontinuous reads reset histories and replay the existing one
second preroll with automation evaluated throughout that history.

Video brightness/contrast/saturation/RGB, opacity, crop, scale and position use
the same evaluated parameter map at each rendered frame, through the existing
CPU effect chain. If opposing crop edges sum to 0.999 or more, only their
evaluated values are proportionally limited to preserve a non-empty region;
saved keys remain unchanged. Effect order stays authoritative.

Movie audio decoding also preserves the continuous sample clock within container
timestamp quantization tolerance, while retaining larger discontinuities. Seeking
clears both cached PCM and its old start index, avoiding a silent first block
when returning to an earlier position. These decoder fixes were exposed by the
automated lossless export and loop-style restart checks.

Preview and export use the same Renderer/evaluator/DSP. Absolute evaluation has
no moving curve cursor, so seeks and loop-style time wraps use the same values.
Movie's existing transport has no automatic loop toggle; repeated seek/restart
and renderer wrap behavior are verified, not a new looping transport feature.

## Compatibility and API

Schema remains version 4: point/target storage already existed in versions 3/4.
The optional `automationVisible` flag defaults to true when absent. Version 2
migration remains unchanged. Legacy unresolved/clip-specific targets are retained
but inactive and shown as unresolved. Legacy duplicate valid targets choose the
lexicographically first enabled lane identity, independently of display order.

All mutations use the document's transactional validation and undo path:

- `movie.automation.create`: `trackId`, `effectId`, `parameterId`.
- `movie.automation.bind`: `laneId` plus the same target tuple; existing normalized
  keys are retained and mapped to the new target's range.
- `movie.automation.point`: `laneId`, optional existing `pointId`, `seconds`,
  actual-unit `value`, optional outgoing `shape` and `tension` (-1 to 1).
- `movie.automation.removePoint`: `laneId`, `pointId`.
- `movie.automation.visible`: `laneId`, Boolean `visible`.

Existing track queries expose authoritative targets/keys. Existing track
enable/remove and effect bypass/reorder commands retain their established roles.
No Station/shared source, capture, effect-track routing, or replacement engine
is introduced.

## Validation and limits

`MovieDocumentTests` covers real samples/pixels, linear/step/choice evaluation,
endpoint holds, key movement/removal/undo, invalid edits, stable identities after
reorder, bypass/removal protection, hidden-lane evaluation, saved-value roundtrip,
block continuity and moving filters at 44.1/48 kHz. It profiles a moving cutoff.

The opt-in Debug mounted-window check extends the existing effects check:

```
"Djehuti Movie.exe" --track-ui-check "<new-output-directory>" "<short-visible-video>" --automation-ui-check
```

It exercises parameter automation buttons, inspector key controls and synthesized
timeline point dragging, saves/reopens, exports FFV1/PCM and H.264/AAC, compares
decoded media with preview rendering and checks transport seek/restart. Native
pointer input is not claimed. See HANDOFF.md for actual results and output paths.

CPU 8-bit SDR frame throughput remains the Milestone 3 limit. Abrupt step keys,
filter mode switches and rapid manual document edits can produce discontinuities;
no crossfade/smoothing system is claimed. Long-memory filter seeking uses bounded
preroll, not an exact unbounded replay from the beginning. No automatic automation
recording, curve pencil, multi-point selection, or native-pointer acceptance is
included. Unresolved legacy lanes require a supported effect target to run.

Final measured source limit: the AAC-only first block after a time wrap differs
from a fresh decoder by about 0.000207 peak; the full mix differs by 0.000209,
while generated WAV automation/filter wraps match exactly. Encoded lossless
audio comparison at 0.5 seconds is within 0.00001525; filter seek error is zero.
See HANDOFF.md for the complete final evidence and remaining Suite CI failure.
