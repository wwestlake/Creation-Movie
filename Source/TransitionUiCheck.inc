int transitionWait=0;
void prepareTransitions()
{
    auto next=owner.document.sequence;next.clips.clear();next.clipTracks.clear();next.transitions.clear();next.fpsNum=30;next.fpsDen=1;
    juce::String videoAsset,audioAsset;
    for(const auto& m:next.media){if(m.info.hasVideo && !m.info.still)videoAsset=m.id;if(m.info.hasAudio && !m.info.hasVideo)audioAsset=m.id;}
    require(videoAsset.isNotEmpty() && audioAsset.isNotEmpty(),"Transition fixtures need real video and WAV");owner.document.restore(next.toJson());
    for(int i=0;i<4;++i)
    {owner.runUi("movie.timeline.place",movie::object({{"assetId",videoAsset},{"trackId",video},{"start",i*1.5},{"duration",1.5},{"sourceIn",i%2?1.0:.5}}));transitionClips.add(owner.document.sequence.clips.back().id);}
    for(int i=0;i<2;++i)
    {owner.runUi("movie.timeline.place",movie::object({{"assetId",audioAsset},{"trackId",audio},{"start",1.2+i*.3},{"duration",.3},{"sourceIn",.2}}));transitionClips.add(owner.document.sequence.clips.back().id);}
    owner.selectClip(transitionClips[0]);owner.showTransition({},transitionClips[0]);
}
void verifyTransitionMedia()
{
    const auto s=owner.document.sequence;const auto tr=s.transitions[0];
    movie::MediaReader exported(output.getChildFile("transitions-sample.mkv").createInputStream(),"transitions-sample.mkv",movie::MediaReader::Kind::video);
    movie::Renderer expected(s);int pixelError=0;
    for(double t:{tr.start-.1,tr.start+tr.duration*.5,tr.end()+.1,4.5})
    {
        const double frameTime=std::round(t/s.frameSeconds())*s.frameSeconds();const auto a=expected.video(frameTime,640,360),b=exported.videoAt(frameTime+s.frameSeconds()*.5,640,360);
        for(int y=0;y<360;y+=8)for(int x=0;x<640;x+=8){const auto aa=a.getPixelAt(x,y),bb=b.getPixelAt(x,y);pixelError=std::max({pixelError,std::abs(aa.getRed()-bb.getRed()),std::abs(aa.getGreen()-bb.getGreen()),std::abs(aa.getBlue()-bb.getBlue())});}
    }
    require(pixelError<=1,("Transition export pixel error "+juce::String(pixelError)).toRawUTF8());
    double variantDifference=0;int variants=0;
    for(const auto* type:{"video.dissolve","video.black","video.wipe"})for(const auto* direction:{"leftToRight","rightToLeft","topToBottom","bottomToTop"})
    {
        if(juce::String(type)!="video.wipe" && juce::String(direction)!="leftToRight")continue;
        auto variant=s;variant.transitions[0].type=type;variant.transitions[0].direction=direction;variant.transitions[0].softness=.15;
        auto plain=variant;plain.transitions.erase(plain.transitions.begin());movie::Renderer processed(variant),manual(plain);const auto a=processed.video(1.5,160,90),b=manual.video(1.5,160,90);
        require(a.isValid(),"Real-media transition variant failed");for(int y=0;y<90;y+=3)for(int x=0;x<160;x+=3)variantDifference+=std::abs(a.getPixelAt(x,y).getRed()-b.getPixelAt(x,y).getRed());++variants;
    }
    require(variantDifference>100,"Real transition variants did not change pixels");
    movie::MediaReader exportedAudio(output.getChildFile("transitions-sample.mkv").createInputStream(),"transitions-sample.mkv",movie::MediaReader::Kind::audio);
    std::vector<float> a(2048),b(2048);const int startSample=69600; // 1.45s, inside the WAV crossfade, before the video audio cut.
    for(int offset=0;offset<startSample;){const int n=std::min(1024,startSample-offset);exportedAudio.audioAt((double)offset/48000,n,48000,b.data());offset+=n;}
    movie::Renderer through(s),seek(s);for(int offset=0;offset<startSample;){const int n=std::min(1024,startSample-offset);through.audio((double)offset/48000,n,48000,a.data());offset+=n;}
    through.audio(1.45,1024,48000,a.data());exportedAudio.audioAt(1.45,1024,48000,b.data());double audioError=0;
    for(size_t i=0;i<a.size();++i)audioError=std::max(audioError,(double)std::abs(a[i]-b[i]));
    seek.audio(1.45,1024,48000,b.data());double seekError=0;for(size_t i=0;i<a.size();++i)seekError=std::max(seekError,(double)std::abs(a[i]-b[i]));
    // Isolate the existing compressed-source seek residual from transition/filter
    // state. The lossless WAV crossfade must reproduce continuous processing.
    auto waveOnly=s,codecOnly=s;
    for(auto& t:waveOnly.tracks)t.enabled=t.id==audio || (t.type=="automation" && t.targetTrackId==audio);
    // Keep the same enabled filters/automation so global decoder preroll is
    // identical; silence other clip inputs rather than disabling their tracks.
    for(auto& c:codecOnly.clips)if(codecOnly.owner(c)!=video)c.videoParams.set("gain",0.0);
    auto seekDifference=[&](const movie::Sequence& sequence){movie::Renderer continuous(sequence),sought(sequence);for(int offset=0;offset<startSample;){const int n=std::min(1024,startSample-offset);continuous.audio((double)offset/48000,n,48000,a.data());offset+=n;}continuous.audio(1.45,1024,48000,a.data());sought.audio(1.45,1024,48000,b.data());double difference=0;for(size_t i=0;i<a.size();++i)difference=std::max(difference,(double)std::abs(a[i]-b[i]));return difference;};
    const double waveSeekError=seekDifference(waveOnly),codecSeekError=seekDifference(codecOnly);
    output.getChildFile("transitions-media-results.json").replaceWithText(juce::JSON::toString(movie::object({{"exportPixelError",pixelError},{"exportAudioError",audioError},{"seekError",seekError},{"waveSeekError",waveSeekError},{"codecSeekError",codecSeekError}})));
    require(audioError<.0001,("Transition continuous/export audio error "+juce::String(audioError,9)).toRawUTF8());
    require(waveSeekError<.000001 && seekError<=codecSeekError+.00001 && seekError<.001,"Transition/effect/automation adds seek error beyond source decoding");
    double gainError=0;
    for(const auto* curve:{"linear","equalPower"})
    {
        auto isolated=s;for(auto& t:isolated.tracks){t.enabled=t.id==audio;t.effects.clear();}
        isolated.transitions.erase(std::remove_if(isolated.transitions.begin(),isolated.transitions.end(),[](const movie::Transition& t){return !t.audio();}),isolated.transitions.end());isolated.transitions[0].curve=curve;
        const auto transition=isolated.transitions[0];auto left=isolated,right=isolated;left.transitions.clear();right.transitions.clear();
        const auto ca=isolated.clips[(size_t)isolated.findClip(transition.outgoing)],cb=isolated.clips[(size_t)isolated.findClip(transition.incoming)];
        auto single=[&](movie::Sequence& seq,movie::Clip clip){clip.sourceStartSeconds+=transition.start-clip.startSeconds;clip.startSeconds=transition.start;clip.durationSeconds=transition.duration;seq.clips={clip};};single(left,ca);single(right,cb);
        movie::Renderer mixed(isolated),outgoing(left),incoming(right);std::vector<float> l(2048),r(2048),actual(2048);outgoing.audio(1.45,1024,48000,l.data());incoming.audio(1.45,1024,48000,r.data());mixed.audio(1.45,1024,48000,actual.data());
        for(int i=0;i<1024;++i){const auto gains=movie::transitionAudioGains(transition,1.45+(double)i/48000);for(int ch=0;ch<2;++ch)gainError=std::max(gainError,std::abs(actual[(size_t)i*2+ch]-(l[(size_t)i*2+ch]*gains.first+r[(size_t)i*2+ch]*gains.second)));}
    }
    require(gainError<.00001,"Production audio crossfade does not match per-sample gain law");
    output.getChildFile("transitions-media-results.json").replaceWithText(juce::JSON::toString(movie::object({{"videoVariants",variants},{"variantDifference",variantDifference},{"exportPixelError",pixelError},{"exportAudioError",audioError},{"seekError",seekError},{"waveSeekError",waveSeekError},{"codecSeekError",codecSeekError},{"audioGainError",gainError}})));
    passed("Six real-video variants, both actual PCM crossfade laws, decoded preview/export and filtered/automated seeking pass");
}
void transitionStep(int stage)
{
    auto& panel=*owner.transitionPanel;
    switch(stage)
    {
        case 100:prepareTransitions();break;
        case 101:panel.type.setSelectedId(1,juce::sendNotificationSync);panel.start.setText("1.0");panel.duration.setText("1.0");panel.apply.triggerClick();break;
        case 102:require(owner.document.sequence.transitions.size()==1,("GUI transition create failed: "+owner.lastStatus).toRawUTF8());dissolveId=owner.document.sequence.transitions[0].id;panel.start.setText("invalid");panel.write();require(owner.document.sequence.transitions[0].start==1.0 && owner.lastStatus.contains("finite number"),"Invalid numerical text was not rejected");panel.start.setText("1.1");panel.duration.setText("0.8");panel.apply.triggerClick();break;
        case 103:
        {
            require(owner.document.sequence.transitions[0].start==1.1 && owner.document.sequence.transitions[0].duration==.8,"Exact numerical transition edit failed");
            owner.pixelsPerSecond=200;owner.visibleStart=0;auto& timeline=*owner.timeline;timeline.reveal(video);
            auto box=timeline.transitionBox(owner.document.sequence.transitions[0]);juce::Point<float> start{(float)box.getRight()-2,(float)box.getCentreY()},end=start+juce::Point<float>(22,0);
            timeline.mouseDown(event(start,start,true));timeline.mouseDrag(event(end,start,true));timeline.mouseUp(event(end,start,false));
            require(owner.document.sequence.transitions[0].duration>.85,"Transition boundary drag failed");
            box=timeline.transitionBox(owner.document.sequence.transitions[0]);start=box.getCentre().toFloat();end=start+juce::Point<float>(12,0);timeline.mouseDown(event(start,start,true));timeline.mouseUp(event(end,start,false));
            require(owner.document.sequence.transitions[0].start>1.1,"Transition move failed");owner.showTransition(dissolveId);panel.remove.triggerClick();break;
        }
        case 104:
        {
            require(owner.document.sequence.transitions.empty(),"GUI transition delete failed");owner.runUi("movie.timeline.undo");owner.runUi("movie.timeline.redo");owner.runUi("movie.timeline.undo");owner.showTransition(dissolveId);
            const auto before=owner.document.sequence.transitions[0];panel.write();const auto& after=owner.document.sequence.transitions[0];
            require(before.start==after.start && before.duration==after.duration,"Applying displayed properties rounded transition timing");
            passed("Mounted transition creation, exact properties, selection, boundary/move drags and delete/undo/redo pass");break;
        }
        case 105:
        {
            auto wipe=owner.execute("movie.transition.create",movie::object({{"type","video.wipe"},{"trackId",video},{"outgoing",transitionClips[2]},{"incoming",transitionClips[3]},{"start",4.0},{"duration",1.0},{"direction","leftToRight"},{"softness",.15}}));require(wipe.ok,wipe.message.toRawUTF8());
            auto cross=owner.execute("movie.transition.create",movie::object({{"type","audio.crossfade"},{"trackId",audio},{"outgoing",transitionClips[4]},{"incoming",transitionClips[5]},{"start",1.4},{"duration",.2},{"curve","equalPower"}}));require(cross.ok,cross.message.toRawUTF8());
            auto updated=owner.execute("movie.transition.update",movie::object({{"transitionId",cross.data["id"]},{"start",1.35},{"duration",.3}}));require(updated.ok,updated.message.toRawUTF8());break;
        }
        case 106:
        {
            const auto snapshotState=juce::JSON::toString(owner.document.sequence.toJson());auto rejected=owner.execute("movie.transition.validate",movie::object({{"transitionId",dissolveId},{"duration",9}}));require(!rejected.ok && snapshotState==juce::JSON::toString(owner.document.sequence.toJson()),"Read-only invalid handle validation failed");
            require(owner.execute("movie.transition.list",movie::object({})).data.size()==3 && owner.execute("movie.transition.get",movie::object({{"transitionId",dissolveId}})).ok && owner.execute("movie.transition.catalog",movie::object({})).data["types"].size()==4,"Transition agent queries failed");
            juce::String error;int cards=0;for(const auto& scope:owner.engineer.loadCards(error))for(const auto& c:scope.cards)if(c.id.startsWith("movie.transitions."))++cards;require(cards==2,"Shipped transition training cards not loaded");passed("Agent transition CRUD/query/catalog/dry-run and shipped training card loading pass");saved=juce::JSON::toString(owner.document.sequence.toJson());owner.runUi("movie.project.save");break;
        }
        case 107:owner.runUi("movie.project.open",movie::object({{"projectId",project}}));break;
        case 108:require(saved==juce::JSON::toString(owner.document.sequence.toJson()),"Transition save/reopen changed definitions");owner.showTransition(dissolveId);owner.pixelsPerSecond=100;owner.timeline->reveal(video);snapshot("transitions-ui-demo.png");owner.runUi("movie.export",movie::object({{"path",output.getChildFile("transitions-sample.mkv").getFullPathName()},{"preset","ffv1"}}));break;
        case 109:verifyTransitionMedia();owner.runUi("movie.export",movie::object({{"path",output.getChildFile("transitions-sample.mp4").getFullPathName()},{"preset","mp4"}}));break;
        case 110:require(output.getChildFile("transitions-sample.mp4").getSize()>1000,"Transition MP4 export failed");transitionWait=0;owner.playback.seek(1.4);owner.playback.play(true);break;
        case 111:
            if(owner.playback.position()<=1.4 && owner.playback.error().isEmpty() && transitionWait++<20){step=111;break;}
            require(owner.playback.position()>1.4 && owner.playback.error().isEmpty(),("Transition transport failed at "+juce::String(owner.playback.position(),6)+": "+owner.playback.error()).toRawUTF8());owner.playback.play(false);transitionWait=0;owner.playback.seek(0);owner.playback.play(true);break;
        case 112:
            if(owner.playback.position()<=0 && owner.playback.error().isEmpty() && transitionWait++<20){step=112;break;}
            require(owner.playback.position()>0 && owner.playback.error().isEmpty(),("Transition restart failed: "+owner.playback.error()).toRawUTF8());owner.playback.play(false);passed("Transition definitions reopen exactly; actual playback/restart and processed MP4 export pass");record();owner.runUi("movie.project.open",movie::object({{"projectId",original},{"discardChanges",true}}));step=90;break;
    }
}
