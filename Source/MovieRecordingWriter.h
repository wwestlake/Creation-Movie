#pragma once
#include "MovieVisualCapture.h"
#include "MovieDocument.h"
#include <creation/services/SuiteVfsServiceClient.h>
namespace movie
{
struct RecordedTrack
{
    juce::String trackId,sourceId,sourceKind,id,operation,path,error;
    juce::var settings,diagnostics;
    MediaInfo info;double start=0;
    bool usable=false;
    juce::var toJson()const;
};
// One encoder/storage worker per track. Never runs on acquisition or GUI threads.
class TrackRecorder
{
public:
    TrackRecorder(const juce::String& project,const juce::String& take,const Track&,std::shared_ptr<CaptureQueue>);
    ~TrackRecorder();
    void prepare(const VisualPreview&);
    void run(juce::int64 origin,const std::atomic<bool>& stop,const std::atomic<juce::int64>& stopTime);
    RecordedTrack result()const;
private:
    struct Impl;std::unique_ptr<Impl> impl;
};
}
