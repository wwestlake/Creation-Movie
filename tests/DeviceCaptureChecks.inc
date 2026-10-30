#include "MovieDeviceCapture.h"
static int deviceChecks(const juce::File& output)
{
    output.createDirectory();juce::Array<juce::var> results,checks;juce::String error;
    auto object=[](std::initializer_list<std::pair<const char*,juce::var>> fields){auto* o=new juce::DynamicObject;for(const auto& f:fields)o->setProperty(f.first,f.second);return juce::var(o);};
    try
    {
        movie::VisualCapture capture;std::vector<movie::VisualSource> catalog;
        require(waitFor([&]{return (bool)capture.status()["deviceDiscoveryComplete"];},20000),("Device discovery failed: "+juce::JSON::toString(capture.status())).toRawUTF8());catalog=capture.sources();
        std::cout<<juce::JSON::toString(capture.status())<<std::endl;
        for(const auto& source:catalog)results.add(source.toJson());
        require(capture.validate("camera:missing","camera",{}).isNotEmpty(),"Missing device was accepted");
        require(movie::captureFormatMatches(object({{"sampleRate",48000},{"channels",2}}),object({{"sampleRate",48000}})),"Partial configuration matching failed");
        require(!movie::captureFormatMatches(object({{"sampleRate",48000}}),object({{"sampleRate",44100}})),"Incompatible configuration accepted");
        capture.setBindings({{"missing","microphone:missing",true,{},false}});
        require(waitFor([&]{return capture.preview("missing").state=="unavailable";}),"Missing device state not reported");capture.setBindings({});
        checks.add("Unavailable-device preservation and supported-format matching passed");
        for(const auto& source:catalog)
        {
            if(source.kind!="camera" && source.kind!="microphone")continue;
            if(source.formats.isEmpty()){checks.add(source.name+": no accessible supported formats; acquisition unverified");continue;}
            const auto format=source.formats[0];
            require(capture.validate(source.id,source.kind,format).isEmpty(),"Catalog format rejected");
            capture.setBindings({{"a",source.id,true,format,false},{"b",source.id,true,format,false}});
            require(waitFor([&]{const auto p=capture.preview("a");return p.frameNumber>2 || p.sampleCount>4096;},15000),("Physical capture failed: "+source.name+" / "+capture.preview("a").message).toRawUTF8());
            const auto first=capture.preview("a");require(first.subscribers==2,"Physical source was not shared");require((int)capture.status()["activeAcquisitions"]==1,"Duplicate physical acquisition");
            require(first.timestamp100ns>0,"No native timestamp");
            if(source.kind=="camera")require(first.image.isValid() && first.image.getWidth()==(int)format["width"] && first.image.getHeight()==(int)format["height"],"Camera pixels do not match native format");
            else
            {
                std::array<float,2048> left{},right{};float* channels[]={left.data(),right.data()};capture.renderAudio(channels,2,2048,48000);
                require(std::all_of(left.begin(),left.end(),[](float v){return v==0;}),"Monitoring sounded while disabled");
                const auto renderedBefore=(juce::int64)capture.status()["monitorRenderedFrames"];
                capture.setBindings({{"a",source.id,true,format,true},{"b",source.id,true,format,false}});
                require(waitFor([&]{left.fill(0);right.fill(0);capture.renderAudio(channels,2,2048,48000);return (juce::int64)capture.status()["monitorRenderedFrames"]>renderedBefore;},5000),"No microphone monitor samples consumed");
                require(std::all_of(left.begin(),left.end(),[](float v){return std::isfinite(v);}),"Nonfinite microphone monitor audio");
                checks.add(source.name+": native samples, timestamps and bounded monitor-buffer consumption passed; peak="+juce::String(capture.preview("a").peak,6));
                capture.setBindings({{"b",source.id,true,format,false}});juce::Thread::sleep(100);left.fill(0);right.fill(0);capture.renderAudio(channels,2,2048,48000);
                require(std::all_of(left.begin(),left.end(),[](float v){return v==0;}),"Removed subscriber left audible monitoring");
            }
            capture.setBindings({{"b",source.id,true,format,false}});
            require(waitFor([&]{const auto p=capture.preview("b");return p.subscribers==1 && (p.frameNumber>first.frameNumber || p.sampleCount>first.sampleCount);}),"Remaining subscriber stopped");
            if(source.formats.size()>1)
            {
                capture.setBindings({{"a",source.id,true,format,false},{"b",source.id,true,source.formats[1],false}});
                require(waitFor([&]{return capture.preview("a").state=="formatConflict";}),"Incompatible subscribers were not rejected");require((int)capture.status()["activeAcquisitions"]==0,"Conflicted physical source left open");
                capture.setBindings({{"b",source.id,true,source.formats[1],false}});
                require(waitFor([&]{const auto p=capture.preview("b");return p.frameNumber>0 || p.sampleCount>0;},15000),"Supported format switching failed");
                checks.add(source.name+": explicit format conflict and alternate native format passed");
            }
            if(source.kind=="camera")for(const auto& alternative:source.formats)if(alternative["width"]!=format["width"] || alternative["height"]!=format["height"])
            {
                capture.setBindings({{"b",source.id,true,alternative,false}});
                require(waitFor([&]{const auto p=capture.preview("b");return p.image.isValid() && p.image.getWidth()==(int)alternative["width"] && p.image.getHeight()==(int)alternative["height"];},15000),"Native camera resolution switching failed");
                checks.add(source.name+": alternate native resolution preview passed");break;
            }
            auto incompatible=format.clone();if(source.kind=="microphone")incompatible.getDynamicObject()->setProperty("sampleRate",1);else incompatible.getDynamicObject()->setProperty("width",1);
            capture.setBindings({{"a",source.id,true,format,false},{"b",source.id,true,incompatible,false}});
            require(waitFor([&]{return capture.preview("a").state=="formatConflict" && (int)capture.status()["activeAcquisitions"]==0;}),"Conflicting source requests left a device open");
            capture.setBindings({{"b",source.id,true,format,false}});require(waitFor([&]{const auto p=capture.preview("b");return p.frameNumber>0 || p.sampleCount>0;},15000),"Device failed to recover from conflicting configuration");
            checks.add(source.name+": explicit incompatible request rejected and acquisition recovered");
            checks.add(source.name+": real "+source.kind+" acquisition, sharing and independent unsubscribe passed");
            capture.setBindings({});require(waitFor([&]{return (int)capture.status()["activeAcquisitions"]==0;}),"Last unsubscribe leaked device");
        }
        const movie::VisualSource* camera=nullptr;const movie::VisualSource* microphone=nullptr;const movie::VisualSource* display=nullptr;
        for(const auto& source:catalog)
        {if(source.kind=="camera" && !source.formats.isEmpty() && (!camera || source.name.contains("C920")))camera=&source;
         if(source.kind=="microphone" && !source.formats.isEmpty() && (!microphone || source.name.contains("C920")))microphone=&source;
         if(source.kind=="display" && !display)display=&source;}
        if(camera && microphone && display)
        {
            capture.setBindings({{"camera",camera->id,true,camera->formats[0],false},{"mic",microphone->id,true,microphone->formats[0],false},{"display",display->id,true}});
            require(waitFor([&]{return capture.preview("camera").frameNumber>2 && capture.preview("mic").sampleCount>4096 && capture.preview("display").frameNumber>2;},15000),"Concurrent camera/microphone/display acquisition failed");
            require((int)capture.status()["activeAcquisitions"]==3,"Concurrent acquisitions not accounted for");
            const auto samples=capture.preview("mic").sampleCount;
            capture.setBindings({{"mic",microphone->id,true,microphone->formats[0],false},{"display",display->id,true}});
            require(waitFor([&]{return capture.preview("mic").sampleCount>samples && (int)capture.status()["activeAcquisitions"]==2;}),"Stopping camera disrupted other capture sources");capture.setBindings({});
            require(waitFor([&]{return (int)capture.status()["activeAcquisitions"]==0;}),"Concurrent acquisition cleanup failed");
            checks.add("Concurrent actual camera/microphone/individual display capture and independent device stop passed");
        }
        output.getChildFile("device-results.json").replaceWithText(juce::JSON::toString(object({{"sources",results},{"checks",checks},{"error",error},{"speakerOutputTested",false}}),true));
    }
    catch(const std::exception& e){error=e.what();output.getChildFile("device-results.json").replaceWithText(juce::JSON::toString(object({{"sources",results},{"checks",checks},{"error",error},{"speakerOutputTested",false}}),true));std::cerr<<error<<std::endl;return 1;}
    std::cout<<"Device tests passed; "<<checks.size()<<" checks. Hardware results: "<<output.getFullPathName()<<std::endl;return 0;
}
