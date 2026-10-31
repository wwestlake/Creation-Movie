#include "MovieWebCam.h"
#include "MovieCaptureMonitor.h"
#include <iostream>
#include <thread>

// Isolated native receiver fixture. Explicit developer output path, no project/VFS writes.
int main(int argc,char** argv){
    juce::ScopedJuceInitialiser_GUI initialise;
    {movie::CaptureMonitor monitor;monitor.enabled=true;monitor.write(1,-1);float left[4]{},right[4]{};float* outputs[]={left,right};monitor.render(outputs,2,4);if(left[0]!=.25f||right[0]!=-.25f)return 3;
     monitor.write(1,1);++monitor.generation;left[0]=right[0]=0;monitor.render(outputs,2,4);if(left[0]||right[0])return 3;
     monitor.write(1,1);monitor.enabled=false;monitor.render(outputs,2,4);if(left[0]||right[0])return 3;}
    if(argc==2&&juce::String(argv[1])=="--unit"){
        try{movie::WebCamCapture::verifyTimingBoundaries();}catch(const std::exception& error){std::cerr<<error.what()<<"\n";return 3;}
        std::cout<<"Monitor and timing boundary checks passed: render/mute/generation, probe outliers, wall step, RTP wrap, missing anchors, bounded receive queue\n";return 0;
    }
    if(argc!=3){std::cerr<<"Usage: WebCamReceiverTests <private-output-folder> <LAN-IPv4>\n";return 2;}
    const juce::File output(juce::String::fromUTF8(argv[1]));if(!output.isDirectory()){std::cerr<<"Create output directory first\n";return 2;}
    try{
        movie::WebCamCapture capture;
        const auto invitation=capture.invite(juce::String::fromUTF8(argv[2]),8765);
        output.getChildFile("invitation.private.json").replaceWithText(juce::JSON::toString(invitation));
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
        capture.stop();output.getChildFile("invitation.private.json").deleteFile();
        if(failure.isNotEmpty()||v1<=60||a<=100||!unsubscribed){std::cerr<<"Native receiver incomplete: video="<<v1<<" audio="<<a<<" "<<failure<<"\n";return 1;}
        std::cout<<"Native receiver: authenticated decoded video/audio, monotonic mapped times, shared source and unsubscribe checks passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<"\n";return 1;}
}
