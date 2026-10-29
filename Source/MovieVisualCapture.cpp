#include "MovieVisualCapture.h"
#include <atomic>
#include <map>
#include <mutex>
#include <thread>
#if JUCE_WINDOWS
#include <windows.h>
#include <dwmapi.h>
#include <d3d11.h>
#include <d3d11_4.h>
#include <dxgi.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#endif

namespace movie
{
juce::var VisualSource::toJson() const
{
    auto* value=new juce::DynamicObject;
    value->setProperty("id",id);value->setProperty("kind",kind);value->setProperty("name",name);
    value->setProperty("width",width);value->setProperty("height",height);return juce::var(value);
}
struct VisualCapture::Impl
{
    mutable std::mutex mutex;
    std::vector<VisualSource> catalog;
    std::vector<VisualBinding> bindings;
    std::map<juce::String,VisualPreview> snapshots;
    juce::String problem;
    std::atomic<bool> stopping{false}, rescan{true};
    std::thread worker;
#if JUCE_WINDOWS
    using Item=winrt::Windows::Graphics::Capture::GraphicsCaptureItem;
    struct Target { VisualSource source; HWND window=nullptr; HMONITOR monitor=nullptr; DWORD pid=0; juce::uint64 processStart=0; };
    std::map<juce::String,Target> targets;
    // Window IDs are session-scoped opaque selectors, never persisted HWNDs.
    std::map<HWND,std::pair<juce::String,juce::uint64>> windowIds;
    inline static thread_local Impl* eventOwner=nullptr;
    static juce::uint64 processBirth(DWORD pid)
    {
        const auto process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
        FILETIME born{},exit{},kernel{},user{};if(!process)return 0;
        const bool ok=GetProcessTimes(process,&born,&exit,&kernel,&user)!=FALSE;CloseHandle(process);
        return ok?((juce::uint64)born.dwHighDateTime<<32)|born.dwLowDateTime:0;
    }
    static bool validWindow(HWND window,DWORD pid,juce::uint64 born)
    {DWORD current=0;GetWindowThreadProcessId(window,&current);return IsWindow(window) && current==pid && born!=0 && processBirth(pid)==born;}
    struct Acquisition
    {
        Target target;
        winrt::com_ptr<ID3D11Device> device;
        winrt::com_ptr<ID3D11DeviceContext> context;
        winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice bridge{nullptr};
        winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool pool{nullptr};
        winrt::Windows::Graphics::Capture::GraphicsCaptureSession session{nullptr};
        Item item{nullptr};
        std::shared_ptr<std::atomic<bool>> closed=std::make_shared<std::atomic<bool>>(false);
        VisualPreview latest;
        winrt::Windows::Graphics::SizeInt32 size{};
        ~Acquisition(){try{if(session)session.Close();if(pool)pool.Close();}catch(...) {}}
        explicit Acquisition(Target value):target(std::move(value))
        {
            using namespace winrt::Windows::Graphics;
            if(!Capture::GraphicsCaptureSession::IsSupported())throw std::runtime_error("Windows Graphics Capture is unavailable.");
            auto interop=winrt::get_activation_factory<Item,IGraphicsCaptureItemInterop>();
            if(target.window)winrt::check_hresult(interop->CreateForWindow(target.window,winrt::guid_of<Item>(),winrt::put_abi(item)));
            else winrt::check_hresult(interop->CreateForMonitor(target.monitor,winrt::guid_of<Item>(),winrt::put_abi(item)));
            winrt::check_hresult(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                nullptr,0,D3D11_SDK_VERSION,device.put(),nullptr,context.put()));
            context.as<ID3D11Multithread>()->SetMultithreadProtected(TRUE);
            auto dxgi=device.as<IDXGIDevice>();winrt::com_ptr<IInspectable> inspectable;
            winrt::check_hresult(CreateDirect3D11DeviceFromDXGIDevice(dxgi.get(),inspectable.put()));
            bridge=inspectable.as<winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice>();size=item.Size();
            item.Closed([flag=closed](auto&&,auto&&){*flag=true;});
            pool=Capture::Direct3D11CaptureFramePool::CreateFreeThreaded(bridge,winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized,2,size);
            session=pool.CreateCaptureSession(item);session.StartCapture();latest.state="starting";
        }
        void poll()
        {
            using namespace winrt::Windows::Graphics;
            if(*closed || (target.window && !validWindow(target.window,target.pid,target.processStart)))throw std::runtime_error("Window closed or replaced. Select a source again.");
            if(target.window && IsIconic(target.window)){latest.image={};latest.state="unavailable";latest.message="Window is minimized. Restore it to resume preview.";return;}
            auto frame=pool.TryGetNextFrame();if(!frame)return;
            const auto content=frame.ContentSize();
            if(content.Width<=0 || content.Height<=0){frame.Close();return;}
            if(content.Width!=size.Width || content.Height!=size.Height)
            {frame.Close();size=content;pool.Recreate(bridge,winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized,2,size);latest.image={};return;}
            auto access=frame.Surface().as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();winrt::com_ptr<ID3D11Texture2D> texture;
            winrt::check_hresult(access->GetInterface(__uuidof(ID3D11Texture2D),texture.put_void()));
            // A fresh bounded readback avoids stale CPU-visible snapshots observed
            // with staging-texture reuse on the validation machine's driver.
            winrt::com_ptr<ID3D11Texture2D> staging;
            {
                D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);desc.Usage=D3D11_USAGE_STAGING;
                desc.BindFlags=0;desc.MiscFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
                winrt::check_hresult(device->CreateTexture2D(&desc,nullptr,staging.put()));
            }
            context->CopyResource(staging.get(),texture.get());context->Flush();D3D11_MAPPED_SUBRESOURCE mapped{};
            winrt::check_hresult(context->Map(staging.get(),0,D3D11_MAP_READ,0,&mapped));
            juce::Image image(juce::Image::ARGB,size.Width,size.Height,true,juce::SoftwareImageType{});
            {juce::Image::BitmapData pixels(image,juce::Image::BitmapData::writeOnly);
                for(int y=0;y<size.Height;++y)std::memcpy(pixels.getLinePointer(y),(const char*)mapped.pData+(size_t)y*mapped.RowPitch,(size_t)size.Width*4);}
            context->Unmap(staging.get(),0);
            latest.image=std::move(image);latest.timestamp100ns=frame.SystemRelativeTime().count();++latest.frameNumber;
            latest.state="live";latest.message={};frame.Close();
        }
    };
    void discover()
    {
        std::map<juce::String,Target> found;
        struct MonitorContext { std::map<juce::String,Target>* found; } monitorContext{&found};
        EnumDisplayMonitors(nullptr,nullptr,[](HMONITOR monitor,HDC,LPRECT,LPARAM data)->BOOL
        {
            auto& output=*reinterpret_cast<MonitorContext*>(data)->found;MONITORINFOEXW info{};info.cbSize=sizeof(info);
            if(!GetMonitorInfoW(monitor,reinterpret_cast<MONITORINFO*>(&info)))return TRUE;
            DISPLAY_DEVICEW child{};child.cb=sizeof(child);EnumDisplayDevicesW(info.szDevice,0,&child,EDD_GET_DEVICE_INTERFACE_NAME);
            if(child.DeviceID[0]==0)return TRUE; // Never silently use a renumberable desktop slot as physical identity.
            Target t;t.monitor=monitor;t.source.id="display:"+juce::String(child.DeviceID);t.source.kind="display";
            t.source.width=info.rcMonitor.right-info.rcMonitor.left;t.source.height=info.rcMonitor.bottom-info.rcMonitor.top;
            t.source.name=juce::String(info.szDevice)+" / "+juce::String(child.DeviceString)+" / "+juce::String(t.source.width)+" x "+juce::String(t.source.height)
                +" @ "+juce::String(info.rcMonitor.left)+","+juce::String(info.rcMonitor.top)+((info.dwFlags&MONITORINFOF_PRIMARY)?" (primary)":"");
            output[t.source.id]=std::move(t);return TRUE;
        },reinterpret_cast<LPARAM>(&monitorContext));
        struct WindowContext { Impl* owner;std::map<juce::String,Target>* found; } windowContext{this,&found};
        EnumWindows([](HWND window,LPARAM data)->BOOL
        {
            auto& c=*reinterpret_cast<WindowContext*>(data);DWORD pid=0;GetWindowThreadProcessId(window,&pid);
            if(pid==GetCurrentProcessId() || !IsWindowVisible(window) || GetWindow(window,GW_OWNER)!=nullptr)return TRUE;
            DWORD cloaked=0;DwmGetWindowAttribute(window,DWMWA_CLOAKED,&cloaked,sizeof(cloaked));if(cloaked)return TRUE;
            wchar_t title[1024]{};GetWindowTextW(window,title,1024);if(title[0]==0)return TRUE;
            const auto born=processBirth(pid);if(!born)return TRUE;
            auto& selector=c.owner->windowIds[window];if(selector.first.isEmpty() || selector.second!=born)selector={"window:"+juce::Uuid().toString(),born};
            Target t;t.window=window;t.pid=pid;t.processStart=born;t.source.id=selector.first;t.source.kind="window";
            RECT rect{};GetWindowRect(window,&rect);t.source.width=rect.right-rect.left;t.source.height=rect.bottom-rect.top;
            t.source.name=juce::String(title)+" / PID "+juce::String((int)pid)+" / "+juce::String(t.source.width)+" x "+juce::String(t.source.height)
                +" @ "+juce::String(rect.left)+","+juce::String(rect.top)+" / "+t.source.id.substring(7,13)+(IsIconic(window)?" (minimized)":"");
            (*c.found)[t.source.id]=std::move(t);return TRUE;
        },reinterpret_cast<LPARAM>(&windowContext));
        for(auto it=windowIds.begin();it!=windowIds.end();)if(!IsWindow(it->first))it=windowIds.erase(it);else ++it;
        targets=std::move(found);std::vector<VisualSource> next;for(const auto& item:targets)next.push_back(item.second.source);
        std::lock_guard lock(mutex);catalog=std::move(next);
    }
