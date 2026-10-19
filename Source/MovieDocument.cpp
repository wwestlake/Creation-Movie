#include "MovieDocument.h"
#include <creation/timeline/TimelineEditOps.h>
#include <set>

namespace movie
{
juce::var object(std::initializer_list<std::pair<juce::Identifier, juce::var>> fields)
{
    auto* o = new juce::DynamicObject();
    for (const auto& [key, value] : fields) o->setProperty(key, value);
    return juce::var(o);
}
bool finiteNumber(const juce::var& v) { return (v.isDouble() || v.isInt() || v.isInt64()) && std::isfinite((double) v); }
juce::var MediaInfo::toJson() const
{
    return object({{"duration",duration},{"width",width},{"height",height},{"fpsNum",fpsNum},{"fpsDen",fpsDen},
        {"sampleRate",sampleRate},{"channels",channels},{"hasVideo",hasVideo},{"hasAudio",hasAudio},{"still",still},
        {"pixelAspect",pixelAspect},{"videoCodec",videoCodec},{"audioCodec",audioCodec},{"colourSpace",colourSpace},{"transfer",transfer}});
}
MediaInfo MediaInfo::fromJson(const juce::var& v)
{
    MediaInfo m;
    m.duration=v["duration"]; m.width=v["width"]; m.height=v["height"]; m.fpsNum=v["fpsNum"];
    m.fpsDen=v.getProperty("fpsDen",1); m.sampleRate=v["sampleRate"]; m.channels=v["channels"];
    m.hasVideo=v["hasVideo"]; m.hasAudio=v["hasAudio"]; m.still=v["still"];
    m.pixelAspect=v.getProperty("pixelAspect",1.0); m.videoCodec=v["videoCodec"].toString();
    m.audioCodec=v["audioCodec"].toString(); m.colourSpace=v["colourSpace"].toString(); m.transfer=v["transfer"].toString();
    return m;
}
double Sequence::duration() const { double d=0; for(const auto& c:clips) d=std::max(d,c.startSeconds+c.durationSeconds); return d; }
const Media* Sequence::findMedia(const juce::String& id) const { for(const auto& m:media) if(m.id==id) return &m; return nullptr; }
int Sequence::findClip(const juce::String& id) const { for(size_t i=0;i<clips.size();++i) if(clips[i].id==id) return (int)i; return -1; }
juce::var Sequence::toJson() const
{
    juce::Array<juce::var> a,c;
    for(const auto& m:media) a.add(object({{"id",m.id},{"versionId",m.versionId},{"name",m.name},{"path",m.path},{"info",m.info.toJson()}}));
    for(const auto& x:clips)
    {
        auto params=juce::var(new juce::DynamicObject());
        params.getDynamicObject()->getProperties()=x.videoParams;
        c.add(object({{"id",x.id},{"assetId",x.assetId},{"name",x.displayName},{"track",x.trackIndex},
            {"kind",x.kind==creation::timeline::ClipKind::audio ? "audio":"video"},
            {"startFrame",(juce::int64)std::llround(x.startSeconds/frameSeconds())},
            {"durationFrames",(juce::int64)std::llround(x.durationSeconds/frameSeconds())},
            {"sourceIn",x.sourceStartSeconds},{"sourceDuration",x.sourceDurationSeconds},{"params",params}}));
    }
    return object({{"schema","djehuti-movie-sequence"},{"version",2},{"name",name},{"width",width},{"height",height},
        {"fpsNum",fpsNum},{"fpsDen",fpsDen},{"media",a},{"clips",c}});
}
bool Sequence::fromJson(const juce::var& v, Sequence& result, juce::String& error)
{
    auto fail=[&](const juce::String& s){ error=s; return false; };
    if(v["schema"].toString()!="djehuti-movie-sequence" || (int)v["version"]!=2) return fail("Unsupported Movie sequence schema.");
    Sequence s; s.name=v["name"].toString(); s.width=v["width"];s.height=v["height"];s.fpsNum=v["fpsNum"];s.fpsDen=v["fpsDen"];
    if(s.width<16 || s.width>8192 || s.height<16 || s.height>8192 || s.fpsNum<1 || s.fpsDen<1
        || (double)s.fpsNum/s.fpsDen>240 || (double)s.fpsNum/s.fpsDen<1) return fail("Invalid sequence dimensions or frame rate.");
    const auto* a=v["media"].getArray(); const auto* c=v["clips"].getArray();
    if(!a || !c || a->size()>10000 || c->size()>100000) return fail("Invalid media or clip list.");
    std::set<juce::String> ids;
    for(const auto& e:*a)
    {
        Media m{e["id"].toString(),e["versionId"].toString(),e["name"].toString(),e["path"].toString(),MediaInfo::fromJson(e["info"])};
        if(m.id.isEmpty() || !ids.insert(m.id).second || !m.path.startsWith("Media/") || m.path.contains("..")
            || !std::isfinite(m.info.duration) || m.info.duration<=0 || m.info.width<0 || m.info.height<0
            || m.info.width>32768 || m.info.height>32768 || !std::isfinite(m.info.pixelAspect) || m.info.pixelAspect<=0)
            return fail("Invalid media identity, path or metadata.");
        s.media.push_back(m);
    }
    ids.clear();
    for(const auto& e:*c)
    {
        Clip x; x.id=e["id"].toString();x.assetId=e["assetId"].toString();x.displayName=e["name"].toString();x.trackIndex=e["track"];
        x.kind=e["kind"].toString()=="audio"?creation::timeline::ClipKind::audio:creation::timeline::ClipKind::video;
        if(!finiteNumber(e["startFrame"]) || !finiteNumber(e["durationFrames"]) || !finiteNumber(e["sourceIn"])) return fail("Invalid clip timing.");
        x.startSeconds=(double)e["startFrame"]*s.frameSeconds();x.durationSeconds=(double)e["durationFrames"]*s.frameSeconds();
        x.sourceStartSeconds=e["sourceIn"];x.sourceDurationSeconds=e["sourceDuration"];
        if(auto* p=e["params"].getDynamicObject()) x.videoParams=p->getProperties();
        const auto* m=s.findMedia(x.assetId);
        const bool title=x.videoParams["title"].toString().isNotEmpty();
        if(x.id.isEmpty() || !ids.insert(x.id).second || x.trackIndex<0 || x.trackIndex>7 || x.startSeconds<0
            || x.startSeconds>86400 || x.durationSeconds<s.frameSeconds() || x.durationSeconds>86400 || x.sourceStartSeconds<0
            || (!title && (!m || (!m->info.still && x.sourceStartSeconds+x.durationSeconds>m->info.duration+0.00001))))
            return fail("Clip is outside its source or sequence limits.");
        const auto gain=x.videoParams.getWithDefault("gain",1.0);
        const auto opacity=x.videoParams.getWithDefault("opacity",1.0);
        if(!finiteNumber(gain) || (double)gain<0 || (double)gain>4 || !finiteNumber(opacity) || (double)opacity<0 || (double)opacity>1)
            return fail("Invalid clip gain or opacity.");
        if(m) x.assetVersionId=m->versionId;
        s.clips.push_back(std::move(x));
    }
    result=std::move(s); return true;
}
struct Document::Edit : juce::UndoableAction
{
    Document& d; Sequence before, after;
    Edit(Document& doc,Sequence a,Sequence b):d(doc),before(std::move(a)),after(std::move(b)){}
    bool apply(const Sequence& s){d.sequence=s;d.dirty=true;if(d.changed)d.changed();return true;}
    bool perform() override{return apply(after);} bool undo() override{return apply(before);}
    int getSizeInUnits() override{return (int)(before.clips.size()+after.clips.size()+1);}
};
void Document::replace(Sequence replacement,bool clean){sequence=std::move(replacement);undo.clearUndoHistory();dirty=!clean;if(changed)changed();}
void Document::restore(const juce::var& v){Sequence s;juce::String e;if(Sequence::fromJson(v,s,e)){s.projectId=sequence.projectId;undo.beginNewTransaction("Restore agent edit");undo.perform(new Edit(*this,sequence,std::move(s)));}}
bool Document::edit(const juce::String& command,const juce::var& a,juce::String& error)
{
    auto s=sequence;
    const auto index=s.findClip(a["clipId"].toString());
    auto fail=[&](const juce::String& why){error=why;return false;};
    if(command=="place" || command=="title")
    {
        Clip c;c.id=juce::Uuid().toString();c.kind=creation::timeline::ClipKind::video;
        c.startSeconds=s.snap((double)a.getProperty("start",s.duration()));c.trackIndex=a.getProperty("track",command=="title"?1:0);
        c.sourceStartSeconds=a.getProperty("sourceIn",0.0);
        if(command=="title")
        {
            c.displayName=a["text"].toString();c.videoParams.set("title",c.displayName);
            if(c.displayName.trim().isEmpty() || c.displayName.length()>2000) return fail("Enter title text (up to 2000 characters).");
            c.durationSeconds=s.snap((double)a.getProperty("duration",3.0));
        }
        else
        {
            auto* m=s.findMedia(a["assetId"].toString());if(!m)return fail("Unknown media asset.");
            c.assetId=m->id;c.assetVersionId=m->versionId;c.displayName=m->name;c.sourceDurationSeconds=m->info.duration;
            c.kind=m->info.hasVideo?creation::timeline::ClipKind::video:creation::timeline::ClipKind::audio;
            c.durationSeconds=s.snap((double)a.getProperty("duration",std::floor((m->info.duration-c.sourceStartSeconds)/s.frameSeconds())*s.frameSeconds()));
        }
        s.clips.push_back(c);
    }
    else
    {
        if(index<0)return fail("Select a timeline clip first.");
        auto& c=s.clips[(size_t)index];
        if(command=="split")
        {
            const auto t=s.snap((double)a["time"]);
            // Shared split retains exact source offsets; its minimum of .02 s is
            // below one frame at the default 29.97 fps.
            if(!creation::timeline::splitClip(s.clips,index,t))return fail("Split must be inside the clip, away from its ends.");
        }
        else if(command=="trim")
        {
            c.sourceStartSeconds=a.getProperty("sourceIn",c.sourceStartSeconds);
            c.durationSeconds=s.snap((double)a.getProperty("duration",c.durationSeconds));
        }
        else if(command=="move"){c.startSeconds=s.snap((double)a["start"]);c.trackIndex=a.getProperty("track",c.trackIndex);}
        else if(command=="remove")
        {
            const auto start=c.startSeconds,end=start+c.durationSeconds,length=c.durationSeconds;
            const bool ripple=a.getProperty("ripple",false);
            if(ripple) for(const auto& other:s.clips)
                if(other.id!=c.id && other.startSeconds<end-1e-8 && other.startSeconds+other.durationSeconds>start+1e-8)
                    return fail("Ripple removal would cut an overlapping clip. Trim/remove that overlap first, or use a plain removal.");
            creation::timeline::deleteClip(s.clips,index);
            if(ripple)for(auto& other:s.clips)if(other.startSeconds>=end-1e-8)other.startSeconds-=length;
        }
        else if(command=="levels")
        {
            c.videoParams.set("gain",a.getProperty("gain",c.videoParams.getWithDefault("gain",1.0)));
            c.videoParams.set("opacity",a.getProperty("opacity",c.videoParams.getWithDefault("opacity",1.0)));
        }
        else return fail("Unknown edit command.");
    }
    Sequence checked;
    if(!Sequence::fromJson(s.toJson(),checked,error))return false;
    checked.projectId=sequence.projectId;
    undo.beginNewTransaction(command);
    return undo.perform(new Edit(*this,sequence,std::move(checked)));
}
}
