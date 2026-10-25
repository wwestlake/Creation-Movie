#include "MovieAutomation.h"
namespace movie
{
const EffectParameter* automationParameter(const Sequence& s,const Track& lane,const Effect** effect)
{
    if(lane.type!="automation" || lane.targetClipId.isNotEmpty())return nullptr;
    const int ti=s.findTrack(lane.targetTrackId);if(ti<0)return nullptr;
    for(const auto& e:s.tracks[(size_t)ti].effects)if(e.id==lane.targetEffectId)
        if(const auto* definition=effectDefinition(e.type))for(const auto& p:definition->parameters)
            if(p.id==lane.parameterId && p.automatable){if(effect)*effect=&e;return &p;}
    return nullptr;
}
EffectAutomation resolveAutomation(const Sequence& s)
{
    EffectAutomation result;
    // Legacy files may contain duplicate/unresolved dormant targets. Ignore unresolved
    // targets; stable lane identity wins duplicates independently of display order.
    std::vector<const Track*> lanes;
    for(const auto& lane:s.tracks)if(lane.enabled && !lane.points.empty() && automationParameter(s,lane))lanes.push_back(&lane);
    std::sort(lanes.begin(),lanes.end(),[](const Track* a,const Track* b){return a->id<b->id;});
    for(const auto* lane:lanes)
        result[lane->targetEffectId].emplace(lane->parameterId,ParameterCurve{&lane->points,automationParameter(s,*lane)});
    return result;
}
std::vector<Effect> evaluatedEffects(const std::vector<Effect>& chain,const EffectAutomation& automation,double seconds)
{
    auto evaluated=chain;
    for(auto& e:evaluated)
    {
        const auto found=automation.find(e.id);if(found==automation.end())continue;
        for(const auto& p:found->second)e.values[p.first]=p.second.at(seconds);
        if(e.type=="video.transform")
        {
            // Coupled crop edges may cross during interpolation. Preserve their ratio
            // while retaining a small non-empty region, without changing saved keys.
            for(const auto& pair:std::initializer_list<std::pair<const char*,const char*>>{{"left","right"},{"top","bottom"}})
            {const double a=e.value(pair.first),b=e.value(pair.second),sum=a+b;if(sum>=.999){e.values[pair.first]=a*.999/sum;e.values[pair.second]=b*.999/sum;}}
        }
    }
    return evaluated;
}
bool editAutomation(Sequence& s,const juce::String& op,const juce::var& a,juce::String& error)
{
    auto fail=[&](const char* why){error=why;return false;};
    if(op=="create" || op=="bind")
    {
        Track lane;int index=-1;
        if(op=="bind")
        {
            index=s.findTrack(a["laneId"].toString());if(index<0 || s.tracks[(size_t)index].type!="automation")return fail("Choose an automation lane.");
            lane=s.tracks[(size_t)index];
        }
        else{lane.id=juce::Uuid().toString();lane.type="automation";lane.heightPixels=120;}
        lane.targetTrackId=a["trackId"].toString();lane.targetEffectId=a["effectId"].toString();lane.parameterId=a["parameterId"].toString();lane.targetClipId={};
        const Effect* effect=nullptr;const auto* parameter=automationParameter(s,lane,&effect);
        if(!parameter)return fail("Choose an existing automatable effect parameter on its owning track.");
        for(const auto& other:s.tracks)if(other.id!=lane.id && other.type=="automation" && other.targetEffectId==lane.targetEffectId && other.parameterId==lane.parameterId)
            return fail("This parameter already has an automation lane.");
        if(op=="create")
        {
            lane.name=effectDefinition(effect->type)->name+" / "+parameter->name;
            creation::timeline::AutomationPoint point;point.id=juce::Uuid().toString();
            point.value=(float)((effect->value(parameter->id)-parameter->minimum)/(parameter->maximum-parameter->minimum));lane.points.push_back(point);
            s.tracks.push_back(std::move(lane));
        }
        else s.tracks[(size_t)index]=std::move(lane);
        return true;
    }
    const int ti=s.findTrack(a["laneId"].toString());if(ti<0 || s.tracks[(size_t)ti].type!="automation")return fail("Choose an automation lane.");
    auto& lane=s.tracks[(size_t)ti];
    if(op=="visible"){if(!a["visible"].isBool())return fail("Visibility requires a boolean.");lane.automationVisible=a["visible"];return true;}
    const auto* parameter=automationParameter(s,lane);
    if(!parameter)return fail("Automation target is unresolved. Bind an existing effect parameter first.");
    const auto id=a["pointId"].toString();auto found=std::find_if(lane.points.begin(),lane.points.end(),[&](const auto& p){return p.id==id;});
    if(op=="removePoint")
    {if(found==lane.points.end())return fail("Unknown keyframe.");lane.points.erase(found);return true;}
    if(op!="point")return fail("Unknown automation operation.");
    if(!id.isEmpty() && found==lane.points.end())return fail("Unknown keyframe.");
    if(!finiteNumber(a["seconds"]) || !finiteNumber(a["value"]))return fail("Keyframe time and value must be finite numbers.");
    const double seconds=a["seconds"],value=a["value"];
    if(seconds<0 || seconds>86400 || value<parameter->minimum || value>parameter->maximum
        || (parameter->kind=="choice" && std::floor(value)!=value))return fail("Keyframe time or parameter value is outside its range.");
    for(const auto& p:lane.points)if(p.id!=id && std::abs(p.seconds-seconds)<1e-9)return fail("A keyframe already exists at that time.");
    creation::timeline::AutomationPoint point;if(found!=lane.points.end())point=*found;else point.id=juce::Uuid().toString();
    point.seconds=seconds;point.value=(float)((value-parameter->minimum)/(parameter->maximum-parameter->minimum));
    if(a.hasProperty("shape"))
    {
        const auto shape=a["shape"].toString();
        if(!juce::StringArray{"linear","step","sCurve","power","bezier"}.contains(shape))return fail("Unknown interpolation shape.");
        point.shape=creation::timeline::automationCurveShapeFromStorageToken(shape);
    }
    if(a.hasProperty("tension")){if(!finiteNumber(a["tension"]) || (double)a["tension"]<-1 || (double)a["tension"]>1)return fail("Tension must be -1 to 1.");point.tension=(float)(double)a["tension"];}
    if(found==lane.points.end())lane.points.push_back(point);else *found=point;
    std::sort(lane.points.begin(),lane.points.end(),[](const auto& x,const auto& y){return x.seconds<y.seconds;});return true;
}
}