#endif
    Impl(){worker=std::thread([this]{run();});}
    ~Impl(){stopping=true;worker.join();}
    void run()
    {
#if JUCE_WINDOWS
        try
        {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
            eventOwner=this;
            struct WindowEvents
            {
                HWINEVENTHOOK hook=nullptr;
                ~WindowEvents(){if(hook)UnhookWinEvent(hook);}
            } events;
            events.hook=SetWinEventHook(EVENT_OBJECT_DESTROY,EVENT_OBJECT_DESTROY,nullptr,
                [](HWINEVENTHOOK,DWORD,HWND window,LONG object,LONG child,DWORD,DWORD)
                {if(eventOwner && object==OBJID_WINDOW && child==CHILDID_SELF && eventOwner->windowIds.erase(window)>0)eventOwner->rescan=true;},
                0,0,WINEVENT_OUTOFCONTEXT|WINEVENT_SKIPOWNPROCESS);
            if(!events.hook)throw std::runtime_error("Cannot observe window lifetime safely.");
            std::map<juce::String,std::unique_ptr<Acquisition>> sessions;
            std::map<juce::String,VisualPreview> failed;
            auto scanAt=std::chrono::steady_clock::now();
            while(!stopping)
            {
                MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}
                const auto now=std::chrono::steady_clock::now();
                const bool requested=rescan.exchange(false);
                if(requested)failed.clear();
                if(requested || now>=scanAt){discover();scanAt=now+std::chrono::seconds(2);}
                std::vector<VisualBinding> wanted;{std::lock_guard lock(mutex);wanted=bindings;}
                std::map<juce::String,int> counts;for(const auto& b:wanted)if(b.active)++counts[b.sourceId];
                for(auto it=sessions.begin();it!=sessions.end();)if(!counts.count(it->first) || !targets.count(it->first)
                    || it->second->target.monitor!=targets[it->first].monitor)it=sessions.erase(it);else ++it;
                for(auto it=failed.begin();it!=failed.end();)if(!counts.count(it->first) || !targets.count(it->first))it=failed.erase(it);else ++it;
                std::map<juce::String,VisualPreview> next;
                for(const auto& count:counts)
                {
                    const auto target=targets.find(count.first);VisualPreview current;current.subscribers=count.second;
                    if(target==targets.end()){current.state="unavailable";current.message="Saved source is unavailable. Refresh and select the source explicitly.";}
                    else if(target->second.window && windowIds[target->second.window].first!=count.first)
                    {sessions.erase(count.first);rescan=true;current.state="unavailable";current.message="Window identity expired. Select the window again.";}
                    else if(failed.count(count.first))current=failed[count.first];
                    else try
                    {
                        auto& session=sessions[count.first];if(!session)session=std::make_unique<Acquisition>(target->second);
                        session->poll();current=session->latest;
                    }
                    catch(const winrt::hresult_error& e){current.state="failed";current.message=juce::String(e.message().c_str());sessions.erase(count.first);failed[count.first]=current;}
                    catch(const std::exception& e){current.state="failed";current.message=e.what();if(target->second.window){windowIds.erase(target->second.window);rescan=true;}sessions.erase(count.first);failed[count.first]=current;}
                    current.subscribers=count.second;next[count.first]=std::move(current);
                }
                {std::lock_guard lock(mutex);snapshots=std::move(next);activeAcquisitions=(int)sessions.size();}
                std::this_thread::sleep_for(std::chrono::milliseconds(33));
            }
            sessions.clear();eventOwner=nullptr;winrt::uninit_apartment();
        }
        catch(const winrt::hresult_error& e){std::lock_guard lock(mutex);problem=juce::String(e.message().c_str());}
        catch(const std::exception& e){std::lock_guard lock(mutex);problem=e.what();}
