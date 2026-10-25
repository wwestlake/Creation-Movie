#include "MovieTransitions.h"
#include "MovieDocument.h"
#include <set>
namespace movie
{
namespace
{
bool member(const juce::String& s,const char* tokens){return juce::StringArray::fromTokens(tokens,"|","").contains(s);}
}
juce::var Transition::toJson(bool includeEnd)const
{auto result=object({{"id",id},{"trackId",trackId},{"outgoing",outgoing},{"incoming",incoming},{"type",type},{"start",start},{"duration",duration},{"curve",curve},{"direction",direction},{"softness",softness},{"midpoint",midpoint},{"tension",tension}});if(includeEnd)result.getDynamicObject()->setProperty("end",end());return result;}
bool Transition::fromJson(const juce::var& v,Transition& result,juce::String& error)
{
    auto fail=[&](const char* why){error=why;return false;};Transition t;
    if(!v.isObject())return fail("Transition must be an object.");
    for(const auto& property:v.getDynamicObject()->getProperties())if(!member(property.name.toString(),"id|trackId|outgoing|incoming|type|start|duration|end|curve|direction|softness|midpoint|tension"))return fail("Unknown transition property; refusing to discard data.");
    for(const auto* key:{"id","trackId","outgoing","incoming","type","curve","direction"})if(!v[key].isString())return fail("Transition identity/type fields must be strings.");
    t.id=v["id"].toString();t.trackId=v["trackId"].toString();t.outgoing=v["outgoing"].toString();t.incoming=v["incoming"].toString();t.type=v["type"].toString();
    t.curve=v["curve"].toString();t.direction=v["direction"].toString();
    for(const auto* key:{"start","duration","softness","midpoint","tension"})if(!finiteNumber(v[key]))return fail("Transition properties must be finite numbers.");
    t.start=v["start"];t.duration=v["duration"];t.softness=v["softness"];t.midpoint=v["midpoint"];t.tension=v["tension"];
    if(t.id.isEmpty() || t.trackId.isEmpty() || t.outgoing.isEmpty() || t.incoming.isEmpty() || t.outgoing==t.incoming
        || !member(t.type,"video.dissolve|video.black|video.wipe|audio.crossfade"))return fail("Invalid transition identity, clips or type.");
    if(t.start<0 || t.duration<1.0/48000 || t.end()>86400 || t.softness<0 || t.softness>1 || t.midpoint<.01 || t.midpoint>.99 || t.tension<-1 || t.tension>1)return fail("Transition time/parameters outside bounds.");
    if(!member(t.direction,"leftToRight|rightToLeft|topToBottom|bottomToTop") || !(t.audio()?member(t.curve,"linear|equalPower"):member(t.curve,"linear|sCurve|power|bezier")))return fail("Unsupported transition curve or direction.");
    if(v.hasProperty("end") && (!finiteNumber(v["end"]) || std::abs((double)v["end"]-t.end())>1e-8))return fail("Transition end must match start plus duration.");
    result=std::move(t);return true;
}
juce::String transitionName(const juce::String& type)
{if(type=="video.dissolve")return "Cross Dissolve";if(type=="video.black")return "Fade Through Black";if(type=="video.wipe")return "Directional Wipe";if(type=="audio.crossfade")return "Audio Crossfade";return type;}
juce::var transitionCatalog()
{
    juce::Array<juce::var> types;
    for(const auto* type:{"video.dissolve","video.black","video.wipe","audio.crossfade"})
        types.add(object({{"id",type},{"name",transitionName(type)},{"domain",juce::String(type).startsWith("audio.")?"audio":"video"}}));
    return object({{"types",types},{"defaults",object({{"type","video.dissolve"},{"curve","linear"},{"direction","leftToRight"},{"start",0},{"duration",1},{"softness",0},{"midpoint",.5},{"tension",0}})},{"parameters",object({
        {"start",object({{"units","seconds"},{"minimum",0},{"maximum",86400}})},
        {"duration",object({{"units","seconds"},{"minimum",1.0/48000},{"maximum",86400}})},
        {"softness",object({{"units","fraction of frame axis"},{"minimum",0},{"maximum",1}})},
        {"midpoint",object({{"units","fraction of transition"},{"minimum",.01},{"maximum",.99}})},
        {"tension",object({{"minimum",-1},{"maximum",1}})},
        {"videoCurves",juce::Array<juce::var>{"linear","sCurve","power","bezier"}},
        {"audioCurves",juce::Array<juce::var>{"linear","equalPower"}},
        {"directions",juce::Array<juce::var>{"leftToRight","rightToLeft","topToBottom","bottomToTop"}}})}});
}
bool validateTransitions(const Sequence& s,juce::String& error)
{
    auto fail=[&](const char* why){error=why;return false;};if(s.transitions.size()>4096)return fail("Too many transitions.");std::set<juce::String> ids;
    for(const auto& t:s.transitions)
    {
        Transition checked;if(!Transition::fromJson(t.toJson(),checked,error))return false;
        if(!ids.insert(t.id).second)return fail("Duplicate transition identity.");
        const int ai=s.findClip(t.outgoing),bi=s.findClip(t.incoming),ti=s.findTrack(t.trackId);
        if(ai<0 || bi<0 || ti<0)return fail("Transition references a missing clip/track.");
        const auto& a=s.clips[(size_t)ai];const auto& b=s.clips[(size_t)bi];
        const auto* am=s.findMedia(a.assetId);const auto* bm=s.findMedia(b.assetId);
        if(s.owner(a)!=t.trackId || s.owner(b)!=t.trackId || !am || !bm
            || (t.audio()?(!am->info.hasAudio || !bm->info.hasAudio || a.soundDetached || b.soundDetached):(!am->info.hasVideo || !bm->info.hasVideo || am->info.still || bm->info.still)))return fail("Transitions require compatible source clips on the same track.");
        const double ae=a.startSeconds+a.durationSeconds,be=b.startSeconds+b.durationSeconds;
        if(a.startSeconds>=b.startSeconds || b.startSeconds>ae+1e-7 || t.start>a.startSeconds+a.durationSeconds+1e-7
            || t.start>b.startSeconds+1e-7 || t.end()<ae-1e-7 || t.start<a.startSeconds-1e-7 || t.end()>be+1e-7)return fail("Transition must bridge an adjacent cut or cover the full existing overlap, within the pair's outer bounds. Gaps are unsupported.");
        for(const auto* c:{&a,&b})
        {
            const auto* m=s.findMedia(c->assetId);const double from=c->sourceStartSeconds+t.start-c->startSeconds,to=c->sourceStartSeconds+t.end()-c->startSeconds;
            if(from<-1e-7 || to>m->info.duration+1e-7)return fail("Insufficient source handles. Shorten/reposition the transition or explicitly trim/place the source clips; clip ranges were not changed.");
        }
        for(const auto& c:s.clips)if(c.id!=a.id && c.id!=b.id && s.owner(c)==t.trackId && c.startSeconds<t.end()-1e-8 && c.startSeconds+c.durationSeconds>t.start+1e-8)
        {const auto* m=s.findMedia(c.assetId);if(m && (t.audio()?m->info.hasAudio && !c.soundDetached:m->info.hasVideo))return fail("A third compatible clip overlaps the transition interval.");}
        for(const auto& other:s.transitions)if(other.id!=t.id && other.trackId==t.trackId && other.audio()==t.audio() && other.start<t.end()-1e-8 && other.end()>t.start+1e-8)return fail("Transitions in the same track/domain cannot overlap.");
    }
    return true;
}
bool editTransition(Sequence& s,const juce::String& op,const juce::var& a,juce::String& error)
{
    auto fail=[&](const char* why){error=why;return false;};if(!a.isObject())return fail("Transition arguments must be an object.");auto found=std::find_if(s.transitions.begin(),s.transitions.end(),[&](const Transition& t){return t.id==a["transitionId"].toString();});
    if(op=="remove"){if(found==s.transitions.end())return fail("Unknown transition.");s.transitions.erase(found);return true;}
    if(op!="create" && op!="update")return fail("Unknown transition operation.");
    if(op=="update" && found==s.transitions.end())return fail("Unknown transition.");
    Transition t;if(op=="update")t=*found;else t.id=juce::Uuid().toString();
    auto value=t.toJson();for(const auto& p:a.getDynamicObject()->getProperties())
    {
        const auto key=p.name.toString();if(key=="transitionId")continue;
        if(!member(key,"trackId|outgoing|incoming|type|start|duration|curve|direction|softness|midpoint|tension"))return fail("Unknown/read-only transition property.");
        value.getDynamicObject()->setProperty(p.name,p.value);
    }
    value.getDynamicObject()->setProperty("end",(double)value["start"]+(double)value["duration"]);
    if(!Transition::fromJson(value,t,error))return false;
    if(op=="create")s.transitions.push_back(t);else *found=t;
    return validateTransitions(s,error);
}
double transitionProgress(const Transition& t,double seconds)
{
    const double x=juce::jlimit(0.0,1.0,(seconds-t.start)/t.duration);
    if(t.audio())return x;
    creation::timeline::AutomationPoint a{"",0,0,creation::timeline::automationCurveShapeFromStorageToken(t.curve),(float)t.tension},b{"",1,1,creation::timeline::AutomationCurveShape::linear,0};
    return creation::timeline::evaluateAutomationSegment(a,b,x);
}
std::pair<double,double> transitionAudioGains(const Transition& t,double seconds)
{
    const double x=transitionProgress(t,seconds);if(t.curve=="equalPower")return {std::cos(x*juce::MathConstants<double>::halfPi),std::sin(x*juce::MathConstants<double>::halfPi)};return {1-x,x};
}
juce::Image blendTransition(const Transition& t,double seconds,const juce::Image& a,const juce::Image& b)
{
    const int w=a.getWidth(),h=a.getHeight();juce::Image output(juce::Image::ARGB,w,h,true);
    juce::Image::BitmapData ad(a,juce::Image::BitmapData::readOnly),bd(b,juce::Image::BitmapData::readOnly),dst(output,juce::Image::BitmapData::writeOnly);
    const double p=transitionProgress(t,seconds);const bool black=t.type=="video.black",wipe=t.type=="video.wipe",horizontal=t.direction=="leftToRight" || t.direction=="rightToLeft",reverse=t.direction=="rightToLeft" || t.direction=="bottomToTop";
    for(int y=0;y<h;++y)for(int x=0;x<w;++x)
    {
        double ga=1-p,gb=p;
        if(black){ga=p<t.midpoint?1-p/t.midpoint:0;gb=p>=t.midpoint?(p-t.midpoint)/(1-t.midpoint):0;}
        if(wipe)
        {
            double axis=horizontal?(x+.5)/w:(y+.5)/h;
            if(reverse)axis=1-axis;
            gb=t.softness>0?juce::jlimit(0.0,1.0,((p*(1+t.softness)-t.softness*.5)-axis)/t.softness+.5):(axis<p?1:0);ga=1-gb;
        }
        const auto& pa=*reinterpret_cast<const juce::PixelARGB*>(ad.getPixelPointer(x,y));const auto& pb=*reinterpret_cast<const juce::PixelARGB*>(bd.getPixelPointer(x,y));
        auto mix=[&](int aa,int bb){return (juce::uint8)juce::jlimit(0,255,(int)std::lround(aa*ga+bb*gb));};
        const auto alpha=black?(juce::uint8)juce::jlimit(0,255,(int)std::lround(pa.getAlpha()*ga+pb.getAlpha()*gb+255*(1-ga-gb))):mix(pa.getAlpha(),pb.getAlpha());
        reinterpret_cast<juce::PixelARGB*>(dst.getPixelPointer(x,y))->setARGB(alpha,mix(pa.getRed(),pb.getRed()),mix(pa.getGreen(),pb.getGreen()),mix(pa.getBlue(),pb.getBlue()));
    }
    return output;
}
}
