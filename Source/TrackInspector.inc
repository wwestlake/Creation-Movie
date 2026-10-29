class MainComponent::TrackInspector final : public juce::Component
{
public:
#if JUCE_DEBUG
    friend class MainComponent::TrackUiCheck;
#endif
    explicit TrackInspector(MainComponent& o):owner(o)
    {
        addAndMakeVisible(view);view.setViewedComponent(&form,false);view.setScrollBarsShown(true,false);
        for(auto* c:std::initializer_list<juce::Component*>{&heading,&nameLabel,&name,&typeLabel,&type,&enabled,&armed,&monitor,
            &heightLabel,&height,&sourceLabel,&category,&media,&identity,&settingsLabel,&widthLabel,&pixelsLabel,&fpsLabel,&rateLabel,&channelsLabel,&width,&pixels,&fps,&rate,&channels,&applySource,&clearSource,&note,&automation,&remove,&effects,&rescan,&captureFormat,&signal})form.addAndMakeVisible(c);
        heading.setText("Track Inspector",juce::dontSendNotification);
        nameLabel.setText("Name",juce::dontSendNotification);typeLabel.setText("Type",juce::dontSendNotification);
        heightLabel.setText("Lane height",juce::dontSendNotification);sourceLabel.setText("Source",juce::dontSendNotification);
        settingsLabel.setText("Requested format (optional)",juce::dontSendNotification);
        widthLabel.setText("Width (pixels)",juce::dontSendNotification);pixelsLabel.setText("Height (pixels)",juce::dontSendNotification);
        fpsLabel.setText("Frame rate numerator",juce::dontSendNotification);rateLabel.setText("Sample rate (Hz)",juce::dontSendNotification);channelsLabel.setText("Channels",juce::dontSendNotification);
        for(const auto* token:{"video","audio","image","effects","automation"})type.addItem(juce::String(token).toUpperCase(),type.getNumItems()+1);
        height.addItem("Compact / 64",64);height.addItem("Standard / 100",100);height.addItem("Large / 140",140);height.addItem("Extra large / 200",200);
        name.setName("Track name");type.setName("Track type");category.setName("Track source category");media.setName("Project media source");captureFormat.setName("Supported capture format");
        identity.setName("Saved source label");identity.setTextToShowWhenEmpty("Saved device or window label",juce::Colours::grey);
        auto field=[](juce::TextEditor& e,const char* label){e.setName(label);e.setTextToShowWhenEmpty(label,juce::Colours::grey);e.setInputRestrictions(7,"0123456789");};
        field(width,"Width");field(pixels,"Height");field(fps,"Frames / sec");field(rate,"Samples / sec");field(channels,"Channels");
        name.onReturnKey=[this]{rename();};name.onFocusLost=[this]{rename();};
        type.onChange=[this]{if(!syncing)mutate("type",movie::object({{"type",types[(size_t)(type.getSelectedId()-1)]}}));};
        enabled.onClick=[this]{mutate("enabled",movie::object({{"enabled",enabled.getToggleState()}}));};
        armed.onClick=[this]{mutate("preferences",movie::object({{"armed",armed.getToggleState()}}));};
        monitor.onClick=[this]{mutate("preferences",movie::object({{"monitoring",monitor.getToggleState()}}));};
        height.onChange=[this]{if(!syncing)mutate("preferences",movie::object({{"height",height.getSelectedId()}}));};
        media.onChange=[this]{if(!syncing)populateFormats();};
        category.onChange=[this]{if(!syncing){populateMedia();layout();}};
        applySource.onClick=[this]{saveSource();};clearSource.onClick=[this]{mutate("source",movie::object({{"source",juce::var()}}));};
        rescan.onClick=[this]{owner.visualCapture.refresh();populateMedia();};
        remove.onClick=[this]{owner.removeTrack(owner.selectedTrack);};
        effects.onClick=[this]{if(owner.dock)owner.dock->activatePanel("effects");};
        note.setJustificationType(juce::Justification::topLeft);automation.setJustificationType(juce::Justification::topLeft);
    }
    void sync()
    {
        syncing=true;
        const int index=owner.document.sequence.findTrack(owner.selectedTrack);
        valid=index>=0;
        if(valid)
        {
            const auto& t=owner.document.sequence.tracks[(size_t)index];
            trackId=t.id;
            name.setText(t.name,false);type.setSelectedId((int)(std::find(types.begin(),types.end(),t.type)-types.begin())+1,juce::dontSendNotification);
            enabled.setToggleState(t.enabled,juce::dontSendNotification);armed.setToggleState(t.armed,juce::dontSendNotification);monitor.setToggleState(t.monitoring,juce::dontSendNotification);
            height.setSelectedId(t.heightPixels,juce::dontSendNotification);
            if(height.getSelectedId()==0){height.addItem("Custom / "+juce::String(t.heightPixels),t.heightPixels);height.setSelectedId(t.heightPixels,juce::dontSendNotification);}
            category.clear(juce::dontSendNotification);categories.clear();
            auto add=[&](const char* key,const char* label){categories.push_back(key);category.addItem(label,(int)categories.size());};
            add("none","No source assigned");
            if(t.type=="video" || t.type=="audio" || t.type=="image")add("media","Imported project media");
            if(t.type=="video"){add("display","Screen / individual display");add("window","Application window");add("camera","Camera");}
            if(t.type=="audio"){add("microphone","Microphone");add("systemAudio","System audio ... saved configuration only");}
            const auto token=t.source["category"].toString();
            auto found=std::find(categories.begin(),categories.end(),token);
            category.setSelectedId(found==categories.end()?1:(int)(found-categories.begin())+1,juce::dontSendNotification);
            identity.setText(t.source["identity"].toString(),false);
            populateMedia();
            for(size_t i=0;i<mediaIds.size();++i)if(mediaIds[i]==t.source["identity"].toString())media.setSelectedId((int)i+1,juce::dontSendNotification);
            auto setting=[&](juce::TextEditor& e,const char* key){e.setText(t.source["settings"].hasProperty(key)?t.source["settings"][key].toString():juce::String(),false);};
            setting(width,"width");setting(pixels,"height");setting(fps,"fpsNum");setting(rate,"sampleRate");setting(channels,"channels");
            juce::String target="No target stored";
            if(t.targetTrackId.isNotEmpty())
            {
                const int ti=owner.document.sequence.findTrack(t.targetTrackId);
                target=ti>=0?owner.document.sequence.tracks[(size_t)ti].name:"Unavailable track";
            }
            if(t.targetClipId.isNotEmpty())
            {
                const int ci=owner.document.sequence.findClip(t.targetClipId);
                if(ci>=0)target+=" / "+owner.document.sequence.clips[(size_t)ci].displayName;
            }
            if(t.targetEffectId.isNotEmpty())
            {const movie::Effect* e=nullptr;if(movie::automationParameter(owner.document.sequence,t,&e))target+=" / "+movie::effectDefinition(e->type)->name;else target+=" / unresolved effect";}
            if(t.parameterId.isNotEmpty())target+=" / "+t.parameterId;
            automation.setText("Stored target: "+target+"\n"+juce::String(t.points.size())+" curve points\nChoose Effects to edit keyframes. Unresolved legacy targets remain inactive.",juce::dontSendNotification);
            heading.setText(t.legacyMixed?"Track Inspector / legacy mixed media":"Track Inspector",juce::dontSendNotification);
        }
        else {trackId={};heading.setText("Select a track to configure it",juce::dontSendNotification);}
        syncing=false;layout();repaint();
    }
    void paint(juce::Graphics& g)override{g.fillAll(creation_movie::branding::panelColour());}
    void resized()override{view.setBounds(getLocalBounds());layout();}
public:
    void refreshCaptureChoices()
    {
        const auto cat=selectedCategory();if(cat!="display" && cat!="window" && cat!="camera" && cat!="microphone")return;
        juce::String stamp;for(const auto& source:owner.visualCapture.sources())if(source.kind==cat)stamp+=source.id+source.name+juce::JSON::toString(juce::var(source.formats));
        if(stamp!=catalogStamp){catalogStamp=stamp;populateMedia();populateFormats();}
        const auto p=owner.visualCapture.preview(owner.selectedTrack);
        signal.setText((cat=="microphone"?"Input: "+p.state+" / "+juce::String(p.peak>0?20*std::log10(p.peak):-100,1)+" dBFS":"Video: "+p.state)+"\n"+p.message,juce::dontSendNotification);
    }
private:
    juce::String selectedCategory()const
    {
        const int i=category.getSelectedId()-1;return juce::isPositiveAndBelow(i,(int)categories.size())?categories[(size_t)i]:"none";
    }
    void layout()
    {
        const int w=std::max(190,view.getMaximumVisibleWidth()-20);int y=8;
        auto row=[&](juce::Component& c,int h=26){c.setBounds(10,y,w,h);y+=h+5;};
        row(heading,32);
        for(auto* c:std::initializer_list<juce::Component*>{&nameLabel,&name,&typeLabel,&type,&enabled,&armed,&monitor,&heightLabel,&height,&sourceLabel,&category,&media,&identity,&settingsLabel,&widthLabel,&pixelsLabel,&fpsLabel,&rateLabel,&channelsLabel,&width,&pixels,&fps,&rate,&channels,&applySource,&clearSource,&note,&automation,&remove})c->setVisible(valid);
        effects.setVisible(valid);
        if(!valid){form.setSize(w+20,std::max(70,view.getHeight()));return;}
        const int i=owner.document.sequence.findTrack(owner.selectedTrack);if(i<0)return;
        const auto& t=owner.document.sequence.tracks[(size_t)i];const bool av=t.type=="video" || t.type=="audio";
        row(nameLabel,18);row(name);row(typeLabel,18);row(type);row(enabled);
        armed.setVisible(av);monitor.setVisible(av);if(av){row(armed);row(monitor);}
        row(heightLabel,18);row(height);row(effects);
        const bool sources=av || t.type=="image";
        sourceLabel.setVisible(sources);category.setVisible(sources);applySource.setVisible(sources);clearSource.setVisible(sources);
        if(sources){row(sourceLabel,18);row(category);}
        const auto cat=selectedCategory();const bool live=cat!="none" && cat!="media";
        const bool device=cat=="camera" || cat=="microphone";const bool visual=cat=="display" || cat=="window" || device;
        monitor.setButtonText(cat=="microphone"?"Listen to microphone (use headphones)":visual?"Live capture preview":"Monitor preference (inactive)");
        rescan.setVisible(sources && visual);
        media.setVisible(sources && (cat=="media" || visual));identity.setVisible(sources && live && !visual);
        if(sources && (cat=="media" || visual))row(media);
        if(sources && visual)row(rescan);
        captureFormat.setVisible(device);signal.setVisible(device);if(device){row(captureFormat);row(signal,64);}
        if(sources && live && !visual)row(identity);
        const bool videoSettings=live && !visual && t.type=="video",audioSettings=live && !visual && t.type=="audio";
        widthLabel.setVisible(valid && videoSettings);pixelsLabel.setVisible(valid && videoSettings);fpsLabel.setVisible(valid && videoSettings);rateLabel.setVisible(valid && audioSettings);channelsLabel.setVisible(valid && audioSettings);
        settingsLabel.setVisible(live && !visual);width.setVisible(videoSettings);pixels.setVisible(videoSettings);fps.setVisible(videoSettings);rate.setVisible(audioSettings);channels.setVisible(audioSettings);
        if(live && !visual)row(settingsLabel,20);
        if(videoSettings){row(widthLabel,18);row(width);row(pixelsLabel,18);row(pixels);row(fpsLabel,18);row(fps);}
        if(audioSettings){row(rateLabel,18);row(rate);row(channelsLabel,18);row(channels);}
        if(sources){row(applySource);row(clearSource);}
        note.setText(device?"Select a supported device format, then Apply Source. Microphone levels run while the track is enabled. Listening is off for new assignments: use headphones to avoid speaker feedback. Shared tracks must request compatible formats. Recording is not active.":visual?"Apply a source to preview it in Source Monitor. Live preview starts on assignment; uncheck Live capture preview to stop. Tracks sharing a source use one acquisition. Window selections must be renewed after restarting Movie. Recording is not active.":live?"Configuration only. No live device discovery, capture or monitoring is active.":t.type=="effects"?"Effects-track routing is undefined. Use effects on a video, audio or image track.":"Source changes keep existing clips. Arm remains a saved preference only.",juce::dontSendNotification);
        row(note,visual?130:68);automation.setVisible(t.type=="automation");if(t.type=="automation")row(automation,100);
        row(remove);form.setSize(w+20,std::max(y,view.getHeight()));
    }
    void populateMedia()
    {
        const int old=media.getSelectedId()-1;juce::String chosen=juce::isPositiveAndBelow(old,(int)mediaIds.size())?mediaIds[(size_t)old]:juce::String();
        media.clear(juce::dontSendNotification);mediaIds.clear();
        const int i=owner.document.sequence.findTrack(owner.selectedTrack);if(i<0)return;
        const auto categoryToken=selectedCategory();
        if(categoryToken=="display" || categoryToken=="window" || categoryToken=="camera" || categoryToken=="microphone")
        {
            if(chosen.isEmpty())chosen=owner.document.sequence.tracks[(size_t)i].source["identity"].toString();
            for(const auto& source:owner.visualCapture.sources())if(source.kind==categoryToken){mediaIds.push_back(source.id);media.addItem(source.name,(int)mediaIds.size());}
            for(size_t n=0;n<mediaIds.size();++n)if(mediaIds[n]==chosen)media.setSelectedId((int)n+1,juce::dontSendNotification);
            media.setTextWhenNothingSelected("Choose an available "+categoryToken+"...");populateFormats();return;
        }
        const auto& typeToken=owner.document.sequence.tracks[(size_t)i].type;
        for(const auto& m:owner.document.sequence.media)
            if((typeToken=="video" && m.info.hasVideo) || (typeToken=="audio" && m.info.hasAudio) || (typeToken=="image" && m.info.still))
            {mediaIds.push_back(m.id);media.addItem(m.name,(int)mediaIds.size());}
        media.setTextWhenNothingSelected(mediaIds.empty()?"Import compatible media first":"Choose project media...");
    }
    void populateFormats()
    {
        const int selected=captureFormat.getSelectedId()-1;juce::var previous;
        if(juce::isPositiveAndBelow(selected,formats.size()))previous=formats[selected];
        captureFormat.clear(juce::dontSendNotification);formats.clear();
        const int mi=media.getSelectedId()-1;if(!juce::isPositiveAndBelow(mi,(int)mediaIds.size()))return;
        const int ti=owner.document.sequence.findTrack(owner.selectedTrack);
        if((syncing || previous.isVoid()) && ti>=0)previous=owner.document.sequence.tracks[(size_t)ti].source["settings"];
        for(const auto& source:owner.visualCapture.sources())if(source.id==mediaIds[(size_t)mi])
            for(const auto& f:source.formats)
            {
                formats.add(f);juce::String label;
                juce::String subtype=f["format"].toString();const auto fourcc=(juce::uint32)subtype.substring(1,9).getHexValue32();juce::String readable;
                for(int byte=0;byte<4;++byte){const auto c=(juce::juce_wchar)((fourcc>>(byte*8))&255);if(c<32 || c>126){readable={};break;}readable+=juce::String::charToString(c);}
                if(readable.isNotEmpty())subtype=readable;
                if(source.kind=="camera")label=juce::String((int)f["width"])+" x "+juce::String((int)f["height"])+" / "+juce::String((int)f["fpsNum"]/(double)std::max(1,(int)f["fpsDen"]),3)+" fps / "+subtype;
                else label=juce::String((int)f["sampleRate"])+" Hz / "+juce::String((int)f["channels"])+" channels / float32";
                captureFormat.addItem(label,formats.size());
                if(juce::JSON::toString(previous)==juce::JSON::toString(f))captureFormat.setSelectedId(formats.size(),juce::dontSendNotification);
            }
        if(captureFormat.getSelectedId()==0 && formats.size()>0)captureFormat.setSelectedId(1,juce::dontSendNotification);
        captureFormat.setTextWhenNothingSelected("No supported formats available");
    }
    void rename()
    {
        const int i=owner.document.sequence.findTrack(trackId);
        if(!syncing && i>=0 && name.getText()!=owner.document.sequence.tracks[(size_t)i].name)mutate("rename",movie::object({{"name",name.getText()}}));
    }
    void mutate(const char* operation,juce::var a)
    {
        if(syncing || trackId.isEmpty())return;
        a.getDynamicObject()->setProperty("trackId",trackId);owner.runUi("movie.track."+juce::String(operation),a);sync();
    }
    void saveSource()
    {
        const auto cat=selectedCategory();
        if(cat=="none"){mutate("source",movie::object({{"source",juce::var()}}));return;}
        juce::String id=identity.getText().trim();
        if(cat=="media" || cat=="display" || cat=="window" || cat=="camera" || cat=="microphone")
        {
            const int mi=media.getSelectedId()-1;
            if(!juce::isPositiveAndBelow(mi,(int)mediaIds.size())){owner.warn("Choose an available source first.");return;}
            id=mediaIds[(size_t)mi];
        }
        if(id.isEmpty()){owner.warn("Enter a label for the saved source configuration.");return;}
        auto settings=movie::object({});
        const int ti=owner.document.sequence.findTrack(trackId);
        if(ti>=0 && owner.document.sequence.tracks[(size_t)ti].source["category"].toString()==cat
            && owner.document.sequence.tracks[(size_t)ti].source["settings"].isObject())settings=owner.document.sequence.tracks[(size_t)ti].source["settings"].clone();
        auto put=[&](juce::TextEditor& e,const char* key){settings.getDynamicObject()->removeProperty(key);if(e.isVisible() && e.getText().isNotEmpty())settings.getDynamicObject()->setProperty(key,e.getText().getIntValue());};
        put(width,"width");put(pixels,"height");put(fps,"fpsNum");put(rate,"sampleRate");put(channels,"channels");
        if(fps.isVisible() && fps.getText().isNotEmpty() && (ti<0 || fps.getText()!=owner.document.sequence.tracks[(size_t)ti].source["settings"]["fpsNum"].toString()))
            settings.getDynamicObject()->setProperty("fpsDen",1);
        if(cat=="camera" || cat=="microphone")
        {const int fi=captureFormat.getSelectedId()-1;if(!juce::isPositiveAndBelow(fi,formats.size())){owner.warn("Choose a supported capture format first.");return;}settings=formats[fi].clone();}
        mutate("source",movie::object({{"source",movie::object({{"category",cat},{"identity",id},{"settings",settings}})}}));
        if(cat=="display" || cat=="window" || cat=="camera" || cat=="microphone"){owner.selectedMedia={};owner.sourcePlayback.play(false);owner.syncVisualCapture();owner.dock->activatePanel("source");}
    }
    MainComponent& owner;juce::Viewport view;juce::Component form;bool syncing=false,valid=false;juce::String trackId;
    std::vector<juce::String> types{"video","audio","image","effects","automation"},categories,mediaIds;
    juce::Label heading,nameLabel,typeLabel,heightLabel,sourceLabel,settingsLabel,note,automation,widthLabel,pixelsLabel,fpsLabel,rateLabel,channelsLabel;
    juce::TextEditor name,identity,width,pixels,fps,rate,channels;
    juce::ComboBox type,height,category,media,captureFormat;
    juce::Label signal;juce::Array<juce::var> formats;
    juce::ToggleButton enabled{"Enabled"},armed{"Arm preference (no recording)"},monitor{"Monitor preference (inactive)"};
    juce::TextButton applySource{"Apply Source"},clearSource{"Clear Source"},remove{"Remove Track..."};
    juce::TextButton effects{"Effects Browser / Inspector..."};
    juce::TextButton rescan{"Refresh available sources"};
    juce::String catalogStamp;
};
void MainComponent::selectTrack(const juce::String& id,bool showInspector)
{
    if(document.sequence.findTrack(id)<0)return;
    selectedTransition={};selectedTrack=id;selectedClip={};
    const auto cat=document.sequence.tracks[(size_t)document.sequence.findTrack(id)].source["category"].toString();
    if(cat=="display" || cat=="window" || cat=="camera" || cat=="microphone"){selectedMedia={};sourcePlayback.play(false);if(dock)dock->activatePanel("source");}
    if(trackInspector)trackInspector->sync();
    if(effectsPanel)effectsPanel->sync();
    if(inspector)inspector->syncFromSelection();
    if(showInspector && dock)dock->activatePanel("track-inspector");
    if(timeline)timeline->repaint();
}
void MainComponent::renameTrack(){if(timeline)timeline->beginRename();}
void MainComponent::addTrackMenu(juce::Component* button)
{
    juce::PopupMenu m;int i=0;
    for(const auto* type:{"Video","Audio","Image","Effects","Automation"})m.addItem(++i,type);
    const juce::Component::SafePointer<MainComponent> safe(this);
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(button),[safe](int choice){
        if(!safe || choice<1 || choice>5)return;
        static const char* types[]{"video","audio","image","effects","automation"};
        safe->createTrack(types[choice-1]);
    });
}
void MainComponent::createTrack(const juce::String& type)
{
    const auto count=document.sequence.tracks.size();
    runUi("movie.track.create",movie::object({{"type",type}}));
    if(document.sequence.tracks.size()>count)selectTrack(document.sequence.tracks.back().id);
}
void MainComponent::removeTrack(const juce::String& id)
{
    const int i=document.sequence.findTrack(id);if(i<0)return;
    const auto& t=document.sequence.tracks[(size_t)i];
    const bool occupied=std::any_of(document.sequence.clips.begin(),document.sequence.clips.end(),[&](const movie::Clip& c){return document.sequence.owner(c)==id;});
    const bool configured=!t.effects.empty() || !t.source.isVoid() || t.armed || t.monitoring || !t.points.empty() || t.targetTrackId.isNotEmpty() || t.targetClipId.isNotEmpty() || t.targetEffectId.isNotEmpty() || t.parameterId.isNotEmpty();
    if(!occupied && !configured){runUi("movie.track.remove",movie::object({{"trackId",id}}));return;}
    const juce::Component::SafePointer<MainComponent> safe(this);
    juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::WarningIcon,"Remove "+t.name+"?",
        "Remove this track, its configuration and its timeline placements? Imported media will be kept. You can undo this operation.","Remove Track","Cancel",this,
        juce::ModalCallbackFunction::create([safe,id](int result){if(safe && result==1)safe->runUi("movie.track.remove",movie::object({{"trackId",id},{"removePlacements",true}}));}));
}
void MainComponent::trackMenu(const juce::String& id,juce::Point<int> point)
{
    const int i=document.sequence.findTrack(id);if(i<0)return;
    juce::PopupMenu m;m.addItem(1,"Rename Track...");m.addItem(2,"Track Settings...");m.addItem(3,"Enabled",true,document.sequence.tracks[(size_t)i].enabled);
    m.addSeparator();m.addItem(4,"Move Up",i>0);m.addItem(5,"Move Down",i+1<(int)document.sequence.tracks.size());m.addSeparator();m.addItem(6,"Remove Track...");
    const juce::Component::SafePointer<MainComponent> safe(this);
    m.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea({point.x,point.y,1,1}),[safe,id](int choice){
        if(!safe)return;const int index=safe->document.sequence.findTrack(id);if(index<0)return;
        if(choice==1){safe->selectTrack(id,false);safe->renameTrack();}
        if(choice==2)safe->selectTrack(id);
        if(choice==3)safe->runUi("movie.track.enabled",movie::object({{"trackId",id},{"enabled",!safe->document.sequence.tracks[(size_t)index].enabled}}));
        if(choice==4 || choice==5)safe->runUi("movie.track.reorder",movie::object({{"trackId",id},{"order",index+(choice==4?-1:1)}}));
        if(choice==6)safe->removeTrack(id);
    });
}
