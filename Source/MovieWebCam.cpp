#include "MovieWebCam.h"
#include "MovieCaptureMonitor.h"
#include <rtc/rtc.hpp>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}
#include <condition_variable>
#include <deque>
#include <map>
#include <thread>

namespace movie {
namespace {
using Bytes=std::vector<unsigned char>;
bool privateV4(const std::string& ip){const auto a=juce::StringArray::fromTokens(juce::String(ip),".","");if(a.size()!=4)return false;for(const auto& x:a)if(x.isEmpty()||!x.containsOnly("0123456789")||x.getIntValue()>255)return false;return a[0]=="10"||(a[0]=="192"&&a[1]=="168")||(a[0]=="172"&&a[1].getIntValue()>=16&&a[1].getIntValue()<=31);}
juce::var object(std::initializer_list<std::pair<juce::Identifier,juce::var>> fields){auto* o=new juce::DynamicObject;for(const auto& f:fields)o->setProperty(f.first,f.second);return o;}
uint32_t be32(const unsigned char* p){return uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3];}
Bytes randomBytes(int n){Bytes b(n);if(RAND_bytes(b.data(),n)!=1)throw std::runtime_error("Secure random unavailable");return b;}
juce::String b64(const Bytes& b){return juce::Base64::toBase64(b.data(),b.size()).replaceCharacter('+','-').replaceCharacter('/','_').removeCharacters("=");}
Bytes unb64(juce::String s){s=s.replaceCharacter('-','+').replaceCharacter('_','/');while(s.length()%4)s+='=';juce::MemoryOutputStream out;if(!juce::Base64::convertFromBase64(out,s))throw std::runtime_error("Invalid envelope");auto* p=(const unsigned char*)out.getData();return Bytes(p,p+out.getDataSize());}
Bytes hmac(const Bytes& key,const Bytes& data){Bytes result(32);unsigned int length=0;if(!HMAC(EVP_sha256(),key.data(),(int)key.size(),data.data(),data.size(),result.data(),&length)||length!=32)throw std::runtime_error("HMAC unavailable");return result;}
Bytes bytes(const juce::String& s){const auto* p=(const unsigned char*)s.toRawUTF8();return Bytes(p,p+s.getNumBytesAsUTF8());}
Bytes keyFor(const Bytes& secret,const juce::String& session,const char* direction){auto extracted=hmac(bytes(session),secret);auto info=bytes(juce::String("djehuti-webcam-feasibility-v1:")+direction);info.push_back(1);return hmac(extracted,info);}
Bytes crypt(bool encrypt,const Bytes& key,const Bytes& nonce,const Bytes& input,const juce::String& session){
    if(nonce.size()!=12 || (!encrypt && input.size()<16))throw std::runtime_error("Invalid envelope");
    std::unique_ptr<EVP_CIPHER_CTX,decltype(&EVP_CIPHER_CTX_free)> c(EVP_CIPHER_CTX_new(),EVP_CIPHER_CTX_free);
    if(!c)throw std::runtime_error("Cipher unavailable");int n=0,total=0;const auto aad=bytes(session);Bytes out(input.size()+32);
    if(EVP_CipherInit_ex(c.get(),EVP_aes_256_gcm(),nullptr,key.data(),nonce.data(),encrypt?1:0)!=1 || EVP_CipherUpdate(c.get(),nullptr,&n,aad.data(),(int)aad.size())!=1)throw std::runtime_error("Cipher init failed");
    const int size=(int)input.size()-(encrypt?0:16);
    if(!encrypt && EVP_CIPHER_CTX_ctrl(c.get(),EVP_CTRL_GCM_SET_TAG,16,(void*)(input.data()+size))!=1)throw std::runtime_error("Invalid authentication tag");
    if(EVP_CipherUpdate(c.get(),out.data(),&n,input.data(),size)!=1)throw std::runtime_error("Cipher failed");total=n;
    if(EVP_CipherFinal_ex(c.get(),out.data()+total,&n)!=1)throw std::runtime_error("Invitation authentication failed");total+=n;
    if(encrypt){if(EVP_CIPHER_CTX_ctrl(c.get(),EVP_CTRL_GCM_GET_TAG,16,out.data()+total)!=1)throw std::runtime_error("Cipher tag failed");total+=16;}out.resize(total);return out;
}
struct ReceiverReports:rtc::MediaHandler {
    std::function<void(const unsigned char*,size_t)> receive;
    void incoming(rtc::message_vector& messages,const rtc::message_callback&)override {
        for(const auto& m:messages)if(m->size()>=8)receive((const unsigned char*)m->data(),m->size());
    }
};
}
struct WebCamCapture::Impl : std::enable_shared_from_this<WebCamCapture::Impl> {
    mutable std::mutex mutex;std::mutex queueMutex;std::condition_variable wake;std::atomic<bool> quitting{false},queueOverflow{false};
    std::thread worker;std::unique_ptr<rtc::WebSocketServer> server;std::shared_ptr<rtc::PeerConnection> peer;
    std::vector<std::shared_ptr<rtc::WebSocket>> sockets;std::vector<std::shared_ptr<rtc::Track>> tracks;std::shared_ptr<rtc::DataChannel> channel;
    std::vector<VisualBinding> bindings;std::vector<RecordingSubscription> subscriptions;
    CaptureMonitor monitor;std::atomic<double> outputRate{48000};double monitorAt=0,monitorRate=0;
    juce::String session,state{"stopped"},error,address;Bytes secret;juce::int64 expires=0,lastProbe=0;
    bool consumed=false,failed=false,localTransport=false;int clockSamples=0;double phoneToHost=0,wallOffset=0,bestRtt=1e30;juce::int64 uncertainty=INT64_MAX;
    juce::String queueGeneration;
    struct Packet {int stream;Bytes data;juce::int64 arrival;juce::String generation;};std::deque<Packet> incoming;std::atomic<size_t> queued{0};
    struct Stream {
        AVCodecContext* decoder=nullptr;SwsContext* scale=nullptr;SwrContext* resample=nullptr;
        VisualPreview preview;uint32_t lastRtp=0,srRtp=0,ssrc=0,srSsrc=0;int64_t extended=0;double srWall=0;bool anchored=false,haveRtp=false;
        std::map<uint16_t,Bytes> fragments;uint32_t frameTimestamp=0;int first=-1,last=-1;size_t fragmentBytes=0;
        juce::int64 lastArrival=0,sequence=0,audioEnd=-1;int payload=-1,rotationExtension=-1,rotation=0;
        void clear(){avcodec_free_context(&decoder);sws_freeContext(scale);scale=nullptr;swr_free(&resample);fragments.clear();fragmentBytes=0;first=last=-1;anchored=haveRtp=false;preview={};sequence=lastArrival=extended=0;audioEnd=-1;payload=rotationExtension=-1;rotation=0;ssrc=srSsrc=0;lastRtp=srRtp=frameTimestamp=0;srWall=0;}
        ~Stream(){clear();}
    } streams[2];
    Impl(){worker=std::thread([this]{run();});}
    ~Impl(){stop();quitting=true;wake.notify_all();worker.join();}
    void stop(){
        monitor.enabled=false;++monitor.generation;
        std::unique_ptr<rtc::WebSocketServer> oldServer;std::shared_ptr<rtc::PeerConnection> oldPeer;
        std::vector<std::shared_ptr<rtc::WebSocket>> oldSockets;
        {std::lock_guard lock(mutex);oldServer=std::move(server);oldPeer=std::move(peer);oldSockets.swap(sockets);channel.reset();tracks.clear();session={};secret.clear();state="stopped";for(auto& s:streams){s.preview.state="unavailable";s.preview.message="WebCam stopped; pair again to reconnect.";}}
        {std::lock_guard lock(queueMutex);incoming.clear();queued=0;queueGeneration={};queueOverflow=false;}
        if(oldServer)oldServer->stop();for(auto& s:oldSockets){s->resetCallbacks();s->forceClose();}if(oldPeer){oldPeer->resetCallbacks();oldPeer->close();}
    }
    bool enqueue(int stream,const unsigned char* p,size_t n,const juce::String& generation){
        std::lock_guard lock(queueMutex);if(n>65536||generation!=queueGeneration)return false;
        if(queued+n>8*1024*1024){queueOverflow=true;return false;}
        incoming.push_back({stream,Bytes(p,p+n),captureHostTime(),generation});queued+=n;wake.notify_one();
        return true;
    }
    juce::var invite(const juce::String& ip,int port){
        const auto a=juce::StringArray::fromTokens(ip,".","");if(a.size()!=4 || !((a[0]=="10") || (a[0]=="192"&&a[1]=="168") || (a[0]=="172"&&a[1].getIntValue()>=16&&a[1].getIntValue()<=31)))throw std::runtime_error("Choose a private LAN IPv4 address.");
        for(const auto& x:a)if(!x.containsOnly("0123456789")||x.getIntValue()>255)throw std::runtime_error("Invalid LAN address");
        if(port<1024||port>65535)throw std::runtime_error("Choose a port from 1024 to 65535");stop();
        std::lock_guard lock(mutex);for(auto& s:streams)s.clear();secret=randomBytes(32);session=juce::String::toHexString(randomBytes(16).data(),16,0);consumed=false;failed=false;clockSamples=0;bestRtt=1e30;uncertainty=INT64_MAX;expires=captureHostTime()+1200000000;address=ip;state="pairing";error={};
        {std::lock_guard lock(queueMutex);queueGeneration=session;}
        localTransport=false;
        rtc::WebSocketServer::Configuration config;config.bindAddress=ip.toStdString();config.port=(uint16_t)port;config.maxMessageSize=131072;config.connectionTimeout=std::chrono::seconds(10);
        server=std::make_unique<rtc::WebSocketServer>(config);
        server->onClient([this,lifetime=weak_from_this(),generation=session](std::shared_ptr<rtc::WebSocket> socket){
            auto alive=lifetime.lock();if(!alive)return;
            {std::lock_guard lock(mutex);std::erase_if(sockets,[](const auto& s){return s->isClosed();});if(generation!=session || consumed || captureHostTime()>expires || sockets.size()>=4){socket->close();return;}sockets.push_back(socket);}
            std::weak_ptr<rtc::WebSocket> weak=socket;
            socket->onMessage([this,lifetime,weak,generation](rtc::message_variant message){auto alive=lifetime.lock();if(!alive)return;if(auto socket=weak.lock())if(auto* text=std::get_if<std::string>(&message))authenticate(socket,*text,generation);});
        });
        return object({{"protocol","djehuti-webcam-feasibility-v1"},{"session",session},{"secret",b64(secret)},{"endpoint","ws://"+ip+":"+juce::String(server->port())+"/signal"}});
    }
    void authenticate(std::shared_ptr<rtc::WebSocket> socket,const std::string& text,const juce::String& generation){
        try{
            std::shared_ptr<rtc::PeerConnection> connection;Bytes localSecret;juce::String sdp;
            {
                std::lock_guard lock(mutex);if(socket->path().value_or("")!="/signal"||generation!=session||consumed||captureHostTime()>expires||text.size()>131072)throw std::runtime_error("Invitation unavailable");
                const auto envelope=juce::JSON::parse(juce::String(text));if(envelope["session"].toString()!=session)throw std::runtime_error("Wrong session");
                const auto plain=crypt(false,keyFor(secret,session,"offer"),unb64(envelope["nonce"]),unb64(envelope["ciphertext"]),session);
                const auto offer=juce::JSON::parse(juce::String::fromUTF8((const char*)plain.data(),(int)plain.size()));
                sdp=offer["sdp"].toString();if(offer["type"].toString()!="offer"||sdp.length()>96000||sdp.isEmpty())throw std::runtime_error("Invalid offer");
                consumed=true;state="negotiating";localSecret=secret;
                rtc::Configuration config;config.iceServers.clear();config.disableAutoNegotiation=true;config.bindAddress=address.toStdString();
                peer=connection=std::make_shared<rtc::PeerConnection>(config);
            }
            std::weak_ptr<rtc::PeerConnection> weak=connection;
            connection->onTrack([this,lifetime=weak_from_this(),generation](std::shared_ptr<rtc::Track> track){
                auto alive=lifetime.lock();if(!alive)return;
                auto description=track->description();const int stream=description.type()=="video"?0:description.type()=="audio"?1:-1;if(stream<0){track->close();return;}
                int payload=-1;for(int pt:description.payloadTypes()){const auto* format=description.rtpMap(pt);if(format&&juce::String(format->format).equalsIgnoreCase(stream==0?"VP8":"opus")){payload=pt;break;}}
                if(payload<0){track->close();return;}
                for(int pt:description.payloadTypes())if(pt!=payload)description.removeRtpMap(pt);
                track->setDescription(description);
                {std::lock_guard lock(mutex);if(generation!=session)return;streams[stream].payload=payload;streams[stream].rotationExtension=-1;for(int id:description.extIds())if(description.extMap(id)->uri=="urn:3gpp:video-orientation")streams[stream].rotationExtension=id;tracks.push_back(track);streams[stream].preview.state="starting";streams[stream].preview.message="Waiting for media and authenticated clock anchors.";}
                auto report=std::make_shared<ReceiverReports>();report->receive=[this,lifetime,stream,generation](const unsigned char* p,size_t n){auto alive=lifetime.lock();if(!alive)return;if(n>1&&p[1]>=192&&p[1]<=223)enqueue(stream,p,n,generation);};
                // Incoming handlers run tail-first. Observe SR anchors before the
                // receiving session consumes RTCP and emits receiver feedback.
                auto receiver=std::make_shared<rtc::RtcpReceivingSession>();receiver->addToChain(report);track->setMediaHandler(receiver);
                track->onMessage([this,lifetime,stream,generation](rtc::binary data){auto alive=lifetime.lock();if(!alive)return;enqueue(stream,(const unsigned char*)data.data(),data.size(),generation);},nullptr);
            });
            connection->onDataChannel([this,lifetime=weak_from_this(),generation](std::shared_ptr<rtc::DataChannel> c){
                auto alive=lifetime.lock();if(!alive)return;
                if(c->label()!="webcam-timing-v1"){c->close();return;}
                {std::lock_guard lock(mutex);if(generation!=session)return;channel=c;}
                c->onMessage([this,lifetime,generation](rtc::message_variant m){auto alive=lifetime.lock();if(!alive)return;if(auto* text=std::get_if<std::string>(&m))clockReply(*text,generation);});
            });
            connection->onStateChange([this,lifetime=weak_from_this(),generation](rtc::PeerConnection::State value){auto alive=lifetime.lock();if(!alive)return;std::lock_guard lock(mutex);if(generation!=session)return;if(value==rtc::PeerConnection::State::Connected)state="connected";if(value==rtc::PeerConnection::State::Disconnected||value==rtc::PeerConnection::State::Failed||value==rtc::PeerConnection::State::Closed){state="disconnected";for(auto& s:streams){s.preview.state="unavailable";s.preview.message="WebCam disconnected. Pair again with a fresh invitation.";}}});
            connection->onGatheringStateChange([weak,socket,generation,localSecret](rtc::PeerConnection::GatheringState value){
                if(value!=rtc::PeerConnection::GatheringState::Complete)return;
                try{auto c=weak.lock();if(!c)return;auto description=c->localDescription();if(!description)return;
                    auto answer=bytes(juce::JSON::toString(object({{"type","answer"},{"sdp",juce::String(std::string(*description))}})));auto nonce=randomBytes(12);
                    socket->send(juce::JSON::toString(object({{"session",generation},{"nonce",b64(nonce)},{"ciphertext",b64(crypt(true,keyFor(localSecret,generation,"answer"),nonce,answer,generation))}})).toStdString());
                }catch(...){socket->close();}
            });
            connection->setRemoteDescription(rtc::Description(sdp.toStdString(),"offer"));connection->setLocalDescription(rtc::Description::Type::Answer);
        }catch(...){socket->close();}
    }
    void clockReply(const std::string& text,const juce::String& generation,juce::int64 observed=0){
        if(text.size()>4096)return;const auto t3=observed?observed:captureHostTime();const auto j=juce::JSON::parse(juce::String(text));
        if(j["type"].toString()!="clock-reply"||j["generation"].toString()!=generation)return;
        const auto t0=j["hostSend100ns"].toString().getLargeIntValue();const auto p1=j["receiveNs"].toString().getLargeIntValue()/100;const auto p2=j["sendNs"].toString().getLargeIntValue()/100;
        const double roundtrip=double(t3-t0-(p2-p1));if(t0<=0||p2<p1||roundtrip<0||roundtrip>1000000)return;
        std::lock_guard lock(mutex);if(generation!=session||t0!=lastProbe)return;
        const double offset=(double(t0)+double(t3)-double(p1)-double(p2))/2;
        const double wall=double(j["wallMs"].toString().getLargeIntValue())*10000-double(p2);
        if(clockSamples&&std::abs(wall-wallOffset)>1000000){clockSamples=0;bestRtt=1e30;for(auto& s:streams)s.anchored=false;}
        if(roundtrip<=bestRtt*1.25){phoneToHost=clockSamples?phoneToHost*.9+offset*.1:offset;wallOffset=wall;uncertainty=(juce::int64)(roundtrip/2)+20000;bestRtt=std::min(bestRtt,roundtrip);++clockSamples;}
    }
    void decode(int which,const Bytes& data,uint32_t timestamp,juce::int64 arrival){
        auto& s=streams[which];if(!s.decoder){const auto* codec=avcodec_find_decoder(which==0?AV_CODEC_ID_VP8:AV_CODEC_ID_OPUS);if(!codec)throw std::runtime_error("VP8/Opus decoder unavailable");s.decoder=avcodec_alloc_context3(codec);s.decoder->thread_count=1;s.decoder->max_pixels=3840*2160;if(which==1){s.decoder->sample_rate=48000;av_channel_layout_default(&s.decoder->ch_layout,2);}if(avcodec_open2(s.decoder,codec,nullptr)<0)throw std::runtime_error("Cannot open WebCam decoder");}
        if(!s.haveRtp){s.lastRtp=timestamp;s.extended=timestamp;s.haveRtp=true;}else{s.extended+=(int32_t)(timestamp-s.lastRtp);s.lastRtp=timestamp;}
        const int rate=which==0?90000:48000;const double wall=s.srWall+double((int32_t)(timestamp-s.srRtp))*10000000/rate;
        const auto host=(juce::int64)(wall-wallOffset+phoneToHost);
        const bool timing=state=="connected"&&localTransport&&s.anchored&&clockSamples>=3&&uncertainty<500000&&std::abs(host-arrival)<10000000;
        AVPacket* packet=av_packet_alloc();std::unique_ptr<AVFrame,void(*)(AVFrame*)> frame(av_frame_alloc(),[](AVFrame* f){av_frame_free(&f);});if(!packet||!frame){av_packet_free(&packet);throw std::bad_alloc();}
        if(av_new_packet(packet,(int)data.size())<0){av_packet_free(&packet);throw std::bad_alloc();}std::memcpy(packet->data,data.data(),data.size());
        const int result=avcodec_send_packet(s.decoder,packet);av_packet_free(&packet);if(result<0){++s.preview.discontinuities;return;}
        while(avcodec_receive_frame(s.decoder,frame.get())>=0){
            auto sample=std::make_shared<CaptureSample>();sample->epoch=session;sample->clock="webcam-rtp-"+juce::String(rate)+":"+juce::String(s.ssrc);sample->sourceTime=s.extended*10000000/rate;sample->hostTime=host;sample->position=s.extended;sample->sequence=++s.sequence;sample->uncertainty100ns=uncertainty;
            if(which==0){if(frame->width<=0||frame->height<=0||frame->width>3840||frame->height>2160){throw std::runtime_error("WebCam dimensions exceed receiver limit");}sample->image=juce::Image(juce::Image::ARGB,frame->width,frame->height,true);{juce::Image::BitmapData bitmap(sample->image,juce::Image::BitmapData::writeOnly);uint8_t* dest[]={bitmap.data};int strides[]={bitmap.lineStride};s.scale=sws_getCachedContext(s.scale,frame->width,frame->height,(AVPixelFormat)frame->format,frame->width,frame->height,AV_PIX_FMT_BGRA,SWS_BILINEAR,nullptr,nullptr,nullptr);if(!s.scale)throw std::runtime_error("WebCam color conversion failed");sws_scale(s.scale,frame->data,frame->linesize,0,frame->height,dest,strides);}if(s.rotation){const auto original=sample->image;const int w=original.getWidth(),h=original.getHeight();sample->image=juce::Image(juce::Image::ARGB,s.rotation%2?h:w,s.rotation%2?w:h,true);juce::Graphics graphics(sample->image);const auto transform=s.rotation==1?juce::AffineTransform(0,-1,(float)h,1,0,0):s.rotation==2?juce::AffineTransform(-1,0,(float)w,0,-1,(float)h):juce::AffineTransform(0,1,0,-1,0,(float)w);graphics.drawImageTransformed(original,transform);}s.preview.image=sample->image;++s.preview.frameNumber;s.preview.format=object({{"width",sample->image.getWidth()},{"height",sample->image.getHeight()},{"fpsNum",30},{"fpsDen",1}});}
            else {AVChannelLayout stereo=AV_CHANNEL_LAYOUT_STEREO;if(!s.resample){if(swr_alloc_set_opts2(&s.resample,&stereo,AV_SAMPLE_FMT_FLT,48000,&frame->ch_layout,(AVSampleFormat)frame->format,frame->sample_rate,0,nullptr)<0||swr_init(s.resample)<0)throw std::runtime_error("WebCam audio conversion failed");}auto audio=std::make_shared<std::vector<float>>(8192*2);uint8_t* dest[]={(uint8_t*)audio->data()};const int n=swr_convert(s.resample,dest,8192,(const uint8_t**)frame->extended_data,frame->nb_samples);if(n<0)throw std::runtime_error("WebCam audio decode failed");audio->resize(n*2);for(auto& value:*audio)if(!std::isfinite(value))value=0;sample->audio=audio;sample->frames=n;sample->rate=48000;sample->channels=2;s.preview.sampleCount+=n;s.preview.samplePosition=s.extended;s.preview.format=object({{"sampleRate",48000},{"channels",2}});}
            s.lastArrival=arrival;s.preview.timestamp100ns=timing?host:0;s.preview.state=timing?"live":"starting";s.preview.message=timing?"Authenticated WebCam / RTP and clock probes mapped to Movie time.":"Preview available; waiting for sender reports and clock synchronization.";
            int count=0;const juce::String id=which==0?"webcam:video":"webcam:audio";for(const auto& b:bindings)if(b.active&&b.sourceId==id)++count;for(const auto& r:subscriptions)if(r.sourceId==id)++count;s.preview.subscribers=count;
            if(timing){
                if(which==1){
                    if(s.audioEnd>=0 && sample->position<s.audioEnd){++s.preview.discontinuities;continue;}
                    const auto gap=s.audioEnd<0?0:sample->position-s.audioEnd;
                    if(gap>96000)throw std::runtime_error("WebCam audio gap exceeds two seconds; retained recording prefix.");
                    if(gap>0){auto missing=std::make_shared<CaptureSample>(*sample);missing->position=s.audioEnd;missing->sourceTime=s.audioEnd*10000000/48000;missing->hostTime=sample->hostTime-gap*10000000/48000;missing->frames=(int)gap;missing->audio=std::make_shared<const std::vector<float>>((size_t)gap*2,0.0f);missing->discontinuity=true;publishCapture(subscriptions,id,missing);sample->discontinuity=true;++s.preview.discontinuities;}
                    s.audioEnd=sample->position+sample->frames;
                }
                publishCapture(subscriptions,id,sample);
            }
            if(which==1&&sample->audio){
                s.preview.peak=0;for(float value:*sample->audio)if(std::isfinite(value))s.preview.peak=std::max(s.preview.peak,std::abs(value));
                if(monitor.enabled){const auto rate=std::max(8000.0,outputRate.load());if(monitorRate!=rate){monitorAt=0;monitorRate=rate;++monitor.generation;}
                    while(monitorAt<sample->frames){const int frame=(int)monitorAt,next=std::min(frame+1,sample->frames-1);const float alpha=(float)(monitorAt-frame);auto value=[&](int channel){const float a=(*sample->audio)[frame*2+channel],b=(*sample->audio)[next*2+channel];return juce::jlimit(-1.0f,1.0f,a+(b-a)*alpha);};monitor.write(value(0),value(1));monitorAt+=48000/rate;}monitorAt-=sample->frames;
                }
            }
        }
        
    }
    void packet(Packet& packet){
        auto& s=streams[packet.stream];const auto* p=packet.data.data();size_t n=packet.data.size();if(n<12||(p[0]>>6)!=2)return;
        if(p[1]>=192&&p[1]<=223){size_t off=0;while(off+4<=n){const size_t length=4*((size_t(p[off+2])<<8|p[off+3])+1);if(length<4||off+length>n)break;if(p[off+1]==200&&length>=28&&(!s.ssrc||be32(p+off+4)==s.ssrc)){s.srSsrc=be32(p+off+4);const uint64_t sec=be32(p+off+8),fraction=be32(p+off+12);s.srWall=(double(sec)-2208988800.0)*10000000+double(fraction)*10000000/4294967296.0;s.srRtp=be32(p+off+16);s.anchored=true;}off+=length;}return;}
        if((p[1]&127)!=s.payload)return;const auto packetSsrc=be32(p+8);if(s.ssrc&&s.ssrc!=packetSsrc)throw std::runtime_error("WebCam source SSRC changed; pair again for a fresh generation.");s.ssrc=packetSsrc;if(s.anchored&&s.srSsrc!=s.ssrc)s.anchored=false;size_t off=12+4*(p[0]&15);if(off>n)return;if(p[0]&16){if(off+4>n)return;const size_t end=off+4+4*((size_t(p[off+2])<<8)|p[off+3]);if(end>n)return;if(p[off]==0xbe&&p[off+1]==0xde){size_t at=off+4;while(at<end){const auto descriptor=p[at++];if(!descriptor)continue;const int id=descriptor>>4;if(id==15)break;const size_t length=(descriptor&15)+1;if(at+length>end)return;if(id==s.rotationExtension&&length==1)s.rotation=p[at]&3;at+=length;}}off=end;}if(p[0]&32){const int padding=p[n-1];if(!padding||padding>n-off)return;n-=padding;}if(off>=n)return;
        const uint32_t timestamp=be32(p+4);if(packet.stream==1){decode(1,Bytes(p+off,p+n),timestamp,packet.arrival);return;}
        const uint16_t sequence=(uint16_t(p[2])<<8)|p[3];const bool start=(p[off]&0x10)&&!(p[off]&15);const bool extension=p[off++]&128;
        if(extension){if(off>=n)return;const auto x=p[off++];if(x&128){if(off>=n)return;if(p[off++]&128){if(off>=n)return;++off;}}if(x&64)++off;if(x&48)++off;}if(off>=n)return;
        if(timestamp!=s.frameTimestamp){if(!s.fragments.empty())++s.preview.discontinuities;s.fragments.clear();s.fragmentBytes=0;s.first=s.last=-1;s.frameTimestamp=timestamp;}
        if(s.fragments.size()>=256||s.fragmentBytes+n-off>2*1024*1024){s.fragments.clear();s.fragmentBytes=0;s.first=s.last=-1;++s.preview.discontinuities;return;}
        if(start)s.first=sequence;if(p[1]&128)s.last=sequence;if(s.fragments.emplace(sequence,Bytes(p+off,p+n)).second)s.fragmentBytes+=n-off;
        if(s.first>=0&&s.last>=0){const unsigned count=(uint16_t)(s.last-s.first)+1;if(count>256)return;Bytes frame;for(unsigned i=0;i<count;++i){const auto it=s.fragments.find((uint16_t)(s.first+i));if(it==s.fragments.end())return;frame.insert(frame.end(),it->second.begin(),it->second.end());}decode(0,frame,timestamp,packet.arrival);s.fragments.clear();s.fragmentBytes=0;s.first=s.last=-1;}
    }
    void run(){
        while(!quitting){std::deque<Packet> pending;{std::unique_lock lock(queueMutex);wake.wait_for(lock,std::chrono::milliseconds(10));pending.swap(incoming);queued=0;}if(quitting)break;
            try{
                {std::lock_guard lock(mutex);
                if(queueOverflow.exchange(false))throw std::runtime_error("WebCam receive queue overflow; retained recording prefix.");
                if(peer&&state=="connected"&&!localTransport){rtc::Candidate local,remote;if(peer->getSelectedCandidatePair(&local,&remote)){if(local.type()!=rtc::Candidate::Type::Host||remote.type()!=rtc::Candidate::Type::Host||!privateV4(local.address().value_or(""))||!privateV4(remote.address().value_or("")))throw std::runtime_error("WebCam requires a direct private IPv4 host candidate pair.");localTransport=true;}}
                if(channel&&channel->isOpen()&&captureHostTime()-lastProbe>5000000){lastProbe=captureHostTime();channel->send(juce::JSON::toString(object({{"type","clock-probe"},{"id",juce::String(lastProbe)},{"hostSend100ns",juce::String(lastProbe)}})).toStdString());}
                }
                for(auto& p:pending){std::lock_guard lock(mutex);if(!failed&&p.generation==session)packet(p);}
                std::lock_guard lock(mutex);
                for(auto& s:streams)if(s.lastArrival&&captureHostTime()-s.lastArrival>20000000){s.preview.state="unavailable";s.preview.message="No recent WebCam samples; reconnect with a fresh invitation.";}
            }catch(const std::exception& e){std::lock_guard lock(mutex);failed=true;state="failed";error=e.what();monitor.enabled=false;++monitor.generation;for(auto& s:streams){s.preview.state="failed";s.preview.message=error;}}
        }
    }
};
WebCamCapture::WebCamCapture():impl(std::make_shared<Impl>()){}
WebCamCapture::~WebCamCapture(){impl->stop();}
juce::var WebCamCapture::invite(const juce::String& ip,int port){return impl->invite(ip,port);}
void WebCamCapture::stop(){impl->stop();}
std::vector<VisualSource> WebCamCapture::sources()const{std::lock_guard lock(impl->mutex);std::vector<VisualSource> list;for(int i=0;i<2;++i){const auto& s=impl->streams[i];if(s.sequence){VisualSource v;v.id=i==0?"webcam:video":"webcam:audio";v.kind=i==0?"camera":"microphone";v.name=i==0?"Djehuti WebCam video":"Djehuti WebCam microphone";v.width=s.preview.image.getWidth();v.height=s.preview.image.getHeight();v.formats.add(s.preview.format);list.push_back(v);}}return list;}
void WebCamCapture::setBindings(std::vector<VisualBinding> b,std::vector<RecordingSubscription> r){std::lock_guard lock(impl->mutex);const bool audible=std::any_of(b.begin(),b.end(),[](const auto& binding){return binding.active&&binding.monitoring&&binding.sourceId=="webcam:audio";});if(impl->monitor.enabled.exchange(audible)!=audible){++impl->monitor.generation;impl->monitorAt=0;}impl->bindings=std::move(b);impl->subscriptions=std::move(r);}
VisualPreview WebCamCapture::preview(const juce::String& id)const{std::lock_guard lock(impl->mutex);return impl->streams[id=="webcam:audio"?1:0].preview;}
juce::var WebCamCapture::status()const{std::lock_guard lock(impl->mutex);return object({{"state",impl->state},{"error",impl->error},{"localTransportVerified",impl->localTransport},{"clockSamples",impl->clockSamples},{"uncertainty100ns",impl->uncertainty},{"queuedBytes",(juce::int64)impl->queued.load()},{"monitorRenderedFrames",impl->monitor.rendered.load()},{"monitorDroppedFrames",impl->monitor.dropped.load()},{"videoClockAnchor",impl->streams[0].anchored},{"audioClockAnchor",impl->streams[1].anchored},{"videoFrames",impl->streams[0].sequence},{"audioFrames",impl->streams[1].sequence}});}
void WebCamCapture::renderAudio(float*const* output,int channels,int frames,double rate){impl->outputRate=rate;impl->monitor.render(output,channels,frames);}
#if MOVIE_WEBCAM_TESTS
void WebCamCapture::verifyTimingBoundaries(){
    auto require=[](bool condition,const char* message){if(!condition)throw std::runtime_error(message);};
    Impl clock;clock.session="test-generation";
    const juce::int64 t0=100000000000LL,p1=20000000000000LL,p2=p1+2000000;
    clock.lastProbe=t0;
    auto reply=[&](juce::int64 receive,juce::int64 send,juce::int64 wall,juce::int64 original){return juce::JSON::toString(object({{"type","clock-reply"},{"generation","test-generation"},{"hostSend100ns",juce::String(original)},{"receiveNs",juce::String(receive)},{"sendNs",juce::String(send)},{"wallMs",juce::String(wall)}})).toStdString();};
    clock.clockReply(reply(p1,p2,1790000000000LL,t0),"test-generation",t0+100000);
    require(clock.clockSamples==1&&clock.uncertainty==60000,"Clock probe processing-time compensation failed");
    clock.clockReply(reply(p1,p2,1790000000000LL,t0),"wrong-generation",t0+100000);
    require(clock.clockSamples==1,"Old clock generation accepted");
    clock.clockReply(reply(p1,p2,1790000000000LL,t0-1),"test-generation",t0+100000);
    require(clock.clockSamples==1,"Stale clock probe accepted");
    clock.clockReply(reply(p1,p2,1790000000000LL,t0),"test-generation",t0+2000000);
    require(clock.clockSamples==1,"High-delay outlier accepted");
    clock.clockReply(reply(p2,p1,1790000000000LL,t0),"test-generation",t0+100000);
    require(clock.clockSamples==1,"Backward phone processing clock accepted");
    clock.streams[0].anchored=clock.streams[1].anchored=true;
    clock.clockReply(reply(p1,p2,1790000000200LL,t0),"test-generation",t0+100000);
    require(clock.clockSamples==1&&!clock.streams[0].anchored&&!clock.streams[1].anchored,"Wall-clock step did not invalidate anchors");
    {std::lock_guard lock(clock.mutex);
        clock.decode(1,Bytes{0xf8,0xff,0xfe},0xfffffff0,captureHostTime());
        const auto first=clock.streams[1].extended;
        clock.decode(1,Bytes{0xf8,0xff,0xfe},944,captureHostTime());
        require(clock.streams[1].extended-first==960,"RTP timestamp wrap lost audio sample progression");
        require(clock.streams[1].preview.state!="live","Missing source anchor incorrectly became recording-ready");
        {std::lock_guard queueLock(clock.queueMutex);clock.queueGeneration="test-generation";}
        const auto packet=Bytes(65536,0);
        require(!clock.enqueue(1,packet.data(),packet.size(),"old-generation"),"Old receive generation accepted");
        for(int i=0;i<300;++i)clock.enqueue(1,packet.data(),packet.size(),"test-generation");
        require(clock.queueOverflow&&clock.queued<=8*1024*1024,"Receive queue is not bounded/observable");
    }
}
#endif
}
