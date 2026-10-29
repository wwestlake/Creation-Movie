# Djehuti Movie — Phase 1 inventory and approved integration architecture

Date: October 25, 2026  
Status: **Phase 1 COMPLETE — architecture approved; implementation not authorized**  
Authority: Executive & Creative Director / Chief Engineer, “Phase 1 — Architecture Approval and Corrections.”

This document records the Phase 1 findings together with the approved corrections.
The decisions below supersede the alternative phase organization and any implied
automatic track creation in the initial assessment. No Phase 2 implementation is
authorized by the architecture approval.

## 1. Assessment baseline and implementation inventory

Inspected commits: Movie `8c8ac14`, Creation Remote Android `fa33364`, and Creation
Remote Receiver `fd21c80`. Source implementation is distinguished from tested
operation. Neither Remote README accurately describes all current source code.

| Component | Finding and source evidence |
| --- | --- |
| Movie screen/window/camera capture | No live acquisition or enumeration backend found. `Source/MovieTracks.cpp::sourceValid` persists display/window/camera preferences; `Source/TrackInspector.inc` explicitly identifies them as configuration only. |
| Movie microphone/system-audio capture | Preferences only. `Source/MoviePlayback.cpp::initialiseAudio` opens an output device with zero input channels; its audio callback does not capture input. System audio is an inventory finding, not an additional capture requirement introduced by this report. |
| Recording interface | `Source/MainComponent.cpp` hides the header Record button. Arm and Monitor controls are saved inactive preferences. |
| Media encoding | `Source/MovieExport.cpp::exportSequence` implements finite offline H.264/AAC MP4, ProRes/PCM MOV, and FFV1/PCM MKV export. It uses an external staged file and a finite Renderer loop, not an incremental capture sink. Prior Movie application checks exercised MP4 and FFV1 output. |
| Timeline/document | Stable track/clip IDs, media ingestion, effects, automation, transitions and undo/redo exist. `Source/MovieDocument.cpp` saves schema 5; clip start/duration are rounded to sequence frames. Media paths must begin with `Media/`. |
| Project media | `Source/MainComponent.cpp` imports media into project VFS entries and saves Movie's own media list in `Project/movie-state.json`. `Source/MovieMedia.cpp` decodes seekable VFS streams through FFmpeg AVIO. |
| Android pairing | `auth/AuthSession.kt`, `auth/PkceUtil.kt`, `ui/screens/PairingScreen.kt`, and `net/RemoteApiClient.kt` implement browser login, PKCE/state validation, QR scanning, pairing approval, and device/project queries. Phone operation was not verified. Paths are beneath `app/src/main/java/com/lagdaemon/creationremote/`. |
| Android credentials | `auth/TokenStore.kt` stores session tokens in Preferences DataStore. This is not an implemented LAN capture trust mechanism. |
| Android capture | `ui/screens/CaptureScreen.kt` remains a mode-selection stub. No recording implementation or capture-UI call to `sendAsset` was found. Selected project/session metadata is not carried through a completed capture workflow. |
| Android transport | `net/RemoteWebRtcClient.kt` implements offer/answer/ICE signaling and checksummed chunked file sending. It uses public signaling and STUN, has no live media tracks, and lacks send backpressure handling. The class is not wired into the capture screen. |
| Desktop Receiver | `Source/RemoteSessionController.cpp` implements background check-in/pairing. `Source/WebRTCClient.cpp` receives one asset at a time into whole-asset memory, verifies checksums, and deposits it into a project. Receive/deposit processing is marshaled onto the GUI thread. `CMakeLists.txt` builds libdatachannel with `NO_MEDIA`. No end-to-end phone verification was established. |

Receiver deposits assets beneath `remote/...` and updates the shared catalog.
Movie does not automatically adopt those entries into its media list or timeline;
its current media-path validation also excludes those paths. Receiving a file is
therefore not an implemented phone-to-Movie production workflow. Both Remote
READMEs reference a Remote protocol document that was not found in the inspected
checkout.

