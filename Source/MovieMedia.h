#pragma once
#include "MovieDocument.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <atomic>
#include <map>

namespace movie
{
// Each reader belongs to exactly one worker. FFmpeg reads the supplied seekable
// stream directly; a VFS entry is never materialised into an OS temporary file.
class MediaReader
{
public:
    enum class Kind { probe, video, audio };
    MediaReader(std::unique_ptr<juce::InputStream>, const juce::String& name, Kind, std::atomic<bool>* cancel=nullptr);
    ~MediaReader();
    const MediaInfo& info() const;
    juce::Image videoAt(double seconds, int maxWidth, int maxHeight);
    void audioAt(double seconds, int samples, int rate, float* interleavedStereo);
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

class Renderer
{
public:
    explicit Renderer(Sequence sequence, std::atomic<bool>* cancel=nullptr);
    juce::Image video(double seconds,int width,int height);
    void audio(double seconds,int samples,int rate,float* stereo);
private:
    MediaReader& reader(const Media&,bool audio);
    Sequence sequence;
    std::atomic<bool>* cancel;
    std::map<juce::String,std::unique_ptr<MediaReader>> readers;
};

struct ExportOptions { juce::File destination; juce::String preset {"mp4"}; int width=0,height=0; bool overwrite=false; };
void exportSequence(const Sequence&,const ExportOptions&,std::atomic<bool>& cancel,const std::function<void(double)>& progress);
juce::String codecVersion();
}
