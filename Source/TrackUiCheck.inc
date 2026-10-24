// Debug-only, opt-in integration check of the mounted GUI (not a document test).
// Usage: --track-ui-check "<explicit output directory>". Uses its own VFS project.
#if JUCE_DEBUG
class MainComponent::TrackUiCheck final : private juce::Timer
{
public:
    TrackUiCheck(MainComponent& component,juce::File destination,juce::File media):owner(component),output(std::move(destination)),inputVideo(std::move(media))
    {
        original=owner.session.getProjectId();output.createDirectory();startTimer(250);
    }
private:
    void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
    void passed(const char* message){checks.add(message);}
    void snapshot(const char* name)
    {
        auto stream=output.getChildFile(name).createOutputStream();
        require(stream!=nullptr,"Cannot create GUI snapshot");
        require(juce::PNGImageFormat().writeImageToStream(owner.createComponentSnapshot(owner.getLocalBounds()),*stream),"Cannot render GUI snapshot");
    }
    void record()
    {
        output.getChildFile("track-ui-results.json").replaceWithText(juce::JSON::toString(movie::object({
            {"checks",checks},{"error",error},{"projectId",project},{"nativePointerTested",false}})));
    }
    juce::MouseEvent event(juce::Point<float> position,juce::Point<float> start,bool down)
    {
        return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(),position,
            down?juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier):juce::ModifierKeys(),
            0,0,0,0,0,owner.timeline.get(),owner.timeline.get(),juce::Time::getCurrentTime(),start,juce::Time::getCurrentTime(),1,true);
    }
    void timerCallback() override
    {
        if(owner.job->status=="running")return;
        try
        {
            if(owner.job->status=="failed" && step!=90)throw std::runtime_error(owner.job->error.toStdString());
            auto& inspector=*owner.trackInspector;auto& timeline=*owner.timeline;
            switch(step++)
            {
                case 0:
                    owner.runUi("movie.project.new",movie::object({{"name","Movie GUI demonstration "+juce::Uuid().toString()},{"width",640},{"height",360}}));break;
                case 1:
                    project=owner.session.getProjectId();require(project!=original,"Test project not created");
                    while(!owner.document.sequence.tracks.empty())owner.removeTrack(owner.document.sequence.tracks.back().id);
                    require(owner.document.sequence.tracks.empty(),"Empty document setup failed");snapshot("track-ui-empty.png");passed("Empty real window exposes Add Track");break;
                case 2:owner.createTrack("video");video=owner.selectedTrack;break;
                case 3:owner.createTrack("audio");audio=owner.selectedTrack;break;
                case 4:owner.createTrack("image");image=owner.selectedTrack;break;
                case 5:owner.createTrack("effects");break;
                case 6:
                    owner.createTrack("automation");require(owner.document.sequence.tracks.size()==5,"Five track types not created");
                    require(inspector.automation.isVisible() && !inspector.category.isVisible(),"Automation UI misrepresents capabilities");passed("Five track types populate live timeline and type-specific inspector");break;
                case 7:
                    owner.selectTrack(video);inspector.name.setText("Camera configuration",false);inspector.name.onReturnKey();
                    require(owner.document.sequence.tracks[0].name=="Camera configuration","Inspector rename failed");
                    inspector.enabled.triggerClick();break;
                case 8:
                    require(!owner.document.sequence.tracks[0].enabled,"Enable checkbox did not update document");
                    inspector.enabled.triggerClick();break;
                case 9:
                    inspector.armed.triggerClick();inspector.monitor.triggerClick();break;
                case 10:
                    require(owner.document.sequence.tracks[0].armed && owner.document.sequence.tracks[0].monitoring,"Preference controls failed");
                    inspector.category.setSelectedId(5,juce::sendNotificationSync);inspector.identity.setText("Demo camera (configuration only)",false);
                    inspector.width.setText("1280",false);inspector.pixels.setText("720",false);inspector.fps.setText("30",false);inspector.applySource.triggerClick();break;
                case 11:
                    require(owner.document.sequence.tracks[0].source["category"].toString()=="camera","Source controls failed");
                    inspector.height.setSelectedId(140,juce::sendNotificationSync);
                    require(owner.document.sequence.tracks[0].heightPixels==140,"Height control failed");
                    passed("Name, enable, arm, monitor, source and lane-height GUI controls mutate the authoritative document");break;
                case 12:
                {
                    timeline.scroll.setCurrentRangeStart(0);timeline.offset=0;
                    const juce::Point<float> start(50,(float)timeline.laneY(0)+12),end(50,(float)timeline.laneY(1)+12);
                    timeline.mouseDown(event(start,start,true));timeline.mouseDrag(event(end,start,true));timeline.mouseUp(event(end,start,false));
                    require(owner.document.sequence.findTrack(video)==1,"Header drag did not reorder track");
                    owner.runUi("movie.timeline.undo");require(owner.document.sequence.findTrack(video)==0,"Reorder undo failed");
                    owner.runUi("movie.timeline.redo");require(owner.document.sequence.findTrack(video)==1,"Reorder redo failed");
                    passed("Real timeline header drag handlers preserve identity and support undo/redo");break;
                }
                case 13:
                    owner.selectTrack(video);timeline.beginRename();timeline.rename.setText("Video / configured source",false);timeline.rename.onReturnKey();
                    require(owner.document.sequence.tracks[1].name=="Video / configured source","Inline rename failed");
                    timeline.scroll.setCurrentRangeStart(100);timeline.offset=timeline.scroll.getCurrentRangeStart();
                    require(timeline.laneAt(timeline.laneY(1)+10)==1,"Header/lane scrolling lost alignment");passed("Inline rename and synchronized variable-height scrolling work");break;
                case 14:
                    owner.selectTrack(owner.document.sequence.tracks.back().id);inspector.remove.triggerClick();break;
                case 15:
                    require(owner.document.sequence.tracks.size()==4,"Remove control failed");owner.runUi("movie.timeline.undo");
                    require(owner.document.sequence.tracks.size()==5,"Remove undo failed");owner.runUi("movie.timeline.redo");
                    require(owner.document.sequence.tracks.size()==4,"Remove redo failed");owner.runUi("movie.timeline.undo");
                    passed("Inspector removal and document undo/redo update the live view");
                    if(inputVideo.existsAsFile())
                    {
                        owner.runUi("movie.media.import",movie::object({{"path",inputVideo.getFullPathName()}}));step=20;
                    }
                    else throw std::runtime_error("Pass an existing video after the output directory for the multitrack demonstration.");
                    break;
                case 20:
                    owner.selectTrack(video);owner.selectMedia(owner.document.sequence.media.back().id);owner.library->placeBtn.triggerClick();break;
                case 21:
                {
                    require(owner.document.sequence.clips.size()==1,"Video placement through library failed");
                    const auto file=output.getChildFile("demo-audio.wav");
                    {
                        juce::WavAudioFormat wav;auto stream=file.createOutputStream();
                        std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(stream.release(),48000,1,24,{},0));
                        require(writer!=nullptr,"Cannot create audio fixture");
                        juce::AudioBuffer<float> buffer(1,48000);
                        for(int i=0;i<48000;++i)buffer.setSample(0,i,0.1f*std::sin((float)i*juce::MathConstants<float>::twoPi*440/48000));
                        require(writer->writeFromAudioSampleBuffer(buffer,0,48000),"Cannot write audio fixture");
                    }
                    owner.runUi("movie.media.import",movie::object({{"path",file.getFullPathName()}}));break;
                }
                case 22:
                    owner.selectTrack(audio);owner.selectMedia(owner.document.sequence.media.back().id);owner.library->placeBtn.triggerClick();break;
                case 23:
                {
                    require(owner.document.sequence.clips.size()==2,"Audio placement through library failed");
                    juce::Image picture(juce::Image::ARGB,640,360,true);juce::Graphics g(picture);g.fillAll(juce::Colours::darkgreen.withAlpha(0.5f));
                    g.setColour(juce::Colours::white);g.setFont(juce::FontOptions(30));g.drawText("Image overlay",picture.getBounds(),juce::Justification::centred);
                    const auto file=output.getChildFile("demo-image.png");
                    {auto stream=file.createOutputStream();require(stream!=nullptr,"Cannot create image fixture");juce::PNGImageFormat().writeImageToStream(picture,*stream);}
                    owner.runUi("movie.media.import",movie::object({{"path",file.getFullPathName()}}));break;
                }
                case 24:
                    owner.selectTrack(image);owner.selectMedia(owner.document.sequence.media.back().id);owner.library->placeBtn.triggerClick();break;
                case 25:
                    require(owner.document.sequence.clips.size()==3,"Image placement through library failed");
                    inspector.category.setSelectedId(2,juce::sendNotificationSync);inspector.media.setSelectedId(1,juce::dontSendNotification);inspector.applySource.triggerClick();break;
                case 26:
                    require(owner.document.sequence.tracks[(size_t)owner.document.sequence.findTrack(image)].source["category"].toString()=="media","Project-media source picker failed");
                    for(const auto& c:owner.document.sequence.clips)require(owner.document.sequence.owner(c)==video || owner.document.sequence.owner(c)==audio || owner.document.sequence.owner(c)==image,"Clip ownership mismatch");
                    passed("Video, audio and image media placed through Library on their selected tracks; imported-media source picker works");
                    owner.selectTrack(video);timeline.scroll.setCurrentRangeStart(0);timeline.offset=timeline.scroll.getCurrentRangeStart();
                    step=27;break;
                case 27:
                    snapshot("track-ui-demo.png");saved=juce::JSON::toString(owner.document.sequence.toJson());
                    owner.runUi("movie.project.save");step=16;break;
                case 16:owner.runUi("movie.project.open",movie::object({{"projectId",project}}));break;
                case 17:
                    require(saved==juce::JSON::toString(owner.document.sequence.toJson()),"GUI edits did not survive save/reopen");
                    require(owner.document.sequence.findTrack(video)==1,"Stable identity lost on reopen");passed("GUI-edited multitrack project saves and reopens identically");
                    record();owner.runUi("movie.project.open",movie::object({{"projectId",original},{"discardChanges",true}}));step=90;break;
                case 90:stopTimer();owner.requestQuit();break;
            }
        }
        catch(const std::exception& e)
        {
            error=e.what();record();
            if(step==91){stopTimer();return;}
            owner.runUi("movie.project.open",movie::object({{"projectId",original},{"discardChanges",true}}));step=90;
        }
    }
    MainComponent& owner;juce::File output,inputVideo;juce::String original,project,video,audio,image,saved,error;juce::Array<juce::var> checks;int step=0;
};
#endif
