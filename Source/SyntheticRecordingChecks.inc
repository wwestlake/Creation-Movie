// Runs on the existing background-job worker; generated signals are never physical-device evidence.
static juce::var syntheticRecordingChecks(const juce::String& project,MainComponent::Job& job)
{
    auto require=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    using namespace movie;juce::Array<juce::var> outputs;const auto take=juce::Uuid().toString();
    Track track;track.id="synthetic-audio";track.name="Timestamped synthetic drift";track.type="audio";
    track.source=object({{"category","microphone"},{"identity","synthetic-clock"},{"settings",object({{"sampleRate",8000},{"channels",1}})}});
    auto queue=std::make_shared<CaptureQueue>();VisualPreview preview;preview.format=track.source["settings"];TrackRecorder recorder(project,take,track,queue);recorder.prepare(preview);
    const auto origin=captureHostTime();queue->after=origin;std::atomic<bool> stop{false},done{false};std::atomic<juce::int64> end{INT64_MAX};
    std::thread consumer([&]{recorder.run(origin,stop,end);done=true;});
    constexpr int rate=8000,block=4000,blocks=3600;constexpr double ppm=300.0;
    try
    {
        for(int n=0;n<blocks;++n)
        {
            while(queue->bytes.load()>1024*1024 && !done.load() && !job.cancel.load())juce::Thread::sleep(1);
            if(done.load() || job.cancel.load())throw std::runtime_error("Synthetic drift recorder stopped before fixture completion.");
            auto sample=std::make_shared<CaptureSample>();auto data=std::make_shared<std::vector<float>>(block);
            for(int i=0;i<block;++i)(*data)[(size_t)i]=(float)(.25*std::sin(2*juce::MathConstants<double>::pi*440*((juce::int64)n*block+i)/rate));
            sample->audio=data;sample->rate=rate;sample->channels=1;sample->frames=block;sample->position=(juce::int64)n*block;
            sample->hostTime=origin+(juce::int64)std::llround(sample->position*10000000.0/(rate*(1+ppm/1000000)));sample->sourceTime=sample->hostTime;sample->epoch="synthetic-300ppm";sample->clock="synthetic-qpc";sample->sequence=n;queue->push(sample);job.progress=(double)n/blocks;
        }
        end=origin+(juce::int64)std::llround((juce::int64)blocks*block*10000000.0/(rate*(1+ppm/1000000)));stop=true;consumer.join();
    }
    catch(...){stop=true;if(consumer.joinable())consumer.join();throw;}
    auto result=recorder.result();require(result.usable && result.error.isEmpty(),("Synthetic drift output failed: "+result.error).toRawUTF8());
    const double expected=(end.load()-origin)/10000000.0;MediaReader probe(creation::assets::openVfsEntryStream(project,result.path),result.path,MediaReader::Kind::probe);
    const double error=std::abs(probe.info().duration-expected);require(error<.05,("30-minute 300ppm drift residual: "+juce::String(error,9)).toRawUTF8());
    outputs.add(object({{"synthetic",true},{"virtualDurationSeconds",1800},{"inputClockPpm",ppm},{"expectedDuration",expected},{"decodedDuration",probe.info().duration},{"durationErrorSeconds",error},{"result",result.toJson()}}));
    // VFR, repeated timestamps and odd native dimensions exercise both practical encoders.
    for(const int width:{64,65})
    {
        const int height=width==64?48:49;Track video;video.id="synthetic-video";video.type="video";video.source=object({{"category","window"},{"identity","synthetic-window"}});
        auto frames=std::make_shared<CaptureQueue>();frames->after=origin;TrackRecorder writer(project,take,video,frames);VisualPreview format;format.image=juce::Image(juce::Image::ARGB,width,height,true,juce::SoftwareImageType{});writer.prepare(format);
        std::atomic<bool> finished{false};std::atomic<juce::int64> until{origin+11000000};std::thread encoder([&]{writer.run(origin,finished,until);});
        const double times[]={0,.03,.10,.10,.60,1.03};int sequence=0;
        for(const double time:times){auto frame=std::make_shared<CaptureSample>();frame->image=juce::Image(juce::Image::ARGB,width,height,true,juce::SoftwareImageType{});{juce::Graphics g(frame->image);g.fillAll(sequence%2?juce::Colours::red:juce::Colours::blue);}frame->hostTime=frame->sourceTime=origin+(juce::int64)(time*10000000);frame->epoch="synthetic-vfr";frame->sequence=sequence++;frame->clock="synthetic-qpc";frames->push(frame);}
        finished=true;encoder.join();const auto recorded=writer.result();require(recorded.usable,("VFR/odd-size recording failed: "+recorded.error).toRawUTF8());
        MediaReader pixels(creation::assets::openVfsEntryStream(project,recorded.path),recorded.path,MediaReader::Kind::video);require(pixels.info().width==width && pixels.info().height==height && pixels.videoAt(.5,width,height).isValid(),"VFR native dimensions/pixels failed to decode");outputs.add(recorded.toJson());
    }
    return object({{"synthetic",true},{"results",outputs}});
}
