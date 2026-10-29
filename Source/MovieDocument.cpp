#include "MovieDocument.h"
#include "MovieAutomation.h"
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
    juce::Array<juce::var> a,c,t,transitionsJson;
    for(const auto& tr:transitions)transitionsJson.add(tr.toJson(false));
    for(const auto& track:tracks) t.add(track.toJson());
    for(const auto& m:media) a.add(object({{"id",m.id},{"versionId",m.versionId},{"name",m.name},{"path",m.path},{"info",m.info.toJson()}}));
    for(const auto& x:clips)
    {
        auto params=juce::var(new juce::DynamicObject());
        params.getDynamicObject()->getProperties()=x.videoParams;
        c.add(object({{"id",x.id},{"assetId",x.assetId},{"name",x.displayName},{"trackId",owner(x)},
            {"kind",x.kind==creation::timeline::ClipKind::audio ? "audio":"video"},
            {"startFrame",(juce::int64)std::llround(x.startSeconds/frameSeconds())},
            {"durationFrames",(juce::int64)std::llround(x.durationSeconds/frameSeconds())},
            {"sourceIn",x.sourceStartSeconds},{"sourceDuration",x.sourceDurationSeconds},{"params",params}}));
    }
    return object({{"schema","djehuti-movie-sequence"},{"version",5},{"name",name},{"width",width},{"height",height},
        {"fpsNum",fpsNum},{"fpsDen",fpsDen},{"tracks",t},{"media",a},{"clips",c},{"transitions",transitionsJson}});
}
bool Sequence::fromJson(const juce::var& v, Sequence& result, juce::String& error)
{
    auto fail=[&](const juce::String& s){ error=s; return false; };
    const int version=v["version"];
    if(v["schema"].toString()!="djehuti-movie-sequence" || (version!=2 && version!=3 && version!=4 && version!=5)) return fail("Unsupported Movie sequence schema.");
    Sequence s; s.name=v["name"].toString(); s.width=v["width"];s.height=v["height"];s.fpsNum=v["fpsNum"];s.fpsDen=v["fpsDen"];
    if(s.width<16 || s.width>8192 || s.height<16 || s.height>8192 || s.fpsNum<1 || s.fpsDen<1
        || (double)s.fpsNum/s.fpsDen>240 || (double)s.fpsNum/s.fpsDen<1) return fail("Invalid sequence dimensions or frame rate.");
    const auto* a=v["media"].getArray(); const auto* c=v["clips"].getArray();
    if(!a || !c || a->size()>10000 || c->size()>100000) return fail("Invalid media or clip list.");
    std::set<juce::String> ids;
    s.tracks.clear();
    if(version==2)
    {
        // Every old index gets a deterministic identity. Mixed content stays legal;
        // ordering remains ascending, exactly as in the version-2 renderer.
        for(int i=0;i<8;++i)
        {
            Track t; t.id="legacy-track-"+juce::String(i); t.name="Legacy "+juce::String(i+1);
            t.legacyMixed=true; s.tracks.push_back(t);
        }
    }
    else
    {
        const auto* tracks=v["tracks"].getArray();
        if(!tracks || tracks->size()>4096) return fail("Invalid track list.");
        for(const auto& e:*tracks)
        {
            if(version>=4 && !e["effects"].isArray())return fail("Missing effects chain.");
            Track t; if(!Track::fromJson(e,t,error)) return false;
            if(!ids.insert(t.id).second) return fail("Duplicate track identity.");
            s.tracks.push_back(std::move(t));
        }
    }
    ids.clear();
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
        Clip x; x.id=e["id"].toString();x.assetId=e["assetId"].toString();x.displayName=e["name"].toString();
        const auto trackId=version==2?"legacy-track-"+e["track"].toString():e["trackId"].toString();
        if(version==2 && (!e["track"].isInt() || (int)e["track"]<0 || (int)e["track"]>7)) return fail("Invalid legacy track index.");
        x.trackIndex=s.findTrack(trackId);
        if(e["kind"].toString()!="audio" && e["kind"].toString()!="video") return fail("Unsupported clip kind; original document was not changed.");
        x.kind=e["kind"].toString()=="audio"?creation::timeline::ClipKind::audio:creation::timeline::ClipKind::video;
        if(!finiteNumber(e["startFrame"]) || !finiteNumber(e["durationFrames"]) || !finiteNumber(e["sourceIn"])) return fail("Invalid clip timing.");
        x.startSeconds=(double)e["startFrame"]*s.frameSeconds();x.durationSeconds=(double)e["durationFrames"]*s.frameSeconds();
        x.sourceStartSeconds=e["sourceIn"];x.sourceDurationSeconds=e["sourceDuration"];
        if(auto* p=e["params"].getDynamicObject()) x.videoParams=p->getProperties();
        const auto* m=s.findMedia(x.assetId);
        const bool title=x.videoParams["title"].toString().isNotEmpty();
        if(x.id.isEmpty() || !ids.insert(x.id).second || x.trackIndex<0 || x.startSeconds<0
            || x.startSeconds>86400 || x.durationSeconds<s.frameSeconds() || x.durationSeconds>86400 || x.sourceStartSeconds<0
            || (!title && (!m || (!m->info.still && x.sourceStartSeconds+x.durationSeconds>m->info.duration+0.00001))))
            return fail("Clip is outside its source or sequence limits.");
        const auto gain=x.videoParams.getWithDefault("gain",1.0);
        const auto opacity=x.videoParams.getWithDefault("opacity",1.0);
        if(!finiteNumber(gain) || (double)gain<0 || (double)gain>4 || !finiteNumber(opacity) || (double)opacity<0 || (double)opacity>1)
            return fail("Invalid clip gain or opacity.");
        if(m) x.assetVersionId=m->versionId;
        const auto& track=s.tracks[(size_t)x.trackIndex];
        if(!track.legacyMixed && ((track.type=="audio" && x.kind!=creation::timeline::ClipKind::audio)
            || (track.type=="image" && (!m || !m->info.still))
            || (track.type=="video" && x.kind!=creation::timeline::ClipKind::video)
            || track.type=="effects" || track.type=="automation")) return fail("Clip is incompatible with its track type.");
        s.clipTracks[x.id]=trackId;
        s.clips.push_back(std::move(x));
    }
    std::set<juce::String> effectIds;
    for(const auto& t:s.tracks)
    {
        for(const auto& e:t.effects)if(!effectIds.insert(e.id).second)return fail("Duplicate effect instance identity.");
        if(t.targetTrackId.isNotEmpty() && s.findTrack(t.targetTrackId)<0) return fail("Automation target track is missing.");
        if(t.targetClipId.isNotEmpty() && s.findClip(t.targetClipId)<0) return fail("Automation target clip is missing.");
        if(t.source["category"].toString()=="media")
        {
            const auto* m=s.findMedia(t.source["identity"].toString());
            if(!m || (t.type=="audio" && !m->info.hasAudio) || (t.type=="video" && !m->info.hasVideo)
                || (t.type=="image" && !m->info.still)) return fail("Assigned media is incompatible or missing.");
        }
    }
    if(version<5 && v.hasProperty("transitions") && (!v["transitions"].isArray() || v["transitions"].size()>0))return fail("Transition data requires schema 5; refusing to discard it.");
    if(version==5)
    {
        const auto* transitions=v["transitions"].getArray();if(!transitions || transitions->size()>4096)return fail("Invalid/missing transition list.");
        for(const auto& item:*transitions){Transition tr;if(!Transition::fromJson(item,tr,error))return false;s.transitions.push_back(tr);}
        if(!validateTransitions(s,error))return false;
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
    if(command.startsWith("transition."))
    {if(!editTransition(s,command.fromFirstOccurrenceOf("transition.",false,false),a,error))return false;}
    else if(command.startsWith("automation."))
    {if(!editAutomation(s,command.fromFirstOccurrenceOf("automation.",false,false),a,error))return false;}
    else if(command.startsWith("effect."))
    {
        const auto action=command.fromFirstOccurrenceOf("effect.",false,false);
        const int ti=s.findTrack(a["trackId"].toString());if(ti<0)return fail("Select a valid track first.");
        auto& chain=s.tracks[(size_t)ti].effects;
        if(action=="add")
        {
            const auto type=a["type"].toString();if(!compatibleEffect(s.tracks[(size_t)ti].type,type))return fail("Effect is incompatible. Effects tracks have no defined routing; choose an audio/video/image track.");
            Effect e;e.id=juce::Uuid().toString();e.type=type;e.ownerId=s.tracks[(size_t)ti].id;
            chain.push_back(std::move(e));
        }
        else
        {
            const auto found=std::find_if(chain.begin(),chain.end(),[&](const Effect& e){return e.id==a["effectId"].toString();});
            if(found==chain.end())return fail("Unknown effect instance.");
            const int index=(int)(found-chain.begin());
            if(action=="remove")
            {
                for(const auto& t:s.tracks)if(t.targetEffectId==found->id)return fail("Effect is an automation target; clear its stored binding first.");
                chain.erase(found);
            }
            else if(action=="reorder")
            {
                if(!a["order"].isInt() || (int)a["order"]<0 || (int)a["order"]>=(int)chain.size())return fail("Invalid effect order.");
                const auto e=*found;chain.erase(found);chain.insert(chain.begin()+(int)a["order"],e);
            }
            else if(action=="parameter")found->values[a["parameterId"].toString()]=a["value"].isDouble() || a["value"].isInt() || a["value"].isInt64()?(double)a["value"]:std::numeric_limits<double>::quiet_NaN();
            else if(action=="enabled" || action=="bypass")
            {
                if(!a["value"].isBool())return fail("Effect state requires a boolean value.");
                if(action=="enabled")found->enabled=a["value"];else found->bypassed=a["value"];
            }
            else return fail("Unknown effect operation.");
        }
    }
    else if(command.startsWith("track."))
    {
        const auto action=command.fromFirstOccurrenceOf("track.",false,false);
        int ti=s.findTrack(a["trackId"].toString());
        if(action=="create")
        {
            Track t; t.id=juce::Uuid().toString(); t.type=a.getProperty("type","video").toString();
            t.name=a.getProperty("name",t.type+" "+juce::String(s.tracks.size()+1)).toString().trim();
            s.tracks.push_back(t);
        }
        else
        {
            if(ti<0) return fail("Unknown track identity.");
            auto& t=s.tracks[(size_t)ti];
            if(action=="remove")
            {
                const auto id=t.id;
                for(const auto& other:s.tracks)for(const auto& effect:t.effects)
                    if(other.targetEffectId==effect.id)return fail("Track owns an automation-targeted effect; clear the stored binding first.");
                for(const auto& other:s.tracks) if(other.targetTrackId==id) return fail("Track is an automation target; clear the target first.");
                const bool occupied=std::any_of(s.clips.begin(),s.clips.end(),[&](const Clip& c){return s.owner(c)==id;});
                if(occupied && !(bool)a.getProperty("removePlacements",false)) return fail("Track contains clips; explicitly set removePlacements to remove their placements, preserving media.");
                s.clips.erase(std::remove_if(s.clips.begin(),s.clips.end(),[&](const Clip& c){return s.owner(c)==id;}),s.clips.end());
                s.tracks.erase(s.tracks.begin()+ti);
            }
            else if(action=="reorder")
            {
                if(!a["order"].isInt() || (int)a["order"]<0 || (int)a["order"]>=(int)s.tracks.size()) return fail("Invalid display order.");
                auto moved=t; s.tracks.erase(s.tracks.begin()+ti); s.tracks.insert(s.tracks.begin()+(int)a["order"],moved);
            }
            else if(action=="rename") t.name=a["name"].toString().trim();
            else if(action=="type") {t.type=a["type"].toString();t.legacyMixed=false;}
            else if(action=="enabled") {if(!a["enabled"].isBool()) return fail("enabled must be boolean.");t.enabled=a["enabled"];}
            else if(action=="source")
            {
                const auto previous=t.source["category"].toString();t.source=a["source"].clone();
                const auto category=t.source["category"].toString();
                if(category=="display" || category=="window")t.monitoring=true;
                else if(previous=="display" || previous=="window")t.monitoring=false;
            }
            else if(action=="preferences")
            {
                for(const auto* key:{"armed","monitoring"}) if(a.hasProperty(key) && !a[key].isBool()) return fail("Preferences must be boolean.");
                t.armed=a.getProperty("armed",t.armed);t.monitoring=a.getProperty("monitoring",t.monitoring);
                if(a.hasProperty("height")) {if(!a["height"].isInt()) return fail("height must be integer.");t.heightPixels=a["height"];}
            }
            else if(action=="automation")
            {
                auto json=t.toJson();json.getDynamicObject()->setProperty("automation",a["automation"].clone());
                Track changed; if(!Track::fromJson(json,changed,error)) return false; t=std::move(changed);
            }
            else return fail("Unknown track operation.");
        }
        s.resolveTrackIndices();
    }
    else if(command=="place" || command=="title")
    {
        Clip c;c.id=juce::Uuid().toString();c.kind=creation::timeline::ClipKind::video;
        c.startSeconds=s.snap((double)a.getProperty("start",s.duration()));c.trackIndex=a.getProperty("track",command=="title"?1:0);
        if(a.hasProperty("trackId")) c.trackIndex=s.findTrack(a["trackId"].toString());
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
            if(!a.hasProperty("track") && !a.hasProperty("trackId"))
            {
                c.trackIndex=-1;
                for(size_t ti=0;ti<s.tracks.size();++ti)
                {
                    const auto& t=s.tracks[ti];
                    if(t.legacyMixed || (t.type=="audio" && !m->info.hasVideo) || (t.type=="video" && m->info.hasVideo)
                        || (t.type=="image" && m->info.still)) {c.trackIndex=(int)ti;break;}
                }
            }
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
            s.clipTracks[s.clips[(size_t)index+1].id]=s.owner(s.clips[(size_t)index]);
        }
        else if(command=="trim")
        {
            c.sourceStartSeconds=a.getProperty("sourceIn",c.sourceStartSeconds);
            c.durationSeconds=s.snap((double)a.getProperty("duration",c.durationSeconds));
        }
        else if(command=="move")
        {
            c.startSeconds=s.snap((double)a["start"]);c.trackIndex=a.hasProperty("trackId")?s.findTrack(a["trackId"].toString()):(int)a.getProperty("track",c.trackIndex);
            if(!juce::isPositiveAndBelow(c.trackIndex,(int)s.tracks.size())) return fail("Unknown destination track.");
            s.clipTracks[c.id]=s.tracks[(size_t)c.trackIndex].id;
        }
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
    // An explicit clip/track removal invalidates its associated transitions in
    // the same undo transaction. Other incompatible edits reject atomically.
    if(command=="remove" || command=="track.remove")
        s.transitions.erase(std::remove_if(s.transitions.begin(),s.transitions.end(),[&](const Transition& tr){return s.findClip(tr.outgoing)<0 || s.findClip(tr.incoming)<0;}),s.transitions.end());
    Sequence checked;
    if(!Sequence::fromJson(s.toJson(),checked,error))return false;
    checked.projectId=sequence.projectId;
    const auto gesture=command=="effect.parameter"?a["gestureId"].toString()+a["effectId"].toString()+a["parameterId"].toString():juce::String();
    if(a["gestureId"].toString().isEmpty() || gesture!=lastEffectGesture)undo.beginNewTransaction(command);
    lastEffectGesture=gesture;
    return undo.perform(new Edit(*this,sequence,std::move(checked)));
}
}
