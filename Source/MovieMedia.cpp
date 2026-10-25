#include "MovieMedia.h"
#include <creation/assets/VfsEntryInputStream.h>
#include <stdexcept>
#include <limits>
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
#include <libavutil/display.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}

namespace movie
{
namespace
{
void check(int code,const char* operation)
{
    if(code>=0)return;
    char message[AV_ERROR_MAX_STRING_SIZE]{};av_strerror(code,message,sizeof(message));
    throw std::runtime_error(std::string(operation)+": "+message);
}
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int readStream(void* opaque,uint8_t* bytes,int count)
{
    try { const auto n=static_cast<juce::InputStream*>(opaque)->read(bytes,count);return n>0?n:AVERROR_EOF; }
    catch(...){return AVERROR(EIO);}
}
int64_t seekStream(void* opaque,int64_t offset,int whence)
{
    auto& stream=*static_cast<juce::InputStream*>(opaque);
    const auto total=stream.getTotalLength();
    if(whence&AVSEEK_SIZE)return total>=0?total:AVERROR(ENOSYS);
    whence&=~AVSEEK_FORCE;
    if(whence==SEEK_CUR)offset+=stream.getPosition();
    else if(whence==SEEK_END){if(total<0)return AVERROR(ENOSYS);offset+=total;}
    else if(whence!=SEEK_SET)return AVERROR(EINVAL);
    return offset>=0 && (total<0 || offset<=total) && stream.setPosition(offset)?offset:AVERROR(EIO);
}
int interrupted(void* p){return p && static_cast<std::atomic<bool>*>(p)->load()?1:0;}
double timestamp(AVFrame* f,AVStream* s,double origin,double fallback)
{
    return f->best_effort_timestamp==AV_NOPTS_VALUE?fallback:f->best_effort_timestamp*av_q2d(s->time_base)-origin;
}
}
juce::String codecVersion(){return av_version_info();}

struct MediaReader::Impl
{
    std::unique_ptr<juce::InputStream> input;
    AVFormatContext* format=nullptr;AVIOContext* io=nullptr;AVCodecContext* codec=nullptr;
    AVPacket* packet=av_packet_alloc();AVFrame* frame=av_frame_alloc();AVFrame* held=av_frame_alloc();
    SwsContext* scaler=nullptr;SwrContext* resampler=nullptr;
    AVStream* stream=nullptr;
    MediaInfo metadata;
    Kind kind;bool drained=false,hasFuture=false;double origin=0,lastRequest=-100,lastVideoTime=-1,futureTime=0;
    juce::Image lastImage;
    int lastWidth=0,lastHeight=0,outputRate=0,rotationDegrees=0;
    int64_t pcmStart=0,nextSample=0;bool audioClock=false;
    std::vector<float> pcm;
    explicit Impl(std::unique_ptr<juce::InputStream> in,Kind k):input(std::move(in)),kind(k){}
    ~Impl()
    {
        sws_freeContext(scaler);swr_free(&resampler);avcodec_free_context(&codec);
        av_frame_free(&held);av_frame_free(&frame);av_packet_free(&packet);
        avformat_close_input(&format);
        if(io){av_freep(&io->buffer);avio_context_free(&io);}
    }
    void open(const juce::String& name,std::atomic<bool>* cancel)
    {
        require(input!=nullptr,"Media stream could not be opened.");
        const bool canSeek=input->getTotalLength()>=0 && input->setPosition(0);
        require(canSeek,"Media source is not seekable; random-access timeline editing and playback require a seekable stream.");
        auto* buffer=(uint8_t*)av_malloc(65536);require(buffer!=nullptr,"AVIO allocation failed.");
        io=avio_alloc_context(buffer,65536,0,input.get(),readStream,nullptr,seekStream);
        if(!io){av_free(buffer);throw std::runtime_error("AVIO allocation failed.");}
        io->seekable=AVIO_SEEKABLE_NORMAL;
        format=avformat_alloc_context();require(format!=nullptr,"Media context allocation failed.");
        format->pb=io;format->flags|=AVFMT_FLAG_CUSTOM_IO;
        format->interrupt_callback={interrupted,cancel};
        AVDictionary* options=nullptr;
        // Reject network/playlist demuxers; a managed asset is a self-contained stream.
        av_dict_set(&options,"format_whitelist","mov,matroska,webm,avi,mpegts,mpeg,mp3,wav,flac,ogg,aiff,aac,png_pipe,jpeg_pipe,bmp_pipe,tiff_pipe,webp_pipe",0);
        av_dict_set(&options,"protocol_whitelist","",0);
        const int opened=avformat_open_input(&format,name.toRawUTF8(),nullptr,&options);av_dict_free(&options);check(opened,"Open media");
        check(avformat_find_stream_info(format,nullptr),"Read media metadata");
        origin=format->start_time==AV_NOPTS_VALUE?0:format->start_time/(double)AV_TIME_BASE;
        metadata.duration=format->duration==AV_NOPTS_VALUE?0:format->duration/(double)AV_TIME_BASE;
        const int vi=av_find_best_stream(format,AVMEDIA_TYPE_VIDEO,-1,-1,nullptr,0);
        const int ai=av_find_best_stream(format,AVMEDIA_TYPE_AUDIO,-1,-1,nullptr,0);
        if(vi>=0)
        {
            auto* vs=format->streams[vi];auto* p=vs->codecpar;
            metadata.hasVideo=true;metadata.width=p->width;metadata.height=p->height;
            auto rate=av_guess_frame_rate(format,vs,nullptr);
            if(rate.num<=0 || rate.den<=0)rate=vs->avg_frame_rate.num>0 && vs->avg_frame_rate.den>0?vs->avg_frame_rate:AVRational{30,1};
            metadata.fpsNum=rate.num;metadata.fpsDen=std::max(1,rate.den);
            auto aspect=av_guess_sample_aspect_ratio(format,vs,nullptr);metadata.pixelAspect=aspect.num>0 && aspect.den>0?av_q2d(aspect):1;
            metadata.videoCodec=avcodec_get_name(p->codec_id);
            if(auto* label=av_color_space_name(p->color_space))metadata.colourSpace=label;
            if(auto* label=av_color_transfer_name(p->color_trc))metadata.transfer=label;
            // This foundation is explicitly SDR. Never silently flatten HDR values.
            require(p->color_trc!=AVCOL_TRC_SMPTE2084 && p->color_trc!=AVCOL_TRC_ARIB_STD_B67,"HDR media needs a colour-managed tone-map pipeline; import an SDR version for this build.");
            const auto* matrix=av_packet_side_data_get(p->coded_side_data,p->nb_coded_side_data,AV_PKT_DATA_DISPLAYMATRIX);
            if(matrix && matrix->size>=9*sizeof(int32_t))
            {
                const double rot=av_display_rotation_get((const int32_t*)matrix->data);
                if(std::isfinite(rot))
                {
                    int rounded=((int)std::lround(-rot)%360+360)%360;
                    require(rounded%90==0,"Non-orthogonal display-matrix media is not supported.");
                    rotationDegrees=rounded;
                    if(rotationDegrees==90 || rotationDegrees==270)std::swap(metadata.width,metadata.height);
                }
            }
            if(metadata.duration<=0 && vs->duration!=AV_NOPTS_VALUE)metadata.duration=vs->duration*av_q2d(vs->time_base);
            metadata.still=juce::String(format->iformat->name).endsWith("_pipe");
            if(metadata.still)metadata.duration=5;
        }
        if(ai>=0)
        {
            auto* as=format->streams[ai];auto* p=as->codecpar;metadata.hasAudio=true;metadata.sampleRate=p->sample_rate;
            metadata.channels=p->ch_layout.nb_channels;metadata.audioCodec=avcodec_get_name(p->codec_id);
            if(metadata.duration<=0 && as->duration!=AV_NOPTS_VALUE)metadata.duration=std::max(metadata.duration,as->duration*av_q2d(as->time_base));
        }
        require((vi>=0 || ai>=0) && metadata.duration>0 && std::isfinite(metadata.duration),"Media has no usable timed audio/video stream.");
        require(metadata.width<=16384 && metadata.height<=16384,"Source dimensions exceed this build's decoder limit.");
        if(kind==Kind::probe)return;
        const int index=kind==Kind::audio?ai:vi;require(index>=0,"Requested media stream is missing.");
        stream=format->streams[index];const auto* decoder=avcodec_find_decoder(stream->codecpar->codec_id);
        require(decoder!=nullptr,"No decoder for this media codec.");
        codec=avcodec_alloc_context3(decoder);require(codec!=nullptr,"Decoder allocation failed.");
        check(avcodec_parameters_to_context(codec,stream->codecpar),"Configure decoder");codec->thread_count=2;
        check(avcodec_open2(codec,decoder,nullptr),"Start decoder");
    }
    bool decode()
    {
        for(;;)
        {
            const int received=avcodec_receive_frame(codec,frame);
            if(received==0)return true;
            if(received==AVERROR_EOF)return false;
            if(received!=AVERROR(EAGAIN))check(received,"Decode frame");
            if(drained)return false;
            for(;;)
            {
                av_packet_unref(packet);const int read=av_read_frame(format,packet);
                if(read==AVERROR_EOF){check(avcodec_send_packet(codec,nullptr),"Flush decoder");drained=true;break;}
                check(read,"Read media packet");
                if(packet->stream_index==stream->index){check(avcodec_send_packet(codec,packet),"Send media packet");break;}
            }
        }
    }
    void seek(double seconds)
    {
        require(io && (io->seekable&AVIO_SEEKABLE_NORMAL),"Cannot seek a non-seekable media stream.");
        const auto time=(int64_t)std::llround((std::max(0.0,seconds)+origin)/av_q2d(stream->time_base));
        check(av_seek_frame(format,stream->index,time,AVSEEK_FLAG_BACKWARD),"Seek media");
        avcodec_flush_buffers(codec);drained=false;hasFuture=false;lastVideoTime=-1;lastImage={};pcm.clear();pcmStart=0;nextSample=0;audioClock=false;swr_free(&resampler);
    }
    juce::Image convert(AVFrame* f,int maxWidth,int maxHeight)
    {
        const double aspect=metadata.pixelAspect;
        const bool swapped=(rotationDegrees==90 || rotationDegrees==270);
        const double effW=swapped?f->height:f->width*aspect,effH=swapped?f->width*aspect:f->height;
        const double scale=std::min((double)maxWidth/std::max(1.0,effW),(double)maxHeight/std::max(1.0,effH));
        const int w=std::max(1,(int)std::lround(f->width*aspect*scale)),h=std::max(1,(int)std::lround(f->height*scale));
        scaler=sws_getCachedContext(scaler,f->width,f->height,(AVPixelFormat)f->format,w,h,AV_PIX_FMT_BGRA,SWS_BILINEAR,nullptr,nullptr,nullptr);
        require(scaler!=nullptr,"Video conversion could not be configured.");
        const int colour=f->colorspace==AVCOL_SPC_BT709?SWS_CS_ITU709:f->colorspace==AVCOL_SPC_BT2020_NCL?SWS_CS_BT2020:SWS_CS_ITU601;
        sws_setColorspaceDetails(scaler,sws_getCoefficients(colour),f->color_range==AVCOL_RANGE_JPEG,
            sws_getCoefficients(SWS_CS_ITU709),1,0,1<<16,1<<16);
        juce::Image image(juce::Image::ARGB,w,h,true);juce::Image::BitmapData data(image,juce::Image::BitmapData::writeOnly);
        uint8_t* dst[]{data.data,nullptr,nullptr,nullptr};int stride[]{data.lineStride,0,0,0};
        check(sws_scale(scaler,f->data,f->linesize,0,f->height,dst,stride),"Convert video frame");
        // Decoder outputs straight BGRA; JUCE ARGB is premultiplied.
        for(int y=0;y<h;++y)for(int x=0;x<w;++x)reinterpret_cast<juce::PixelARGB*>(data.getPixelPointer(x,y))->premultiply();
        if(rotationDegrees==0)return image;
        juce::Image rotated(juce::Image::ARGB,swapped?h:w,swapped?w:h,true);
        juce::Image::BitmapData srcData(image,juce::Image::BitmapData::readOnly),dstData(rotated,juce::Image::BitmapData::writeOnly);
        for(int y=0;y<h;++y)for(int x=0;x<w;++x)
        {
            const int rx=rotationDegrees==90?h-1-y:rotationDegrees==180?w-1-x:y;
            const int ry=rotationDegrees==90?x:rotationDegrees==180?h-1-y:w-1-x;
            *reinterpret_cast<juce::PixelARGB*>(dstData.getPixelPointer(rx,ry))=*reinterpret_cast<const juce::PixelARGB*>(srcData.getPixelPointer(x,y));
        }
        return rotated;
    }
};
MediaReader::MediaReader(std::unique_ptr<juce::InputStream> input,const juce::String& name,Kind k,std::atomic<bool>* cancel)
    :impl(std::make_unique<Impl>(std::move(input),k)){impl->open(name,cancel);}
MediaReader::~MediaReader()=default;
const MediaInfo& MediaReader::info()const{return impl->metadata;}
juce::Image MediaReader::videoAt(double seconds,int w,int h)
{
    auto& d=*impl;if(d.kind!=Kind::video)throw std::runtime_error("Not a video reader.");
    if(d.metadata.still && d.lastImage.isValid() && w==d.lastWidth && h==d.lastHeight)return d.lastImage;
    if(seconds<d.lastRequest-1e-7 || seconds>d.lastRequest+1.0 || w!=d.lastWidth || h!=d.lastHeight)
        if(!d.metadata.still || d.lastRequest>=0)d.seek(d.metadata.still?0:seconds);
    d.lastRequest=seconds;d.lastWidth=w;d.lastHeight=h;
    if(d.hasFuture && d.futureTime<=seconds+1e-7)
    { d.lastImage=d.convert(d.held,w,h);d.lastVideoTime=d.futureTime;d.hasFuture=false; }
    while(!d.hasFuture && d.decode())
    {
        const auto time=timestamp(d.frame,d.stream,d.origin,std::max(0.0,d.lastVideoTime+d.metadata.fpsDen/(double)std::max(1,d.metadata.fpsNum)));
        if(time<=seconds+1e-7 || d.metadata.still)
        {d.lastImage=d.convert(d.frame,w,h);d.lastVideoTime=time;if(d.metadata.still)break;}
        else {av_frame_unref(d.held);check(av_frame_ref(d.held,d.frame),"Hold next video frame");d.futureTime=time;d.hasFuture=true;}
    }
    return d.lastImage;
}
void MediaReader::audioAt(double seconds,int samples,int rate,float* output)
{
    auto& d=*impl;require(d.kind==Kind::audio,"Not an audio reader.");std::fill(output,output+samples*2,0.0f);
    const auto begin=(int64_t)std::llround(seconds*rate);const auto end=begin+samples;
    if(rate!=d.outputRate || seconds<d.lastRequest-1e-6 || seconds>d.lastRequest+0.5)
    {d.seek(std::max(0.0,seconds-0.1));d.outputRate=rate;d.nextSample=begin;}
    d.lastRequest=seconds+(double)samples/rate;
    for(;;)
    {
        const auto pcmEnd=d.pcmStart+(int64_t)d.pcm.size()/2;
        const auto from=std::max(begin,d.pcmStart),to=std::min(end,pcmEnd);
        if(to>from)std::copy(d.pcm.begin()+(from-d.pcmStart)*2,d.pcm.begin()+(to-d.pcmStart)*2,output+(from-begin)*2);
        if(pcmEnd>=end || d.pcmStart>=end)return;
        if(!d.decode())return;
        if(!d.resampler)
        {
            AVChannelLayout stereo=AV_CHANNEL_LAYOUT_STEREO;
            check(swr_alloc_set_opts2(&d.resampler,&stereo,AV_SAMPLE_FMT_FLT,rate,&d.frame->ch_layout,
                (AVSampleFormat)d.frame->format,d.frame->sample_rate,0,nullptr),"Configure audio resampler");
            check(swr_init(d.resampler),"Start audio resampler");
        }
        const auto delay=swr_get_delay(d.resampler,d.frame->sample_rate);
        const auto time=timestamp(d.frame,d.stream,d.origin,(double)d.nextSample/rate);
        const auto timestampStart=(int64_t)std::llround(time*rate)-av_rescale_rnd(delay,rate,d.frame->sample_rate,AV_ROUND_NEAR_INF);
        // Coarse container timestamps (e.g. Matroska milliseconds) round adjacent
        // PCM packets independently. Keep the exact sample clock inside that
        // quantization tolerance; real discontinuities still re-anchor to PTS.
        const auto tolerance=std::max<int64_t>(1,(int64_t)std::ceil(rate*av_q2d(d.stream->time_base)));
        d.pcmStart=d.audioClock && std::abs(timestampStart-d.nextSample)<=tolerance?d.nextSample:timestampStart;
        d.audioClock=true;
        const int capacity=(int)av_rescale_rnd(delay+d.frame->nb_samples,rate,d.frame->sample_rate,AV_ROUND_UP)+32;
        d.pcm.resize((size_t)capacity*2);uint8_t* destination[]{(uint8_t*)d.pcm.data()};
        const int n=swr_convert(d.resampler,destination,capacity,(const uint8_t**)d.frame->extended_data,d.frame->nb_samples);
        check(n,"Resample audio");d.pcm.resize((size_t)n*2);d.nextSample=d.pcmStart+n;
    }
}

Renderer::Renderer(Sequence s,std::atomic<bool>* c):sequence(std::move(s)),cancel(c)
{
    sequence.resolveTrackIndices();automation=resolveAutomation(sequence);trackClips.resize(sequence.tracks.size());spans.resize(sequence.tracks.size(),{1.0e30,0});audioEffects.resize(sequence.tracks.size());
    audioTransitions.resize(sequence.clips.size());videoTransitions.resize(sequence.tracks.size());
    for(size_t i=0;i<sequence.transitions.size();++i)
    {
        const auto& tr=sequence.transitions[i];
        if(tr.audio()){audioTransitions[(size_t)sequence.findClip(tr.outgoing)].push_back(i);audioTransitions[(size_t)sequence.findClip(tr.incoming)].push_back(i);}
        else videoTransitions[(size_t)sequence.findTrack(tr.trackId)].push_back(i);
    }
    for(size_t i=0;i<sequence.clips.size();++i)
    {
        const auto& clip=sequence.clips[i];if(clip.trackIndex<0)continue;
        const auto ti=(size_t)clip.trackIndex;trackClips[ti].push_back(i);
        spans[ti].first=std::min(spans[ti].first,clip.startSeconds);spans[ti].second=std::max(spans[ti].second,clip.startSeconds+clip.durationSeconds);
    }
}
MediaReader& Renderer::reader(const Media& m,bool audio,const juce::String& instance)
{
    const auto key=m.id+(audio?"/a":"/v")+instance;auto found=readers.find(key);
    if(found!=readers.end())return *found->second;
    // A bounded cache; sequential clips reuse their reader across frames/blocks.
    if(readers.size()>=16)readers.erase(readers.begin());
    auto input=creation::assets::openVfsEntryStream(sequence.projectId,m.path);
    auto r=std::make_unique<MediaReader>(std::move(input),m.name,audio?MediaReader::Kind::audio:MediaReader::Kind::video,cancel);
    auto& ref=*r;readers[key]=std::move(r);return ref;
}
void Renderer::drawVideoClip(juce::Graphics& g,const Clip& c,double seconds,int width,int height)
{
            g.setOpacity((float)(double)c.videoParams.getWithDefault("opacity",1.0));
            auto title=c.videoParams["title"].toString();
            if(title.isNotEmpty())
            {
                const auto box=juce::Rectangle<int>(width/16,height*3/4,width*7/8,height/5);
                g.setColour(juce::Colours::black.withAlpha(0.65f));g.fillRoundedRectangle(box.toFloat(),(float)height/80);
                g.setColour(juce::Colours::white);g.setFont(juce::FontOptions((float)height/18,juce::Font::bold));
                g.drawFittedText(title,box.reduced(width/40),juce::Justification::centred,3);return;
            }
            const auto* m=sequence.findMedia(c.assetId);if(!m || !m->info.hasVideo)return;
            const auto image=reader(*m,false,c.id).videoAt(m->info.still?0:c.sourceStartSeconds+seconds-c.startSeconds,width,height);
            if(image.isValid())g.drawImageWithin(image,0,0,width,height,juce::RectanglePlacement::centred,false);
}
juce::Image Renderer::video(double seconds,int width,int height)
{
    juce::Image output(juce::Image::ARGB,width,height,true);juce::Graphics composite(output);composite.fillAll(juce::Colours::black);
    for(size_t ti=0;ti<sequence.tracks.size();++ti)
    {
        const auto& track=sequence.tracks[ti];if(!track.enabled)continue;
        const bool processing=std::any_of(track.effects.begin(),track.effects.end(),[](const Effect& e){return e.enabled && !e.bypassed && e.type.startsWith("video.");});
        juce::Image layer;if(processing)layer=juce::Image(juce::Image::ARGB,width,height,true);
        std::unique_ptr<juce::Graphics> layerGraphics;if(processing)layerGraphics=std::make_unique<juce::Graphics>(layer);
        auto& g=processing?*layerGraphics:composite;
        const Transition* active=nullptr;
        for(const auto i:videoTransitions[ti])if(sequence.transitions[i].active(seconds)){active=&sequence.transitions[i];break;}
        for(const auto index:trackClips[ti])
        {
            const auto& c=sequence.clips[index];if(active && (c.id==active->outgoing || c.id==active->incoming))continue;if(seconds+1e-8<c.startSeconds || seconds>=c.startSeconds+c.durationSeconds-1e-8)continue;
            if(cancel && cancel->load())throw std::runtime_error("Cancelled");
            drawVideoClip(g,c,seconds,width,height);
        }
        if(active)
        {
            juce::Image a(juce::Image::ARGB,width,height,true),b(juce::Image::ARGB,width,height,true);
            {juce::Graphics ga(a),gb(b);drawVideoClip(ga,sequence.clips[(size_t)sequence.findClip(active->outgoing)],seconds,width,height);drawVideoClip(gb,sequence.clips[(size_t)sequence.findClip(active->incoming)],seconds,width,height);}
            g.setOpacity(1);g.drawImageAt(blendTransition(*active,seconds,a,b),0,0);
        }
        if(processing){layerGraphics.reset();const auto evaluated=evaluatedEffects(track.effects,automation,seconds);processVideoEffects(layer,evaluated);composite.setOpacity(1);composite.drawImageAt(layer,0,0);}
    }
    return output;
}
void Renderer::audioBlock(double seconds,int samples,int rate,float* output)
{
    std::fill(output,output+samples*2,0.0f);scratch.resize((size_t)samples*2);trackBuffer.resize((size_t)samples*2);
    for(size_t ti=0;ti<sequence.tracks.size();++ti)
    {
        const auto& track=sequence.tracks[ti];if(!track.enabled)continue;
        if(cancel && cancel->load())throw std::runtime_error("Cancelled");
        std::fill(trackBuffer.begin(),trackBuffer.end(),0.0f);
        for(const auto index:trackClips[ti])
        {
            const auto& c=sequence.clips[index];auto* m=sequence.findMedia(c.assetId);if(!m || !m->info.hasAudio || c.soundDetached)continue;
            double clipStart=c.startSeconds,clipEnd=c.startSeconds+c.durationSeconds;
            for(const auto tr:audioTransitions[index]){clipStart=std::min(clipStart,sequence.transitions[tr].start);clipEnd=std::max(clipEnd,sequence.transitions[tr].end());}
            const auto from=std::max(seconds,clipStart),to=std::min(seconds+(double)samples/rate,clipEnd);
            if(to<=from)continue;
            const int offset=juce::jlimit(0,samples,(int)std::llround((from-seconds)*rate));
            const int n=juce::jlimit(0,samples-offset,(int)std::llround((to-from)*rate));if(n==0)continue;
            reader(*m,true,c.id).audioAt(c.sourceStartSeconds+from-c.startSeconds,n,rate,scratch.data());
            const float gain=(float)(double)c.videoParams.getWithDefault("gain",1.0);
            for(int i=0;i<n;++i)
            {
                const double t=seconds+(double)(offset+i)/rate;double weight=t>=c.startSeconds && t<c.startSeconds+c.durationSeconds?1:0;
                for(const auto tr:audioTransitions[index])
                {
                    const auto& transition=sequence.transitions[tr];if(!transition.active(t))continue;
                    const auto gains=transitionAudioGains(transition,t);weight=c.id==transition.outgoing?gains.first:gains.second;break;
                }
                for(int ch=0;ch<2;++ch)trackBuffer[((size_t)offset+i)*2+ch]+=scratch[(size_t)i*2+ch]*(float)(gain*weight);
            }
        }
        audioEffects[ti].process(trackBuffer.data(),samples,rate,seconds-spans[ti].first,spans[ti].second-spans[ti].first,track.effects,&automation,seconds);
        for(int i=0;i<samples*2;++i)output[i]+=trackBuffer[(size_t)i];
    }
    for(int i=0;i<samples*2;++i)output[i]=juce::jlimit(-1.0f,1.0f,output[i]);
}
void Renderer::audio(double seconds,int samples,int rate,float* output)
{
    if(rate!=audioRate || std::abs(seconds-nextAudioTime)>.25/rate)
    {
        for(auto& state:audioEffects)state.reset();
        bool filtered=false;
        for(const auto& t:sequence.tracks)if(t.enabled)for(const auto& e:t.effects)filtered|=e.enabled && !e.bypassed && e.type=="audio.filter";
        if(filtered && seconds>0)
        {
            discard.resize(2048);const int64_t end=std::llround(seconds*rate);int64_t sample=std::max<int64_t>(0,end-rate);
            while(sample<end){const int n=(int)std::min<int64_t>(1024,end-sample);audioBlock((double)sample/rate,n,rate,discard.data());sample+=n;}
        }
    }
    audioBlock(seconds,samples,rate,output);audioRate=rate;nextAudioTime=seconds+(double)samples/rate;
}
}