## 2. Approved ownership and user workflow

**A track must exist before a capture source can be assigned.** The workflow is:

1. Create a track of the appropriate type.
2. Select its capture source within that track.
3. Configure that track's recording properties.
4. Arm the track.
5. Press Record to capture all armed tracks simultaneously.

Examples include Screen Capture Track → Monitor 1, another Screen Capture Track
→ Monitor 2, Window Capture Track → selected application window, Camera Track →
selected camera, Audio Track → selected microphone, and Phone Capture Track →
connected phone camera. A screen means one physical display, never an implicit
desktop spanning monitors.

Tracks own persistent source assignments, relevant configuration, and their
recordings. The capture subsystem owns physical acquisition sessions. One Movie
recording coordinator synchronizes the armed tracks. It must not automatically
create tracks or assign sources. Any separately routed companion stream must
have an explicitly configured, already-existing destination track.

### Shared physical acquisition

Multiple tracks may select the same physical source. The capture subsystem opens
that physical source once and fans out immutable frames/samples to subscribed
tracks. Each armed track retains independent configuration, queues, encoding and
recording output. One slow subscriber must not block the other subscribers.

Acquisition identity is based on the physical source/backend endpoint, not the
track ID or recording profile. Compatible subscriber formats may be derived from
one acquired format. An incompatible configuration must be reported explicitly;
opening the hardware a second time is not the fallback. Source lifetime is
reference-counted across prepared, recording and monitoring subscriptions, with
release after the final subscriber leaves. The first track does not reserve the
source exclusively.

## 3. Reuse and interface contracts

Reuse Movie's existing document, timeline, undo manager, Renderer, effects,
automation, monitor components, VFS media reads, and command/result conventions.
Adapt FFmpeg codec/muxing logic out of the finite offline export loop; do not
build another rendering or storage system. Recording needs a coordinator rather
than occupying the existing single background-job slot for every source.
Playback's seekable transport clock must not become the recording clock.

The following names define design contracts, not implemented APIs:

| Contract | Required behavior/data |
| --- | --- |
| Track capture binding | Existing `trackId`, persistent source selector, requested video/audio properties, recording profile, and arm state. No runtime handles or credentials in project data. |
| Source descriptor | Logical source ID, physical acquisition key, source kind, supported stream formats, companion endpoints, capabilities and clock identity. |
| Acquisition session | Asynchronous enumerate/prepare/subscribe/unsubscribe lifecycle; one open physical endpoint; negotiation and shared buffer lifetime management. |
| Capture sample | Source/stream IDs, source epoch, sequence number, integer timestamp/timebase, mapped host timestamp and uncertainty, duration, immutable payload, and discontinuity flags. |
| Track recorder | Subscription to an acquisition session, independent bounded queue, track-specific conversion/encoding, output reference, and per-track loss/error statistics. |
| Recording coordinator | Snapshot of armed track bindings, take ID, common epoch, readiness checks, scheduled start/stop, interruption policy and publication to the existing document. |

Source callbacks only transfer pooled/ref-counted buffers into bounded queues.
They do not encode, call network storage, edit documents, or operate the GUI.
Workers publish status/results to the GUI thread using Movie's existing pattern.

### Source adapters

| Source | Proposed integration contract |
| --- | --- |
| Physical screen | Windows Graphics Capture for one enumerated monitor, retaining actual extent, rotation and format. Multiple tracks selecting that monitor share one acquisition session. |
| Application window | Windows Graphics Capture for one selected window. Revalidate runtime window ownership; a reused HWND is not permission to capture a different window. |
| Camera/video device | Media Foundation enumeration and asynchronous acquisition. Advertise actual formats and accompanying audio when available. Ambiguous audio-device association requires explicit selection. |
| Independent microphone | WASAPI capture with endpoint identity, actual channel layout, sample position and host-clock correlation. Multiple track bindings share the endpoint acquisition. |
| Phone | Movie LAN receiver exposes authenticated remote video/audio streams through the same acquisition/subscription contract. Multiple tracks selecting the same phone stream share reception/acquisition. |

