// The selected timeline lane is edited in the existing Effects dock.
class MainComponent::AutomationEditor final : public juce::Component
{
public:
    explicit AutomationEditor(MainComponent& o):owner(o)
    {
        for(auto* c:std::initializer_list<juce::Component*>{&target,&points,&timeLabel,&time,&valueLabel,&value,&shape,&tension,&add,&apply,&remove,&removeLane,&visible,&hint})form.addAndMakeVisible(c);
        addAndMakeVisible(view);view.setViewedComponent(&form,false);view.setScrollBarsShown(true,false);
        target.setJustificationType(juce::Justification::topLeft);hint.setJustificationType(juce::Justification::topLeft);
        timeLabel.setText("Timeline seconds",juce::dontSendNotification);valueLabel.setText("Parameter value",juce::dontSendNotification);
        time.setRange(0,86400,.001);time.setSliderStyle(juce::Slider::IncDecButtons);time.setTextBoxStyle(juce::Slider::TextBoxLeft,false,130,24);
        value.setSliderStyle(juce::Slider::LinearHorizontal);value.setTextBoxStyle(juce::Slider::TextBoxRight,false,100,24);
        for(const auto* name:{"linear","step","sCurve","power","bezier"})shape.addItem(name,shape.getNumItems()+1);
        tension.setRange(-1,1,.01);tension.setSliderStyle(juce::Slider::LinearHorizontal);tension.setTextBoxStyle(juce::Slider::TextBoxRight,false,75,24);tension.setTextValueSuffix(" tension");
        points.onChange=[this]{if(!syncing){const int i=points.getSelectedId()-1;selected=juce::isPositiveAndBelow(i,(int)ids.size())?ids[(size_t)i]:juce::String();sync();}};
        add.onClick=[this]{write(false);};apply.onClick=[this]{write(true);};
        remove.onClick=[this]{owner.runUi("movie.automation.removePoint",movie::object({{"laneId",owner.selectedTrack},{"pointId",selected}}));selected={};sync();};
        visible.onClick=[this]{owner.runUi("movie.automation.visible",movie::object({{"laneId",owner.selectedTrack},{"visible",visible.getToggleState()}}));};
        removeLane.onClick=[this]{owner.runUi("movie.track.remove",movie::object({{"trackId",owner.selectedTrack}}));};
    }
    void selectPoint(const juce::String& id){selected=id;sync();}
    void sync()
    {
        syncing=true;points.clear(juce::dontSendNotification);ids.clear();
        const int i=owner.document.sequence.findTrack(owner.selectedTrack);if(i<0){syncing=false;return;}
        const auto& lane=owner.document.sequence.tracks[(size_t)i];const movie::Effect* effect=nullptr;
        const auto* p=movie::automationParameter(owner.document.sequence,lane,&effect);
        if(laneId!=lane.id){laneId=lane.id;selected={};}
        juce::String text="Unresolved legacy target. Select an effect parameter and choose Automate to create a working lane.";
        if(p)
        {
            const auto& track=owner.document.sequence.tracks[(size_t)owner.document.sequence.findTrack(lane.targetTrackId)];
            text=track.name+"\n"+movie::effectDefinition(effect->type)->name+" / "+p->name+(p->units.isEmpty()?juce::String():" ("+p->units+")");
            text+=!lane.enabled?"\nLane disabled: manual value plays.":!effect->enabled || effect->bypassed?"\nEffect off/bypassed: curve retained.":"\nTimeline automation active";
            value.setRange(p->minimum,p->maximum,p->step);value.setTextValueSuffix(p->units.isEmpty()?juce::String():" "+p->units);
            for(const auto& point:lane.points)
            {
                ids.push_back(point.id);const double actual=p->minimum+point.value*(p->maximum-p->minimum);
                points.addItem(juce::String(point.seconds,3)+"s : "+juce::String(actual,3),(int)ids.size());
            }
            if(std::find(ids.begin(),ids.end(),selected)==ids.end())selected=ids.empty()?juce::String():ids.front();
            for(size_t n=0;n<lane.points.size();++n)if(lane.points[n].id==selected)
            {
                const auto& point=lane.points[n];points.setSelectedId((int)n+1,juce::dontSendNotification);
                time.setValue(point.seconds,juce::dontSendNotification);value.setValue(p->minimum+point.value*(p->maximum-p->minimum),juce::dontSendNotification);
                shape.setSelectedId((int)point.shape+1,juce::dontSendNotification);tension.setValue(point.tension,juce::dontSendNotification);
            }
            if(lane.points.empty()){time.setValue(owner.playback.position(),juce::dontSendNotification);value.setValue(effect->value(p->id),juce::dontSendNotification);shape.setSelectedId(1,juce::dontSendNotification);}
        }
        visible.setToggleState(lane.automationVisible,juce::dontSendNotification);
        target.setText(text,juce::dontSendNotification);
        hint.setText("Double-click a timeline lane to add a keyframe. Drag a point to move its time/value. Here, set exact time/value then Apply, or Add at playhead. Shape and tension describe the outgoing segment. Choices always step; endpoints hold.",juce::dontSendNotification);
        add.setEnabled(p!=nullptr);apply.setEnabled(p && selected.isNotEmpty());remove.setEnabled(p && selected.isNotEmpty());
        time.setEnabled(p!=nullptr);value.setEnabled(p!=nullptr);shape.setEnabled(p && p->kind!="choice");tension.setEnabled(p && p->kind!="choice");syncing=false;
    }
    void resized()override
    {
        view.setBounds(getLocalBounds());const int width=std::max(180,view.getMaximumVisibleWidth());form.setSize(width,std::max(720,view.getHeight()));
        auto a=form.getLocalBounds().reduced(10);target.setBounds(a.removeFromTop(90));points.setBounds(a.removeFromTop(28));a.removeFromTop(8);
        timeLabel.setBounds(a.removeFromTop(22));time.setBounds(a.removeFromTop(28));valueLabel.setBounds(a.removeFromTop(22));value.setBounds(a.removeFromTop(28));a.removeFromTop(8);
        shape.setBounds(a.removeFromTop(28));tension.setBounds(a.removeFromTop(28));a.removeFromTop(8);
        add.setBounds(a.removeFromTop(28));apply.setBounds(a.removeFromTop(28));remove.setBounds(a.removeFromTop(28));removeLane.setBounds(a.removeFromTop(28));visible.setBounds(a.removeFromTop(28));hint.setBounds(a);
    }
private:
#if JUCE_DEBUG
    friend class MainComponent::TrackUiCheck;
#endif
    void write(bool existing)
    {
        auto a=movie::object({{"laneId",owner.selectedTrack},{"seconds",existing?time.getValue():owner.playback.position()},
            {"value",value.getValue()},{"shape",shape.getText()},{"tension",tension.getValue()}});
        if(existing)a.getDynamicObject()->setProperty("pointId",selected);
        owner.runUi("movie.automation.point",a);sync();
    }
    MainComponent& owner;bool syncing=false;juce::String laneId,selected;std::vector<juce::String> ids;
    juce::Viewport view;juce::Component form;
    juce::Label target,timeLabel,valueLabel,hint;juce::ComboBox points,shape;juce::Slider time,value,tension;
    juce::ToggleButton visible{"Show lane in timeline (processing stays active)"};
    juce::TextButton add{"Add at playhead"},apply{"Apply keyframe changes"},remove{"Remove keyframe"},removeLane{"Remove automation lane"};
};
