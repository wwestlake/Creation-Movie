#include <cstdlib>
#include <charconv>
class MainComponent::TransitionPanel final : public juce::Component
{
public:
    explicit TransitionPanel(MainComponent& o):owner(o)
    {
        addAndMakeVisible(view);view.setViewedComponent(&form,false);view.setScrollBarsShown(true,false);
        auto label=[&](const char* text,juce::Component& field){auto* l=labels.add(new juce::Label);l->setText(text,juce::dontSendNotification);form.addAndMakeVisible(l);form.addAndMakeVisible(field);fields.push_back(&field);};
        label("Saved transition / New",existing);label("Type",type);label("Outgoing clip",outgoing);label("Incoming clip",incoming);
        label("Start (seconds)",start);label("Duration (seconds)",duration);label("Transition curve",curve);label("Direction (wipe)",direction);
        label("Edge softness (0-1)",softness);label("Black midpoint (0.01-0.99)",midpoint);label("Curve tension (-1 to 1)",tension);
        for(auto* c:std::initializer_list<juce::Component*>{&end,&fresh,&apply,&remove,&message})form.addAndMakeVisible(c);
        for(const auto* token:{"video.dissolve","video.black","video.wipe","audio.crossfade"}){typeTokens.add(token);type.addItem(movie::transitionName(token),type.getNumItems()+1);}
        direction.addItem("Left to Right",1);direction.addItem("Right to Left",2);direction.addItem("Top to Bottom",3);direction.addItem("Bottom to Top",4);
        type.onChange=[this]{if(!syncing)curves();};
        existing.onChange=[this]{if(syncing)return;const int i=existing.getSelectedId()-2;owner.showTransition(juce::isPositiveAndBelow(i,(int)transitionIds.size())?transitionIds[(size_t)i]:juce::String());};
        fresh.onClick=[this]{owner.selectedTransition={};sync();};
        apply.onClick=[this]{write();};remove.onClick=[this]{owner.runUi("movie.transition.remove",movie::object({{"transitionId",owner.selectedTransition}}));};
        message.setJustificationType(juce::Justification::topLeft);
    }
    void sync()
    {
        syncing=true;existing.clear(juce::dontSendNotification);existing.addItem("New transition",1);transitionIds.clear();
        const movie::Transition* selected=nullptr;
        for(const auto& tr:owner.document.sequence.transitions)
        {transitionIds.push_back(tr.id);existing.addItem(movie::transitionName(tr.type)+"  "+juce::String(tr.start,3)+"s",(int)transitionIds.size()+1);if(tr.id==owner.selectedTransition){selected=&tr;existing.setSelectedId((int)transitionIds.size()+1,juce::dontSendNotification);}}
        outgoing.clear(juce::dontSendNotification);incoming.clear(juce::dontSendNotification);clipIds.clear();
        for(const auto& c:owner.document.sequence.clips){clipIds.push_back(c.id);const int id=(int)clipIds.size();const auto name=c.displayName+"  @ "+juce::String(c.startSeconds,3)+"s";outgoing.addItem(name,id);incoming.addItem(name,id);}
        movie::Transition draft;if(selected)draft=*selected;
        else
        {
            existing.setSelectedId(1,juce::dontSendNotification);owner.selectedTransition={};
            const int ai=owner.document.sequence.findClip(owner.selectedClip);
            if(ai>=0)
            {
                const auto& a=owner.document.sequence.clips[(size_t)ai];draft.outgoing=a.id;draft.trackId=owner.document.sequence.owner(a);
                if(a.kind==creation::timeline::ClipKind::audio)draft.type="audio.crossfade";
                double next=1e30;for(const auto& b:owner.document.sequence.clips)if(owner.document.sequence.owner(b)==draft.trackId && b.startSeconds>a.startSeconds && b.startSeconds<next){next=b.startSeconds;draft.incoming=b.id;}
                draft.start=std::max(a.startSeconds,a.startSeconds+a.durationSeconds-.5);
            }
        }
        auto select=[&](juce::ComboBox& box,const juce::String& id){auto it=std::find(clipIds.begin(),clipIds.end(),id);box.setSelectedId(it==clipIds.end()?0:(int)(it-clipIds.begin())+1,juce::dontSendNotification);};
        select(outgoing,draft.outgoing);select(incoming,draft.incoming);type.setSelectedId(1,juce::dontSendNotification);
        for(int i=0;i<type.getNumItems();++i)if(typeTokens[i]==draft.type)type.setSelectedId(i+1,juce::dontSendNotification);
        curves();curve.setSelectedId(curveTokens.indexOf(draft.curve)+1,juce::dontSendNotification);direction.setSelectedId(directionTokens.indexOf(draft.direction)+1,juce::dontSendNotification);
        auto exact=[](double value){char text[64];const auto result=std::to_chars(text,text+sizeof(text),value);return juce::String::fromUTF8(text,(int)(result.ptr-text));};
        start.setText(exact(draft.start),false);duration.setText(exact(draft.duration),false);softness.setText(exact(draft.softness),false);midpoint.setText(exact(draft.midpoint),false);tension.setText(exact(draft.tension),false);
        end.setText("End: "+juce::String(draft.end(),6)+" s",juce::dontSendNotification);remove.setEnabled(selected!=nullptr);apply.setButtonText(selected?"Apply exact properties":"Create transition");
        message.setText("Same-track adjacent/overlapping clips only. The transition covers the entire existing overlap. Extra duration consumes real source handles; invalid requests preserve all clips. Video transitions do not automatically crossfade sound: add an audio transition too.",juce::dontSendNotification);
        syncing=false;resized();
    }
    void resized()override
    {
        view.setBounds(getLocalBounds());const int w=std::max(180,getWidth()-view.getScrollBarThickness()-20);int y=8;
        for(int i=0;i<labels.size();++i){labels[i]->setBounds(10,y,w,20);y+=20;fields[(size_t)i]->setBounds(10,y,w,26);y+=31;}
        for(auto* c:std::initializer_list<juce::Component*>{&end,&fresh,&apply,&remove}){c->setBounds(10,y,w,28);y+=32;}
        message.setBounds(10,y,w,150);form.setSize(w+20,y+158);
    }
private:
#if JUCE_DEBUG
    friend class MainComponent::TrackUiCheck;
#endif
    void curves()
    {const auto old=curveTokens[curve.getSelectedId()-1];curve.clear(juce::dontSendNotification);curveTokens=typeTokens[type.getSelectedId()-1]=="audio.crossfade"?juce::StringArray{"linear","equalPower"}:juce::StringArray{"linear","sCurve","power","bezier"};for(const auto& name:curveTokens)curve.addItem(name=="equalPower"?"Equal Power":name=="sCurve"?"Smooth (S-curve)":name=="linear"?"Linear":name=="power"?"Power":"Bezier",curve.getNumItems()+1);curve.setSelectedId(std::max(0,curveTokens.indexOf(old))+1,juce::dontSendNotification);}
    void write()
    {
        auto a=movie::object({{"type",typeTokens[type.getSelectedId()-1]},{"curve",curveTokens[curve.getSelectedId()-1]},{"direction",directionTokens[direction.getSelectedId()-1]}});
        for(const auto& field:std::initializer_list<std::pair<const char*,juce::TextEditor*>>{{"start",&start},{"duration",&duration},{"softness",&softness},{"midpoint",&midpoint},{"tension",&tension}})
        {const auto text=field.second->getText().trim();char* end=nullptr;const double v=std::strtod(text.toRawUTF8(),&end);if(text.isEmpty() || !text.containsOnly("0123456789+-.eE") || !end || *end!=0 || !std::isfinite(v)){owner.warn("Enter a finite number for "+juce::String(field.first));return;}a.getDynamicObject()->setProperty(field.first,v);}
        const int ai=outgoing.getSelectedId()-1,bi=incoming.getSelectedId()-1;if(!juce::isPositiveAndBelow(ai,(int)clipIds.size()) || !juce::isPositiveAndBelow(bi,(int)clipIds.size())){owner.warn("Choose outgoing and incoming clips.");return;}
        a.getDynamicObject()->setProperty("outgoing",clipIds[(size_t)ai]);a.getDynamicObject()->setProperty("incoming",clipIds[(size_t)bi]);
        a.getDynamicObject()->setProperty("trackId",owner.document.sequence.owner(owner.document.sequence.clips[(size_t)owner.document.sequence.findClip(clipIds[(size_t)ai])]));
        const bool creating=owner.selectedTransition.isEmpty();if(!creating)a.getDynamicObject()->setProperty("transitionId",owner.selectedTransition);
        const auto count=owner.document.sequence.transitions.size();owner.runUi(creating?"movie.transition.create":"movie.transition.update",a);
        if(creating && owner.document.sequence.transitions.size()>count)owner.showTransition(owner.document.sequence.transitions.back().id);
    }
    juce::StringArray typeTokens,curveTokens,directionTokens{"leftToRight","rightToLeft","topToBottom","bottomToTop"};
    MainComponent& owner;bool syncing=false;juce::Viewport view;juce::Component form;juce::OwnedArray<juce::Label> labels;std::vector<juce::Component*> fields;std::vector<juce::String> clipIds,transitionIds;
    juce::ComboBox existing,type,outgoing,incoming,curve,direction;juce::TextEditor start,duration,softness,midpoint,tension;juce::Label end,message;
    juce::TextButton fresh{"New from selected clip"},apply{"Create transition"},remove{"Delete transition"};
};
