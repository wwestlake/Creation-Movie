#include "MovieEffects.h"
#include <set>
#include <cmath>
namespace movie
{
namespace
{
juce::var obj(std::initializer_list<std::pair<juce::Identifier,juce::var>> fields)
{
    auto* o=new juce::DynamicObject;for(const auto& p:fields)o->setProperty(p.first,p.second);return juce::var(o);
}
EffectParameter number(const char* id,const char* name,double initial,double lo,double hi,const char* units="",double step=.01)
{return {id,name,units,"number",initial,lo,hi,step,true,{}};}
}
const std::vector<EffectDefinition>& effectCatalog()
{
    static const std::vector<EffectDefinition> catalog{
        {"audio.gain","Gain","audio",{number("gain","Gain",1,0,4,"x")}},
        {"audio.balance","Stereo Balance","audio",{number("balance","Balance",0,-1,1)}},
        {"audio.fade","Track Fade","audio",{number("in","Fade in",0,0,30,"s"),number("out","Fade out",0,0,30,"s")}},
        {"audio.filter","Filter","audio",{number("cutoff","Cutoff",1000,20,20000,"Hz",1),
            {"mode","Mode","","choice",0,0,1,1,true,{"Low pass","High pass"}}}},
        {"video.colour","Brightness / Contrast / Colour","video",{
            number("brightness","Brightness",1,0,2,"x"),number("contrast","Contrast",1,0,3,"x"),number("saturation","Saturation",1,0,2,"x"),
            number("red","Red balance",1,0,2,"x"),number("green","Green balance",1,0,2,"x"),number("blue","Blue balance",1,0,2,"x")}},
        {"video.opacity","Opacity","video",{number("opacity","Opacity",1,0,1)}},
        {"video.transform","Crop / Scale / Position","video",{
            number("left","Crop left",0,0,.95),number("right","Crop right",0,0,.95),number("top","Crop top",0,0,.95),number("bottom","Crop bottom",0,0,.95),
            number("scaleX","Scale X",1,.05,4,"x"),number("scaleY","Scale Y",1,.05,4,"x"),number("x","Position X",0,-2,2,"frame"),number("y","Position Y",0,-2,2,"frame")}}
    };return catalog;
}
const EffectDefinition* effectDefinition(const juce::String& id)
{for(const auto& d:effectCatalog())if(d.id==id)return &d;return nullptr;}
juce::var effectsCatalogJson()
{
    juce::Array<juce::var> definitions;
    for(const auto& d:effectCatalog())
    {
        juce::Array<juce::var> parameters;
        for(const auto& p:d.parameters)
        {
            juce::Array<juce::var> choices;for(const auto& choice:p.choices)choices.add(choice);
            parameters.add(obj({{"id",p.id},{"name",p.name},{"type",p.kind},{"default",p.initial},{"minimum",p.minimum},{"maximum",p.maximum},
                {"step",p.step},{"units",p.units},{"automatable",p.automatable},{"choices",choices}}));
        }
        definitions.add(obj({{"id",d.id},{"name",d.name},{"domain",d.domain},{"parameters",parameters}}));
    }
    return definitions;
}
bool compatibleEffect(const juce::String& track,const juce::String& type)
{
    const auto* d=effectDefinition(type);if(!d)return false;
    return d->domain=="audio"?(track=="audio" || track=="video"):(track=="video" || track=="image");
}
double Effect::value(const juce::String& key)const
{
    const auto found=values.find(key);if(found!=values.end())return found->second;
    if(const auto* d=effectDefinition(type))for(const auto& p:d->parameters)if(p.id==key)return p.initial;
    return 0;
}
juce::var Effect::toJson()const
{
    auto p=juce::var(new juce::DynamicObject);
    if(const auto* d=effectDefinition(type))for(const auto& field:d->parameters)p.getDynamicObject()->setProperty(field.id,value(field.id));
    for(const auto& v:values)p.getDynamicObject()->setProperty(v.first,v.second);
    return obj({{"id",id},{"type",type},{"ownerId",ownerId},{"enabled",enabled},{"bypassed",bypassed},{"parameters",p}});
}
bool Effect::fromJson(const juce::var& v,Effect& result,juce::String& error)
{
    auto fail=[&](const char* text){error=text;return false;};
    Effect e;e.id=v["id"].toString();e.type=v["type"].toString();e.ownerId=v["ownerId"].toString();
    const auto* d=effectDefinition(e.type);
    if(!d || e.id.isEmpty() || e.ownerId.isEmpty() || !v["enabled"].isBool() || !v["bypassed"].isBool())return fail("Invalid effect identity or state.");
    e.enabled=v["enabled"];e.bypassed=v["bypassed"];
    const auto* parameters=v["parameters"].getDynamicObject();if(!parameters)return fail("Invalid effect parameters.");
    for(const auto& item:parameters->getProperties())
    {
        const auto p=std::find_if(d->parameters.begin(),d->parameters.end(),[&](const EffectParameter& field){return field.id==item.name.toString();});
        if(p==d->parameters.end() || !(item.value.isDouble() || item.value.isInt() || item.value.isInt64()))return fail("Unknown or invalid effect parameter.");
        const double n=item.value;
        if(!std::isfinite(n) || n<p->minimum || n>p->maximum || (p->kind=="choice" && std::floor(n)!=n))return fail("Effect parameter outside range.");
        e.values[p->id]=n;
    }
    if(e.type=="video.transform" && (e.value("left")+e.value("right")>=1 || e.value("top")+e.value("bottom")>=1))return fail("Crop must leave a visible area.");
    result=std::move(e);return true;
}
bool parseEffects(const juce::var& v,const juce::String& owner,const juce::String& type,std::vector<Effect>& result,juce::String& error)
{
    const auto* a=v.getArray();if(!a || a->size()>64){error="Invalid effects chain (maximum 64).";return false;}
    std::vector<Effect> parsed;std::set<juce::String> ids;
    for(const auto& item:*a)
    {
        Effect e;if(!Effect::fromJson(item,e,error))return false;
        if(e.ownerId!=owner || !ids.insert(e.id).second || !compatibleEffect(type,e.type)){error="Incompatible effect or invalid owner/duplicate identity.";return false;}
        parsed.push_back(std::move(e));
    }
    result=std::move(parsed);return true;
}
void processVideoEffects(juce::Image& frame,const std::vector<Effect>& chain)
{
    for(const auto& e:chain)
    {
        if(!e.enabled || e.bypassed || !e.type.startsWith("video."))continue;
        if(e.type=="video.opacity"){if(e.value("opacity")!=1)frame.multiplyAllAlphas((float)e.value("opacity"));}
        else if(e.type=="video.transform")
        {
            const int w=frame.getWidth(),h=frame.getHeight();juce::Image transformed(juce::Image::ARGB,w,h,true);
            juce::Graphics g(transformed);
            const auto cropped=frame.getClippedImage({(int)std::round(w*e.value("left")),(int)std::round(h*e.value("top")),
                std::max(1,(int)std::round(w*(1-e.value("left")-e.value("right")))),std::max(1,(int)std::round(h*(1-e.value("top")-e.value("bottom"))))});
            const auto transform=juce::AffineTransform::translation((float)(w*e.value("left")-w*.5),(float)(h*e.value("top")-h*.5))
                .scaled((float)e.value("scaleX"),(float)e.value("scaleY")).translated((float)(w*(.5+e.value("x"))),(float)(h*(.5+e.value("y"))));
            g.drawImageTransformed(cropped,transform);frame=std::move(transformed);
        }
        else if(e.type=="video.colour")
        {
            const float brightness=(float)e.value("brightness"),contrast=(float)e.value("contrast"),saturation=(float)e.value("saturation");
            const float red=(float)e.value("red"),green=(float)e.value("green"),blue=(float)e.value("blue");
            if(brightness==1 && contrast==1 && saturation==1 && red==1 && green==1 && blue==1)continue;
            // Channel-independent work is a tiny LUT; avoid Colour/HSV round trips
            // for the common unchanged-saturation case. PixelARGB handles alpha.
            std::array<std::array<juce::uint8,256>,3> lookup;
            const float tint[]{red,green,blue};
            for(int c=0;c<3;++c)for(int i=0;i<256;++i)
                lookup[(size_t)c][(size_t)i]=(juce::uint8)juce::jlimit(0,255,(int)std::lround(((i/255.0f*brightness-.5f)*contrast+.5f)*tint[c]*255));
            if(frame.getFormat()!=juce::Image::ARGB)frame=frame.convertedToFormat(juce::Image::ARGB);
            juce::Image::BitmapData pixels(frame,juce::Image::BitmapData::readWrite);
            for(int y=0;y<frame.getHeight();++y)
            {
                auto* line=reinterpret_cast<juce::PixelARGB*>(pixels.getLinePointer(y));
                for(int x=0;x<frame.getWidth();++x)
                {
                    auto& p=line[x];const auto a=p.getAlpha();if(a==0)continue;
                    if(a!=255)p.unpremultiply();
                    auto r=lookup[0][p.getRed()],g=lookup[1][p.getGreen()],b=lookup[2][p.getBlue()];
                    if(saturation!=1){const auto c=juce::Colour(r,g,b).withMultipliedSaturation(saturation);r=c.getRed();g=c.getGreen();b=c.getBlue();}
                    p.setARGB(a,r,g,b);if(a!=255)p.premultiply();
                }
            }
        }
    }
}
double ParameterCurve::at(double seconds) const
{
    if(!points || points->empty() || !parameter)return parameter?parameter->initial:0;
    const auto& curve=*points;
    auto next=std::upper_bound(curve.begin(),curve.end(),seconds,[](double t,const auto& p){return t<p.seconds;});
    float value;
    if(next==curve.begin())value=curve.front().value;
    else if(next==curve.end())value=curve.back().value;
    else if(parameter->kind=="choice")value=(next-1)->value;
    else value=creation::timeline::evaluateAutomationSegment(*(next-1),*next,seconds);
    const double actual=parameter->minimum+juce::jlimit(0.0,1.0,(double)value)*(parameter->maximum-parameter->minimum);
    return parameter->kind=="choice"?std::round(actual):actual;
}
void AudioEffects::reset(){for(auto& s:states){s.second->left.reset();s.second->right.reset();}}
void AudioEffects::process(float* data,int count,int rate,double time,double duration,const std::vector<Effect>& chain,const EffectAutomation* automation,double timelineTime)
{
    juce::ScopedNoDenormals noDenormals;
    for(const auto& e:chain)
    {
        if(!e.enabled || e.bypassed || !e.type.startsWith("audio."))continue;
        const std::map<juce::String,ParameterCurve>* curves=nullptr;
        if(automation){const auto found=automation->find(e.id);if(found!=automation->end())curves=&found->second;}
        auto curve=[&](const char* id)->const ParameterCurve*{if(!curves)return nullptr;auto it=curves->find(id);return it==curves->end()?nullptr:&it->second;};
        // Resolve curve and default lookups once per effect/block, never per sample.
        if(e.type=="audio.filter")
        {
            auto& state=states[e.id];if(!state)state=std::make_unique<State>();
            const auto* cutoffCurve=curve("cutoff");const auto* modeCurve=curve("mode");
            const double manualCutoff=e.value("cutoff"),manualMode=e.value("mode");
            for(int i=0;i<count;++i)
            {
                const double t=timelineTime+(double)i/rate;
                const double cutoff=std::min(cutoffCurve?cutoffCurve->at(t):manualCutoff,rate*.45),mode=modeCurve?modeCurve->at(t):manualMode;
                if(state->rate!=rate || state->cutoff!=cutoff || state->mode!=mode)
                {
                    const auto coefficients=mode==0?juce::IIRCoefficients::makeLowPass(rate,cutoff):juce::IIRCoefficients::makeHighPass(rate,cutoff);
                    state->left.setCoefficients(coefficients);state->right.setCoefficients(coefficients);state->rate=rate;state->cutoff=cutoff;state->mode=mode;
                }
                data[i*2]=state->left.processSingleSampleRaw(data[i*2]);data[i*2+1]=state->right.processSingleSampleRaw(data[i*2+1]);
            }
        }
        else
        {
            const bool gaining=e.type=="audio.gain",balancing=e.type=="audio.balance",fading=e.type=="audio.fade";
            const auto* gainCurve=curve("gain");const auto* panCurve=curve("balance");const auto* inCurve=curve("in");const auto* outCurve=curve("out");
            const double gain=gaining?e.value("gain"):1,pan=balancing?e.value("balance"):0;
            const double fadeIn=fading?e.value("in"):0,fadeOut=fading?e.value("out"):0;
            for(int i=0;i<count;++i)
            {
                const double absolute=timelineTime+(double)i/rate;
                const double g=gainCurve?gainCurve->at(absolute):gain,p=panCurve?panCurve->at(absolute):pan;
                float fade=1;
                if(fading)
                {
                    const double t=time+(double)i/rate,in=inCurve?inCurve->at(absolute):fadeIn,out=outCurve?outCurve->at(absolute):fadeOut;
                    fade=(float)std::min(in>0?juce::jlimit(0.0,1.0,t/in):1.0,out>0?juce::jlimit(0.0,1.0,(duration-t)/out):1.0);
                }
                data[i*2]*=(float)(g*std::min(1.0,1-p))*fade;data[i*2+1]*=(float)(g*std::min(1.0,1+p))*fade;
            }
        }
    }
}
}
