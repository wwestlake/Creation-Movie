// Included inside MainComponent.cpp: this view always reads the document tracks.
class MainComponent::Timeline final : public juce::Component, private juce::ScrollBar::Listener
{
public:
#if JUCE_DEBUG
    friend class MainComponent::TrackUiCheck;
#endif
    explicit Timeline(MainComponent& o) : owner(o)
    {
        addAndMakeVisible(add); addAndMakeVisible(remove); addAndMakeVisible(scroll);
        addAndMakeVisible(rename); rename.setVisible(false);
        add.onClick=[this]{owner.addTrackMenu(&add);};
        remove.onClick=[this]{owner.removeTrack(owner.selectedTrack);};
        scroll.addListener(this);
        rename.onReturnKey=[this]{finishRename();};
        rename.onEscapeKey=[this]{rename.setVisible(false);};
        rename.onFocusLost=[this]{if(rename.isVisible())finishRename();};
        setWantsKeyboardFocus(true);
    }
    void sync()
    {
        const double total=contentHeight(), visible=std::max(0,getHeight()-top);
        scroll.setRangeLimits(0,std::max(total,visible));
        scroll.setCurrentRange(juce::jlimit(0.0,std::max(0.0,total-visible),offset),visible);
        offset=scroll.getCurrentRangeStart();
        remove.setEnabled(owner.document.sequence.findTrack(owner.selectedTrack)>=0);
        repaint();
    }
    void resized() override
    {
        add.setBounds(6,3,110,25); remove.setBounds(122,3,108,25);
        scroll.setBounds(getWidth()-14,top,14,std::max(0,getHeight()-top)); sync();
    }
    int laneY(int index) const
    {
        int y=top-(int)offset;
        for(int i=0;i<index;++i)y+=owner.document.sequence.tracks[(size_t)i].heightPixels;
        return y;
    }
    int laneAt(int y, bool clamp=false) const
    {
        if(y<top && !clamp)return -1;
        int bottom=top-(int)offset;
        const auto& tracks=owner.document.sequence.tracks;
        for(size_t i=0;i<tracks.size();++i)
        {
            bottom+=tracks[i].heightPixels;
            if(y<bottom)return (int)i;
        }
        return clamp && !tracks.empty()?(int)tracks.size()-1:-1;
    }
    void beginRename()
    {
        const int i=owner.document.sequence.findTrack(owner.selectedTrack);
        if(i<0)return;
        const auto& t=owner.document.sequence.tracks[(size_t)i];
        const double y=laneY(i);
        if(y<top || y+t.heightPixels>getHeight())
        {
            scroll.setCurrentRangeStart(std::max(0.0,offset+y-top));
            offset=scroll.getCurrentRangeStart();
        }
        renameId=t.id; rename.setText(t.name,false);
        rename.setBounds(28,laneY(i)+3,headerW-36,24);
        rename.setVisible(true);rename.grabKeyboardFocus();rename.selectAll();
    }
    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff0f1623));
        g.setColour(juce::Colours::silver);g.setFont(juce::FontOptions(11));
        g.drawText("Drag headers to reorder / double-click to rename",242,3,std::max(0,getWidth()-250),25,juce::Justification::centredLeft);
        auto bounds=getLocalBounds().withTrimmedRight(14);
        const double pps=std::max(5.0,owner.pixelsPerSecond);
        const double step=pps>80?0.5:pps>30?1.0:pps>12?5.0:10.0;
        g.setColour(juce::Colour(0xff1b2538));g.fillRect(0,30,bounds.getWidth(),top-30);
        g.setColour(juce::Colours::silver);
        g.drawText("TRACKS  ("+juce::String(owner.document.sequence.tracks.size())+")",8,30,headerW-8,22,juce::Justification::centredLeft);
        for(double s=std::floor(owner.visibleStart/step)*step;s<=owner.visibleStart+std::max(0,bounds.getWidth()-headerW)/pps;s+=step)
        {
            const int x=headerW+(int)std::lround((s-owner.visibleStart)*pps);
            if(x<headerW)continue;
            g.drawText(juce::String(s,step<1?1:0)+"s",x+3,30,48,22,juce::Justification::centredLeft);
        }
        g.saveState();g.reduceClipRegion(0,top,bounds.getWidth(),std::max(0,getHeight()-top));
        const auto& seq=owner.document.sequence;
        if(seq.tracks.empty())
            g.drawText("No tracks yet. Choose Add Track to begin.",bounds.withTrimmedTop(top),juce::Justification::centred);
        for(size_t i=0;i<seq.tracks.size();++i)
        {
            const auto& t=seq.tracks[i];const int y=laneY((int)i),h=t.heightPixels;
            if(y+h<top || y>getHeight())continue;
            const bool selected=t.id==owner.selectedTrack;
            g.setColour(selected?juce::Colour(0xff273f5d):juce::Colour(0xff182234));
            g.fillRect(0,y,headerW,h-2);
            g.setColour(t.enabled?juce::Colours::lightgreen:juce::Colours::grey);g.fillEllipse(8.0f,(float)y+9,12,12);
            g.setColour(t.enabled?juce::Colours::white:juce::Colours::grey);
            g.setFont(juce::FontOptions(12,juce::Font::bold));
            g.drawText(t.name,28,y+3,headerW-36,24,juce::Justification::centredLeft,true);
            g.setFont(juce::FontOptions(10));
            g.drawText(t.type.toUpperCase()+(t.legacyMixed?" / mixed media":"")+(!t.enabled?" / OFF":""),28,y+25,headerW-30,18,juce::Justification::centredLeft);
            if(h>=64)
            {
                if(t.type=="audio" || t.type=="video")
                {
                    g.setColour(t.armed?juce::Colours::salmon:juce::Colours::silver);g.drawText(t.armed?"Arm on":"Arm",28,y+h-22,48,18,juce::Justification::centredLeft);
                    g.setColour(t.monitoring?juce::Colours::lightblue:juce::Colours::silver);g.drawText(t.monitoring?"Mon on":"Mon",85,y+h-22,48,18,juce::Justification::centredLeft);
                }
                g.setColour(juce::Colours::silver);g.drawText("Configure...",151,y+h-22,84,18,juce::Justification::centredLeft);
            }
            g.setColour(!t.enabled?juce::Colour(0xff10141c):selected?juce::Colour(0xff17263b):juce::Colour(0xff111927));
            g.fillRect(headerW,y,std::max(0,bounds.getWidth()-headerW),h-2);
        }
        for(const auto& c:seq.clips)
        {
            const int index=seq.findTrack(seq.owner(c));if(index<0)continue;
            const int y=laneY(index)+3,h=seq.tracks[(size_t)index].heightPixels-8;
            if(y+h<top || y>getHeight())continue;
            const int x=headerW+(int)std::lround((c.startSeconds-owner.visibleStart)*pps);
            const int width=std::max(6,(int)std::lround(c.durationSeconds*pps));
            juce::Rectangle<int> box(x,y,width,h);
            g.saveState();g.reduceClipRegion(headerW,top,std::max(0,bounds.getWidth()-headerW),std::max(0,getHeight()-top));
            auto colour=c.videoParams["title"].toString().isNotEmpty()?juce::Colour(0xff8b5cf6):c.kind==creation::timeline::ClipKind::audio?juce::Colour(0xffea580c):juce::Colour(0xff2563eb);
            g.setColour(colour.withAlpha(seq.trackEnabled(c)?0.8f:0.25f));g.fillRoundedRectangle(box.toFloat(),4);
            g.setColour(c.id==owner.selectedClip?juce::Colours::yellow:juce::Colours::silver);g.drawRoundedRectangle(box.toFloat(),4,1);
            g.setFont(juce::FontOptions(12));g.drawText(c.displayName,box.reduced(6),juce::Justification::centredLeft,true);
            g.restoreState();
        }
        if(dropIndex>=0)
        {
            g.setColour(juce::Colours::orange);g.fillRect(0,laneY(dropIndex),bounds.getWidth(),3);
        }
        const int playX=headerW+(int)std::lround((owner.playback.position()-owner.visibleStart)*pps);
        if(playX>=headerW && playX<bounds.getWidth()){g.setColour(juce::Colours::red);g.drawVerticalLine(playX,(float)top,(float)getHeight());}
        g.restoreState();
    }
    void mouseWheelMove(const juce::MouseEvent& e,const juce::MouseWheelDetails& w) override
    {
        if(e.mods.isShiftDown() || std::abs(w.deltaX)>std::abs(w.deltaY))
            owner.visibleStart=std::max(0.0,owner.visibleStart-(w.deltaX!=0?w.deltaX:w.deltaY)*500/std::max(5.0,owner.pixelsPerSecond));
        else scroll.setCurrentRangeStart(offset-w.deltaY*350);
        offset=scroll.getCurrentRangeStart();rename.setVisible(false);repaint();
    }
    void mouseDoubleClick(const juce::MouseEvent& e) override
    {
        if(e.x<headerW && laneAt(e.y)>=0)beginRename();
    }
    void mouseDown(const juce::MouseEvent& e) override
    {
        grabKeyboardFocus();dragClip={};dragTrack={};dropIndex=-1;
        if(e.y<top)
        {
            if(e.x>=headerW)owner.runUi("movie.transport.seek",movie::object({{"time",timeAt(e.x)}}));
            return;
        }
        const int index=laneAt(e.y);if(index<0)return;
        const auto t=owner.document.sequence.tracks[(size_t)index];
        if(e.x<headerW)
        {
            owner.selectTrack(t.id);
            if(e.mods.isPopupMenu()){owner.trackMenu(t.id,e.getScreenPosition());return;}
            const int local=e.y-laneY(index);
            if(e.x<26 && local<28)owner.runUi("movie.track.enabled",movie::object({{"trackId",t.id},{"enabled",!t.enabled}}));
            else if(local>=t.heightPixels-24 && t.heightPixels>=64)
            {
                if((t.type=="audio" || t.type=="video") && e.x<80)owner.runUi("movie.track.preferences",movie::object({{"trackId",t.id},{"armed",!t.armed}}));
                else if((t.type=="audio" || t.type=="video") && e.x<140)owner.runUi("movie.track.preferences",movie::object({{"trackId",t.id},{"monitoring",!t.monitoring}}));
            }
            else dragTrack=t.id;
            return;
        }
        owner.selectTrack(t.id,false);
        const auto time=timeAt(e.x);
        for(const auto& c:owner.document.sequence.clips)
            if(owner.document.sequence.owner(c)==t.id && time>=c.startSeconds && time<=c.startSeconds+c.durationSeconds)
            {
                owner.selectClip(c.id);dragClip=c.id;dragOffset=time-c.startSeconds;
                if(e.mods.isPopupMenu())
                {
                    juce::PopupMenu m;m.addItem(1,"Split Clip Here");m.addItem(2,"Delete Clip");m.addItem(3,"Ripple Delete Clip");
                    const juce::Component::SafePointer<MainComponent> safe(&owner);
                    m.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea({e.getScreenX(),e.getScreenY(),1,1}),[safe,id=c.id,time](int choice){
                        if(!safe)return;
                        if(choice==1)safe->runUi("movie.timeline.split",movie::object({{"clipId",id},{"time",safe->document.sequence.snap(time)}}));
                        if(choice==2 || choice==3)safe->runUi("movie.timeline.remove",movie::object({{"clipId",id},{"ripple",choice==3}}));
                    });dragClip={};
                }
                return;
            }
        owner.selectedClip={};
        if(e.mods.isPopupMenu())
        {
            juce::PopupMenu m;m.addItem(1,"Place Selected Media Here",owner.selectedMedia.isNotEmpty());m.addItem(2,"Track Settings...");
            const juce::Component::SafePointer<MainComponent> safe(&owner);
            m.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea({e.getScreenX(),e.getScreenY(),1,1}),[safe,id=t.id,time](int choice){
                if(!safe)return;
                if(choice==1)safe->runUi("movie.timeline.place",movie::object({{"assetId",safe->selectedMedia},{"trackId",id},{"start",safe->document.sequence.snap(time)}}));
                if(choice==2)safe->selectTrack(id);
            });
        }
        else owner.runUi("movie.transport.seek",movie::object({{"time",owner.document.sequence.snap(time)}}));
    }
    void mouseDrag(const juce::MouseEvent& e) override
    {
        if(!dragTrack.isEmpty() && e.getDistanceFromDragStart()>6)
        {
            if(e.y<top+20)scroll.setCurrentRangeStart(offset-12);
            if(e.y>getHeight()-20)scroll.setCurrentRangeStart(offset+12);
            offset=scroll.getCurrentRangeStart();dropIndex=laneAt(e.y,true);repaint();
        }
    }
    void mouseUp(const juce::MouseEvent& e) override
    {
        if(dragTrack.isNotEmpty() && dropIndex>=0)
            owner.runUi("movie.track.reorder",movie::object({{"trackId",dragTrack},{"order",dropIndex}}));
        if(dragClip.isNotEmpty() && e.getDistanceFromDragStart()>4)
        {
            const int i=laneAt(e.y);
            if(i>=0)owner.runUi("movie.timeline.move",movie::object({{"clipId",dragClip},{"trackId",owner.document.sequence.tracks[(size_t)i].id},{"start",owner.document.sequence.snap(std::max(0.0,timeAt(e.x)-dragOffset))}}));
        }
        dragClip={};dragTrack={};dropIndex=-1;repaint();
    }
private:
    double timeAt(int x) const{return std::max(0.0,owner.visibleStart+(x-headerW)/std::max(5.0,owner.pixelsPerSecond));}
    int contentHeight()const{int h=0;for(const auto& t:owner.document.sequence.tracks)h+=t.heightPixels;return h;}
    void scrollBarMoved(juce::ScrollBar*,double start)override{offset=start;rename.setVisible(false);repaint();}
    void finishRename(){const auto id=renameId,text=rename.getText();rename.setVisible(false);owner.runUi("movie.track.rename",movie::object({{"trackId",id},{"name",text}}));}
    MainComponent& owner;
    static constexpr int headerW=246,top=54;
    juce::TextButton add{"+ Add Track"},remove{"Remove Track"};
    juce::ScrollBar scroll{true};juce::TextEditor rename;
    juce::String dragClip,dragTrack,renameId;double dragOffset=0,offset=0;int dropIndex=-1;
};
