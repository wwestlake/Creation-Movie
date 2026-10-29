#include "MovieCaptureStream.h"
#include "MovieDocument.h"
#include <iostream>
#include <stdexcept>
using namespace movie;
static void check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
static void edit(Document& d,const char* command,juce::var args)
{
    juce::String error;const bool ok=d.edit(command,args,error);check(ok,error.toRawUTF8());
}
#include "EffectsTests.inc"
#include "AutomationTests.inc"
#include "TransitionTests.inc"
int main()
{
    try
    {
        Document d;
        Media media;media.id="asset";media.versionId="original-version";media.name="test";media.path="Media/test";
        media.info.hasVideo=true;media.info.hasAudio=true;media.info.duration=20;media.info.width=640;media.info.height=360;
        d.sequence.media.push_back(media);
        auto legacy=d.sequence.toJson();legacy.getDynamicObject()->setProperty("version",2);
        legacy.getDynamicObject()->removeProperty("tracks");
        juce::Array<juce::var> clips;
        for(int i=0;i<8;++i)
            clips.add(object({{"id","clip-"+juce::String(i)},{"assetId","asset"},{"name","test"},{"track",i},
                {"kind",i%2?"audio":"video"},{"startFrame",i*30},{"durationFrames",30},{"sourceIn",2.0},
                {"sourceDuration",20.0},{"params",object({{"gain",0.5},{"opacity",0.75}})}}));
        legacy.getDynamicObject()->setProperty("clips",clips);
        juce::String error;Sequence loaded;
        check(Sequence::fromJson(legacy,loaded,error),error.toRawUTF8());
        check(loaded.tracks.size()==8 && loaded.clips.size()==8,"Legacy content lost");
        const auto migrated=juce::JSON::toString(loaded.toJson());
        Sequence repeated;check(Sequence::fromJson(legacy,repeated,error),"Repeat migration failed");
        check(migrated==juce::JSON::toString(repeated.toJson()),"Migration identities not deterministic");
        for(int i=0;i<8;++i)
        {
            const auto& c=loaded.clips[(size_t)i];
            check(c.trackIndex==i && c.assetId=="asset" && c.assetVersionId=="original-version" && c.sourceStartSeconds==2.0,"Legacy references/order changed");
            check(c.startSeconds==i*30*loaded.frameSeconds() && c.durationSeconds==30*loaded.frameSeconds(),"Legacy timing changed");
        }
        d.replace(loaded,true);
        const auto first=d.sequence.tracks[0].id;
        edit(d,"track.reorder",object({{"trackId",first},{"order",7}}));
        check(d.sequence.clips[0].trackIndex==7 && d.sequence.owner(d.sequence.clips[0])==first,"Reorder changed ownership");
        check(d.sequence.clips[0].startSeconds==0 && d.sequence.clips[0].assetId=="asset","Reorder changed clip");
        d.undo.undo();check(d.sequence.clips[0].trackIndex==0,"Undo reorder failed");
        d.undo.redo();check(d.sequence.clips[0].trackIndex==7,"Redo reorder failed");
        check(!d.edit("track.remove",object({{"trackId",first}}),error),"Occupied removal accepted implicitly");
        edit(d,"track.remove",object({{"trackId",first},{"removePlacements",true}}));
        check(d.sequence.media.size()==1 && d.sequence.clips.size()==7,"Removal deleted media");
        d.undo.undo();check(d.sequence.clips.size()==8,"Removal undo failed");
        edit(d,"track.rename",object({{"trackId",first},{"name","Renamed"}}));
        edit(d,"track.enabled",object({{"trackId",first},{"enabled",false}}));
        check(!d.sequence.trackEnabled(d.sequence.clips[0]),"Disabled track participates");
        edit(d,"track.source",object({{"trackId",first},{"source",object({{"category","camera"},{"identity","camera-symbolic-id"},{"settings",object({{"width",1920},{"fpsNum",30}})}})}}));
        check(d.sequence.clips.size()==8,"Source assignment replaced clips");
        edit(d,"track.preferences",object({{"trackId",first},{"armed",true},{"monitoring",true},{"height",120}}));
        auto before=juce::JSON::toString(d.sequence.toJson());
        check(!d.edit("track.source",object({{"trackId",first},{"source",object({{"category","camera"},{"identity","id"},{"handle",123}})}}),error),"Runtime handle accepted");
        check(before==juce::JSON::toString(d.sequence.toJson()),"Failed operation mutated document");
        edit(d,"track.source",object({{"trackId",first},{"source",juce::var()}}));
        check(d.sequence.tracks[7].source.isVoid(),"Source clear failed");
        edit(d,"track.source",object({{"trackId",first},{"source",object({{"category","display"},{"identity","display:test-monitor"}})}}));
        check(d.sequence.tracks[7].monitoring,"Visual assignment did not enable preview");
        const auto captureSaved=juce::JSON::toString(d.sequence.toJson());Sequence captureReopened;
        check(Sequence::fromJson(juce::JSON::parse(captureSaved),captureReopened,error) && captureReopened.tracks[7].source["identity"].toString()=="display:test-monitor","Visual source assignment did not persist");
        edit(d,"track.source",object({{"trackId",first},{"source",juce::var()}}));check(!d.sequence.tracks[7].monitoring,"Clearing visual source retained preview subscription");
        d.undo.undo();check(d.sequence.tracks[7].monitoring && d.sequence.tracks[7].source["identity"].toString()=="display:test-monitor","Source undo lost assignment/preview state");d.undo.redo();
        for(const auto* type:{"video","audio","image","effects","automation"})
            edit(d,"track.create",object({{"type",type},{"name",type}}));
        const auto autoId=d.sequence.tracks.back().id;
        edit(d,"track.automation",object({{"trackId",autoId},{"automation",object({{"trackId",first},{"clipId","clip-0"},{"effectId","future-effect"},{"parameterId","opacity"},
            {"points",juce::Array<juce::var>{object({{"id","point"},{"seconds",0.0},{"value",0.5},{"shape","bezier"},{"tension",0.2}})}}})}}));
        edit(d,"track.reorder",object({{"trackId",first},{"order",0}}));
        check(d.sequence.tracks.back().targetTrackId==first,"Automation retargeted during reorder");
        check(!d.edit("track.remove",object({{"trackId",first},{"removePlacements",true}}),error),"Referenced target removed");
        const auto saved=juce::JSON::toString(d.sequence.toJson());Sequence reopened;
        check(Sequence::fromJson(juce::JSON::parse(saved),reopened,error),error.toRawUTF8());
        check(saved==juce::JSON::toString(reopened.toJson()),"Save/reopen changed state");
        auto broken=juce::JSON::parse(saved);broken["clips"].getArray()->getReference(0).getDynamicObject()->setProperty("trackId","missing");
        const auto original=juce::JSON::toString(reopened.toJson());
        check(!Sequence::fromJson(broken,reopened,error),"Missing track accepted");
        check(original==juce::JSON::toString(reopened.toJson()),"Failed load changed active sequence");
        edit(d,"split",object({{"clipId","clip-0"},{"time",15*d.sequence.frameSeconds()}}));
        check(d.sequence.owner(d.sequence.clips[1])==first,"Shared split lost ownership");
        edit(d,"track.create",object({{"type","image"},{"name","Empty"}}));
        const auto empty=d.sequence.tracks.back().id;
        edit(d,"track.type",object({{"trackId",empty},{"type","audio"}}));
        check(!d.edit("track.type",object({{"trackId",first},{"type","effects"}}),error),"Invalid occupied type change accepted");
        const creation::timeline::AutomationPoint a{"a",0,0,creation::timeline::AutomationCurveShape::linear,0};
        const creation::timeline::AutomationPoint b{"b",1,1,creation::timeline::AutomationCurveShape::linear,0};
        check(creation::timeline::evaluateAutomationSegment(a,b,0.5)==0.5f,"Shared curve evaluator unavailable");
        Document devices;
        edit(devices,"track.create",object({{"type","video"}}));const auto cameraTrack=devices.sequence.tracks.back().id;
        const auto cameraConfig=object({{"category","camera"},{"identity","camera:unavailable-saved"},{"settings",object({{"width",1280},{"height",720},{"fpsNum",30000},{"fpsDen",1001},{"format","{3231564E-0000-0010-8000-00AA00389B71}"}})}});
        edit(devices,"track.source",object({{"trackId",cameraTrack},{"source",cameraConfig}}));check(devices.sequence.tracks.back().monitoring,"Camera assignment did not enable preview");
        edit(devices,"track.create",object({{"type","audio"}}));const auto microphoneTrack=devices.sequence.tracks.back().id;
        edit(devices,"track.preferences",object({{"trackId",microphoneTrack},{"monitoring",true}}));
        edit(devices,"track.source",object({{"trackId",microphoneTrack},{"source",object({{"category","microphone"},{"identity","microphone:unavailable-saved"},{"settings",object({{"sampleRate",48000},{"channels",2}})}})}}));
        check(!devices.sequence.tracks.back().monitoring,"New microphone assignment enabled speaker monitoring");
        const auto deviceJson=juce::JSON::toString(devices.sequence.toJson());Sequence deviceReopened;
        check(Sequence::fromJson(juce::JSON::parse(deviceJson),deviceReopened,error),error.toRawUTF8());check(deviceJson==juce::JSON::toString(deviceReopened.toJson()),"Device identities/native formats changed on reopen");
        edit(devices,"track.source",object({{"trackId",cameraTrack},{"source",juce::var()}}));devices.undo.undo();check(juce::JSON::toString(devices.sequence.tracks[(size_t)devices.sequence.findTrack(cameraTrack)].source)==juce::JSON::toString(cameraConfig),"Device source undo changed format identity");devices.undo.redo();check(devices.sequence.tracks[(size_t)devices.sequence.findTrack(cameraTrack)].source.isVoid(),"Device source redo failed");
        Document take;take.sequence.tracks.clear();edit(take,"track.create",object({{"type","video"}}));const auto captureTrack=take.sequence.tracks.back().id;
        edit(take,"track.source",object({{"trackId",captureTrack},{"source",object({{"category","display"},{"identity","display:test-recording"}})}}));
        MediaInfo captured;captured.duration=1.000123456;captured.width=64;captured.height=48;captured.hasVideo=true;captured.fpsNum=30;captured.fpsDen=1;
        auto output=object({{"trackId",captureTrack},{"sourceId","display:test-recording"},{"sourceKind","display"},{"id","captured-media"},{"path","Media/captured.mkv"},{"info",captured.toJson()},{"usable",true},{"startSeconds",1.234567891}});
        auto publication=object({{"tracks",juce::Array<juce::var>{output}}});edit(take,"recording.publish",publication);
        check(take.sequence.clips.size()==1 && std::abs(take.sequence.clips[0].startSeconds-1.234567891)<1e-12 && std::abs(take.sequence.clips[0].durationSeconds-captured.duration)<1e-12,"Recording placement was frame-rounded");
        const auto precise=juce::JSON::toString(take.sequence.toJson());Sequence preciseReopened;check(Sequence::fromJson(juce::JSON::parse(precise),preciseReopened,error) && precise==juce::JSON::toString(preciseReopened.toJson()),"Subframe recording timing changed on reopen");
        take.undo.undo();check(take.sequence.clips.empty() && take.sequence.media.size()==1,"Recording undo discarded media instead of only placements");take.undo.redo();check(take.sequence.clips.size()==1,"Recording redo failed");
        edit(take,"recording.publish",publication);check(take.sequence.clips.size()==1 && take.sequence.media.size()==1,"Recording recovery duplicated assets/placements");
        auto legacyTake=take.sequence.toJson();legacyTake.getDynamicObject()->setProperty("version",5);check(!Sequence::fromJson(legacyTake,preciseReopened,error),"Precise timing silently accepted as an older schema");for(auto& clip:*legacyTake["clips"].getArray()){clip.getDynamicObject()->removeProperty("startSeconds");clip.getDynamicObject()->removeProperty("durationSeconds");clip.getDynamicObject()->setProperty("durationFrames",(juce::int64)std::floor(captured.duration/take.sequence.frameSeconds()));}const bool legacyOk=Sequence::fromJson(legacyTake,preciseReopened,error);check(legacyOk,("Schema-5 project compatibility failed: "+error).toRawUTF8());
        edit(take,"track.source",object({{"trackId",captureTrack},{"source",object({{"category","display"},{"identity","display:changed"}})}}));output.getDynamicObject()->setProperty("id","unplaced-media");output.getDynamicObject()->setProperty("path","Media/unplaced.mkv");edit(take,"recording.publish",object({{"tracks",juce::Array<juce::var>{output}}}));check(take.sequence.media.size()==2 && take.sequence.clips.size()==1,"Stale capture target was rebound or its media discarded");
        CaptureQueue bounded(128);bounded.after=0;auto sample=std::make_shared<CaptureSample>();sample->audio=std::make_shared<const std::vector<float>>(32,0.5f);sample->frames=32;sample->rate=8000;sample->channels=1;sample->hostTime=1;bounded.push(sample);bounded.push(sample);check(bounded.overflow && bounded.peakBytes<=128,"Capture queue is not bounded");check(bounded.pop()!=nullptr && bounded.bytes==0,"Capture queue accounting failed");
        runEffectsTests();
        runAutomationTests();
        runTransitionTests();
        std::cout<<"PASS: migration, stable ownership, commands, undo/redo, sources, automation, save/reopen, atomic failure, shared split/curves\n";
        return 0;
    }
    catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
