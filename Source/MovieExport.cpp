#include "MovieMedia.h"
#include <stdexcept>
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}
namespace movie
{
namespace
{
void checked(int code,const char* label)
{
    if(code>=0)return;
    char message[AV_ERROR_MAX_STRING_SIZE]{};av_strerror(code,message,sizeof(message));
    throw std::runtime_error(std::string(label)+": "+message);
}
struct Writer
{
    AVFormatContext* format=nullptr;AVCodecContext* video=nullptr;AVCodecContext* audio=nullptr;
    AVStream *vs=nullptr,*as=nullptr;AVFrame *vf=av_frame_alloc(),*af=av_frame_alloc();
    AVPacket* packet=av_packet_alloc();SwsContext* scale=nullptr;SwrContext* resample=nullptr;
    ~Writer()
    {
        sws_freeContext(scale);swr_free(&resample);av_frame_free(&vf);av_frame_free(&af);av_packet_free(&packet);
        avcodec_free_context(&video);avcodec_free_context(&audio);
        if(format){if(format->pb)avio_closep(&format->pb);avformat_free_context(format);}
    }
    void packets(AVCodecContext* codec,AVStream* stream)
    {
        for(;;)
        {
            int r=avcodec_receive_packet(codec,packet);
            if(r==AVERROR(EAGAIN) || r==AVERROR_EOF)return;
            checked(r,"Receive encoded packet");
            if(codec==video && packet->duration<=0)packet->duration=1;
            av_packet_rescale_ts(packet,codec->time_base,stream->time_base);
            packet->stream_index=stream->index;const auto written=av_interleaved_write_frame(format,packet);av_packet_unref(packet);
            checked(written,"Write encoded packet");
        }
    }
    void send(AVCodecContext* codec,AVStream* stream,AVFrame* frame)
    {checked(avcodec_send_frame(codec,frame),"Encode frame");packets(codec,stream);}
};
}
void exportSequence(const Sequence& sequence,const ExportOptions& options,std::atomic<bool>& cancel,const std::function<void(double)>& progress)
{
    const int width=options.width>0?options.width:sequence.width,height=options.height>0?options.height:sequence.height;
    if(width<16 || height<16 || width>8192 || height>8192 || width%2 || height%2)
        throw std::runtime_error("Export dimensions must be even, between 16 and 8192.");
    if(sequence.duration()<=0)throw std::runtime_error("The timeline is empty.");
    const bool mp4=options.preset=="mp4",prores=options.preset=="prores",lossless=options.preset=="ffv1";
    if(!mp4 && !prores && !lossless)throw std::runtime_error("Unknown export preset.");
    if(!options.destination.getParentDirectory().isDirectory())throw std::runtime_error("Export folder does not exist.");
    if(options.destination.exists() && !options.overwrite)throw std::runtime_error("Export destination already exists; choose another name or explicitly allow overwrite.");
    const auto extension=mp4?".mp4":prores?".mov":".mkv";
    if(!options.destination.hasFileExtension(extension))throw std::runtime_error("Export filename extension does not match the preset.");
    // Staging beside an explicitly selected export is part of the export, not a
    // second working-media cache. The old destination survives any failed render.
    juce::TemporaryFile temporary(options.destination);
    {
        const auto path=temporary.getFile().getFullPathName();
        Writer w;checked(avformat_alloc_output_context2(&w.format,nullptr,mp4?"mp4":prores?"mov":"matroska",path.toRawUTF8()),"Create output container");
        const auto* encoder=avcodec_find_encoder_by_name(mp4?"libopenh264":prores?"prores_ks":"ffv1");
        if(!encoder)throw std::runtime_error("The selected export encoder is not available in this FFmpeg build.");
        w.video=avcodec_alloc_context3(encoder);if(!w.video)throw std::bad_alloc();
        auto* v=w.video;v->width=width;v->height=height;v->time_base={sequence.fpsDen,sequence.fpsNum};v->framerate={sequence.fpsNum,sequence.fpsDen};
        v->pix_fmt=prores?AV_PIX_FMT_YUV422P10LE:lossless?AV_PIX_FMT_BGRA:AV_PIX_FMT_YUV420P;
        v->thread_count=2;v->gop_size=60;v->max_b_frames=0;
        v->bit_rate=mp4?std::max<int64_t>(4000000,(int64_t)width*height*6):0;
        v->color_range=lossless?AVCOL_RANGE_JPEG:AVCOL_RANGE_MPEG;v->colorspace=AVCOL_SPC_BT709;
        v->color_primaries=AVCOL_PRI_BT709;v->color_trc=AVCOL_TRC_BT709;
        if(w.format->oformat->flags&AVFMT_GLOBALHEADER)v->flags|=AV_CODEC_FLAG_GLOBAL_HEADER;
        if(prores)av_opt_set(v->priv_data,"profile","3",0);
        checked(avcodec_open2(v,encoder,nullptr),"Open video encoder");
        w.vs=avformat_new_stream(w.format,nullptr);if(!w.vs)throw std::bad_alloc();w.vs->time_base=v->time_base;
        checked(avcodec_parameters_from_context(w.vs->codecpar,v),"Set video stream");
        const auto* ac=avcodec_find_encoder(mp4?AV_CODEC_ID_AAC:AV_CODEC_ID_PCM_S16LE);
        if(!ac)throw std::runtime_error("Audio encoder unavailable.");
        w.audio=avcodec_alloc_context3(ac);if(!w.audio)throw std::bad_alloc();
        auto* a=w.audio;a->sample_rate=48000;a->time_base={1,48000};a->sample_fmt=mp4?AV_SAMPLE_FMT_FLTP:AV_SAMPLE_FMT_S16;
        av_channel_layout_default(&a->ch_layout,2);a->bit_rate=192000;a->thread_count=1;
        if(w.format->oformat->flags&AVFMT_GLOBALHEADER)a->flags|=AV_CODEC_FLAG_GLOBAL_HEADER;
        checked(avcodec_open2(a,ac,nullptr),"Open audio encoder");
        w.as=avformat_new_stream(w.format,nullptr);if(!w.as)throw std::bad_alloc();w.as->time_base=a->time_base;
        checked(avcodec_parameters_from_context(w.as->codecpar,a),"Set audio stream");
        checked(avio_open(&w.format->pb,path.toRawUTF8(),AVIO_FLAG_READ_WRITE),"Open export destination");
        const auto header=avformat_write_header(w.format,nullptr);checked(header,"Write export header");
        w.vf->format=v->pix_fmt;w.vf->width=width;w.vf->height=height;w.vf->color_range=v->color_range;
        w.vf->colorspace=v->colorspace;w.vf->color_primaries=v->color_primaries;w.vf->color_trc=v->color_trc;
        checked(av_frame_get_buffer(w.vf,32),"Allocate output video frame");
        const int block=a->frame_size>0?a->frame_size:1024;
        w.af->format=a->sample_fmt;w.af->sample_rate=48000;w.af->nb_samples=block;
        checked(av_channel_layout_copy(&w.af->ch_layout,&a->ch_layout),"Set output audio channels");
        checked(av_frame_get_buffer(w.af,0),"Allocate output audio frame");
        AVChannelLayout stereo=AV_CHANNEL_LAYOUT_STEREO;
        checked(swr_alloc_set_opts2(&w.resample,&stereo,a->sample_fmt,48000,&stereo,AV_SAMPLE_FMT_FLT,48000,0,nullptr),"Configure export audio");
        checked(swr_init(w.resample),"Start export audio");
        w.scale=sws_getContext(width,height,AV_PIX_FMT_BGRA,width,height,v->pix_fmt,SWS_BICUBIC,nullptr,nullptr,nullptr);
        if(!w.scale)throw std::runtime_error("Export pixel conversion unavailable.");
        sws_setColorspaceDetails(w.scale,sws_getCoefficients(SWS_CS_ITU709),1,sws_getCoefficients(SWS_CS_ITU709),lossless?1:0,0,1<<16,1<<16);
        Renderer renderer(sequence,&cancel);
        const int64_t frameCount=(int64_t)std::llround(sequence.duration()/sequence.frameSeconds());
        const int64_t sampleCount=(int64_t)std::llround(frameCount*sequence.frameSeconds()*48000);
        int64_t frame=0,sample=0;std::vector<float> sound((size_t)block*2);
        while(frame<frameCount || sample<sampleCount)
        {
            if(cancel.load())throw std::runtime_error("Export cancelled.");
            if(frame<frameCount && (sample>=sampleCount || frame*sequence.frameSeconds()<=(double)sample/48000))
            {
                const auto image=renderer.video(frame*sequence.frameSeconds(),width,height);
                juce::Image::BitmapData data(image,juce::Image::BitmapData::readOnly);
                checked(av_frame_make_writable(w.vf),"Prepare video frame");
                const uint8_t* src[]{data.data,nullptr,nullptr,nullptr};int strides[]{data.lineStride,0,0,0};
                checked(sws_scale(w.scale,src,strides,0,height,w.vf->data,w.vf->linesize),"Convert output video");
                w.vf->pts=frame++;w.send(v,w.vs,w.vf);
                progress((double)frame/frameCount);
            }
            else
            {
                const int count=(int)std::min<int64_t>(block,sampleCount-sample);
                renderer.audio((double)sample/48000,count,48000,sound.data());
                checked(av_frame_make_writable(w.af),"Prepare audio frame");w.af->nb_samples=count;w.af->pts=sample;
                const uint8_t* src[]{(const uint8_t*)sound.data()};
                checked(swr_convert(w.resample,w.af->data,count,src,count),"Convert output audio");w.send(a,w.as,w.af);sample+=count;
            }
        }
        w.send(v,w.vs,nullptr);w.send(a,w.as,nullptr);checked(av_write_trailer(w.format),"Finish export container");
        checked(avio_closep(&w.format->pb),"Close export container");
    }
    if(cancel.load())throw std::runtime_error("Export cancelled.");
    if(options.destination.exists() && !options.overwrite)throw std::runtime_error("Destination appeared during export; existing file preserved.");
    if(!temporary.overwriteTargetFileWithTemporary())throw std::runtime_error("Could not install completed export at its destination.");
    progress(1);
}
}
