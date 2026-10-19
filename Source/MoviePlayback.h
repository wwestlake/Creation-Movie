#pragma once
#include "MovieMedia.h"
#include <juce_audio_devices/juce_audio_devices.h>
#include <array>
#include <thread>
#include <condition_variable>

namespace movie
{
// Audio is the transport clock. The device callback only consumes prepared
// blocks: no codec, VFS, lock, allocation or GUI work on the audio thread.
class Playback final : private juce::AudioIODeviceCallback
{
public:
    Playback();
    ~Playback() override;
    juce::String initialiseAudio();
    void setSequence(const Sequence&);
    void seek(double);
    void play(bool);
    bool playing()const{return isPlaying.load();}
    double position()const{return positionSeconds.load();}
    void requestPreview(double,int width=960,int height=540);
    juce::Image preview()const;
    juce::String error()const;
    uint64_t underruns()const{return underflowCount.load();}
    bool audioReady()const{return deviceReady.load();}
private:
    void audioDeviceIOCallbackWithContext(const float*const*,int,float*const*,int,int,const juce::AudioIODeviceCallbackContext&)override;
    void audioDeviceAboutToStart(juce::AudioIODevice*)override;
    void audioDeviceStopped()override;
    void produceAudio();void produceVideo();
    struct Block { uint64_t generation=0; int64_t start=0;int count=0; std::array<float,2048> samples{}; };
    std::array<Block,32> blocks;
    juce::AbstractFifo fifo{32};int readOffset=0;
    std::atomic<bool> stop{false},isPlaying{false},deviceReady{false};
    std::atomic<double> positionSeconds{0},requestedSeconds{0},rate{48000},duration{0};
    std::atomic<uint64_t> generation{1},underflowCount{0};
    mutable std::mutex mutex;
    std::condition_variable wake;
    Sequence sequence;uint64_t revision=0,previewRequest=0;
    double previewTime=0;int previewWidth=960,previewHeight=540;
    juce::Image image;juce::String problem;
    std::thread audioWorker,videoWorker;
    juce::AudioDeviceManager device;
};
}
