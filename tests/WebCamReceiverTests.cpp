#include "MovieWebCam.h"
#include "MovieCaptureMonitor.h"
#include "MovieWebCamNetwork.h"
#include "MovieWebCamQr.h"
#include "WebCamPairingLifecycleTests.h"
#include <iostream>
#include <thread>

// Isolated native receiver fixture. Explicit developer output path, no project/VFS writes.
int main(int argc,char** argv){
    juce::ScopedJuceInitialiser_GUI initialise;
    {movie::CaptureMonitor monitor;monitor.enabled=true;monitor.write(1,-1);float left[4]{},right[4]{};float* outputs[]={left,right};monitor.render(outputs,2,4);if(left[0]!=.25f||right[0]!=-.25f)return 3;
     monitor.write(1,1);++monitor.generation;left[0]=right[0]=0;monitor.render(outputs,2,4);if(left[0]||right[0])return 3;
     monitor.write(1,1);monitor.enabled=false;monitor.render(outputs,2,4);if(left[0]||right[0])return 3;}
    if(argc==2&&juce::String(argv[1])=="--lifecycle"){
        try{verifyPairingLifecycle();std::cout<<"Pairing lifecycle checks passed: modeless workspace, waiting Close, successful auto-dismiss, reopen/Done without reconnect, failed Retry/Close, teardown and explicit disconnect\n";return 0;}
        catch(const std::exception& error){std::cerr<<error.what()<<"\n";return 4;}
    }
    if(argc==2&&juce::String(argv[1])=="--unit"){
        try{
            const auto candidates=movie::selectWebCamAddresses({
                {"192.168.2.3",true,true,true,false,20},{"10.20.0.4",true,true,false,false,5},
                {"172.16.3.4",true,false,false,true,1},{"192.168.2.3",true,true,true,false,20},
                {"192.168.40.1",true,false,false,false,1},{"192.168.80.1",false,true,true,false,1},
                {"8.8.8.8",true,true,true,false,1},{"169.254.1.1",true,true,false,false,1}});
            if(candidates!=std::vector<std::string>{"10.20.0.4","192.168.2.3","172.16.3.4"})throw std::runtime_error("Automatic interface ranking/filtering failed");
            movie::WebCamCapture::verifyTimingBoundaries();}catch(const std::exception& error){std::cerr<<error.what()<<"\n";return 3;}
        std::cout<<"Monitor and timing boundary checks passed: render/mute/generation, probe outliers, wall step, RTP wrap, missing anchors, bounded receive queue\n";return 0;
    }
    if(argc==3&&juce::String(argv[1])=="--pairing-check"){
        const juce::File output(juce::String::fromUTF8(argv[2]));if(!output.isDirectory())return 2;
        movie::WebCamCapture capture;
        auto issue=[&]{const auto invitation=capture.invite({},8765);output.getChildFile("invitation.private.json").replaceWithText(juce::JSON::toString(invitation));
            auto qr=movieWebCamQrImage(juce::JSON::toString(invitation,true));auto file=output.getChildFile("invitation.private.png").createOutputStream();juce::PNGImageFormat().writeImageToStream(qr,*file);};
        issue();const auto end=juce::Time::getMillisecondCounterHiRes()+45000;
        while(juce::Time::getMillisecondCounterHiRes()<end&&!output.getChildFile("stop").existsAsFile()){
            juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
            if(output.getChildFile("expire").existsAsFile()){output.getChildFile("expire").deleteFile();capture.expireInvitationForTest();}
            if(output.getChildFile("renew").existsAsFile()){output.getChildFile("renew").deleteFile();issue();}
            output.getChildFile("status.json").replaceWithText(juce::JSON::toString(capture.status()));
        }
        capture.stop();output.getChildFile("invitation.private.json").deleteFile();output.getChildFile("invitation.private.png").deleteFile();return 0;
    }
    if(argc!=3){std::cerr<<"Usage: WebCamReceiverTests <private-output-folder> <LAN-IPv4>\n";return 2;}
    const juce::File output(juce::String::fromUTF8(argv[1]));if(!output.isDirectory()){std::cerr<<"Create output directory first\n";return 2;}
    try{
        movie::WebCamCapture capture;
        const auto invitation=capture.invite(juce::String(argv[2])=="auto"?juce::String():juce::String::fromUTF8(argv[2]),8765);
        output.getChildFile("invitation.private.json").replaceWithText(juce::JSON::toString(invitation));
        bool pairingAutoClosed=false;
        std::unique_ptr<MovieWebCamPairingWindow> pairingWindow;
        auto panel=std::make_unique<MovieWebCamPairing>([&]{return invitation;},[&]{return capture.status();},[&]{capture.stop();},[&]{pairingAutoClosed=true;pairingWindow.reset();});
        pairingWindow=std::make_unique<MovieWebCamPairingWindow>(std::move(panel),nullptr,false);
        if(pairingWindow->isCurrentlyModal())throw std::runtime_error("Live pairing window became modal");
        auto first=std::make_shared<movie::CaptureQueue>(),second=std::make_shared<movie::CaptureQueue>(),audio=std::make_shared<movie::CaptureQueue>();
        first->after=0;second->after=0;audio->after=0;
        capture.setBindings({{"monitor","webcam:audio",true,{},true}},{{"first","webcam:video",{},first},{"second","webcam:video",{},second},{"audio","webcam:audio",{},audio}});
        int v1=0,v2=0,a=0;bool unsubscribed=false;juce::int64 start=0,lastVideo=0,lastAudio=0;juce::String failure;
        const auto deadline=juce::Time::getMillisecondCounterHiRes()+90000;
        while(juce::Time::getMillisecondCounterHiRes()<deadline){
            juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
            float left[480]{},right[480]{};float* outputs[]={left,right};capture.renderAudio(outputs,2,480,48000);
            while(auto sample=first->pop()){if(start==0)start=sample->hostTime;if(lastVideo&&sample->hostTime<lastVideo)failure="Video timestamps moved backwards";lastVideo=sample->hostTime;++v1;}
            while(auto sample=second->pop())++v2;
            while(auto sample=audio->pop()){if(lastAudio&&sample->hostTime<lastAudio)failure="Audio timestamps moved backwards";lastAudio=sample->hostTime;++a;}
            if(v1>=30&&!unsubscribed){if(v1!=v2)failure="Shared source subscribers differ";capture.setBindings({{"monitor","webcam:audio",true,{},true}},{{"first","webcam:video",{},first},{"audio","webcam:audio",{},audio}});unsubscribed=true;}
            auto status=capture.status();auto* result=status.getDynamicObject();result->setProperty("firstSubscriberFrames",v1);result->setProperty("secondSubscriberFrames",v2);result->setProperty("audioSubscriberFrames",a);result->setProperty("subscriberRemoved",unsubscribed);result->setProperty("testFailure",failure);
            output.getChildFile("native-results.json").replaceWithText(juce::JSON::toString(status));
            if(v1>60&&a>100&&unsubscribed)break;
            if(output.getChildFile("stop").existsAsFile())break;
        }
        const auto liveSources=capture.sources();
        bool hasVideo=false,hasAudio=false;for(const auto& source:liveSources){hasVideo|=source.id=="webcam:video";hasAudio|=source.id=="webcam:audio";}
        if(!pairingAutoClosed||pairingWindow||capture.status()["state"].toString()!="connected"||!hasVideo||!hasAudio)failure="Auto-dismiss lost connection or source catalog";
        pairingWindow.reset();
        capture.stop();output.getChildFile("invitation.private.json").deleteFile();
        if(capture.status()["state"].toString()!="stopped")failure="Explicit disconnect did not stop receiver";
        if(failure.isNotEmpty()||v1<=60||a<=100||!unsubscribed){std::cerr<<"Native receiver incomplete: video="<<v1<<" audio="<<a<<" "<<failure<<"\n";return 1;}
        std::cout<<"Native receiver: authenticated decoded video/audio, monotonic mapped times, shared source, unsubscribe and live pairing auto-dismiss/source catalog checks passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<"\n";return 1;}
}
