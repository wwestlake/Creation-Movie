class MainComponent::EffectsPanel final : public juce::Component
{
public:
#if JUCE_DEBUG
    friend class MainComponent::TrackUiCheck;
#endif
    explicit EffectsPanel(MainComponent& o):automation(o),owner(o)
    {
        for(auto* c:std::initializer_list<juce::Component*>{&context,&browserLabel,&browser,&add,&chainLabel,&chain,&up,&down,&remove,&enabled,&bypass,&notice,&view})addAndMakeVisible(c);
        addAndMakeVisible(automation);
        view.setViewedComponent(&parameters,false);view.setScrollBarsShown(true,false);
        browserLabel.setText("Available effects",juce::dontSendNotification);chainLabel.setText("Processing order",juce::dontSendNotification);
        add.onClick=[this]{
            const int i=browser.getSelectedId()-1;if(!juce::isPositiveAndBelow(i,(int)available.size()))return;
            const int ti=owner.document.sequence.findTrack(owner.selectedTrack);if(ti<0)return;
            const auto count=owner.document.sequence.tracks[(size_t)ti].effects.size();
            owner.runUi("movie.effect.add",movie::object({{"trackId",owner.selectedTrack},{"type",available[(size_t)i]}}));
            if(owner.document.sequence.tracks[(size_t)ti].effects.size()>count)selected=owner.document.sequence.tracks[(size_t)ti].effects.back().id;
            sync();
        };
        chain.onChange=[this]{if(!syncing){const int i=chain.getSelectedId()-1;selected=juce::isPositiveAndBelow(i,(int)ids.size())?ids[(size_t)i]:juce::String();sync();}};
        remove.onClick=[this]{command("remove");};
        up.onClick=[this]{move(-1);};down.onClick=[this]{move(1);};
        enabled.onClick=[this]{command("enabled",movie::object({{"value",enabled.getToggleState()}}));};
        bypass.onClick=[this]{command("bypass",movie::object({{"value",bypass.getToggleState()}}));};
        notice.setJustificationType(juce::Justification::topLeft);
    }
    void selectPoint(const juce::String& id){automation.selectPoint(id);}
    void sync()
    {
        syncing=true;const int ti=owner.document.sequence.findTrack(owner.selectedTrack);
        const bool isAutomation=ti>=0 && owner.document.sequence.tracks[(size_t)ti].type=="automation";
        for(auto* child:getChildren())child->setVisible(child==&automation?isAutomation:!isAutomation);
        if(isAutomation){automation.sync();syncing=false;resized();return;}
        browser.clear(juce::dontSendNotification);chain.clear(juce::dontSendNotification);available.clear();ids.clear();
        const movie::Effect* effect=nullptr;
        if(ti>=0)
        {
            const auto& t=owner.document.sequence.tracks[(size_t)ti];context.setText("Track: "+t.name,juce::dontSendNotification);
            for(const auto& d:movie::effectCatalog())if(movie::compatibleEffect(t.type,d.id))
            {available.push_back(d.id);browser.addItem(d.domain.toUpperCase()+" / "+d.name,(int)available.size());}
            for(const auto& e:t.effects)
            {
                ids.push_back(e.id);const auto* d=movie::effectDefinition(e.type);
                chain.addItem(juce::String(ids.size())+". "+d->name+(!e.enabled?" [off]":e.bypassed?" [bypassed]":""),(int)ids.size());
            }
            if(std::find(ids.begin(),ids.end(),selected)==ids.end())selected=ids.empty()?juce::String():ids.back();
            for(const auto& e:t.effects)if(e.id==selected)effect=&e;
            notice.setText(t.type=="effects"?"Effects-track routing has not been defined. Insert effects on a video, audio or image track.":t.type=="automation"?"Select an automation lane to edit its keyframes here.":"Track effects process after clip gain/opacity and before track mixing/composition. Later visual tracks appear above earlier tracks.",juce::dontSendNotification);
        }
        else{context.setText("Select a track first",juce::dontSendNotification);selected={};notice.setText("",juce::dontSendNotification);}
        browser.setSelectedId(available.empty()?0:std::min(std::max(1,lastBrowser),(int)available.size()),juce::dontSendNotification);
        browser.onChange=[this]{if(!syncing)lastBrowser=browser.getSelectedId();};add.setEnabled(!available.empty());
        const auto found=std::find(ids.begin(),ids.end(),selected);const int index=found==ids.end()?-1:(int)(found-ids.begin());
        chain.setSelectedId(index+1,juce::dontSendNotification);
        enabled.setEnabled(effect!=nullptr);bypass.setEnabled(effect!=nullptr);remove.setEnabled(effect!=nullptr);up.setEnabled(index>0);down.setEnabled(index>=0 && index+1<(int)ids.size());
        if(effect){enabled.setToggleState(effect->enabled,juce::dontSendNotification);bypass.setToggleState(effect->bypassed,juce::dontSendNotification);}
        const auto signature=owner.selectedTrack+selected+(effect?effect->type:juce::String());
        if(signature!=parameterSignature)
        {
            sliders.clear();selectors.clear();labels.clear();automateButtons.clear();parameterSignature=signature;
            if(effect)for(const auto& p:movie::effectDefinition(effect->type)->parameters)
            {
                auto* label=labels.add(new juce::Label);label->setText(p.name+(p.automatable?"":" (manual)"),juce::dontSendNotification);parameters.addAndMakeVisible(label);
                auto* automate=automateButtons.add(new juce::TextButton("Automate"));automate->setComponentID(p.id);automate->setEnabled(p.automatable);parameters.addAndMakeVisible(automate);
                automate->onClick=[this,key=p.id]{openAutomation(key);};
                if(p.kind=="choice")
                {
                    auto* choice=selectors.add(new juce::ComboBox);for(int i=0;i<p.choices.size();++i)choice->addItem(p.choices[i],i+1);
                    choice->setComponentID(p.id);choice->setName(p.name);parameters.addAndMakeVisible(choice);
                    choice->onChange=[this,choice,key=p.id]{if(!syncing)parameter(key,choice->getSelectedId()-1,{});};
                }
                else
                {
                    auto* slider=sliders.add(new juce::Slider);slider->setRange(p.minimum,p.maximum,p.step);slider->setSliderStyle(juce::Slider::LinearHorizontal);
                    if(p.units=="Hz")slider->setSkewFactorFromMidPoint(std::sqrt(p.minimum*p.maximum));
                    slider->setTextBoxStyle(juce::Slider::TextBoxRight,false,82,24);slider->setTextValueSuffix(p.units.isEmpty()?juce::String():" "+p.units);slider->setComponentID(p.id);slider->setName(p.name);
                    parameters.addAndMakeVisible(slider);slider->onDragStart=[this]{gesture=juce::Uuid().toString();};slider->onDragEnd=[this]{gesture={};};
                    slider->onValueChange=[this,slider,key=p.id]{if(!syncing)parameter(key,slider->getValue(),gesture);};
                }
            }
        }
        if(effect)
        {
            for(auto* slider:sliders)slider->setValue(effect->value(slider->getComponentID()),juce::dontSendNotification);
            for(auto* selector:selectors)selector->setSelectedId((int)effect->value(selector->getComponentID())+1,juce::dontSendNotification);
        }
        syncing=false;resized();repaint();
    }
    void resized()override
    {
        if(automation.isVisible()){automation.setBounds(getLocalBounds());return;}
        auto area=getLocalBounds().reduced(10);context.setBounds(area.removeFromTop(30));browserLabel.setBounds(area.removeFromTop(22));browser.setBounds(area.removeFromTop(28));area.removeFromTop(5);add.setBounds(area.removeFromTop(26));
        chainLabel.setBounds(area.removeFromTop(24));chain.setBounds(area.removeFromTop(28));area.removeFromTop(5);
        auto buttons=area.removeFromTop(26);up.setBounds(buttons.removeFromLeft(50));down.setBounds(buttons.removeFromLeft(60));remove.setBounds(buttons);
        enabled.setBounds(area.removeFromTop(26));bypass.setBounds(area.removeFromTop(26));notice.setBounds(area.removeFromTop(72));view.setBounds(area);
        int y=0,si=0,ci=0;const int width=std::max(100,view.getMaximumVisibleWidth()-8);
        const int ti=owner.document.sequence.findTrack(owner.selectedTrack);const movie::Effect* effect=nullptr;
        if(ti>=0)for(const auto& e:owner.document.sequence.tracks[(size_t)ti].effects)if(e.id==selected)effect=&e;
        if(effect)for(size_t i=0;i<movie::effectDefinition(effect->type)->parameters.size();++i)
        {
            labels[(int)i]->setBounds(0,y,std::max(20,width-90),22);automateButtons[(int)i]->setBounds(std::max(20,width-90),y,90,22);y+=22;
            const auto& p=movie::effectDefinition(effect->type)->parameters[i];
            if(p.kind=="choice")selectors[ci++]->setBounds(0,y,width,26);else sliders[si++]->setBounds(0,y,width,26);y+=34;
        }
        parameters.setSize(width,std::max(y,view.getHeight()));
    }
    void paint(juce::Graphics& g)override{g.fillAll(creation_movie::branding::panelColour());}
private:
    void openAutomation(const juce::String& key)
    {
        const auto track=owner.selectedTrack,effect=selected;juce::String lane;
        for(const auto& t:owner.document.sequence.tracks)if(t.type=="automation" && t.targetTrackId==track && t.targetEffectId==effect && t.parameterId==key){lane=t.id;break;}
        if(lane.isEmpty())
        {
            owner.runUi("movie.automation.create",movie::object({{"trackId",track},{"effectId",effect},{"parameterId",key}}));
            for(const auto& t:owner.document.sequence.tracks)if(t.type=="automation" && t.targetTrackId==track && t.targetEffectId==effect && t.parameterId==key){lane=t.id;break;}
        }
        if(lane.isNotEmpty()){owner.selectTrack(lane,false);owner.dock->activatePanel("effects");owner.timeline->reveal(lane);}
    }
    void parameter(const juce::String& id,double value,const juce::String& group)
    {command("parameter",movie::object({{"parameterId",id},{"value",value},{"gestureId",group}}));}
    void command(const char* operation,juce::var args=movie::object({}))
    {
        if(selected.isEmpty())return;args.getDynamicObject()->setProperty("trackId",owner.selectedTrack);args.getDynamicObject()->setProperty("effectId",selected);
        owner.runUi("movie.effect."+juce::String(operation),args);sync();
    }
    void move(int delta){const auto i=std::find(ids.begin(),ids.end(),selected);if(i!=ids.end())command("reorder",movie::object({{"order",(int)(i-ids.begin())+delta}}));}
    AutomationEditor automation;
    juce::OwnedArray<juce::TextButton> automateButtons;
    MainComponent& owner;bool syncing=false;int lastBrowser=1;juce::String selected,parameterSignature,gesture;
    std::vector<juce::String> available,ids;
    juce::Label context,browserLabel,chainLabel,notice;juce::ComboBox browser,chain;
    juce::TextButton add{"Insert Effect"},up{"Up"},down{"Down"},remove{"Remove Effect"};
    juce::ToggleButton enabled{"Effect enabled"},bypass{"Bypass (keep settings)"};
    juce::Viewport view;juce::Component parameters;juce::OwnedArray<juce::Slider> sliders;juce::OwnedArray<juce::ComboBox> selectors;juce::OwnedArray<juce::Label> labels;
};
