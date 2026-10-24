#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <map>
namespace movie
{
struct EffectParameter
{
    juce::String id,name,units,kind {"number"};
    double initial=0,minimum=0,maximum=1,step=0.01;
    bool automatable=true;
    juce::StringArray choices;
};
struct EffectDefinition
{
    juce::String id,name,domain;
    std::vector<EffectParameter> parameters;
};
struct Effect
{
    juce::String id,type,ownerId;
    bool enabled=true,bypassed=false;
    std::map<juce::String,double> values;
    double value(const juce::String&) const;
    juce::var toJson() const;
    static bool fromJson(const juce::var&,Effect&,juce::String&);
};
const std::vector<EffectDefinition>& effectCatalog();
const EffectDefinition* effectDefinition(const juce::String&);
juce::var effectsCatalogJson();
bool compatibleEffect(const juce::String& trackType,const juce::String& effectType);
bool parseEffects(const juce::var&,const juce::String& owner,const juce::String& trackType,std::vector<Effect>&,juce::String&);
// Same parameter map is the future automation override boundary. No UI widgets
// own parameter identities or effect mathematics.
void processVideoEffects(juce::Image&,const std::vector<Effect>&);
class AudioEffects
{
public:
    AudioEffects()=default;
    AudioEffects(const AudioEffects&)=delete;
    AudioEffects& operator=(const AudioEffects&)=delete;
    AudioEffects(AudioEffects&&) noexcept=default;
    AudioEffects& operator=(AudioEffects&&) noexcept=default;
    void reset();
    void process(float* stereo,int count,int sampleRate,double localTime,double duration,const std::vector<Effect>&);
private:
    struct State {juce::IIRFilter left,right;int rate=0;double cutoff=0,mode=-1;};
    std::map<juce::String,std::unique_ptr<State>> states;
};
}
