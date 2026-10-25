#include "MovieAutomation.h"
static void runAutomationTests()
{
    Document d;const auto owner=d.sequence.tracks[2].id;
    edit(d,"effect.add",object({{"trackId",owner},{"type","audio.gain"}}));const auto effect=d.sequence.tracks[2].effects[0].id;
    edit(d,"automation.create",object({{"trackId",owner},{"effectId",effect},{"parameterId","gain"}}));
    const auto lane=d.sequence.tracks.back().id,first=d.sequence.tracks.back().points[0].id;
    edit(d,"automation.point",object({{"laneId",lane},{"pointId",first},{"seconds",0.0},{"value",0.0}}));
    edit(d,"automation.point",object({{"laneId",lane},{"seconds",1.0},{"value",2.0}}));
    auto curves=resolveAutomation(d.sequence);const auto& curve=curves.at(effect).at("gain");
    check(curve.at(-1)==0 && curve.at(0)==0 && curve.at(.5)==1 && curve.at(1)==2 && curve.at(5)==2,"Linear endpoints/interpolation incorrect");
    std::vector<float> whole(96000,.2f),split(96000,.2f);AudioEffects a,b;
    a.process(whole.data(),48000,48000,0,1,d.sequence.tracks[2].effects,&curves,0);
    for(int n=0;n<48000;){const int count=std::min(257,48000-n);b.process(split.data()+n*2,count,48000,(double)n/48000,1,d.sequence.tracks[2].effects,&curves,(double)n/48000);n+=count;}
    double error=0;for(size_t i=0;i<whole.size();++i)error=std::max(error,(double)std::abs(whole[i]-split[i]));
    check(error<1e-6 && whole[0]==0 && std::abs(whole[48000]-.2f)<1e-6,"Sample-accurate gain automation or block continuity failed");
    // Reset/wrap the timeline: evaluation is absolute, not dependent on a cursor.
    std::vector<float> restart(200,.2f);b.reset();b.process(restart.data(),100,48000,0,1,d.sequence.tracks[2].effects,&curves,0);
    check(std::equal(restart.begin(),restart.end(),whole.begin()),"Loop/restart changed gain automation");
    edit(d,"automation.point",object({{"laneId",lane},{"pointId",first},{"seconds",0.0},{"value",0.0},{"shape","step"}}));
    curves=resolveAutomation(d.sequence);check(curves.at(effect).at("gain").at(.999)==0 && curves.at(effect).at("gain").at(1)==2,"Step transition incorrect");
    const auto second=d.sequence.tracks.back().points.back().id;
    edit(d,"automation.point",object({{"laneId",lane},{"pointId",second},{"seconds",2.0},{"value",3.0}}));
    d.undo.undo();check(d.sequence.tracks.back().points.back().seconds==1,"Keyframe move undo failed");d.undo.redo();
    edit(d,"automation.removePoint",object({{"laneId",lane},{"pointId",second}}));d.undo.undo();check(d.sequence.tracks.back().points.size()==2,"Keyframe removal undo failed");
    edit(d,"automation.visible",object({{"laneId",lane},{"visible",false}}));check(!d.sequence.tracks.back().automationVisible && !resolveAutomation(d.sequence).empty(),"Hiding disabled automation");
    edit(d,"effect.add",object({{"trackId",owner},{"type","audio.balance"}}));edit(d,"effect.reorder",object({{"trackId",owner},{"effectId",effect},{"order",1}}));
    edit(d,"track.reorder",object({{"trackId",owner},{"order",0}}));check(resolveAutomation(d.sequence).count(effect)==1,"Reorder broke stable automation binding");
    edit(d,"effect.bypass",object({{"trackId",owner},{"effectId",effect},{"value",true}}));curves=resolveAutomation(d.sequence);
    std::vector<float> bypass(200,.2f);a.process(bypass.data(),100,48000,0,1,d.sequence.tracks[0].effects,&curves,0);check(bypass[0]==.2f,"Bypass altered samples");
    juce::String failure;check(!d.edit("effect.remove",object({{"trackId",owner},{"effectId",effect}}),failure),"Automated effect removal lost its curve");
    const auto saved=juce::JSON::toString(d.sequence.toJson());Sequence restored;check(Sequence::fromJson(juce::JSON::parse(saved),restored,failure),"Automation reopen failed");
    check(saved==juce::JSON::toString(restored.toJson()),"Automation identities/values/visibility changed on reopen");
    check(!d.edit("automation.point",object({{"laneId",lane},{"seconds",2.0},{"value",99.0}}),failure),"Out-of-range keyframe accepted");
    check(saved==juce::JSON::toString(d.sequence.toJson()),"Invalid keyframe mutated document");
    const auto video=d.sequence.tracks[1].id;edit(d,"effect.add",object({{"trackId",video},{"type","video.opacity"}}));const auto opacity=d.sequence.tracks[1].effects.back().id;
    edit(d,"automation.create",object({{"trackId",video},{"effectId",opacity},{"parameterId","opacity"}}));const auto vl=d.sequence.tracks.back().id,vp=d.sequence.tracks.back().points[0].id;
    edit(d,"automation.point",object({{"laneId",vl},{"pointId",vp},{"seconds",0.0},{"value",0.0}}));edit(d,"automation.point",object({{"laneId",vl},{"seconds",1.0},{"value",1.0}}));
    curves=resolveAutomation(d.sequence);auto chain=evaluatedEffects(d.sequence.tracks[1].effects,curves,.5);juce::Image frame(juce::Image::ARGB,16,16,true);frame.clear(frame.getBounds(),juce::Colours::white);processVideoEffects(frame,chain);
    check(std::abs((int)frame.getPixelAt(8,8).getAlpha()-128)<=1,"Automated opacity did not process pixels");
    // Choice parameters step even if saved points request a continuous curve.
    const auto* def=effectDefinition("audio.filter");ParameterCurve choice{&d.sequence.tracks.back().points,&def->parameters[1]};check(choice.at(.5)==0 && choice.at(1)==1,"Choice interpolation was not stepped");
    // Moving cutoff uses actual filter samples at both common device rates.
    Effect filter;filter.id="swept-filter";filter.type="audio.filter";filter.ownerId=owner;
    std::vector<creation::timeline::AutomationPoint> sweep{{"lo",0,0,creation::timeline::AutomationCurveShape::linear,0},{"hi",1,.4f,creation::timeline::AutomationCurveShape::linear,0}};
    EffectAutomation filterCurves;filterCurves[filter.id]["cutoff"]={&sweep,&effectDefinition(filter.type)->parameters[0]};
    double filterMs=0;
    for(int rate:{44100,48000})
    {
        std::vector<float> x((size_t)rate*2),y;for(int i=0;i<rate;++i)x[(size_t)i*2]=x[(size_t)i*2+1]=(float)(.1*std::sin(juce::MathConstants<double>::twoPi*3000*i/rate));y=x;
        AudioEffects one,many;const auto started=juce::Time::getMillisecondCounterHiRes();one.process(x.data(),rate,rate,0,1,{filter},&filterCurves,0);filterMs=juce::Time::getMillisecondCounterHiRes()-started;
        for(int i=0;i<rate;){const int n=std::min(257,rate-i);many.process(y.data()+i*2,n,rate,(double)i/rate,1,{filter},&filterCurves,(double)i/rate);i+=n;}
        double delta=0;for(size_t i=0;i<x.size();++i){check(std::isfinite(x[i]),"Automated filter produced non-finite audio");delta=std::max(delta,(double)std::abs(x[i]-y[i]));}
        check(delta<1e-5,"Automated filter depends on block size");
        filter.values["mode"]=1;AudioEffects high;high.process(y.data(),rate,rate,0,1,{filter},&filterCurves,0);for(float sample:y)check(std::isfinite(sample),"Automated high-pass produced non-finite audio");
    }
    // Crossing opposing crop keys is bounded only in the evaluated surface state.
    Effect transform;transform.id="crop";transform.type="video.transform";transform.ownerId=video;
    const auto* td=effectDefinition(transform.type);EffectAutomation crop;crop[transform.id]["left"]={&sweep,&td->parameters[0]};crop[transform.id]["right"]={&sweep,&td->parameters[1]};
    sweep.back().value=1;const auto cropped=evaluatedEffects({transform},crop,1);check(cropped[0].value("left")+cropped[0].value("right")<1,"Crossing crop keys left an invalid region");
    std::cout<<"PROFILE: 1s stereo 48kHz sample-automated filter "<<filterMs<<" ms\n";
    std::cout<<"PASS: automation keyframes, interpolation, actual samples/pixels, block continuity ("<<error<<"), loop/restart, identities, bypass/removal, undo/redo, visibility, persistence and atomic validation\n";
}
