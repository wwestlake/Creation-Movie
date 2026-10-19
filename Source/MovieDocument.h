#pragma once
#include <creation/timeline/TimelineClip.h>
#include <juce_data_structures/juce_data_structures.h>
#include <functional>

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
struct Sequence
{
    juce::String projectId, name { "Untitled Movie" };
    int width = 1920, height = 1080, fpsNum = 30000, fpsDen = 1001;
    std::vector<Media> media;
    std::vector<Clip> clips;
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
};
juce::var object(std::initializer_list<std::pair<juce::Identifier, juce::var>> fields);
bool finiteNumber(const juce::var& value);
}
