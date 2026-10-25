#pragma once
#include <creation/timeline/TimelineClip.h>
#include <juce_data_structures/juce_data_structures.h>
#include <functional>
#include <map>
#include "MovieEffects.h"

namespace movie
{
using Clip = creation::timeline::TimelineClip;
struct MediaInfo
{
    double duration = 0;
    int width = 0, height = 0, fpsNum = 0, fpsDen = 1, sampleRate = 0, channels = 0;
    double pixelAspect = 1;
    bool hasVideo = false, hasAudio = false, still = false;
    juce::String videoCodec, audioCodec, colourSpace, transfer;
    juce::var toJson() const;
    static MediaInfo fromJson(const juce::var&);
};
struct Media
{
    juce::String id, versionId, name, path;
    MediaInfo info;
};
// Reuse Station's shared identity/name/display and automation curve types.
// Movie-specific source configuration never contains runtime device handles.
struct Track : creation::timeline::TimelineTrack
{
    juce::String type { "video" };
    bool automationVisible = true;
    bool enabled = true, armed = false, monitoring = false, legacyMixed = false;
    juce::var source;
    juce::String targetTrackId, targetClipId, targetEffectId, parameterId;
    std::vector<creation::timeline::AutomationPoint> points;
    std::vector<Effect> effects;
    juce::var toJson() const;
    static bool fromJson(const juce::var&, Track&, juce::String&);
};
struct Sequence
{
    Sequence();
    juce::String projectId, name { "Untitled Movie" };
    int width = 1920, height = 1080, fpsNum = 30000, fpsDen = 1001;
    std::vector<Media> media;
    std::vector<Clip> clips;
    std::vector<Track> tracks;
    // Stable ownership; TimelineClip::trackIndex is only a shared-helper/render adapter.
    std::map<juce::String, juce::String> clipTracks;
    int findTrack(const juce::String&) const;
    juce::String owner(const Clip&) const;
    bool trackEnabled(const Clip&) const;
    void resolveTrackIndices();
    double frameSeconds() const { return (double) fpsDen / fpsNum; }
    double snap(double seconds) const { return std::round(seconds / frameSeconds()) * frameSeconds(); }
    double duration() const;
    const Media* findMedia(const juce::String&) const;
    int findClip(const juce::String&) const;
    juce::var toJson() const;
    static bool fromJson(const juce::var&, Sequence&, juce::String& error);
};

// One document, one edit path. Saved JSON carries integer sequence frame positions;
// source offsets are seconds on the source's timestamp clock (including VFR media).
class Document
{
public:
    Sequence sequence;
    juce::UndoManager undo;
    std::function<void()> changed;
    bool dirty = false;
    bool edit(const juce::String& command, const juce::var& arguments, juce::String& error);
    void replace(Sequence replacement, bool clean);
    void restore(const juce::var&);
private:
    struct Edit;
    juce::String lastEffectGesture;
};
juce::var object(std::initializer_list<std::pair<juce::Identifier, juce::var>> fields);
bool finiteNumber(const juce::var& value);
}
