static void runEffectsTests()
{
    using namespace movie;
    auto effect=[](const char* type){Effect e;e.id=juce::Uuid().toString();e.type=type;e.ownerId="owner";return e;};
    auto gain=effect("audio.gain");gain.values["gain"]=.5;
    std::vector<float> samples(96000,.2f);AudioEffects dsp;dsp.process(samples.data(),48000,48000,0,1,{gain});
    check(std::abs(samples[3000]-.1f)<1e-6,"Gain did not process audio samples");
    gain.enabled=false;std::vector<float> inactive(200,.2f);dsp.process(inactive.data(),100,48000,0,1,{gain});
    check(inactive[100]==.2f,"Disabled effect changed samples");gain.enabled=true;
    auto balance=effect("audio.balance");balance.values["balance"]=1;
    dsp.process(samples.data(),48000,48000,0,1,{balance});check(samples[2000]==0 && std::abs(samples[2001]-.1f)<1e-6,"Stereo balance incorrect");
    auto fade=effect("audio.fade");fade.values["in"]=.1;fade.values["out"]=.1;
    std::fill(samples.begin(),samples.end(),.2f);dsp.process(samples.data(),48000,48000,0,1,{fade});
    check(samples[0]==0 && samples[48000]>.19f && samples.back()<.001f,"Fade timing incorrect");
    auto filter=effect("audio.filter");filter.values["cutoff"]=500;
    std::vector<float> signal(96000),whole,blocked;
    for(int i=0;i<48000;++i)signal[(size_t)i*2]=signal[(size_t)i*2+1]=.2f*std::sin((float)i*juce::MathConstants<float>::twoPi*8000/48000);
    whole=signal;blocked=signal;AudioEffects full,blocks;const auto audioStart=juce::Time::getMillisecondCounterHiRes();full.process(whole.data(),48000,48000,0,1,{filter});
    const auto audioMs=juce::Time::getMillisecondCounterHiRes()-audioStart;
    for(int i=0;i<48000;i+=257){const int n=std::min(257,48000-i);blocks.process(blocked.data()+i*2,n,48000,(double)i/48000,1,{filter});}
    double originalEnergy=0,filteredEnergy=0;float error=0;
    for(size_t i=4096;i<whole.size();++i){originalEnergy+=signal[i]*signal[i];filteredEnergy+=whole[i]*whole[i];error=std::max(error,std::abs(whole[i]-blocked[i]));}
    check(filteredEnergy<originalEnergy*.01 && error<1e-7,"Filter attenuation or block continuity failed");
    filter.bypassed=true;blocked=signal;AudioEffects bypass;bypass.process(blocked.data(),48000,44100,0,1,{filter});check(blocked==signal,"Bypassed filter changed samples");
    auto colour=effect("video.colour");colour.values["brightness"]=.5;
    juce::Image image(juce::Image::ARGB,64,64,true);image.clear(image.getBounds(),juce::Colour::fromFloatRGBA(.4f,.6f,.2f,1));
    auto original=image.createCopy();processVideoEffects(image,{colour});
    check(std::abs(image.getPixelAt(20,20).getRed()-51)<=1,"Brightness did not change pixels");
    auto contrast=effect("video.colour");contrast.values["contrast"]=2;
    auto forward=original.createCopy(),reverse=original.createCopy();processVideoEffects(forward,{colour,contrast});processVideoEffects(reverse,{contrast,colour});
    check(forward.getPixelAt(0,0)!=reverse.getPixelAt(0,0),"Noncommutative effect ordering lost");
    colour.bypassed=true;image=original.createCopy();processVideoEffects(image,{colour});check(image.getPixelAt(0,0)==original.getPixelAt(0,0),"Video bypass changed pixels");
    colour.bypassed=false;colour.values["brightness"]=1;colour.values["saturation"]=0;image=original.createCopy();processVideoEffects(image,{colour});
    check(image.getPixelAt(0,0).getRed()==image.getPixelAt(0,0).getGreen(),"Saturation processing failed");
    colour.values["saturation"]=1;colour.values["red"]=0;image=original.createCopy();processVideoEffects(image,{colour});check(image.getPixelAt(0,0).getRed()==0,"Colour adjustment failed");
    auto opacity=effect("video.opacity");opacity.values["opacity"]=.5;image=original.createCopy();processVideoEffects(image,{opacity});check(std::abs(image.getPixelAt(0,0).getAlpha()-128)<=1,"Alpha handling failed");
    auto transform=effect("video.transform");transform.values["left"]=.25;transform.values["scaleX"]=.5;transform.values["x"]=.1;
    image=original.createCopy();processVideoEffects(image,{transform});check(image.getWidth()==64 && image.getPixelAt(0,0).getAlpha()==0,"Crop/transform failed");
    Document document;const auto track=document.sequence.tracks[0].id;juce::String failure;
    edit(document,"effect.add",object({{"trackId",track},{"type","video.colour"}}));const auto stable=document.sequence.tracks[0].effects[0].id;
    edit(document,"effect.parameter",object({{"trackId",track},{"effectId",stable},{"parameterId","brightness"},{"value",.7}}));
    edit(document,"effect.add",object({{"trackId",track},{"type","video.opacity"}}));
    edit(document,"effect.reorder",object({{"trackId",track},{"effectId",stable},{"order",1}}));check(document.sequence.tracks[0].effects[1].id==stable,"Effect identity changed during reorder");
    edit(document,"track.reorder",object({{"trackId",track},{"order",2}}));check(document.sequence.tracks[2].effects[1].ownerId==track,"Effect owner changed with track order");
    const auto saved=juce::JSON::toString(document.sequence.toJson());Sequence restored;
    check(Sequence::fromJson(juce::JSON::parse(saved),restored,failure),"Effects save/reopen failed");check(saved==juce::JSON::toString(restored.toJson()),"Effects persistence not exact");
    check(!document.edit("effect.parameter",object({{"trackId",track},{"effectId",stable},{"parameterId","brightness"},{"value",999}}),failure),"Invalid effect parameter accepted");
    check(saved==juce::JSON::toString(document.sequence.toJson()),"Invalid effects mutation changed document");
    edit(document,"effect.bypass",object({{"trackId",track},{"effectId",stable},{"value",true}}));document.undo.undo();check(!document.sequence.tracks[2].effects[1].bypassed,"Effect undo failed");document.undo.redo();
    edit(document,"effect.remove",object({{"trackId",track},{"effectId",stable}}));document.undo.undo();check(document.sequence.tracks[2].effects[1].id==stable,"Effect removal undo lost identity");
    const auto audio=document.sequence.tracks[3].id; // original audio lane after reorder
    check(!document.edit("effect.add",object({{"trackId",audio},{"type","video.colour"}}),failure),"Incompatible video effect accepted on audio track");
    auto version3=juce::JSON::parse(saved);version3.getDynamicObject()->setProperty("version",3);
    for(auto& t:*version3["tracks"].getArray())t.getDynamicObject()->removeProperty("effects");
    check(Sequence::fromJson(version3,restored,failure) && restored.tracks[2].id==track && restored.tracks[2].effects.empty(),"Version-3 migration failed");
    const auto start=juce::Time::getMillisecondCounterHiRes();
    colour.values["red"]=1;colour.values["brightness"]=.7;
    for(int i=0;i<8;++i){juce::Image frame(juce::Image::ARGB,1280,720,true);frame.clear(frame.getBounds(),juce::Colours::grey);processVideoEffects(frame,{colour,opacity,transform});}
    const double frameMs=(juce::Time::getMillisecondCounterHiRes()-start)/8;
    colour.values["saturation"]=1.2;const auto saturationStart=juce::Time::getMillisecondCounterHiRes();
    for(int i=0;i<3;++i){juce::Image frame(juce::Image::ARGB,1280,720,true);frame.clear(frame.getBounds(),juce::Colours::grey);processVideoEffects(frame,{colour,opacity,transform});}
    std::cout<<"PASS: effects DSP/pixels/order/bypass/continuity, version-3/4 persistence, undo/redo, validation; 720p colour+opacity+transform "<<frameMs<<" ms/frame; non-neutral saturation "<<(juce::Time::getMillisecondCounterHiRes()-saturationStart)/3<<" ms/frame; 1s stereo 48kHz filter "<<audioMs<<" ms; filter block error "<<error<<"\n";
}
