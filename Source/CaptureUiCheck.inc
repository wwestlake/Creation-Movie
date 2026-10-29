juce::String captureSourceA,captureTrackB,captureFixtureTitle,captureWindowSource;
juce::int64 captureFirstFrame=0;int captureRetries=0;
bool captureUndoDone=false;
bool retryCapture(int stage,bool ready)
{
    if(ready){captureRetries=0;return false;}
    require(++captureRetries<40,("Timed out at capture stage "+juce::String(stage)+": "+juce::JSON::toString(owner.visualCapture.status(),true)).toRawUTF8());step=stage;return true;
}
void chooseCapture(const char* kind,const juce::String& id)
{
    auto& inspector=*owner.trackInspector;inspector.category.setSelectedId(juce::String(kind)=="display"?3:4,juce::sendNotificationSync);
    inspector.populateMedia();for(size_t i=0;i<inspector.mediaIds.size();++i)if(inspector.mediaIds[i]==id)inspector.media.setSelectedId((int)i+1,juce::dontSendNotification);
    inspector.applySource.triggerClick();
}
void captureStep(int stage)
{
    switch(stage)
    {
        case 200:
            require(fixtureExecutable.existsAsFile(),"Supply the built MovieVisualCaptureTests executable for the owned window fixture");
            captureFixtureTitle="Movie capture UI fixture "+juce::Uuid().toString();
            require(captureFixture.start(juce::StringArray{fixtureExecutable.getFullPathName(),"--fixture",captureFixtureTitle}),"Cannot launch owned capture fixture");
            owner.selectTrack(video);for(const auto& source:owner.visualCapture.sources())if(source.kind=="display"){captureSourceA=source.id;break;}
            require(captureSourceA.isNotEmpty(),"No physical display found");chooseCapture("display",captureSourceA);break;
        case 201:
            if(retryCapture(201,owner.visualCapture.preview(video).image.isValid()))break;
            require(owner.liveSourceSelected(),"Source Monitor did not switch to the selected capture track");
            captureFirstFrame=owner.visualCapture.preview(video).frameNumber;
            owner.createTrack("video");captureTrackB=owner.selectedTrack;chooseCapture("display",captureSourceA);break;
        case 202:
            if(retryCapture(202,owner.visualCapture.preview(captureTrackB).subscribers==2))break;
            require((int)owner.visualCapture.status()["activeAcquisitions"]==1,"Two tracks opened duplicate physical captures");
            owner.trackInspector->clearSource.triggerClick();break;
        case 203:
        {
            owner.selectTrack(video);
            juce::String window;
            for(const auto& source:owner.visualCapture.sources())if(source.kind=="window" && source.name.contains(captureFixtureTitle))window=source.id;
            if(retryCapture(203,window.isNotEmpty() && owner.visualCapture.preview(video).subscribers==1 && owner.visualCapture.preview(video).frameNumber>captureFirstFrame))break;
            snapshot("display-source-monitor.png");captureWindowSource=window;chooseCapture("window",window);captureFirstFrame=0;break;
        }
        case 204:
            if(retryCapture(204,owner.document.sequence.tracks[(size_t)owner.document.sequence.findTrack(video)].source["identity"].toString()==captureWindowSource && owner.visualCapture.preview(video).frameNumber>2))break;
            require(owner.liveSourceSelected() && owner.visualCapture.preview(video).state=="live","Window preview is not live in Source Monitor");
            snapshot("window-source-monitor.png");saved=juce::JSON::toString(owner.document.sequence.toJson());owner.runUi("movie.project.save");break;
        case 205:owner.runUi("movie.project.open",movie::object({{"projectId",project}}));break;
        case 206:
            require(saved==juce::JSON::toString(owner.document.sequence.toJson()),"Track capture assignments changed on save/reopen");
            if(retryCapture(206,owner.trackInspector->isEnabled()))break;
            owner.selectTrack(video);captureUndoDone=false;owner.trackInspector->clearSource.triggerClick();break;
        case 207:
            if(!captureUndoDone)
            {
                if(retryCapture(207,owner.document.sequence.tracks[(size_t)owner.document.sequence.findTrack(video)].source.isVoid()))break;
                owner.runUi("movie.timeline.undo");owner.runUi("movie.timeline.redo");owner.runUi("movie.timeline.undo");captureUndoDone=true;
            }
            if(retryCapture(207,owner.visualCapture.preview(video).image.isValid()))break;
            captureFixture.kill();break;
        case 208:
            if(retryCapture(208,!owner.visualCapture.preview(video).image.isValid() && owner.visualCapture.preview(video).state!="live"))break;
            passed("Actual display/window capture previews, shared track acquisition, source switching/clearing, VFS reopen, undo/redo and disappeared-window handling pass");
            record();owner.runUi("movie.project.open",movie::object({{"projectId",original},{"discardChanges",true}}));step=90;break;
    }
}
