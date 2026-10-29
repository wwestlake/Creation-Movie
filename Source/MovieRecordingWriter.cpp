#include "MovieRecordingWriter.h"
#include <thread>
#include <cmath>
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavutil/opt.h>
}
namespace movie
{
namespace
{
void checked(int code,const char* what){if(code>=0)return;char error[256]{};av_strerror(code,error,256);throw std::runtime_error((juce::String(what)+": "+juce::String(error)).toStdString());}
struct Stream
{
    creation::services::SuiteVfsServiceClient client;juce::String project,operation,path;
    juce::int64 position=0,length=0,revision=0;juce::var status;juce::String problem;
    Stream(const juce::String& p,const juce::String& target):project(p),operation(juce::Uuid().toString()),path(target)
    {
        if(!client.discover() || !client.supportsStreamingWrites())throw std::runtime_error("The VFS service needs the Phase 4 streaming interface. No filesystem fallback is used.");
        request("begin",nullptr,0);
    }
    void request(const char* action,const void* data,size_t size)
    {
        for(int retry=0;retry<2;++retry)
        {
            if(client.streamProjectEntry(project,operation,action,path,position,revision,data,size,status))
            {revision=(juce::int64)status["revision"];length=(juce::int64)status["size"];return;}
        }
        problem=client.getLastWriteError();throw std::runtime_error(problem.toStdString());
    }
    void append(const juce::String& text){position=length;request("write",text.toRawUTF8(),text.getNumBytesAsUTF8());position=length;}
    void finish(){request("finish",nullptr,0);}
    static int write(void* opaque,const uint8_t* data,int size)
    {auto& s=*static_cast<Stream*>(opaque);try{s.request("write",data,(size_t)size);s.position+=size;return size;}catch(const std::exception& e){s.problem=e.what();return AVERROR(EIO);}}
    static int64_t seek(void* opaque,int64_t offset,int whence)
    {auto& s=*static_cast<Stream*>(opaque);if(whence==AVSEEK_SIZE)return s.length;whence&=~AVSEEK_FORCE;
     const auto next=(whence==SEEK_SET?0:whence==SEEK_CUR?s.position:whence==SEEK_END?s.length:INT64_MIN)+offset;
     if(next<0 || next>s.length)return AVERROR(EINVAL);s.position=next;return next;}
};
}
juce::var RecordedTrack::toJson()const
{return object({{"trackId",trackId},{"sourceId",sourceId},{"sourceKind",sourceKind},{"settings",settings},{"id",id},{"versionId",id},{"path",path},{"operationId",operation},{"error",error},{"usable",usable},{"startSeconds",start},{"info",info.toJson()},{"diagnostics",diagnostics}});}
struct TrackRecorder::Impl
{
    RecordedTrack result;std::shared_ptr<CaptureQueue> queue;std::unique_ptr<Stream> output,timing;
    AVFormatContext* mux=nullptr;AVCodecContext* codec=nullptr;AVStream* stream=nullptr;AVIOContext* io=nullptr;
    AVFrame* frame=nullptr;AVPacket* packet=av_packet_alloc();SwsContext* scale=nullptr;SwrContext* resampler=nullptr;
    juce::int64 origin=0,first=0,last=0,packets=0,written=0,lastVideoPts=-1,compensateAt=0,gaps=0,discontinuities=0;
    juce::int64 firstAudioPosition=0,previousAudioEnd=0;double observedPpm=0;
    juce::int64 firstNative=0,lastNative=0,maxUncertainty=0,maxResidual=0;double maxDriftPpm=0;juce::String epoch,clock,timingLines;
    int width=0,height=0,rate=0,channels=0;bool audio=false,header=false;double nominalPeriod=1.0/30;
    std::shared_ptr<const CaptureSample> previous;
    Impl(const juce::String& project,const juce::String& take,const Track& track,std::shared_ptr<CaptureQueue> q):queue(std::move(q))
    {
        result.trackId=track.id;result.sourceId=track.source["identity"].toString();result.sourceKind=track.source["category"].toString();result.settings=track.source["settings"].clone();
        result.id=juce::Uuid().toString();audio=track.type=="audio";result.path="Media/Capture/"+take+"/"+result.id+(audio?".wav":".nut");
        output=std::make_unique<Stream>(project,result.path);result.operation=output->operation;
        timing=std::make_unique<Stream>(project,"Media/Capture/"+take+"/"+result.id+".timing.jsonl");
    }
    ~Impl()
    {
        sws_freeContext(scale);swr_free(&resampler);av_frame_free(&frame);av_packet_free(&packet);avcodec_free_context(&codec);
        if(mux){mux->pb=nullptr;avformat_free_context(mux);}if(io){av_freep(&io->buffer);avio_context_free(&io);}
    }
    void prepare(const VisualPreview& preview)
    {
        checked(avformat_alloc_output_context2(&mux,nullptr,audio?"wav":"nut",nullptr),"Create recording container");
        auto* buffer=(unsigned char*)av_malloc(256*1024);if(!buffer)throw std::bad_alloc();
        io=avio_alloc_context(buffer,256*1024,1,output.get(),nullptr,Stream::write,Stream::seek);if(!io){av_free(buffer);throw std::bad_alloc();}
        mux->pb=io;mux->flags|=AVFMT_FLAG_CUSTOM_IO;stream=avformat_new_stream(mux,nullptr);if(!stream)throw std::bad_alloc();
        AVDictionary* options=nullptr;
        if(audio)
        {
            rate=(int)preview.format["sampleRate"];channels=(int)preview.format["channels"];if(rate<8000 || channels<1 || channels>8)throw std::runtime_error("Invalid active microphone format.");
            auto* p=stream->codecpar;p->codec_type=AVMEDIA_TYPE_AUDIO;p->codec_id=AV_CODEC_ID_PCM_F32LE;p->format=AV_SAMPLE_FMT_FLT;p->sample_rate=rate;p->bits_per_coded_sample=32;
            av_channel_layout_default(&p->ch_layout,channels);stream->time_base={1,rate};av_dict_set(&options,"rf64","always",0);
            AVChannelLayout layout{};av_channel_layout_default(&layout,channels);
            checked(swr_alloc_set_opts2(&resampler,&layout,AV_SAMPLE_FMT_FLT,rate,&layout,AV_SAMPLE_FMT_FLT,rate,0,nullptr),"Prepare capture resampler");av_channel_layout_uninit(&layout);
            av_opt_set_int(resampler,"flags",SWR_FLAG_RESAMPLE,0);checked(swr_init(resampler),"Initialize drift resampler");
            result.info.hasAudio=true;result.info.sampleRate=rate;result.info.channels=channels;result.info.audioCodec="pcm_f32le";
        }
        else
        {
            width=preview.image.getWidth();height=preview.image.getHeight();if(width<1 || height<1)throw std::runtime_error("No valid source frame during preparation.");
            const bool even=width%2==0 && height%2==0;const auto* encoder=even?avcodec_find_encoder_by_name("libopenh264"):avcodec_find_encoder(AV_CODEC_ID_FFV1);
            if(!encoder)throw std::runtime_error("Required recording video encoder is unavailable.");
            codec=avcodec_alloc_context3(encoder);codec->width=width;codec->height=height;codec->time_base={1,1000000};codec->framerate={30,1};
            if((int)preview.format["fpsNum"]>0 && (int)preview.format["fpsDen"]>0){codec->framerate={(int)preview.format["fpsNum"],(int)preview.format["fpsDen"]};nominalPeriod=(double)codec->framerate.den/codec->framerate.num;}
            codec->pix_fmt=even?AV_PIX_FMT_YUV420P:AV_PIX_FMT_BGRA;codec->bit_rate=std::max<int64_t>(4000000,(int64_t)width*height*4);codec->gop_size=30;codec->max_b_frames=0;codec->thread_count=1;
            if(mux->oformat->flags&AVFMT_GLOBALHEADER)codec->flags|=AV_CODEC_FLAG_GLOBAL_HEADER;
            checked(avcodec_open2(codec,encoder,nullptr),"Open recording encoder");checked(avcodec_parameters_from_context(stream->codecpar,codec),"Video stream parameters");stream->time_base=codec->time_base;stream->avg_frame_rate=codec->framerate;
            frame=av_frame_alloc();frame->format=codec->pix_fmt;frame->width=width;frame->height=height;checked(av_frame_get_buffer(frame,32),"Video frame allocation");
            scale=sws_getContext(width,height,AV_PIX_FMT_BGRA,width,height,codec->pix_fmt,SWS_BILINEAR,nullptr,nullptr,nullptr);if(!scale)throw std::runtime_error("Video conversion unavailable.");
            result.info.hasVideo=true;result.info.width=width;result.info.height=height;result.info.fpsNum=codec->framerate.num;result.info.fpsDen=codec->framerate.den;result.info.videoCodec=encoder->name;
        }
        if(!audio)av_dict_set(&options,"write_index","0",0);
        const int status=avformat_write_header(mux,&options);av_dict_free(&options);checked(status,"Write streaming header");avio_flush(io);checked(io->error,"Flush recording header");header=true;
    }
    void writePackets(juce::int64 duration)
    {
        for(;;){const auto code=avcodec_receive_packet(codec,packet);if(code==AVERROR(EAGAIN) || code==AVERROR_EOF)return;checked(code,"Receive recording packet");
            packet->duration=std::max<juce::int64>(1,duration);av_packet_rescale_ts(packet,codec->time_base,stream->time_base);packet->stream_index=stream->index;
            const int writeResult=av_interleaved_write_frame(mux,packet);av_packet_unref(packet);checked(writeResult,"Write video recording packet");}
    }
    void video(const CaptureSample& sample,juce::int64 duration)
    {
        if(sample.image.getWidth()!=width || sample.image.getHeight()!=height)throw std::runtime_error("Source dimensions changed; finalized the existing size as a partial take.");
        const auto pts=(sample.hostTime-first)/10;if(pts<=lastVideoPts){++gaps;return;}lastVideoPts=pts;
        checked(av_frame_make_writable(frame),"Writable recording frame");juce::Image::BitmapData pixels(sample.image,juce::Image::BitmapData::readOnly);
        const uint8_t* data[]={pixels.data};int strides[]={pixels.lineStride};sws_scale(scale,data,strides,0,height,frame->data,frame->linesize);frame->pts=pts;
        checked(avcodec_send_frame(codec,frame),"Encode captured video");writePackets(duration/10);++written;
        result.info.duration=(sample.hostTime-first+duration)/10000000.0;
    }
    void audioPacket(const float* samples,int count)
    {
        if(count<=0)return;checked(av_new_packet(packet,count*channels*(int)sizeof(float)),"Audio recording packet");std::memcpy(packet->data,samples,(size_t)packet->size);
        packet->pts=packet->dts=av_rescale_q(written,{1,rate},stream->time_base);packet->duration=av_rescale_q(count,{1,rate},stream->time_base);packet->stream_index=stream->index;
        const int writeResult=av_interleaved_write_frame(mux,packet);av_packet_unref(packet);checked(writeResult,"Write original microphone signal");written+=count;result.info.duration=written/(double)rate;
    }
    void audioSample(const CaptureSample& sample)
    {
        if(sample.rate!=rate || sample.channels!=channels || !sample.audio)throw std::runtime_error("Microphone format changed during recording.");
        if(sample.invalidTimestamp)throw std::runtime_error("Microphone reported an invalid clock timestamp; retained partial recording.");
        const auto desired=(juce::int64)std::llround((sample.hostTime-first)*rate/10000000.0);
        const auto pending=swr_get_delay(resampler,rate);const auto residual=desired-written-pending;maxResidual=std::max(maxResidual,std::abs(residual));
        if(sample.discontinuity || (packets>0 && sample.position!=previousAudioEnd))++discontinuities;
        if(packets==0)firstAudioPosition=sample.position;
        if(sample.hostTime-first>10000000)observedPpm=1000000.0*((sample.position-firstAudioPosition)*10000000.0/((sample.hostTime-first)*rate)-1.0);
        previousAudioEnd=sample.position+sample.frames;
        if(residual>rate*2)throw std::runtime_error("Audio clock jumped by more than two seconds; retained partial recording.");
        if(residual>rate/10)
        {++gaps;std::vector<float> silence((size_t)2048*channels,0);while(written+pending<desired)audioPacket(silence.data(),(int)std::min<juce::int64>(2048,desired-written-pending));}
        else if(residual< -rate/10)throw std::runtime_error("Audio clock moved backwards; retained partial recording.");
        else if(written>=compensateAt)
        {const int correction=(int)juce::jlimit<juce::int64>(-std::max(1,rate/1000),std::max(1,rate/1000),residual);checked(swr_set_compensation(resampler,correction,rate),"Apply measured audio clock correction");compensateAt=written+rate/2;maxDriftPpm=std::max(maxDriftPpm,1000000.0*std::abs(correction)/rate);}
        const int capacity=swr_get_out_samples(resampler,sample.frames);std::vector<float> converted((size_t)capacity*channels);uint8_t* output[]={reinterpret_cast<uint8_t*>(converted.data())};const uint8_t* input[]={reinterpret_cast<const uint8_t*>(sample.audio->data())};
        const int count=swr_convert(resampler,output,capacity,input,sample.frames);checked(count,"Resample original capture for session clock");audioPacket(converted.data(),count);
    }
    void consume(const std::shared_ptr<const CaptureSample>& sample)
    {
        if(!audio && (sample->image.getWidth()!=width || sample->image.getHeight()!=height))throw std::runtime_error("Source dimensions changed; finalized the existing size as a partial take.");
        if(!first){first=sample->hostTime;firstNative=sample->sourceTime;epoch=sample->epoch;clock=sample->clock;result.start=(first-origin)/10000000.0;}
        if(sample->epoch!=epoch || (last && sample->hostTime<last))throw std::runtime_error("Source clock epoch/discontinuity changed; retained partial recording.");
        maxUncertainty=std::max(maxUncertainty,sample->uncertainty100ns);lastNative=sample->sourceTime;
        const auto line=object({{"sourceTime100ns",sample->sourceTime},{"sessionTime100ns",sample->hostTime-origin},{"sourcePosition",sample->position},{"sequence",sample->sequence},{"frames",sample->frames},{"uncertainty100ns",sample->uncertainty100ns},{"discontinuity",sample->discontinuity},{"repeated",sample->repeated},{"epoch",sample->epoch},{"clock",sample->clock}});
        timingLines+=juce::JSON::toString(line,true)+"\n";if(timingLines.getNumBytesAsUTF8()>32768){timing->append(timingLines);timingLines={};}
        if(audio)audioSample(*sample);
        else {if(previous){const auto duration=sample->hostTime-previous->hostTime;if(duration>(juce::int64)(nominalPeriod*20000000))++gaps;video(*previous,duration);}previous=sample;}
        last=sample->hostTime;++packets;
    }
    void finalize(juce::int64 stopTime)
    {
        if(!header)return;
        if(previous){const auto duration=std::max<juce::int64>(1,std::min<juce::int64>((juce::int64)(nominalPeriod*10000000),std::max<juce::int64>(1,stopTime-previous->hostTime)));video(*previous,duration);previous.reset();}
        if(codec){checked(avcodec_send_frame(codec,nullptr),"Flush video encoder");writePackets((juce::int64)(nominalPeriod*1000000));}
        if(audio && first)
        {std::vector<float> data((size_t)2048*channels);uint8_t* out[]={reinterpret_cast<uint8_t*>(data.data())};for(;;){const int n=swr_convert(resampler,out,2048,nullptr,0);checked(n,"Drain drift resampler");if(!n)break;audioPacket(data.data(),n);}}
        checked(av_write_trailer(mux),"Finalize recording media");avio_flush(io);checked(io->error,"Flush VFS recording");output->finish();
        result.usable=written>0 && result.info.duration>0;
        if(!result.usable && result.error.isEmpty())result.error="No captured samples arrived before recording stopped.";
        try{if(timingLines.isNotEmpty())timing->append(timingLines);timing->finish();}
        catch(const std::exception& e){result.error+=(result.error.isEmpty()?"":"; ")+juce::String("Timing sidecar retained incomplete: ")+e.what();}
    }
    void run(juce::int64 zero,const std::atomic<bool>& stop,const std::atomic<juce::int64>& stopTime)
    {
        origin=zero;
        try
        {
            for(;;)
            {if(queue->overflow.load() && result.error.isEmpty()){result.error="Bounded recording queue overflow; finalized retained prefix.";queue->after=INT64_MAX;}auto sample=queue->pop();if(sample){if(sample->audio)
                {auto trimmed=std::make_shared<CaptureSample>(*sample);const int skip=(int)std::max<juce::int64>(0,(juce::int64)std::ceil((zero-sample->hostTime)*sample->rate/10000000.0));
                 if(skip>=sample->frames)continue;if(skip>0){trimmed->audio=std::make_shared<const std::vector<float>>(sample->audio->begin()+(size_t)skip*sample->channels,sample->audio->end());trimmed->frames-=skip;trimmed->hostTime+=(juce::int64)(skip*10000000.0/sample->rate);trimmed->sourceTime=trimmed->hostTime;trimmed->position+=skip;}
                 if(stopTime.load()!=INT64_MAX)trimmed->frames=std::min(trimmed->frames,(int)std::max<juce::int64>(0,(stopTime.load()-trimmed->hostTime)*trimmed->rate/10000000));if(trimmed->frames>0)consume(trimmed);}
                 else if(sample->hostTime>=zero && sample->hostTime<=stopTime.load())consume(sample);continue;}if(stop.load() || result.error.isNotEmpty())break;std::this_thread::sleep_for(std::chrono::milliseconds(3));}
        }catch(const std::exception& e){queue->after=INT64_MAX;result.error=e.what();}
        try{finalize(stopTime.load()==INT64_MAX?captureHostTime():stopTime.load());}catch(const std::exception& e){result.error+=(result.error.isEmpty()?"":"; ")+juce::String(e.what());result.usable=false;}
        result.diagnostics=object({{"sourceClock",clock},{"sourceEpoch",epoch},{"firstSourceTime100ns",firstNative},{"lastSourceTime100ns",lastNative},{"firstHostTime100ns",first},{"lastHostTime100ns",last},{"maxClockUncertainty100ns",maxUncertainty},{"packets",packets},{"gaps",gaps},{"discontinuities",discontinuities},{"maxAudioResidualFrames",maxResidual},{"maxCorrectionPpm",maxDriftPpm},{"observedAudioClockPpm",observedPpm},{"queuePeakBytes",(juce::int64)queue->peakBytes.load()},{"queueLimitBytes",(juce::int64)queue->limit},{"timingPath",timing->path}});
    }
};
TrackRecorder::TrackRecorder(const juce::String& p,const juce::String& t,const Track& track,std::shared_ptr<CaptureQueue> q):impl(std::make_unique<Impl>(p,t,track,std::move(q))){}
TrackRecorder::~TrackRecorder()=default;
void TrackRecorder::prepare(const VisualPreview& p){impl->prepare(p);}void TrackRecorder::run(juce::int64 t,const std::atomic<bool>& s,const std::atomic<juce::int64>& end){impl->run(t,s,end);}RecordedTrack TrackRecorder::result()const{return impl->result;}
}
