#include "MovieCaptureMonitor.h"
#include "MovieDeviceCapture.h"
#include <atomic>
#include <array>
#include <map>
#include <mutex>
#include <thread>
#include <cmath>
#if JUCE_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wrl/client.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include <ksmedia.h>
#endif
namespace movie
{
namespace
{
juce::var values(std::initializer_list<std::pair<const char*,juce::var>> fields)
{auto* o=new juce::DynamicObject;for(const auto& f:fields)o->setProperty(f.first,f.second);return juce::var(o);}
}
bool captureFormatMatches(const juce::var& available,const juce::var& requested)
{
    if(requested.isVoid())return true;
    if(!requested.isObject())return false;
    for(const auto& p:requested.getDynamicObject()->getProperties())
    {
        if(p.name==juce::Identifier("channel"))continue; // Historical routing preference, not acquisition format.
        if(!available.hasProperty(p.name) || available[p.name]!=p.value)return false;
    }
    return true;
}
struct DeviceCapture::Impl
{
    mutable std::mutex mutex;
    std::vector<VisualSource> catalog;
    std::vector<VisualBinding> bindings;
    std::vector<RecordingSubscription> recordings;
    std::map<juce::String,VisualPreview> snapshots;
    juce::String problem;
    int acquisitions=0;bool discovered=false;
    std::atomic<bool> stopping{false},rescan{true};
    std::thread worker;
    // Fixed slots: one producer (native worker), one consumer (existing output callback).
    // Generation tags invalidate queued sound immediately after mute/reassignment.
    using MonitorPipe = CaptureMonitor;
    std::array<MonitorPipe,16> pipes;
    std::atomic<double> outputRate{48000};
#if JUCE_WINDOWS
    template<class T> using Com=Microsoft::WRL::ComPtr<T>;
    static void check(HRESULT hr,const char* operation)
    {if(FAILED(hr))throw std::runtime_error((juce::String(operation)+" (0x"+juce::String::toHexString((int)hr)+"). Check Windows privacy permissions, device availability and format.").toStdString());}
    static juce::String guidText(const GUID& guid)
    {wchar_t text[48]{};StringFromGUID2(guid,text,48);return juce::String(text);}
    static juce::String attribute(IMFAttributes* attributes,const GUID& key)
    {wchar_t* text=nullptr;UINT32 length=0;check(attributes->GetAllocatedString(key,&text,&length),"Read camera identity");juce::String result(text);CoTaskMemFree(text);return result;}
    static WAVEFORMATEXTENSIBLE audioFormat(int rate,int channels)
    {
        WAVEFORMATEXTENSIBLE f{};f.Format.wFormatTag=WAVE_FORMAT_EXTENSIBLE;f.Format.nChannels=(WORD)channels;
        f.Format.nSamplesPerSec=(DWORD)rate;f.Format.wBitsPerSample=32;f.Format.nBlockAlign=(WORD)(channels*4);
        f.Format.nAvgBytesPerSec=(DWORD)(rate*channels*4);f.Format.cbSize=22;f.Samples.wValidBitsPerSample=32;
        f.dwChannelMask=channels==1?SPEAKER_FRONT_CENTER:channels==2?SPEAKER_FRONT_LEFT|SPEAKER_FRONT_RIGHT:0;
        f.SubFormat=KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;return f;
    }
    struct Target
    {
        VisualSource source;
        Com<IMFActivate> camera;
        Com<IMMDevice> microphone;
        std::vector<Com<IMFMediaType>> cameraTypes;
    };
    std::map<juce::String,Target> targets;
    void discover(bool retry)
    {
        std::map<juce::String,Target> found;juce::String errors;
        try
        {
            Com<IMFAttributes> attributes;check(MFCreateAttributes(&attributes,1),"Camera attributes");
            check(attributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID),"Camera enumeration");
            IMFActivate** devices=nullptr;UINT32 count=0;check(MFEnumDeviceSources(attributes.Get(),&devices,&count),"Enumerate cameras");
            for(UINT32 i=0;i<count;++i)
            {
                Target t;t.camera.Attach(devices[i]);
                try
                {
                    t.source.kind="camera";t.source.id="camera:"+attribute(t.camera.Get(),MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK);
                    t.source.name=attribute(t.camera.Get(),MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME);
                    const auto old=targets.find(t.source.id);
                    if(old!=targets.end() && (!old->second.source.formats.isEmpty() || !retry))
                    {found[t.source.id]=old->second;continue;}
                    // Enumerating native types opens only the source's presentation descriptor, never a second streaming session.
                    Com<IMFMediaSource> source;check(t.camera->ActivateObject(IID_PPV_ARGS(&source)),"Open camera capabilities");
                    Com<IMFPresentationDescriptor> pd;check(source->CreatePresentationDescriptor(&pd),"Camera descriptor");
                    DWORD streams=0;pd->GetStreamDescriptorCount(&streams);
                    for(DWORD stream=0;stream<streams;++stream)
                    {
                        BOOL selected=FALSE;Com<IMFStreamDescriptor> sd;Com<IMFMediaTypeHandler> handler;GUID major{};
                        if(FAILED(pd->GetStreamDescriptorByIndex(stream,&selected,&sd)) || FAILED(sd->GetMediaTypeHandler(&handler)) || FAILED(handler->GetMajorType(&major)) || major!=MFMediaType_Video)continue;
                        DWORD n=0;handler->GetMediaTypeCount(&n);
                        for(DWORD k=0;k<n;++k)
                        {
                            Com<IMFMediaType> type;if(FAILED(handler->GetMediaTypeByIndex(k,&type)))continue;
                            UINT32 w=0,h=0,num=0,den=0;GUID sub{};
                            if(FAILED(MFGetAttributeSize(type.Get(),MF_MT_FRAME_SIZE,&w,&h)) || FAILED(MFGetAttributeRatio(type.Get(),MF_MT_FRAME_RATE,&num,&den)) || FAILED(type->GetGUID(MF_MT_SUBTYPE,&sub)) || !w || !h || !num || !den)continue;
                            // Bound preview memory. Other native formats are not offered as supported preview configurations.
                            if(w>8192 || h>8192 || num/(double)den>240)continue;
                            UINT32 a=num,b=den;while(b){const auto r=a%b;a=b;b=r;}num/=a;den/=a;
                            auto f=values({{"width",(int)w},{"height",(int)h},{"fpsNum",(int)num},{"fpsDen",(int)den},{"format",guidText(sub)}});
                            if(std::none_of(t.source.formats.begin(),t.source.formats.end(),[&](const juce::var& old){return juce::JSON::toString(old)==juce::JSON::toString(f);}))
                            {t.source.formats.add(f);t.cameraTypes.push_back(type);}
                        }
                        break;
                    }
                    source->Shutdown();t.camera->ShutdownObject();
                    found[t.source.id]=std::move(t);
                }
                catch(const std::exception& e)
                {errors+=juce::String(e.what())+" ";if(t.source.id.isNotEmpty())if(t.camera)t.camera->ShutdownObject();found[t.source.id]=std::move(t);}
            }
            CoTaskMemFree(devices);
        }catch(const std::exception& e){errors+=e.what();}
        try
        {
            Com<IMMDeviceEnumerator> enumerator;check(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,IID_PPV_ARGS(&enumerator)),"Audio enumerator");
            Com<IMMDeviceCollection> devices;check(enumerator->EnumAudioEndpoints(eCapture,DEVICE_STATE_ACTIVE,&devices),"Enumerate microphones");UINT count=0;devices->GetCount(&count);
            for(UINT i=0;i<count;++i)
            {
                Target t;check(devices->Item(i,&t.microphone),"Microphone endpoint");LPWSTR id=nullptr;check(t.microphone->GetId(&id),"Microphone identity");t.source.id="microphone:"+juce::String(id);CoTaskMemFree(id);t.source.kind="microphone";
                Com<IPropertyStore> properties;if(SUCCEEDED(t.microphone->OpenPropertyStore(STGM_READ,&properties)))
                {PROPVARIANT v;PropVariantInit(&v);if(SUCCEEDED(properties->GetValue(PKEY_Device_FriendlyName,&v)) && v.vt==VT_LPWSTR)t.source.name=v.pwszVal;PropVariantClear(&v);}
                if(t.source.name.isEmpty())t.source.name="Audio input "+juce::String((int)i+1);
                Com<IAudioClient> client;
                if(SUCCEEDED(t.microphone->Activate(__uuidof(IAudioClient),CLSCTX_ALL,nullptr,(void**)client.GetAddressOf())))
                {
                    WAVEFORMATEX* mix=nullptr;int mixChannels=2,mixRate=48000;
                    if(SUCCEEDED(client->GetMixFormat(&mix))){mixChannels=mix->nChannels;mixRate=(int)mix->nSamplesPerSec;CoTaskMemFree(mix);}
                    std::vector<int> rates{mixRate,44100,48000,96000,192000,32000,16000,8000};
                    for(const auto rate:rates)for(const auto channels:{mixChannels,1,2})
                    {
                        if(channels<1 || channels>8)continue;auto f=audioFormat(rate,channels);WAVEFORMATEX* closest=nullptr;
                        const auto hr=client->IsFormatSupported(AUDCLNT_SHAREMODE_SHARED,&f.Format,&closest);if(closest)CoTaskMemFree(closest);
                        auto format=values({{"sampleRate",rate},{"channels",channels}});
                        if(hr==S_OK && std::none_of(t.source.formats.begin(),t.source.formats.end(),[&](const juce::var& old){return juce::JSON::toString(old)==juce::JSON::toString(format);}))t.source.formats.add(format);
                    }
                }
                found[t.source.id]=std::move(t);
            }
        }catch(const std::exception& e){errors+=e.what();}
        targets=std::move(found);std::lock_guard lock(mutex);catalog.clear();for(const auto& t:targets)catalog.push_back(t.second.source);problem=errors;discovered=true;
    }
    struct Pending
    {
        std::mutex mutex;Com<IMFSample> sample;HRESULT result=S_OK;DWORD flags=0;LONGLONG timestamp=0;bool ready=false;juce::int64 receivedAt=0;
    };
    class ReaderCallback final:public IMFSourceReaderCallback
    {
        std::atomic<ULONG> refs{1};std::shared_ptr<Pending> pending;
    public:
        explicit ReaderCallback(std::shared_ptr<Pending> p):pending(std::move(p)){}
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out)override
        {if(!out)return E_POINTER;*out=nullptr;if(id==__uuidof(IUnknown) || id==__uuidof(IMFSourceReaderCallback)){*out=static_cast<IMFSourceReaderCallback*>(this);AddRef();return S_OK;}return E_NOINTERFACE;}
        ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}
        ULONG STDMETHODCALLTYPE Release()override{auto n=--refs;if(!n)delete this;return n;}
        HRESULT STDMETHODCALLTYPE OnReadSample(HRESULT hr,DWORD,DWORD flags,LONGLONG time,IMFSample* sample)override
        {std::lock_guard lock(pending->mutex);pending->receivedAt=captureHostTime();pending->result=hr;pending->flags=flags;pending->timestamp=time;pending->sample=sample;pending->ready=true;return S_OK;}
        HRESULT STDMETHODCALLTYPE OnEvent(DWORD,IMFMediaEvent*)override{return S_OK;}
        HRESULT STDMETHODCALLTYPE OnFlush(DWORD)override{return S_OK;}
    };
    struct Session
    {
        VisualPreview latest;juce::var format;juce::String kind;
        Com<IMFMediaSource> camera;Com<IMFSourceReader> reader;std::shared_ptr<Pending> pending;
        Com<IAudioClient> audio;Com<IAudioCaptureClient> capture;
        MonitorPipe* pipe=nullptr;int channels=0,sampleRate=0;double resampleAt=0,monitorRate=0;
        juce::String epoch=juce::Uuid().toString();juce::int64 cameraOffset=INT64_MAX;
        LONG stride=0;int width=0,height=0;
        std::chrono::steady_clock::time_point lastActivity=std::chrono::steady_clock::now();
        ~Session(){if(pipe){pipe->enabled=false;++pipe->generation;}if(audio)audio->Stop();if(reader)reader->Flush(MF_SOURCE_READER_ALL_STREAMS);reader.Reset();if(camera)camera->Shutdown();}
        Session(const Target& target,int index,MonitorPipe* monitor):format(target.source.formats[index]),kind(target.source.kind),pipe(monitor)
        {
            try {
            latest.state="starting";latest.format=format;
            if(kind=="camera")
            {
                check(target.camera->ActivateObject(IID_PPV_ARGS(&camera)),"Activate camera");target.camera->DetachObject();
                Com<IMFAttributes> attributes;check(MFCreateAttributes(&attributes,3),"Reader attributes");pending=std::make_shared<Pending>();
                Com<ReaderCallback> callback;callback.Attach(new ReaderCallback(pending));check(attributes->SetUnknown(MF_SOURCE_READER_ASYNC_CALLBACK,callback.Get()),"Async camera callback");
                check(attributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING,TRUE),"Camera conversion");
                check(MFCreateSourceReaderFromMediaSource(camera.Get(),attributes.Get(),&reader),"Create camera reader");
                check(reader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS,FALSE),"Camera streams");
                check(reader->SetStreamSelection(MF_SOURCE_READER_FIRST_VIDEO_STREAM,TRUE),"Select video stream");
                Com<IMFSourceReaderEx> nativeReader;check(reader.As(&nativeReader),"Native camera format interface");DWORD changed=0;
                check(nativeReader->SetNativeMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM,target.cameraTypes[(size_t)index].Get(),&changed),"Select native camera format");
                Com<IMFMediaType> rgb;check(MFCreateMediaType(&rgb),"RGB preview format");rgb->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Video);rgb->SetGUID(MF_MT_SUBTYPE,MFVideoFormat_RGB32);
                MFSetAttributeSize(rgb.Get(),MF_MT_FRAME_SIZE,(UINT32)(int)format["width"],(UINT32)(int)format["height"]);MFSetAttributeRatio(rgb.Get(),MF_MT_FRAME_RATE,(UINT32)(int)format["fpsNum"],(UINT32)(int)format["fpsDen"]);
                check(reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,nullptr,rgb.Get()),"Convert camera to preview RGB32");
                Com<IMFMediaType> actual;check(reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,&actual),"Camera active format");
                UINT32 w=0,h=0;MFGetAttributeSize(actual.Get(),MF_MT_FRAME_SIZE,&w,&h);width=(int)w;height=(int)h;
                UINT32 actualNum=0,actualDen=0;check(MFGetAttributeRatio(actual.Get(),MF_MT_FRAME_RATE,&actualNum,&actualDen),"Camera active frame rate");
                if(width!=(int)format["width"] || height!=(int)format["height"] || (juce::int64)actualNum*(int)format["fpsDen"]!=(juce::int64)actualDen*(int)format["fpsNum"])throw std::runtime_error("Camera output does not match requested dimensions/frame rate.");
                UINT32 storedStride=0;if(SUCCEEDED(actual->GetUINT32(MF_MT_DEFAULT_STRIDE,&storedStride)))stride=(LONG)storedStride;else check(MFGetStrideForBitmapInfoHeader(MFVideoFormat_RGB32.Data1,width,&stride),"Camera stride");
                check(reader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM,0,nullptr,nullptr,nullptr,nullptr),"Start camera acquisition");
            }
            else
            {
                channels=(int)format["channels"];sampleRate=(int)format["sampleRate"];
                check(target.microphone->Activate(__uuidof(IAudioClient),CLSCTX_ALL,nullptr,(void**)audio.GetAddressOf()),"Activate microphone");
                // One second of bounded native buffering tolerates camera/driver
                // polling stalls; available packets are still drained immediately.
                auto f=audioFormat(sampleRate,channels);check(audio->Initialize(AUDCLNT_SHAREMODE_SHARED,0,10000000,0,&f.Format,nullptr),"Configure microphone");
                check(audio->GetService(IID_PPV_ARGS(&capture)),"Microphone capture service");check(audio->Start(),"Start microphone");
            }
            }catch(...){if(pipe){pipe->enabled=false;++pipe->generation;}if(audio)audio->Stop();if(reader)reader->Flush(MF_SOURCE_READER_ALL_STREAMS);reader.Reset();if(camera)camera->Shutdown();throw;}
        }
        void poll(double outputRate,bool monitoring,const std::vector<RecordingSubscription>& sinks,const juce::String& sourceId)
        {
            if(kind=="camera")
            {
                Com<IMFSample> sample;LONGLONG timestamp=0;DWORD flags=0;juce::int64 receivedAt=0;
                {std::lock_guard lock(pending->mutex);if(!pending->ready){if(std::chrono::steady_clock::now()-lastActivity>std::chrono::seconds(8))throw std::runtime_error("Camera delivered no frames for 8 seconds. Refresh to retry.");return;}
                 check(pending->result,"Acquire camera frame");sample=std::move(pending->sample);timestamp=pending->timestamp;receivedAt=pending->receivedAt;flags=pending->flags;pending->ready=false;}
                if(flags&(MF_SOURCE_READERF_ERROR|MF_SOURCE_READERF_ENDOFSTREAM|MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED))throw std::runtime_error("Camera stopped or changed its active format. Refresh to retry.");
                if(sample)
                {
                    Com<IMFMediaBuffer> buffer;check(sample->ConvertToContiguousBuffer(&buffer),"Camera buffer");Com<IMF2DBuffer> planar;BYTE* data=nullptr;LONG pitch=stride;DWORD length=0;bool twoD=SUCCEEDED(buffer.As(&planar)) && SUCCEEDED(planar->Lock2D(&data,&pitch));
                    if(!twoD){check(buffer->Lock(&data,nullptr,&length),"Read camera pixels");if(length<(DWORD)(std::abs(stride)*height)){buffer->Unlock();throw std::runtime_error("Camera pixel buffer is shorter than its format.");}if(stride<0)data+=(size_t)(height-1)*std::abs(stride);}
                    juce::Image image(juce::Image::ARGB,width,height,true,juce::SoftwareImageType{});
                    {juce::Image::BitmapData bitmap(image,juce::Image::BitmapData::writeOnly);for(int y=0;y<height;++y){auto* dest=bitmap.getLinePointer(y);std::memcpy(dest,data+(ptrdiff_t)y*pitch,(size_t)width*4);for(int x=0;x<width;++x)dest[x*4+3]=255;}}
                    if(twoD)planar->Unlock2D();else buffer->Unlock();latest.image=std::move(image);latest.timestamp100ns=timestamp;++latest.frameNumber;latest.state="live";lastActivity=std::chrono::steady_clock::now();
                    if(!sinks.empty())
                    {
                        auto frame=std::make_shared<CaptureSample>();frame->image=latest.image;frame->sourceTime=timestamp;frame->epoch=epoch;frame->sequence=latest.frameNumber;
                        UINT64 deviceTime=0;
                        if(SUCCEEDED(sample->GetUINT64(MFSampleExtension_DeviceTimestamp,&deviceTime)) && (juce::int64)deviceTime<=receivedAt+10000000 && receivedAt-(juce::int64)deviceTime<50000000)
                        {frame->hostTime=(juce::int64)deviceTime;frame->clock="device-qpc-100ns";frame->uncertainty100ns=10000;}
                        else
                        {cameraOffset=std::min(cameraOffset,receivedAt-timestamp);frame->hostTime=timestamp+cameraOffset;frame->clock="arrival-envelope-correlation";frame->uncertainty100ns=(juce::int64)(10000000.0*(int)format["fpsDen"]/(int)format["fpsNum"])+std::max<juce::int64>(0,receivedAt-frame->hostTime);}
                        publishCapture(sinks,sourceId,frame);
                    }
                }
                check(reader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM,0,nullptr,nullptr,nullptr,nullptr),"Request camera frame");
            }
            else
            {
                if(pipe && (pipe->enabled.exchange(monitoring)!=monitoring || monitorRate!=outputRate)){++pipe->generation;resampleAt=0;monitorRate=outputRate;}
                UINT32 packet=0;check(capture->GetNextPacketSize(&packet),"Microphone availability");float peak=0;bool received=false;
                int packets=0;while(packet && ++packets<=32)
                {
                    BYTE* bytes=nullptr;UINT32 frames=0;DWORD flags=0;UINT64 position=0,qpc=0;
                    check(capture->GetBuffer(&bytes,&frames,&flags,&position,&qpc),"Acquire microphone samples");
                    const auto* input=reinterpret_cast<const float*>(bytes);const bool silent=(flags&AUDCLNT_BUFFERFLAGS_SILENT)!=0;
                    for(UINT32 f=0;f<frames;++f)for(int c=0;c<channels;++c){const float v=silent?0:input[(size_t)f*channels+c];if(std::isfinite(v))peak=std::max(peak,std::abs(v));}
                    if(pipe && monitoring)
                    {
                        const double step=sampleRate/std::max(8000.0,outputRate);
                        while(resampleAt<frames)
                        {
                            const int f=(int)resampleAt,next=std::min(f+1,(int)frames-1);const float alpha=(float)(resampleAt-f);
                            auto value=[&](int c){if(silent)return 0.0f;const float a=input[(size_t)f*channels+c],b=input[(size_t)next*channels+c];const float v=a+(b-a)*alpha;return std::isfinite(v)?juce::jlimit(-1.0f,1.0f,v):0.0f;};
                            pipe->write(value(0),value(channels>1?1:0));resampleAt+=step;
                        }
                        resampleAt-=frames;
                    }
                    if(!sinks.empty())
                    {auto sample=std::make_shared<CaptureSample>();auto payload=std::make_shared<std::vector<float>>((size_t)frames*channels,0.0f);if(!silent)std::copy(input,input+payload->size(),payload->begin());
                     sample->audio=payload;sample->sourceTime=sample->hostTime=(juce::int64)qpc;sample->position=(juce::int64)position;sample->sequence=latest.sampleCount;sample->rate=sampleRate;sample->channels=channels;sample->frames=(int)frames;sample->epoch=epoch;sample->clock="wasapi-qpc-100ns";
                     sample->discontinuity=(flags&AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY)!=0;sample->invalidTimestamp=(flags&AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR)!=0;publishCapture(sinks,sourceId,sample);}
                    capture->ReleaseBuffer(frames);latest.sampleCount+=frames;latest.samplePosition=(juce::int64)position;latest.timestamp100ns=(juce::int64)qpc;latest.state="live";
                    if(flags&AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY)++latest.discontinuities;
                    if(flags&AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR)latest.message="Device reported an uncertain audio timestamp.";
                    received=true;lastActivity=std::chrono::steady_clock::now();check(capture->GetNextPacketSize(&packet),"Microphone packets");
                }
                if(received)latest.peak=peak;else if(std::chrono::steady_clock::now()-lastActivity>std::chrono::milliseconds(250))latest.peak=0;
                if(std::chrono::steady_clock::now()-lastActivity>std::chrono::seconds(3))throw std::runtime_error("Microphone delivered no samples for 3 seconds. Refresh to retry.");
            }
        }
    };
    void run()
    {
        const auto com=CoInitializeEx(nullptr,COINIT_MULTITHREADED);bool mf=false;
        try
        {
            check(com,"Capture COM initialization");check(MFStartup(MF_VERSION,MFSTARTUP_LITE),"Media Foundation initialization");mf=true;
            std::map<juce::String,std::unique_ptr<Session>> sessions;std::map<juce::String,std::pair<juce::String,juce::String>> failed;
            auto scanAt=std::chrono::steady_clock::now();
            while(!stopping)
            {
                const bool requested=rescan.exchange(false);const auto now=std::chrono::steady_clock::now();
                if(requested || now>=scanAt){if(requested)failed.clear();discover(requested);scanAt=now+std::chrono::seconds(2);}
                // Device presence is checked without reopening an active camera for discovery.
                std::vector<VisualBinding> wanted;std::vector<RecordingSubscription> sinks;{std::lock_guard lock(mutex);wanted=bindings;sinks=recordings;}
                std::map<juce::String,std::vector<VisualBinding>> groups;for(const auto& b:wanted)if(b.active)groups[b.sourceId].push_back(b);
                for(auto it=sessions.begin();it!=sessions.end();)if(!groups.count(it->first)){failed.erase(it->first);it=sessions.erase(it);}else ++it;
                for(auto it=failed.begin();it!=failed.end();)if(!groups.count(it->first))it=failed.erase(it);else ++it;
                std::map<juce::String,VisualPreview> next;
                for(const auto& group:groups)
                {
                    VisualPreview value;value.subscribers=(int)group.second.size();const auto target=targets.find(group.first);int formatIndex=-1;bool conflict=false,monitor=false;
                    if(target!=targets.end())
                    {
                        for(int i=0;i<target->second.source.formats.size();++i)
                            if(std::all_of(group.second.begin(),group.second.end(),[&](const VisualBinding& b){return captureFormatMatches(target->second.source.formats[i],b.settings);})){formatIndex=i;break;}
                        conflict=formatIndex<0;
                    }
                    for(const auto& b:group.second)monitor=monitor||b.monitoring;
                    if(target==targets.end()){sessions.erase(group.first);failed.erase(group.first);value.state="unavailable";value.message="Saved device is unavailable. Reconnect it and refresh sources.";}
                    else if(conflict){sessions.erase(group.first);value.state=group.second.size()>1?"formatConflict":"unsupportedFormat";value.message="No supported format satisfies all subscribers. Select one compatible format on every track.";}
                    else
                    {
                        const auto requestedFormat=target->second.source.formats[formatIndex];
                        if(failed.count(group.first) && failed[group.first].second!=juce::JSON::toString(requestedFormat))failed.erase(group.first);
                        auto existing=sessions.find(group.first);if(existing!=sessions.end() && juce::JSON::toString(existing->second->format)!=juce::JSON::toString(requestedFormat)){sessions.erase(existing);failed.erase(group.first);}
                        if(failed.count(group.first)){value.state="failed";value.message=failed[group.first].first;}
                        else try
                        {
                            auto& session=sessions[group.first];
                            if(!session)
                            {
                                MonitorPipe* pipe=nullptr;if(target->second.source.kind=="microphone")
                                {for(auto& p:pipes){bool used=false;for(const auto& s:sessions)if(s.second && s.second->pipe==&p)used=true;if(!used){pipe=&p;break;}}if(!pipe)throw std::runtime_error("Live monitoring capacity (16 microphones) exceeded.");}
                                session=std::make_unique<Session>(target->second,formatIndex,pipe);
                            }
                            session->poll(outputRate.load(),monitor,sinks,group.first);value=session->latest;
                        }
                        catch(const std::exception& e){sessions.erase(group.first);failed[group.first]={e.what(),juce::JSON::toString(requestedFormat)};value.state="failed";value.message=e.what();}
                    }
                    value.subscribers=(int)group.second.size();next[group.first]=std::move(value);
                }
                {std::lock_guard lock(mutex);snapshots=std::move(next);acquisitions=(int)sessions.size();}
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            sessions.clear();targets.clear();
        }catch(const std::exception& e){std::lock_guard lock(mutex);problem=e.what();}
        if(mf)MFShutdown();if(SUCCEEDED(com))CoUninitialize();
    }
#else
    void run(){std::lock_guard lock(mutex);problem="Camera and microphone capture require Windows.";}
#endif
    Impl(){worker=std::thread([this]{run();});}
    ~Impl(){stopping=true;worker.join();}
};
DeviceCapture::DeviceCapture():impl(std::make_unique<Impl>()){} DeviceCapture::~DeviceCapture()=default;
std::vector<VisualSource> DeviceCapture::sources()const{std::lock_guard lock(impl->mutex);return impl->catalog;}
void DeviceCapture::refresh(){impl->rescan=true;}
void DeviceCapture::setBindings(std::vector<VisualBinding> b){std::lock_guard lock(impl->mutex);impl->bindings=std::move(b);}
void DeviceCapture::setRecordingSubscriptions(std::vector<RecordingSubscription> r){std::lock_guard lock(impl->mutex);impl->recordings=std::move(r);}
VisualPreview DeviceCapture::preview(const juce::String& track)const
{
    std::lock_guard lock(impl->mutex);VisualPreview p;
    for(const auto& b:impl->bindings)if(b.trackId==track){if(!b.active)return p;auto it=impl->snapshots.find(b.sourceId);if(it!=impl->snapshots.end())return it->second;p.state="starting";p.message=impl->problem;return p;}return p;
}
juce::var DeviceCapture::status()const
{
    std::lock_guard lock(impl->mutex);juce::Array<juce::var> sources;
    for(const auto& s:impl->snapshots){const auto& p=s.second;const bool available=std::any_of(impl->catalog.begin(),impl->catalog.end(),[&](const VisualSource& source){return source.id==s.first;});int monitors=0;for(const auto& b:impl->bindings)if(b.active && b.monitoring && b.sourceId==s.first)++monitors;sources.add(values({{"available",available},{"monitorSubscribers",monitors},{"sourceId",s.first},{"state",p.state},{"message",p.message},{"subscribers",p.subscribers},{"frames",p.frameNumber},{"samples",p.sampleCount},{"samplePosition",p.samplePosition},{"timestampClock",s.first.startsWith("camera:")?"source-relative-100ns":"host-qpc-100ns"},{"timestamp100ns",p.timestamp100ns},{"peak",p.peak},{"format",p.format},{"discontinuities",p.discontinuities}}));}
    juce::int64 dropped=0,rendered=0;for(const auto& p:impl->pipes){dropped+=p.dropped.load();rendered+=p.rendered.load();}
    return values({{"sources",sources},{"deviceDiscoveryComplete",impl->discovered},{"activeAcquisitions",impl->acquisitions},{"error",impl->problem},{"monitorDroppedFrames",dropped},{"monitorRenderedFrames",rendered}});
}
void DeviceCapture::renderAudio(float*const* outputs,int channels,int frames,double rate)
{impl->outputRate=rate;for(auto& pipe:impl->pipes)pipe.render(outputs,channels,frames);}
}