Existing closed source-validation vocabulary must be extended deliberately for
these bindings. Device selection belongs in Movie's track interface, not a
filesystem browser or global automatic-source workflow.

## 4. Approved recording and timing architecture

Prepare devices, permissions, codecs, VFS capacity and aggregate resources before
accepting Record. All armed track recorders must reach readiness or return an
explicit failure. Schedule one host-monotonic recording epoch, `T0`; actual source
timestamps, rather than command arrival time, determine placement.

Preserve source integer timestamps/timebases and clock mappings. Estimate clock
offset and rate continuously, including uncertainty. Audio sample positions
determine timing within blocks; drift correction uses gradual resampling where
needed. Video retains mapped presentation timing. Seeking/stopping playback
must not change the recording epoch.

Each armed track records independently from its subscription. Keep source media
independently editable; existing track effects/automation remain playback/export
operations. At stop `T1`, drain bounded queued data through the stop boundary,
finalize outputs, and attach recordings to their already-existing owner tracks
through an undoable document transaction. Undo removes placements without
discarding recorded media.

Preserve subframe start/duration in a versioned Movie document extension while
retaining existing timeline objects and rendering. Current frame-rounded
serialization is insufficient for independent audio-source alignment. Older
projects remain readable. Capture publication must preserve stable project,
track, media and clip identities, and must reject stale/deleted target bindings
rather than silently create replacements.

An interrupted source finalizes its recoverable portion and records an explicit
gap/status; unrelated tracks continue. Reconnection establishes a new source
clock epoch without shifting existing material. Shared-source failure is reported
to every affected subscriber. Queue saturation is visible and bounded; a stalled
track recorder cannot stall shared acquisition. Storage exhaustion and
unrecoverable audio overflow must preserve completed data and report failure.
Project replacement/close must resolve active recording explicitly.

## 5. Approved VFS streaming-write dependency

The general-purpose VFS streaming-write extension is **approved supporting
infrastructure**, implemented within the phase where necessary, not a new phase.

Existing reuse points are `ProjectSession`, `SuiteVfsServiceClient`,
`services/VfsService/Source/VfsChunkUploadRoutes.h`, and
`VfsProjectStore::writeEntryChunk`. Current chunk uploads require a known total
size; no public open-ended seekable recording writer was found.

Proposed interface responsibilities are:

- Begin a project-scoped write operation with stable operation identity.
- Submit bounded incremental writes, including specified offsets for container
  header/index updates.
- Acknowledge ordered, durable progress and define retry/idempotency behavior.
- Checkpoint recoverable media boundaries and write state.
- Finalize to an ordinary project entry after container completion.
- Define interruption, retained-partial recovery, explicit abort and cleanup.

**The VFS owns ordering, placement and persistence mechanics.** Movie uses the
public interface, including FFmpeg AVIO adaptation; it must not implement private
disk storage, write around the VFS, create OS scratch recordings, or hold entire
recordings in memory. Finalized media remains readable by existing VFS-backed
media readers. A project-owned take journal binds checkpoints to track/output
identities and identifies any incomplete tail after interruption.

Shared manifest commits currently replace the whole manifest without revision
checking. Capture checkpoints should use Movie-owned entries and serialized
publication, not concurrent Receiver catalog writes. Broader concurrent catalog
editing is not silently included in this approved writer extension.

## 6. Approved Creation Remote LAN capture integration

Required path: **Phone → local Wi-Fi → Movie host → assigned Movie track**.
The LAN live-capture mode is approved. Reuse pairing UI, QR mechanisms,
communication dependencies and protocol helpers where practical; the existing
public-service dependency is not required for the local connection. Receiver
need not become an intermediate recording app or synchronization authority.

