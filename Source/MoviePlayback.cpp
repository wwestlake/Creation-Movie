#include "MoviePlayback.h"
#include "MovieVisualCapture.h"
namespace movie
{
Playback::Playback(){audioWorker=std::thread([this]{produceAudio();});videoWorker=std::thread([this]{produceVideo();});}
Playback::~Playback(){stop=true;wake.notify_all();device.removeAudioCallback(this);device.closeAudioDevice();audioWorker.join();videoWorker.join();}
void Playback::setCaptureMonitor(VisualCapture* capture){const juce::ScopedLock lock(device.getAudioCallbackLock());captureMonitor.store(capture);}
juce::String Playback::initialiseAudio()
{
    const auto e=device.initialise(0,2,nullptr,true);if(e.isEmpty())device.addAudioCallback(this);return e;
}
void Playback::setSequence(const Sequence& s)
{
    {std::lock_guard guard(mutex);sequence=s;++revision;problem={};duration=s.duration();}
    seek(std::min(position(),s.duration()));
}
void Playback::seek(double t)
{
    requestedSeconds=juce::jlimit(0.0,duration.load(),t);positionSeconds=requestedSeconds.load();++generation;
    requestPreview(position());wake.notify_all();
}
void Playback::play(bool enabled)
{
    if(enabled && duration.load()>0 && position()>=duration.load()-1e-6)seek(0.0);
    isPlaying=enabled && duration.load()>0;
    wake.notify_all();
}
void Playback::requestPreview(double t,int width,int height)
{
    {std::lock_guard guard(mutex);previewTime=t;previewWidth=width;previewHeight=height;++previewRequest;}wake.notify_all();
}
juce::Image Playback::preview()const{std::lock_guard guard(mutex);return image;}
juce::String Playback::error()const{std::lock_guard guard(mutex);return problem;}
void Playback::audioDeviceAboutToStart(juce::AudioIODevice* d){rate=d->getCurrentSampleRate();deviceReady=true;++generation;wake.notify_all();}
void Playback::audioDeviceStopped(){deviceReady=false;isPlaying=false;}
void Playback::audioDeviceIOCallbackWithContext(const float*const*,int,float*const* outputs,int channels,int count,const juce::AudioIODeviceCallbackContext&)
{
    for(int c=0;c<channels;++c)if(outputs[c])std::fill(outputs[c],outputs[c]+count,0.0f);
    if(auto* capture=captureMonitor.load())capture->renderAudio(outputs,channels,count,rate.load());
    if(!isPlaying.load())return;
    const auto current=generation.load();int written=0;
    while(written<count)
    {
        int a,n,b,m;fifo.prepareToRead(1,a,n,b,m);juce::ignoreUnused(b,m);
        if(n==0){++underflowCount;return;}
        const auto& block=blocks[(size_t)a];
        if(block.generation!=current){readOffset=0;fifo.finishedRead(1);continue;}
        const int amount=std::min(count-written,block.count-readOffset);
        for(int c=0;c<std::min(2,channels);++c)if(outputs[c])
            for(int i=0;i<amount;++i)outputs[c][written+i]+=block.samples[(size_t)(readOffset+i)*2+c];
        written+=amount;readOffset+=amount;
        // A concurrent seek owns the new clock; never overwrite it with old data.
        if(generation.load()==current)positionSeconds=std::min(duration.load(),(block.start+readOffset)/rate.load());
        if(readOffset>=block.count){readOffset=0;fifo.finishedRead(1);}
        if(position()>=duration.load()-1e-8){isPlaying=false;return;}
    }
}
void Playback::produceAudio()
{
    uint64_t gen=0,rev=~uint64_t{};int64_t sample=0;std::unique_ptr<Renderer> renderer;
    auto lastWall=std::chrono::steady_clock::now();
    while(!stop.load())
    {
        const auto current=generation.load();const int sampleRate=(int)std::max(8000.0,rate.load());
        if(current!=gen)
        {
            std::lock_guard guard(mutex);
            if(rev!=revision){renderer=std::make_unique<Renderer>(sequence,&stop);rev=revision;}
            gen=current;sample=(int64_t)std::llround(requestedSeconds.load()*sampleRate);
            lastWall=std::chrono::steady_clock::now();
        }
        if(!deviceReady.load())
        {
            if(isPlaying.load() && duration.load()>0)
            {
                const auto now=std::chrono::steady_clock::now();
                const double dt=std::chrono::duration<double>(now-lastWall).count();
                lastWall=now;
                const double next=std::min(duration.load(),positionSeconds.load()+std::clamp(dt,0.0,0.1));
                if(generation.load()==current)positionSeconds=next;
                if(next>=duration.load()-1e-8)isPlaying=false;
            }
            else lastWall=std::chrono::steady_clock::now();
            std::unique_lock lock(mutex);wake.wait_for(lock,std::chrono::milliseconds(10));continue;
        }
        int a,n,b,m;fifo.prepareToWrite(1,a,n,b,m);juce::ignoreUnused(b,m);
        if(!isPlaying.load() || !renderer || n==0 || (double)sample/sampleRate>=duration.load())
        {std::unique_lock lock(mutex);wake.wait_for(lock,std::chrono::milliseconds(10));continue;}
        auto& block=blocks[(size_t)a];block.generation=gen;block.start=sample;
        block.count=(int)std::min<int64_t>(1024,std::llround(duration.load()*sampleRate)-sample);
        try{renderer->audio((double)sample/sampleRate,block.count,sampleRate,block.samples.data());}
        catch(const std::exception& e){std::lock_guard guard(mutex);problem=e.what();isPlaying=false;continue;}
        if(gen==generation.load()){fifo.finishedWrite(1);sample+=block.count;}
    }
}
void Playback::produceVideo()
{
    uint64_t request=0,rev=~uint64_t{};std::unique_ptr<Renderer> renderer;
    while(!stop.load())
    {
        double t;int w,h;uint64_t wanted;
        {
            std::unique_lock lock(mutex);wake.wait(lock,[&]{return stop.load() || previewRequest!=request;});if(stop.load())return;
            wanted=previewRequest;t=previewTime;w=previewWidth;h=previewHeight;
            if(rev!=revision){renderer=std::make_unique<Renderer>(sequence,&stop);rev=revision;}
        }
        try
        {
            auto frame=renderer->video(t,w,h);std::lock_guard guard(mutex);
            // Do not publish a frame from an older document. During continuous
            // playback publishing the latest completed frame avoids starvation.
            if(rev==revision && (wanted==previewRequest || isPlaying.load()))image=frame;
        }
        catch(const std::exception& e){std::lock_guard guard(mutex);problem=e.what();}
        request=wanted;
    }
}
}
