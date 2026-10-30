#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <atomic>
#include <array>
#include <deque>
#include <memory>
#include <mutex>
#include <vector>
namespace movie
{
inline juce::int64 captureHostTime()
{return (juce::int64)((long double)juce::Time::getHighResolutionTicks()*10000000/juce::Time::getHighResolutionTicksPerSecond());}
struct CaptureSample
{
    juce::Image image;
    std::shared_ptr<const std::vector<float>> audio;
    juce::String epoch,clock;
    juce::int64 sourceTime=0,hostTime=0,position=0,sequence=0,uncertainty100ns=0;
    int rate=0,channels=0,frames=0;
    bool discontinuity=false,invalidTimestamp=false,repeated=false;
    size_t bytes()const{return audio?audio->size()*sizeof(float):(size_t)image.getWidth()*image.getHeight()*4;}
};
struct CaptureQueue
{
    // Acquisition never waits for an encoder/storage consumer. Byte budget is per subscription.
    std::array<std::shared_ptr<const CaptureSample>,4096> pending;
    juce::AbstractFifo fifo{4096};std::atomic<size_t> bytes{0},peakBytes{0};const size_t limit;
    std::atomic<juce::int64> after{INT64_MAX},before{INT64_MAX};
    std::atomic<bool> overflow{false};std::atomic<juce::int64> lastDelivery{0};
    explicit CaptureQueue(size_t budget=64*1024*1024):limit(budget){}
    void push(const std::shared_ptr<const CaptureSample>& sample)
    {
        const auto end=sample->hostTime+(sample->audio?(juce::int64)(sample->frames*10000000.0/sample->rate):0);
        if(end<after.load() || sample->hostTime>before.load())return;
        if(sample->bytes()>limit-bytes.load()){overflow=true;return;}
        int a,n,b,m;fifo.prepareToWrite(1,a,n,b,m);if(!n){overflow=true;return;}
        pending[(size_t)a]=sample;const auto total=bytes.fetch_add(sample->bytes())+sample->bytes();peakBytes=std::max(peakBytes.load(),total);lastDelivery=captureHostTime();fifo.finishedWrite(1);
    }
    std::shared_ptr<const CaptureSample> pop()
    {int a,n,b,m;fifo.prepareToRead(1,a,n,b,m);if(!n)return {};auto value=std::move(pending[(size_t)a]);bytes-=value->bytes();fifo.finishedRead(1);return value;}

};
struct RecordingSubscription
{
    juce::String trackId,sourceId;juce::var settings;std::shared_ptr<CaptureQueue> queue;
};
inline void publishCapture(const std::vector<RecordingSubscription>& subscriptions,const juce::String& source,const std::shared_ptr<const CaptureSample>& sample)
{for(const auto& subscription:subscriptions)if(subscription.sourceId==source)subscription.queue->push(sample);}
}