Proposed connection contract:

- Movie listens on a displayed LAN control port. QR pairing conveys host
  address/port, certificate fingerprint and a short-lived one-use invitation.
- Confirmed phone identity receives a revocable capture grant. Do not reuse
  website bearer tokens as LAN credentials or place credentials in projects.
- Local authenticated signaling negotiates encrypted WebRTC live media using
  LAN connectivity, without cloud signaling, external STUN/TURN or relay.
- Initial proposed codec profile is negotiated H.264 video/Opus audio, subject to
  actual codec preflight. The current desktop `NO_MEDIA` build cannot supply
  live-media reception/depacketization; that adapter remains new work.
- Map phone capture timestamps to host time using clock probes and anchors
  between stream/RTP timestamps and phone monotonic capture time. Packet arrival
  time is not a synchronization substitute.
- Bound jitter buffers, report missing/late media, request recovery keyframes,
  and establish fresh timing after reconnect.
- Bind the connection to configured track subscriptions and the coordinator's
  active take. The phone does not choose arbitrary project IDs or VFS paths.

The phone source participates in the same shared-acquisition lifetime management,
track-owned configuration, independent track recording and synchronization model
as local sources. Existing discrete file-transfer behavior is preserved.

## 7. Platform constraints and evidence limits

The inspected implementation baseline is Windows desktop and Android. Other
platform capture backends were not found. Graphics Capture monitor/window
interop requires Windows 10 version 1903 or later. Privacy denial, protected
content, minimized/closed windows, changing displays and device contention need
explicit unavailable/interrupted states. Camera/device profiles must be checked
against actual capabilities rather than inferred from saved labels.

Android currently targets API 35 with minimum 26. Camera/microphone runtime
permissions and applicable foreground-service declarations/lifecycle behavior
must be implemented. The current manifest lacks camera/microphone foreground
service declarations. Test backgrounding, calls, thermal throttling and Wi-Fi
changes. No iOS implementation was found.

Firewall rules, blocked UDP and Wi-Fi client isolation must produce useful
connection errors. Internet failure must not prevent the approved LAN workflow.
Movie's existing SDR/8-bit and stereo processing limitations remain; negotiated
profiles must state conversion/channel policy. Supported concurrency depends on
measured aggregate device, encoder, storage and memory capacity, without imposing
exclusive ownership of a physical source on its first track.

Reference platform contracts:

