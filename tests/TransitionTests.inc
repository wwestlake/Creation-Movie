#include "MovieTransitions.h"
static void runTransitionTests()
{
    Document d;d.sequence.fpsNum=30;d.sequence.fpsDen=1;
    Media media;media.id="transition-media";media.versionId="version";media.name="transition-test.mp4";media.path="Media/transition-test.mp4";media.info.hasVideo=media.info.hasAudio=true;media.info.duration=10;d.sequence.media.push_back(media);
    const auto owner=d.sequence.tracks[0].id;
    edit(d,"place",object({{"assetId",media.id},{"trackId",owner},{"start",0},{"duration",2},{"sourceIn",1}}));const auto a=d.sequence.clips.back().id;
    edit(d,"place",object({{"assetId",media.id},{"trackId",owner},{"start",2},{"duration",2},{"sourceIn",1}}));const auto b=d.sequence.clips.back().id;
    const auto clips=juce::JSON::toString(d.sequence.toJson()["clips"]);
    const auto args=object({{"trackId",owner},{"outgoing",a},{"incoming",b},{"type","video.dissolve"},{"start",1.5},{"duration",1.0}});
    edit(d,"transition.create",args);const auto id=d.sequence.transitions[0].id;
    check(clips==juce::JSON::toString(d.sequence.toJson()["clips"]),"Transition changed clip timing/source ranges");
    juce::Image red(juce::Image::ARGB,16,16,true),blue(juce::Image::ARGB,16,16,true);red.clear(red.getBounds(),juce::Colours::red);blue.clear(blue.getBounds(),juce::Colours::blue);
    auto tr=d.sequence.transitions[0];auto middle=blendTransition(tr,2,red,blue).getPixelAt(8,8);check(middle.getRed()==128 && middle.getBlue()==128,"Dissolve midpoint pixels incorrect");
    check(blendTransition(tr,1.5,red,blue).getPixelAt(8,8)==juce::Colours::red && blendTransition(tr,2.5,red,blue).getPixelAt(8,8)==juce::Colours::blue,"Dissolve endpoints incorrect");
    tr.type="video.black";tr.midpoint=.25;check(blendTransition(tr,1.75,red,blue).getPixelAt(8,8)==juce::Colours::black,"Black midpoint not opaque black");
    check(blendTransition(tr,1.625,red,blue).getPixelAt(8,8).getRed()==128,"Black outgoing fade incorrect");
    juce::Image translucentRed(juce::Image::ARGB,16,16,true),translucentBlue(juce::Image::ARGB,16,16,true);
    translucentRed.clear(translucentRed.getBounds(),juce::Colours::red.withAlpha((juce::uint8)128));translucentBlue.clear(translucentBlue.getBounds(),juce::Colours::blue.withAlpha((juce::uint8)64));
    check(blendTransition(tr,tr.start,translucentRed,translucentBlue).getPixelAt(8,8).getAlpha()==128
        && blendTransition(tr,tr.end(),translucentRed,translucentBlue).getPixelAt(8,8).getAlpha()==64
        && blendTransition(tr,tr.start+tr.duration*tr.midpoint,translucentRed,translucentBlue).getPixelAt(8,8)==juce::Colours::black,"Fade-through-black discarded endpoint clip opacity");
    tr.type="video.wipe";for(const auto* direction:{"leftToRight","rightToLeft","topToBottom","bottomToTop"})
    {
        tr.direction=direction;const auto image=blendTransition(tr,2,red,blue);const bool reverse=tr.direction=="rightToLeft" || tr.direction=="bottomToTop";
        check(image.getPixelAt(0,0)==(reverse?juce::Colours::red:juce::Colours::blue) && image.getPixelAt(15,15)==(reverse?juce::Colours::blue:juce::Colours::red),"Wipe direction incorrect");
        tr.softness=.5;const auto edge=blendTransition(tr,2,red,blue).getPixelAt(8,8);check(edge.getRed()>0 && edge.getBlue()>0,"Wipe softness missing");tr.softness=0;
    }
    auto audio=args.clone();audio.getDynamicObject()->setProperty("type","audio.crossfade");audio.getDynamicObject()->setProperty("curve","equalPower");edit(d,"transition.create",audio);
    tr=d.sequence.transitions.back();auto gains=transitionAudioGains(tr,2);check(std::abs(gains.first*gains.first+gains.second*gains.second-1)<1e-12,"Equal-power energy law incorrect");
    tr.curve="linear";gains=transitionAudioGains(tr,2);check(gains.first==.5 && gains.second==.5,"Linear gains incorrect");
    double maxJump=0,previous=0;for(int sample=0;sample<=48000;++sample){const auto g=transitionAudioGains(tr,1.5+(double)sample/48000);const double value=.2*g.first-.1*g.second;if(sample)maxJump=std::max(maxJump,std::abs(value-previous));previous=value;}
    check(maxJump<.00001,"Linear transition introduced a sample discontinuity");
    edit(d,"transition.update",object({{"transitionId",id},{"start",1.6},{"duration",.8},{"curve","sCurve"}}));check(d.sequence.transitions[0].id==id && d.sequence.transitions[0].start==1.6,"Numerical edit changed identity");d.undo.undo();check(d.sequence.transitions[0].start==1.5,"Transition edit undo failed");d.undo.redo();
    edit(d,"transition.update",object({{"transitionId",id},{"start",1.1666666666666667},{"duration",.9}}));
    const auto saved=juce::JSON::toString(d.sequence.toJson());Sequence restored;juce::String error;check(Sequence::fromJson(juce::JSON::parse(saved),restored,error) && saved==juce::JSON::toString(restored.toJson()),"Transitions did not roundtrip exactly");
    check(!d.edit("transition.create",args,error),"Competing same-domain transition accepted");check(saved==juce::JSON::toString(d.sequence.toJson()),"Invalid transition mutated document");
    check(!d.edit("transition.update",object({{"transitionId",id},{"start",0.0},{"duration",3.0}}),error),"Missing incoming handles accepted");
    check(!d.edit("transition.update",object({{"transitionId",id},{"duration",-1}}),error),"Negative transition duration accepted");
    check(!d.edit("transition.update",object({{"transitionId",id},{"unrecognizedParameter",1}}),error),"Unknown transition property silently discarded");
    auto malformed=d.sequence.toJson();malformed["transitions"][0].getDynamicObject()->setProperty("type","video.future");
    const auto previousRestored=juce::JSON::toString(restored.toJson());
    check(!Sequence::fromJson(malformed,restored,error) && previousRestored==juce::JSON::toString(restored.toJson()),"Invalid transition load modified the destination");
    malformed=d.sequence.toJson();malformed.getDynamicObject()->setProperty("version",4);
    check(!Sequence::fromJson(malformed,restored,error),"Older schema silently discarded transitions");
    check(!d.edit("move",object({{"clipId",b},{"start",3.0}}),error),"Move silently invalidated transition");
    edit(d,"track.reorder",object({{"trackId",owner},{"order",2}}));check(d.sequence.transitions[0].trackId==owner,"Track reorder lost ownership");
    edit(d,"remove",object({{"clipId",a}}));check(d.sequence.transitions.empty(),"Clip deletion retained dangling transitions");d.undo.undo();check(d.sequence.transitions.size()==2 && d.sequence.transitions[0].id==id,"Clip removal undo lost transitions");
    Document edges;edges.sequence=d.sequence;edges.sequence.transitions.clear();edges.sequence.clips[(size_t)edges.sequence.findClip(b)].sourceStartSeconds=2;
    auto edgeArgs=args.clone();edgeArgs.getDynamicObject()->setProperty("start",0.0);edgeArgs.getDynamicObject()->setProperty("duration",4.0);edit(edges,"transition.create",edgeArgs);
    check(edges.sequence.transitions[0].active(0) && !edges.sequence.transitions[0].active(edges.sequence.duration()),"Sequence-boundary transition ownership incorrect");
    auto old=d.sequence.toJson();old.getDynamicObject()->setProperty("version",4);old.getDynamicObject()->removeProperty("transitions");check(Sequence::fromJson(old,restored,error) && restored.transitions.empty(),"Version-4 transition migration failed");
    std::cout<<"PASS: transition types/pixels/softness/curves, audio gains/sample continuity, handles, overlap rejection, timing preservation, precise edits, identity, undo/cascade and version-4/5 persistence\n";
}
