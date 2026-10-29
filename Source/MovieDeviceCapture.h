#pragma once
#include "MovieVisualCapture.h"
namespace movie
{
// Native adapters owned by VisualCapture, not a separate track/source authority.
class DeviceCapture
{
public:
    DeviceCapture(); ~DeviceCapture();
    std::vector<VisualSource> sources() const;
    void refresh(); void setBindings(std::vector<VisualBinding>);
    void setRecordingSubscriptions(std::vector<RecordingSubscription>);
    VisualPreview preview(const juce::String&) const;
    juce::var status() const;
    void renderAudio(float*const*,int,int,double);
private:
    struct Impl; std::unique_ptr<Impl> impl;
};
bool captureFormatMatches(const juce::var& available,const juce::var& requested);
}