- [Windows monitor capture](https://learn.microsoft.com/en-us/windows/win32/api/windows.graphics.capture.interop/nf-windows-graphics-capture-interop-igraphicscaptureiteminterop-createformonitor)
- [Windows window capture](https://learn.microsoft.com/en-us/windows/win32/api/windows.graphics.capture.interop/nf-windows-graphics-capture-interop-igraphicscaptureiteminterop-createforwindow)
- [WASAPI capture timestamps](https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudiocaptureclient-getbuffer)
- [Android foreground service types](https://developer.android.com/develop/background-work/services/fgs/service-types)
- [WebRTC signaling](https://webrtc.org/getting-started/peer-connections)

## 8. Authoritative Phase 2–5 implementation contracts

The original phase structure is authoritative. This table replaces the alternate
ordering proposed in the initial Phase 1 report. Dependencies are implemented
inside the relevant phase, never as additional phases.

| Phase | Approved objective | Required handoff contract |
| --- | --- | --- |
| 1 | Inventory and integration architecture | This report and approved corrections. Complete. |
| 2 | Individual screen and application-window capture | Track-first display/window selection and recording properties; one physical display per screen source; shared acquisition/subscriptions; bounded timestamped acquisition and recording integration as required. Implement VFS dependencies here if required for these recordings. |
| 3 | Camera and microphone capture | Camera/video-device and independent microphone adapters, accompanying audio when available, format/permission/device-loss behavior, and integration into the same track/source/recording contracts. |
| 4 | Synchronized multitrack recording | One coordinator records all armed tracks against the common epoch; independent outputs, drift management, interruption handling, subframe persistence, undoable attachment to existing tracks, and save/reopen/recovery validation. |
| 5 | Phone capture over local Wi-Fi | Creation Remote LAN mode and Movie host connection; authenticated phone sources assigned within existing tracks; shared reception, timestamps, buffering and synchronized recording through the same coordinator. |

Future phase reports must be written in full to project files. Conversation
responses provide concise findings, summary and file location unless an extensive
inline report is specifically requested.

## 9. Acceptance test matrix

Numerical synchronization limits below are proposed engineering targets for
review, not measured capability or newly approved product requirements.

| Test | Required evidence |
| --- | --- |
| Track-first workflow | Source assignment requires an existing compatible track. Record operates only on explicitly armed/configured tracks; no automatic track creation or source assignment. |
| Shared source | Two tracks select Monitor 1: exactly one physical acquisition session, two independently configured recording outputs, aligned timestamps, and correct subscribe/unsubscribe lifetime. Repeat for microphone and phone sources where applicable. |
| Subscriber isolation | Stop/remove one subscription while another records; hardware stays open for the remaining subscriber. Saturate one output queue without blocking other subscribers or the GUI. |
| Physical displays | Distinct patterns on two monitors; each selected source contains only that physical display. Cover mixed DPI, rotation and changing display topology. |
| Application window | Verify selected-window content, occlusion, resizing, minimization, closure and HWND reuse without silent target switching. |
| Camera/audio | Real camera plus configured accompanying audio; cover audio-less devices, ambiguous associations, busy devices and permission denial. |
| Independent microphone | Mono/stereo capture, supported rate conversion, endpoint sharing, unplug and device discontinuities. |
| Multitrack timing | Concurrent display/window/camera/microphone/phone capture at declared profiles. Analyze flash/clap and timestamps over 30 minutes. Proposed targets: A/V within one output frame; local audio within 5 ms and phone audio within 20 ms. Report drift and clock uncertainty. |
| LAN-only phone | Internet disabled: pair, assign phone to a track, record and stop. Exercise revoked/expired/replayed invitations and unauthorized connections. |
| Network interruption | Jitter/loss/reordering and Wi-Fi interruption produce bounded buffering and explicit gaps; local recordings continue and reconnect preserves timing. |
| VFS streaming | Offset/header patches, bounded memory, ordering, retries, checkpoints, finalization, interruption and retained-partial recovery; no whole-recording buffers or OS scratch files. |
| Persistence/editing | Save/reopen preserves source assignments, configurations, owner IDs and subframe alignment. Independently edit outputs, undo/redo attachment, apply effects/automation and export through the existing Renderer. |
| Failure/recovery | Slow/full/unavailable storage, encoder overload and forced termination preserve acknowledged media, identify incomplete tails and report affected tracks without losing unrelated recordings. |

## 10. Verification, decisions and completion

Phase 1 used targeted source/configuration inspection and ran the existing Movie
console test executable successfully. Document, effects, automation, transitions
and persistence checks passed. Prior actual-application results contained 18
passing groups, including MP4/FFV1 export; those are editing/export evidence, not
capture evidence.

No physical capture, phone pairing, LAN streaming, multisource synchronization or
capture recovery was verified. No Remote build/test run was performed, and no
dedicated Remote automated test target was found in the inspected application
sources. Dependencies and release workflows alone do not prove operation.

The Executive approval resolves the VFS streaming-write and Creation Remote LAN
scope decisions identified by the assessment. No further ownership negotiation
or architecture approval is requested here. Implementation details must conform
to these contracts and the original phase order under separate phase assignments.

This recording of the approved decisions is documentation only. No application
source modification or capture implementation is included. No commit, merge or
push is authorized or performed under this order.

**Phase 1 is COMPLETE. Await a separate Phase 2 assignment.**
