juce::String recordCamera,recordMic,recordSecondMic,recordSecondMicSource,recordDisplay,recordDuplicate,recordWindowTrack,recordDisplayId;
juce::var recordingFirstTake;int recordingRetries=0,recordingBaseClips=0;juce::int64 recordingOrigin=0;bool recordingUndoDone=false;
bool waitRecording(int stage,bool ready)
{
    if(ready){recordingRetries=0;return false;}
    const auto status=owner.recording.status();require(status["state"].toString()!="failed",("Recording failed: "+status["error"].toString()).toRawUTF8());
    require(++recordingRetries<160,("Recording UI timeout at stage "+juce::String(stage)+": "+status["state"].toString()).toRawUTF8());step=stage;return true;
}
void armRecording(const juce::String& id,bool armed,bool monitoring=false)
{owner.runUi("movie.track.preferences",movie::object({{"trackId",id},{"armed",armed},{"monitoring",monitoring}}));}
void verifyRecordedMedia(const juce::var& take,int expected)
{
    const auto* tracks=take["tracks"].getArray();require(tracks && tracks->size()==expected,"Wrong independently recorded track count");
    juce::Array<juce::var> evidence;
    for(const auto& track:*tracks)
    {
        require((bool)track["usable"],("Recording not usable: "+track["error"].toString()).toRawUTF8());const auto path=track["path"].toString();
        const auto id=track["id"].toString();require(owner.document.sequence.findMedia(id)!=nullptr,"Final recording missing from ordinary Media Library");
        const auto clip=owner.document.sequence.findClip("capture-"+id);require(clip>=0,"Recording missing from owner timeline track");
        const auto& placed=owner.document.sequence.clips[(size_t)clip];require(owner.document.sequence.owner(placed)==track["trackId"].toString(),"Recording placed on a different track");
        require(std::abs(placed.startSeconds-(double)track["startSeconds"])<1e-10,"Recorded offset was snapped to a timeline frame");
        require((juce::int64)track["diagnostics"]["queuePeakBytes"]<=(juce::int64)track["diagnostics"]["queueLimitBytes"],"Recorder exceeded bounded queue budget");
        movie::MediaReader reader(creation::assets::openVfsEntryStream(owner.session.getProjectId(),path),path,movie::MediaReader::Kind::probe);
        require(reader.info().duration>1,"Recorded media duration is empty/too short");
        if(reader.info().hasVideo){movie::MediaReader videoReader(creation::assets::openVfsEntryStream(owner.session.getProjectId(),path),path,movie::MediaReader::Kind::video);const auto initial=videoReader.videoAt(.5,320,240);require(initial.isValid(),"Recorded video did not decode through existing media reader");require(videoReader.videoAt(std::max(.5,reader.info().duration-.1),320,240).isValid(),"Recorded video could not seek near its end");const auto repeated=videoReader.videoAt(.5,320,240);require(repeated.isValid() && repeated.getPixelAt(100,100)==initial.getPixelAt(100,100),"Recorded video seek/replay changed decoded pixels");}
        else {movie::MediaReader audioReader(creation::assets::openVfsEntryStream(owner.session.getProjectId(),path),path,movie::MediaReader::Kind::audio);std::vector<float> samples(2048);audioReader.audioAt(.5,1024,48000,samples.data());require(std::all_of(samples.begin(),samples.end(),[](float value){return std::isfinite(value);}),"Recorded microphone PCM is not finite");}
        evidence.add(track);
    }
    output.getChildFile("recording-media-"+take["state"].toString()+"-"+take["takeId"].toString()+"-results.json").replaceWithText(juce::JSON::toString(movie::object({{"take",take},{"tracks",evidence},{"physicalFlashClapTested",false}})));
}
void recordingStep(int stage)
{
    switch(stage)
    {
        case 300:
            if(waitRecording(300,(bool)owner.visualCapture.status()["deviceDiscoveryComplete"]))break;
            for(const auto& source:owner.visualCapture.sources())
            {if(source.kind=="camera" && !source.formats.isEmpty() && (recordCamera.isEmpty() || source.name.contains("C920")))recordCamera=source.id;if(source.kind=="microphone" && !source.formats.isEmpty() && (recordMic.isEmpty() || source.name.contains("C920")))recordMic=source.id;if(source.kind=="microphone" && !source.formats.isEmpty() && source.name.containsIgnoreCase("USB Audio CODEC"))recordSecondMicSource=source.id;if(source.kind=="display" && recordDisplayId.isEmpty())recordDisplayId=source.id;}
            require(recordCamera.isNotEmpty() && recordMic.isNotEmpty() && recordDisplayId.isNotEmpty() && recordSecondMicSource.isNotEmpty() && recordSecondMicSource!=recordMic,"Recording hardware fixture requires camera, two independent physical microphones and individual display");
            for(const auto& track:owner.document.sequence.tracks)if(track.type=="video" || track.type=="audio")armRecording(track.id,false,false);
            recordingBaseClips=(int)owner.document.sequence.clips.size();owner.selectTrack(video);chooseDevice("camera",recordCamera);break;
        case 301:
            if(waitRecording(301,owner.document.sequence.tracks[(size_t)owner.document.sequence.findTrack(video)].source["identity"].toString()==recordCamera))break;
            armRecording(video,true);owner.selectTrack(audio);chooseDevice("microphone",recordMic);break;
        case 302:
            if(waitRecording(302,owner.document.sequence.tracks[(size_t)owner.document.sequence.findTrack(audio)].source["identity"].toString()==recordMic))break;
            armRecording(audio,true);owner.createTrack("video");recordDisplay=owner.selectedTrack;chooseCapture("display",recordDisplayId);break;
        case 303:
            if(waitRecording(303,owner.document.sequence.tracks[(size_t)owner.document.sequence.findTrack(recordDisplay)].source["identity"].toString()==recordDisplayId))break;
            armRecording(recordDisplay,true);owner.createTrack("video");recordDuplicate=owner.selectedTrack;chooseDevice("camera",recordCamera);break;
        case 304:
            if(waitRecording(304,owner.document.sequence.tracks[(size_t)owner.document.sequence.findTrack(recordDuplicate)].source["identity"].toString()==recordCamera))break;
            armRecording(recordDuplicate,true);owner.createTrack("audio");recordSecondMic=owner.selectedTrack;chooseDevice("microphone",recordSecondMicSource);step=320;break;
        case 320:
            if(waitRecording(320,owner.document.sequence.tracks[(size_t)owner.document.sequence.findTrack(recordSecondMic)].source["identity"].toString()==recordSecondMicSource))break;
            armRecording(recordSecondMic,true);step=305;owner.playback.seek(2.123456789);require((bool)owner.recording.readiness(owner.document.sequence)["ready"],"Configured armed source readiness failed");owner.header.onRecord();break;
        case 305:
            if(waitRecording(305,owner.recording.status()["state"].toString()=="recording" && (double)owner.recording.status()["elapsed"]>1))break;
            require((int)owner.visualCapture.status()["activeAcquisitions"]==4,"Recording duplicated physical source acquisition");recordingOrigin=(juce::int64)owner.recording.status()["origin100ns"];
            require(owner.execute("movie.recording.start",movie::object({})).ok==false,"Conflicting Record command accepted");armRecording(video,true,true);break;
        case 306:
            if(waitRecording(306,(double)owner.recording.status()["elapsed"]>3))break;
            require(owner.visualCapture.preview(video).image.isValid(),"Preview did not coexist with recording");armRecording(video,true,false);owner.playback.seek(.25);armRecording(recordDuplicate,false);break;
        case 307:
            if(waitRecording(307,(double)owner.recording.status()["elapsed"]>7))break;
            require((juce::int64)owner.recording.status()["origin100ns"]==recordingOrigin && (int)owner.visualCapture.status()["activeAcquisitions"]==4,"Seeking/preview stop changed recording epoch or acquisition");snapshot("recording-session.png");owner.header.onStop();break;
        case 308:
            if(waitRecording(308,!owner.recording.active()))break;
            recordingFirstTake=owner.recording.status()["results"];verifyRecordedMedia(recordingFirstTake,5);
            require((int)owner.document.sequence.clips.size()==recordingBaseClips+5,"Recording altered existing clips or ignored armed snapshot");
            saved=juce::JSON::toString(owner.document.sequence.toJson());owner.runUi("movie.timeline.undo");require((int)owner.document.sequence.clips.size()==recordingBaseClips && owner.document.sequence.media.size()>=8,"Undo discarded recording assets");owner.runUi("movie.timeline.redo");require(saved==juce::JSON::toString(owner.document.sequence.toJson()),"Recording redo changed precise timing");owner.runUi("movie.project.save");break;
        case 309:owner.runUi("movie.project.open",movie::object({{"projectId",project}}));break;
        case 310:
            require(saved==juce::JSON::toString(owner.document.sequence.toJson()),"Recorded assets/offsets changed on VFS reopen");passed("Real camera/two independent microphones/display recording, shared hardware, independent preview, armed snapshot, session clock, valid decoded VFS media, precise placement, undo/redo and reopen pass");
            for(const auto& track:owner.document.sequence.tracks)if(track.type=="video" || track.type=="audio")armRecording(track.id,false,false);
            captureFixtureTitle="Movie recording window "+juce::Uuid().toString();require(captureFixture.start(juce::StringArray{fixtureExecutable.getFullPathName(),"--fixture",captureFixtureTitle}),"Cannot create recording window fixture");owner.visualCapture.refresh();break;
        case 311:
            captureWindowSource={};for(const auto& source:owner.visualCapture.sources())if(source.kind=="window" && source.name.contains(captureFixtureTitle))captureWindowSource=source.id;
            if(waitRecording(311,captureWindowSource.isNotEmpty()))break;
            owner.createTrack("video");recordWindowTrack=owner.selectedTrack;chooseCapture("window",captureWindowSource);break;
        case 312:
            if(waitRecording(312,owner.document.sequence.tracks[(size_t)owner.document.sequence.findTrack(recordWindowTrack)].source["identity"].toString()==captureWindowSource))break;
            armRecording(recordWindowTrack,true);armRecording(audio,true);owner.header.onRecord();break;
        case 313:
            if(waitRecording(313,owner.recording.status()["state"].toString()=="recording" && (double)owner.recording.status()["elapsed"]>2))break;
            captureFixture.kill();break;
        case 314:
            if(waitRecording(314,(double)owner.recording.status()["elapsed"]>5))break;
            require(owner.recording.active(),"Window loss terminated unaffected microphone recording");owner.header.onStop();break;
        case 315:
            if(waitRecording(315,!owner.recording.active()))break;
            require(owner.recording.status()["state"].toString()=="partial","Window disappearance did not produce an explicit partial session");
            verifyRecordedMedia(owner.recording.status()["results"],2);passed("Window disappearance finalizes its partial recording while unaffected microphone continues");
            owner.startJob("synthetic-recording-clock",[project=owner.session.getProjectId()](MainComponent::Job& job){return syntheticRecordingChecks(project,job);},[this](const juce::var& result){output.getChildFile("recording-synthetic-results.json").replaceWithText(juce::JSON::toString(result));});break;
        case 316:
            passed("Synthetic 30-minute 300ppm clock drift, bounded incremental VFS output, variable frame timing and native odd-size video decode pass");
            for(const auto& track:owner.document.sequence.tracks)if(track.type=="video" || track.type=="audio")armRecording(track.id,false,false);
            armRecording(audio,true);require(owner.recording.start(owner.document.sequence,0).isEmpty(),"Cancellation test could not prepare");owner.recording.stop();break;
        case 317:
            if(waitRecording(317,!owner.recording.active()))break;
            require(owner.recording.status()["state"].toString()=="failed" && owner.recording.status()["error"].toString().containsIgnoreCase("cancel"),"Startup cancellation was not reported explicitly");
            {auto missing=owner.document.sequence;auto& track=missing.tracks[(size_t)missing.findTrack(audio)];track.source=movie::object({{"category","microphone"},{"identity","microphone:missing-fixture"}});require(!(bool)owner.recording.readiness(missing)["ready"] && owner.recording.start(missing,0).isNotEmpty(),"Unavailable source was accepted before recording");}
            owner.header.onRecord();break;
        case 318:
            if(waitRecording(318,owner.recording.status()["state"].toString()=="recording" && (double)owner.recording.status()["elapsed"]>3))break;
            owner.header.onStop();break;
        case 319:
            if(waitRecording(319,!owner.recording.active()))break;
            verifyRecordedMedia(owner.recording.status()["results"],1);passed("Single microphone recording, immediate startup cancellation and unavailable-source rejection pass");
            record();owner.runUi("movie.project.open",movie::object({{"projectId",original},{"discardChanges",true}}));step=90;break;
    }
}
