#include "MovieDocument.h"
#include <set>

namespace movie
{
namespace
{
bool oneOf(const juce::String& value, const char* choices)
{
    return juce::StringArray::fromTokens(choices, "|", "").contains(value);
}
bool sourceValid(const Track& t, juce::String& error)
{
    if (t.source.isVoid()) return true;
    auto fail = [&] { error = "Invalid persistent source configuration for track type."; return false; };
    if (!t.source.isObject()) return fail();
    const auto category = t.source["category"].toString();
    if (t.type == "video" && !oneOf(category, "media|display|window|camera")) return fail();
    if (t.type == "audio" && !oneOf(category, "media|microphone|systemAudio")) return fail();
    if (t.type == "image" && category != "media") return fail();
    if (t.type == "effects" || t.type == "automation") return fail();
    // Closed configuration vocabulary: no arbitrary handles or active-state fields.
    for (const auto& property : t.source.getDynamicObject()->getProperties())
        if (!oneOf(property.name.toString(), "category|identity|settings")) return fail();
    if (!t.source["identity"].isString() || t.source["identity"].toString().trim().isEmpty()) return fail();
    const auto settings = t.source["settings"];
    if (!settings.isVoid())
    {
        if (!settings.isObject()) return fail();
        for (const auto& p : settings.getDynamicObject()->getProperties())
        {
            if (!oneOf(p.name.toString(), "width|height|fpsNum|fpsDen|sampleRate|channels|channel")) return fail();
            if (!finiteNumber(p.value) || (double)p.value < 0 || (double)p.value > 1000000) return fail();
        }
    }
    return true;
}
}
juce::var Track::toJson() const
{
    juce::Array<juce::var> curve;
    for (const auto& p : points)
        curve.add(object({{"id",p.id},{"seconds",p.seconds},{"value",p.value},
            {"shape",creation::timeline::toStorageToken(p.shape)},{"tension",p.tension}}));
    return object({{"id",id},{"name",name},{"type",type},{"enabled",enabled},{"height",heightPixels},
        {"armed",armed},{"monitoring",monitoring},{"legacyMixed",legacyMixed},{"source",source},
        {"automation",object({{"trackId",targetTrackId},{"clipId",targetClipId},{"effectId",targetEffectId},
            {"parameterId",parameterId},{"points",curve}})}});
}
bool Track::fromJson(const juce::var& v, Track& result, juce::String& error)
{
    auto fail = [&](const char* text) { error = text; return false; };
    Track t;
    t.id=v["id"].toString(); t.name=v["name"].toString(); t.type=v["type"].toString();
    if (t.id.isEmpty() || t.name.trim().isEmpty() || t.name.length()>256
        || !oneOf(t.type,"video|audio|image|effects|automation")) return fail("Invalid track identity, name or type.");
    for (const auto* key : {"enabled","armed","monitoring","legacyMixed"})
        if (!v[key].isBool()) return fail("Track flags must be boolean.");
    if (!v["height"].isInt() || (int)v["height"]<44 || (int)v["height"]>512) return fail("Invalid track height.");
    t.enabled=v["enabled"]; t.armed=v["armed"]; t.monitoring=v["monitoring"]; t.legacyMixed=v["legacyMixed"];
    t.heightPixels=v["height"]; t.source=v["source"].clone();
    if ((t.armed || t.monitoring) && t.type!="audio" && t.type!="video") return fail("Only audio/video tracks support arm/monitor preferences.");
    if (!sourceValid(t,error)) return false;
    const auto automation=v["automation"];
    t.targetTrackId=automation["trackId"].toString(); t.targetClipId=automation["clipId"].toString();
    t.targetEffectId=automation["effectId"].toString(); t.parameterId=automation["parameterId"].toString();
    const auto* curve=automation["points"].getArray();
    if (!curve || curve->size()>100000) return fail("Invalid automation points.");
    if (t.type!="automation" && (curve->size()>0 || t.targetTrackId.isNotEmpty() || t.targetClipId.isNotEmpty()
        || t.targetEffectId.isNotEmpty() || t.parameterId.isNotEmpty())) return fail("Automation data requires an automation track.");
    std::set<juce::String> ids;
    double previous=-1;
    for (const auto& e : *curve)
    {
        creation::timeline::AutomationPoint p;
        p.id=e["id"].toString();
        if (p.id.isEmpty() || !ids.insert(p.id).second || !finiteNumber(e["seconds"]) || !finiteNumber(e["value"])
            || !finiteNumber(e["tension"]) || !oneOf(e["shape"].toString(),"linear|step|sCurve|power|bezier")) return fail("Invalid automation point.");
        p.seconds=e["seconds"]; p.value=(float)(double)e["value"]; p.tension=(float)(double)e["tension"];
        if (p.seconds<previous || p.seconds<0 || p.seconds>86400 || p.value<0 || p.value>1 || p.tension<-1 || p.tension>1) return fail("Automation point outside bounds.");
        previous=p.seconds;
        p.shape=creation::timeline::automationCurveShapeFromStorageToken(e["shape"].toString());
        t.points.push_back(p);
    }
    result=std::move(t); return true;
}
Sequence::Sequence()
{
    for (int i=0;i<4;++i)
    {
        Track t; t.id=juce::Uuid().toString(); t.type=i<2?"video":"audio";
        t.name=(i<2?"Video ":"Audio ")+juce::String(i%2+1);
        tracks.push_back(t);
    }
}
int Sequence::findTrack(const juce::String& id) const
{
    for (size_t i=0;i<tracks.size();++i) if (tracks[i].id==id) return (int)i;
    return -1;
}
juce::String Sequence::owner(const Clip& c) const
{
    const auto found=clipTracks.find(c.id);
    if (found!=clipTracks.end()) return found->second;
    return juce::isPositiveAndBelow(c.trackIndex,(int)tracks.size())?tracks[(size_t)c.trackIndex].id:juce::String();
}
bool Sequence::trackEnabled(const Clip& c) const
{
    const auto i=findTrack(owner(c)); return i>=0 && tracks[(size_t)i].enabled;
}
void Sequence::resolveTrackIndices()
{
    for (auto& c : clips) c.trackIndex=findTrack(owner(c));
}
}
