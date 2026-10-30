#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include "MovieCaptureStream.h"
#include <vector>

namespace movie
{
struct VisualSource
{
    juce::String id, kind, name;
    int width=0, height=0;
    juce::Array<juce::var> formats;
    juce::var toJson() const;
};
struct VisualBinding
{
    juce::String trackId, sourceId;
    bool active=false;
    juce::var settings;
    bool monitoring=false;
};
struct VisualPreview
{
    juce::Image image;
    juce::String state{"stopped"}, message;
    juce::int64 frameNumber=0, timestamp100ns=0;
    int subscribers=0;
    juce::int64 sampleCount=0, samplePosition=0, discontinuities=0;
    float peak=0;
    juce::var format;
};
// Owns physical acquisition, independently of document track assignments.
// Discovery, D3D readback and session destruction occur on one MTA worker.
// Snapshots are immutable; only the latest image is retained (no growing queue).
class VisualCapture
{
public:
    VisualCapture();
    ~VisualCapture();
    std::vector<VisualSource> sources() const;
    void refresh();
    void setBindings(std::vector<VisualBinding>);
    void setRecordingSubscriptions(std::vector<RecordingSubscription>);
    VisualPreview preview(const juce::String& trackId) const;
    juce::var status() const;
    juce::String validate(const juce::String& id,const juce::String& kind,const juce::var& settings) const;
    // Existing output callback consumes bounded microphone monitor queues, without locks.
    void renderAudio(float*const* outputs,int channels,int frames,double rate);
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
