#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <atomic>
#include <array>
namespace movie {
// Existing native microphone monitor, shared by the remote microphone adapter.
// One producer/consumer; generation tags discard queued sound after mute/reassign.
struct CaptureMonitor {
    struct Frame {float l=0,r=0;unsigned generation=0;};
    std::array<Frame,8192> frames;
    juce::AbstractFifo fifo{8192};
    std::atomic<unsigned> generation{1};
    std::atomic<bool> enabled{false};
    std::atomic<juce::int64> dropped{0},rendered{0};
    void write(float l,float r){int a,n,b,m;fifo.prepareToWrite(1,a,n,b,m);if(n){frames[(size_t)a]={l,r,generation.load()};fifo.finishedWrite(1);}else ++dropped;}
    void render(float*const* out,int channels,int count){
        const auto epoch=generation.load();const bool audible=enabled.load();int written=0;
        while(written<count){int a,n,b,m;fifo.prepareToRead(1,a,n,b,m);if(!n)break;const auto f=frames[(size_t)a];fifo.finishedRead(1);
            if(!audible||f.generation!=epoch)continue;
            if(channels>0&&out[0])out[0][written]+=f.l*.25f;
            if(channels>1&&out[1])out[1][written]+=f.r*.25f;
            ++written;++rendered;
        }
    }
};
}
