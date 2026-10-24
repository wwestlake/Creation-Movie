# Movie effects (Milestone 3)

## Existing facilities and reuse

Movie already used one `Renderer` for audio/video playback workers and FFmpeg
export, with clip gain, opacity and titles. That renderer remains authoritative.
Station's ordered insert host and AudioProcessorValueTreeState parameters are
coupled to Station's engine/plugin rack. Movie does not copy that host or its
processors. It reuses JUCE's IIRFilter/IIRCoefficients DSP (also used by Station
VocalFocus), rather than implementing filter equations. Gain/fade/balance are
simple stereo sample stages. Balance retains unity at centre, unlike Station's
mono panning law. No VST scanning or new plugin-hosting framework was added.

Shared Video has D3D11 ColourBalance, TransformCrop and a linear GPU graph
executor. Movie's decoding/compositing surfaces are CPU JUCE images. Routing
those through D3D would introduce uploads/readbacks and a new preview/export
execution path. This milestone keeps CPU images, uses JUCE Colour operations,
alpha multiplication, crop views and AffineTransform/Graphics resampling, and
adds a small CPU colour adapter. It does not claim pixel equivalence with the
shared shader's linear-light colour math. Preview and export both use the same
CPU implementation. Station and shared source remain unchanged.

Shared automation curves/target fields are retained. Station APVTS and shared
node-codegen parameter structures are not an appropriate document authority for
Movie's CPU renderer. Movie's small immutable catalog supplies the same metadata
to document validation, widgets, processing and API discovery.

## Model, compatibility and commands

Schema 4 adds `effects` arrays to tracks. Version 2 still maps old indices to
deterministic mixed-media tracks; version 3 opens with empty chains. Migration is
in memory and never writes until Save. Invalid data leaves the active document
and saved entry unchanged. Unknown effect types/parameters reject opening rather
than silently dropping configuration. Existing media/timing/order is preserved.

Each instance stores a globally unique id, type token, owner track id, enabled,
bypassed, and parameter values. Array position defines execution order. Defaults
are materialized on save. Reorder never changes instance/owner/parameter IDs.
Track type changes validate compatibility. Removing a referenced effect or its
owner is rejected until its stored automation binding is cleared.

`movie.effects.catalog` exposes stable type/parameter IDs, names, domains, data
types, ranges, defaults, steps, units, choices and automation eligibility.
`movie.effect.add/remove/reorder/parameter/enabled/bypass` use `trackId`,
`effectId`, and their relevant `type`, `order`, `parameterId`, `value` fields.
All mutations use the existing document snapshot/undo transaction path. Slider
gestures group their parameter edits into one undo transaction.

The future automation target is owner track ID + effect instance ID + parameter
ID. Processing consumes the same effect parameter map as manual edits; Milestone
4 can supply evaluated engineering values there. Existing normalized curves will
map through metadata ranges/choices. This milestone does not evaluate curves.
Previously stored unresolved effect targets remain preserved for later repair.

## Processing order

Audio: decode -> clip gain -> sum clips within a track -> ordered audio chain ->
sum enabled tracks -> existing output ceiling -> device/output encoder.

Video: decode/title -> clip opacity/placement -> transparent track layer when
video processing is needed -> ordered video chain -> ascending track-order
composition over black -> preview/output encoder. Tracks without active visual
effects retain the existing direct composition path. Image tracks use the same
visual processors. Embedded sound on video tracks may use audio effects.

No general clip-level chain previously existed. Clip gain/opacity remain the
clip stage; the new chain algorithms are independent of widgets/owners and can
serve future clip chains without duplicating mathematics.

The Effects track type has no established routing target/group semantics in the
current document. Its inspector/browser explicitly explains that limitation and
rejects insertion. It does not pretend to process lower tracks. Defining that
routing requires a product decision; ordinary track effects are operational.

## Catalog

| Domain | Effect | Parameters |
| --- | --- | --- |
| Audio | Gain | multiplier 0-4 |
| Audio | Stereo Balance | -1 left to +1 right, unity centre |
| Audio | Track Fade | in/out seconds, relative to the track's placement extent |
| Audio | Filter | low/high-pass selector, cutoff 20-20000 Hz (clamped below Nyquist) |
| Video | Brightness / Contrast / Colour | brightness/contrast, saturation, RGB balance |
| Video | Opacity | alpha multiplier 0-1 |
| Video | Crop / Scale / Position | normalized edge crops, independent scale, frame-relative position |

Colour uses SDR encoded RGB brightness/contrast and tint followed by JUCE HSV
saturation; alpha is retained via JUCE's straight-colour access to premultiplied
images. Crop keeps the remaining image's canvas coordinates; scaling is centred
on the track canvas and position offsets are fractions of its dimensions.
Effects never modify stored source media.

Filter histories belong to each worker renderer, track, instance and channel.
Sequential blocks keep state and coefficients. A discontinuity resets history
and replays up to one second of source before the requested point. Fade timing
uses absolute timeline/sample position, not callback count. Buffers grow only as
needed on workers; the device callback continues consuming prepared samples.
Manual document changes use Movie's existing renderer revision mechanism, so
rapid edits can invalidate work and are not guaranteed click-free. Continuous
parameter smoothing/automation is not claimed.

## Interface and verification

Open **View > Effects Browser / Inspector**, or use the Track Inspector's Effects
button. Available effects are filtered by track compatibility and labeled Audio
or Video. Select a chain entry to see metadata-generated sliders/numeric entry
or selectors. Up/Down changes processing order; enabled and bypass retain values.

`MovieDocumentTests` includes actual DSP/pixel checks, noncommutative order,
bypass, block continuity, compatibility, persistence and a representative 720p
CPU timing measurement. The Debug window check accepts:

```
"Djehuti Movie.exe" --track-ui-check "<new-output-directory>" "<existing-video>" --effects-ui-check
```

It creates its own VFS demonstration, exercises mounted effect widgets, saves and
reopens it, exports FFV1/PCM and H.264/AAC samples, measures real pixels/samples,
compares decoded lossless output to the preview renderer, restores the original
project, and exits normally. Results distinguish synthesized GUI events from
native pointer testing. Output paths must be new to preserve earlier artifacts.

The path remains CPU/8-bit SDR. Multiple high-resolution colour/transform layers
can exceed real-time throughput; no GPU acceleration, proxy system, HDR pipeline,
new codecs, automation runtime, effects-track routing or external plugin host is
introduced. Encoded MP4 is lossy; lossless comparison uses FFV1 and PCM tolerance.
