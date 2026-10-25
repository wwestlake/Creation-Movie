#pragma once
#include "MovieDocument.h"
namespace movie
{
const EffectParameter* automationParameter(const Sequence&,const Track&,const Effect** effect=nullptr);
EffectAutomation resolveAutomation(const Sequence&);
std::vector<Effect> evaluatedEffects(const std::vector<Effect>&,const EffectAutomation&,double seconds);
bool editAutomation(Sequence&,const juce::String& operation,const juce::var&,juce::String& error);
}
