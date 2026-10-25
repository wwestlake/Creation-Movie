#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <creation/timeline/TimelineTypes.h>
namespace movie
{
struct Sequence;
struct Transition
{
    juce::String id,trackId,outgoing,incoming,type{"video.dissolve"},curve{"linear"},direction{"leftToRight"};
    double start=0,duration=1,softness=0,midpoint=.5,tension=0;
    double end()const{return start+duration;}
    bool audio()const{return type=="audio.crossfade";}
    bool active(double t)const{return t>=start && t<end();}
    juce::var toJson(bool includeEnd=true)const;
    static bool fromJson(const juce::var&,Transition&,juce::String&);
};
juce::String transitionName(const juce::String& type);
juce::var transitionCatalog();
bool validateTransitions(const Sequence&,juce::String&);
bool editTransition(Sequence&,const juce::String&,const juce::var&,juce::String&);
double transitionProgress(const Transition&,double seconds);
std::pair<double,double> transitionAudioGains(const Transition&,double seconds);
juce::Image blendTransition(const Transition&,double seconds,const juce::Image& outgoing,const juce::Image& incoming);
}
