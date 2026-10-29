#pragma once
#include "MovieVisualCapture.h"
#include "MovieDocument.h"
namespace movie
{
// One worker coordinates track recorders; GUI publishes completed results through Document.
class RecordingCoordinator
{
public:
    explicit RecordingCoordinator(VisualCapture&);~RecordingCoordinator();
    juce::var readiness(const Sequence&)const;
    juce::String start(const Sequence&,double timelinePosition);
    void stop();bool active()const;
    juce::var status()const;
    juce::var takeResults();
private:
    struct Impl;std::unique_ptr<Impl> impl;
};
}
