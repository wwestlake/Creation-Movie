juce::String deviceCameraId,deviceMicrophoneId,deviceTrackB;bool deviceUndo=false;
void chooseDevice(const char* kind,const juce::String& id,int format=1)
{
    auto& inspector=*owner.trackInspector;
    const auto wanted=juce::String(kind);for(size_t i=0;i<inspector.categories.size();++i)if(inspector.categories[i]==wanted)inspector.category.setSelectedId((int)i+1,juce::sendNotificationSync);
    inspector.populateMedia();for(size_t i=0;i<inspector.mediaIds.size();++i)if(inspector.mediaIds[i]==id)inspector.media.setSelectedId((int)i+1,juce::sendNotificationSync);
    require(inspector.captureFormat.getNumItems()>0,"Device has no supported format choices");inspector.captureFormat.setSelectedId(format,juce::dontSendNotification);inspector.applySource.triggerClick();
}
void deviceStep(int stage)
{
    switch(stage)
    {
        case 240:
            if(retryCapture(240,(bool)owner.visualCapture.status()["deviceDiscoveryComplete"]))break;
            for(const auto& source:owner.visualCapture.sources())if(!source.formats.isEmpty()){if(source.kind=="camera" && (deviceCameraId.isEmpty() || source.name.contains("C920")))deviceCameraId=source.id;if(source.kind=="microphone" && (deviceMicrophoneId.isEmpty() || source.name.contains("C920")))deviceMicrophoneId=source.id;}
            if(deviceCameraId.isEmpty()){passed("No accessible camera with supported formats; physical camera UI validation unavailable");step=247;break;}
            owner.selectTrack(video);chooseDevice("camera",deviceCameraId);break;
        case 241:
            if(retryCapture(241,owner.document.sequence.tracks[(size_t)owner.document.sequence.findTrack(video)].source["identity"].toString()==deviceCameraId && owner.visualCapture.preview(video).frameNumber>2))break;
            require(owner.liveSourceSelected(),"Camera did not activate Source Monitor");snapshot("camera-source-monitor.png");owner.createTrack("video");deviceTrackB=owner.selectedTrack;chooseDevice("camera",deviceCameraId);break;
        case 242:
            if(retryCapture(242,owner.visualCapture.preview(deviceTrackB).subscribers==2))break;
            require((int)owner.visualCapture.status()["activeAcquisitions"]==1,"Camera UI opened duplicate acquisition");owner.trackInspector->clearSource.triggerClick();break;
        case 243:
            if(retryCapture(243,owner.document.sequence.tracks[(size_t)owner.document.sequence.findTrack(deviceTrackB)].source.isVoid() && owner.visualCapture.preview(video).subscribers==1))break;
            owner.selectTrack(video);saved=juce::JSON::toString(owner.document.sequence.toJson());owner.runUi("movie.project.save");break;
        case 244:owner.runUi("movie.project.open",movie::object({{"projectId",project}}));break;
        case 245:
            require(saved==juce::JSON::toString(owner.document.sequence.toJson()),"Camera configuration changed on VFS reopen");owner.selectTrack(video);deviceUndo=false;owner.trackInspector->clearSource.triggerClick();break;
        case 246:
            if(!deviceUndo){if(retryCapture(246,owner.document.sequence.tracks[(size_t)owner.document.sequence.findTrack(video)].source.isVoid()))break;owner.runUi("movie.timeline.undo");owner.runUi("movie.timeline.redo");owner.runUi("movie.timeline.undo");deviceUndo=true;}
            if(retryCapture(246,owner.visualCapture.preview(video).image.isValid()))break;
            passed("Physical camera catalog/format widgets, Source Monitor, sharing, independent clear, VFS reopen and undo/redo pass");owner.trackInspector->clearSource.triggerClick();break;
        case 247:
            if(deviceMicrophoneId.isEmpty()){passed("No accessible microphone with supported formats; physical microphone UI validation unavailable");step=254;break;}
            owner.selectTrack(audio);chooseDevice("microphone",deviceMicrophoneId);break;
        case 248:
            if(retryCapture(248,owner.visualCapture.preview(audio).sampleCount>4096))break;
            require(!owner.document.sequence.tracks[(size_t)owner.document.sequence.findTrack(audio)].monitoring,"Microphone listening enabled on assignment");
            require(owner.trackInspector->signal.getText().contains("Input:"),"Live microphone meter status missing");snapshot("microphone-source-monitor.png");
            owner.createTrack("audio");deviceTrackB=owner.selectedTrack;chooseDevice("microphone",deviceMicrophoneId);break;
        case 249:
            if(retryCapture(249,owner.visualCapture.preview(deviceTrackB).subscribers==2))break;
            require((int)owner.visualCapture.status()["activeAcquisitions"]==1,"Microphone UI opened duplicate acquisition");owner.trackInspector->monitor.triggerClick();break;
        case 250:
            if(retryCapture(250,(juce::int64)owner.visualCapture.status()["monitorRenderedFrames"]>0))break;
            owner.trackInspector->monitor.triggerClick();break;
        case 251:
            if(retryCapture(251,!owner.document.sequence.tracks[(size_t)owner.document.sequence.findTrack(deviceTrackB)].monitoring))break;
            saved=juce::JSON::toString(owner.document.sequence.toJson());owner.runUi("movie.project.save");break;
        case 252:owner.runUi("movie.project.open",movie::object({{"projectId",project}}));break;
        case 253:
            require(saved==juce::JSON::toString(owner.document.sequence.toJson()),"Microphone format/subscriptions changed on VFS reopen");
            passed("Physical microphone catalog/format widgets, input samples/meters, shared acquisition, independent listen controls, existing audio output callback and VFS reopen pass");break;
        case 254:
            owner.visualCapture.setBindings({});record();owner.runUi("movie.project.open",movie::object({{"projectId",original},{"discardChanges",true}}));step=90;break;
    }
}