#else
        std::lock_guard lock(mutex);problem="Visual capture requires Windows 10 version 1903 or later.";
#endif
    }
    int activeAcquisitions=0;
};
VisualCapture::VisualCapture():impl(std::make_unique<Impl>()){}
VisualCapture::~VisualCapture()=default;
std::vector<VisualSource> VisualCapture::sources() const {std::lock_guard lock(impl->mutex);return impl->catalog;}
void VisualCapture::refresh(){impl->rescan=true;}
void VisualCapture::setBindings(std::vector<VisualBinding> bindings){std::lock_guard lock(impl->mutex);impl->bindings=std::move(bindings);}
VisualPreview VisualCapture::preview(const juce::String& track) const
{
    std::lock_guard lock(impl->mutex);VisualPreview result;
    if(impl->problem.isNotEmpty()){result.state="failed";result.message=impl->problem;return result;}
    for(const auto& b:impl->bindings)if(b.trackId==track)
    {if(!b.active){result.message="Live preview stopped.";return result;}const auto it=impl->snapshots.find(b.sourceId);if(it!=impl->snapshots.end())return it->second;result.state="starting";return result;}
    result.message="No display or window assigned.";return result;
}
juce::var VisualCapture::status() const
{
    std::lock_guard lock(impl->mutex);juce::Array<juce::var> entries;
    for(const auto& entry:impl->snapshots)
    {
        auto* o=new juce::DynamicObject;const auto& p=entry.second;o->setProperty("sourceId",entry.first);o->setProperty("state",p.state);
        o->setProperty("message",p.message);o->setProperty("subscribers",p.subscribers);o->setProperty("frames",p.frameNumber);
        o->setProperty("timestamp100ns",p.timestamp100ns);o->setProperty("width",p.image.getWidth());o->setProperty("height",p.image.getHeight());entries.add(juce::var(o));
    }
    auto* result=new juce::DynamicObject;result->setProperty("sources",entries);result->setProperty("activeAcquisitions",impl->activeAcquisitions);result->setProperty("error",impl->problem);return juce::var(result);
}
}
