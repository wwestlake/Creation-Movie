#include "MovieRecording.h"
#include "MovieRecordingWriter.h"
#include "MovieDeviceCapture.h"
#include <creation/assets/VfsEntryInputStream.h>
#include "MovieMedia.h"
#include <thread>
#include <cmath>
#include <mutex>
namespace movie
{
struct RecordingCoordinator::Impl
{
    VisualCapture& capture;mutable std::mutex mutex;juce::String state{"idle"},error,take,project;
    juce::var results;std::atomic<bool> stop{false};std::atomic<juce::int64> stopTime{INT64_MAX},origin{0};
    bool pending=false;std::thread worker;juce::Array<juce::var> progress;
    explicit Impl(VisualCapture& c):capture(c){}
    ~Impl(){stopTime=captureHostTime();stop=true;if(worker.joinable())worker.join();capture.setRecordingSubscriptions({});}
    void setState(const juce::String& s){std::lock_guard lock(mutex);state=s;}
    juce::var readiness(const Sequence& sequence)const
    {
        juce::Array<juce::var> tracks,errors;const auto sources=capture.sources();std::map<juce::String,std::vector<juce::var>> requests;
        for(const auto& track:sequence.tracks)if(track.armed)
        {
            const auto kind=track.source["category"].toString(),id=track.source["identity"].toString();juce::String problem;
            if(track.type!="video" && track.type!="audio")continue;
            if(!track.enabled)problem="Armed track is disabled.";
            else if((track.type=="video" && kind!="display" && kind!="window" && kind!="camera") || (track.type=="audio" && kind!="microphone"))problem="Assign a supported physical capture source.";
            else problem=capture.validate(id,kind,track.source["settings"]);
            tracks.add(object({{"trackId",track.id},{"name",track.name},{"sourceId",id},{"kind",kind},{"error",problem}}));
            if(problem.isNotEmpty())errors.add(track.name+": "+problem);else requests[id].push_back(track.source["settings"]);
        }
        for(const auto& request:requests)
            for(const auto& source:sources)if(source.id==request.first && (source.kind=="camera" || source.kind=="microphone"))
            {bool compatible=false;for(const auto& format:source.formats)if(std::all_of(request.second.begin(),request.second.end(),[&](const juce::var& settings){return captureFormatMatches(format,settings);}))compatible=true;if(!compatible)errors.add(source.name+": armed tracks request incompatible formats.");}
        if(tracks.isEmpty())errors.add("Arm at least one video/audio track.");if(tracks.size()>16)errors.add("A session supports at most 16 armed tracks.");
        if(sequence.projectId.isEmpty())errors.add("Open a VFS project first.");
        return object({{"ready",errors.isEmpty()},{"tracks",tracks},{"errors",errors},{"streamingServiceCheckedDuringPrepare",true}});
    }
    void run(Sequence sequence,double position)
    {
        struct Task
        {Track track;std::shared_ptr<CaptureQueue> queue=std::make_shared<CaptureQueue>();std::unique_ptr<TrackRecorder> recorder;std::thread worker;std::atomic<bool> stop{false},done{false};juce::String failure;};
        std::vector<std::unique_ptr<Task>> tasks;creation::services::SuiteVfsServiceClient client;juce::Array<juce::var> entries;
        const auto journalPath="Project/Capture/"+take+".json";
        auto journal=[&](const char* phase)
        {
            auto record=object({{"schema","movie-capture-take"},{"version",1},{"takeId",take},{"projectId",project},{"state",phase},{"origin100ns",origin.load()},{"stop100ns",stopTime.load()},{"timelinePosition",position},{"tracks",entries}});
            const auto json=juce::JSON::toString(record);if(!client.writeProjectEntry(project,journalPath,juce::MemoryBlock(json.toRawUTF8(),json.getNumBytesAsUTF8())))throw std::runtime_error(client.getLastWriteError().toStdString());return record;
        };
        try
        {
            if(!client.discover() || !client.supportsStreamingWrites())throw std::runtime_error("The running VFS service lacks open-ended streaming writes. Update the service before recording; no media was recorded.");
            std::vector<RecordingSubscription> subscriptions;
            for(const auto& track:sequence.tracks)if(track.armed && (track.type=="video" || track.type=="audio"))
            {auto task=std::make_unique<Task>();task->track=track;subscriptions.push_back({track.id,track.source["identity"].toString(),track.source["settings"].clone(),task->queue});tasks.push_back(std::move(task));}
            journal("preparing");capture.setRecordingSubscriptions(subscriptions);
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(12);
            for(;;)
            {
                if(stop.load())throw std::runtime_error("Recording cancelled during preparation.");bool ready=true;
                for(const auto& task:tasks)
                {const auto p=capture.preview("record:"+task->track.id);if(p.state=="failed" || p.state=="unavailable" || p.state=="formatConflict" || p.state=="unsupportedFormat")throw std::runtime_error((task->track.name+": "+p.message).toStdString());if(p.state!="live" || (task->track.type=="video"?!p.image.isValid():p.sampleCount==0))ready=false;}
                if(ready)break;if(std::chrono::steady_clock::now()>deadline)throw std::runtime_error("Timed out preparing all armed sources.");std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
            for(auto& task:tasks)
            {if(stop.load())throw std::runtime_error("Recording cancelled during preparation.");task->recorder=std::make_unique<TrackRecorder>(project,take,task->track,task->queue);task->recorder->prepare(capture.preview("record:"+task->track.id));entries.add(task->recorder->result().toJson());}
            journal("prepared");origin=captureHostTime()+1000000;for(auto& task:tasks)task->queue->after=origin.load();setState("recording");
            for(auto& task:tasks)
            {auto* current=task.get();current->worker=std::thread([this,current]{current->recorder->run(origin.load(),current->stop,stopTime);current->done=true;});}
            while(!stop.load())
            {
                bool allDone=true;juce::Array<juce::var> states;std::vector<RecordingSubscription> live;
                for(auto& task:tasks)
                {
                    const auto p=capture.preview("record:"+task->track.id);
                    if(!task->done.load() && !task->stop.load())
                    {
                        const auto now=captureHostTime();const bool stalled=now-origin.load()>20000000 && now-task->queue->lastDelivery.load()>20000000;
                        if(stalled || p.state=="failed" || p.state=="unavailable" || p.state=="formatConflict" || p.state=="unsupportedFormat")
                        {task->failure=stalled?"Capture source stopped delivering samples for two seconds.":p.message.isNotEmpty()?p.message:"Capture source became "+p.state;task->stop=true;task->queue->before=now;}
                        else live.push_back({task->track.id,task->track.source["identity"].toString(),task->track.source["settings"].clone(),task->queue});
                    }
                    if(!task->done.load())allDone=false;
                    states.add(object({{"trackId",task->track.id},{"sourceId",task->track.source["identity"]},{"state",task->done.load()?"finalized":task->stop.load()?"failed":p.state},{"message",task->failure},{"frames",p.frameNumber},{"samples",p.sampleCount}}));
                }
                {std::lock_guard lock(mutex);progress=states;}
                capture.setRecordingSubscriptions(std::move(live));
                if(allDone){stopTime=captureHostTime();stop=true;break;}
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            setState("stopping");if(stopTime.load()==INT64_MAX)stopTime=captureHostTime();
            for(auto& task:tasks)task->queue->before=stopTime.load();
            // Bounded delivery drain: late-arriving samples through T1 still reach queues.
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            capture.setRecordingSubscriptions({});for(auto& task:tasks)task->stop=true;
            for(auto& task:tasks)if(task->worker.joinable())task->worker.join();
            entries.clear();bool any=false,failed=false;
            for(auto& task:tasks)
            {
                auto result=task->recorder->result();result.start+=position;
                if(task->failure.isNotEmpty())result.error+=(result.error.isEmpty()?"":"; ")+task->failure;
                if(result.usable)try
                {MediaReader probe(creation::assets::openVfsEntryStream(project,result.path),result.path,MediaReader::Kind::probe);const auto actual=probe.info();if(actual.hasVideo!=result.info.hasVideo || actual.hasAudio!=result.info.hasAudio)throw std::runtime_error("Finalized media stream does not match capture type.");const auto encodedDuration=result.info.duration;result.info=actual;if(result.info.hasVideo)result.info.duration=std::max(result.info.duration,encodedDuration);}
                catch(const std::exception& e){result.error+=(result.error.isEmpty()?"":"; ")+juce::String(e.what());result.usable=false;}
                any=any||result.usable;failed=failed||!result.usable || result.error.isNotEmpty();entries.add(result.toJson());
            }
            auto final=journal(any?(failed?"partial":"completed"):"failed");{std::lock_guard lock(mutex);results=final;pending=true;state=any?(failed?"partial":"completed"):"failed";}
        }
        catch(const std::exception& e)
        {
            capture.setRecordingSubscriptions({});for(auto& task:tasks)task->stop=true;for(auto& task:tasks)if(task->worker.joinable())task->worker.join();
            entries.clear();bool any=false;for(auto& task:tasks)if(task->recorder){auto result=task->recorder->result();result.start+=position;any=any||result.usable;entries.add(result.toJson());}
            juce::var retained=object({{"takeId",take},{"projectId",project},{"state",any?"partial":"failed"},{"tracks",entries},{"origin100ns",origin.load()},{"timelinePosition",position}});
            try{retained=journal(any?"partial":"failed");}catch(...){}
            {std::lock_guard lock(mutex);error=e.what();state=any?"partial":"failed";results=retained;pending=entries.size()>0;}
        }
    }
};
RecordingCoordinator::RecordingCoordinator(VisualCapture& c):impl(std::make_unique<Impl>(c)){}RecordingCoordinator::~RecordingCoordinator()=default;
juce::var RecordingCoordinator::readiness(const Sequence& sequence)const{return impl->readiness(sequence);}
juce::String RecordingCoordinator::start(const Sequence& sequence,double position)
{
    if(active())return "A recording is already active or awaiting publication.";if(!std::isfinite(position) || position<0 || position>86400)return "Invalid recording timeline position.";
    const auto ready=readiness(sequence);if(!(bool)ready["ready"])return juce::JSON::toString(ready["errors"]);
    if(impl->worker.joinable())impl->worker.join();
    {std::lock_guard lock(impl->mutex);impl->state="preparing";impl->error={};impl->take=juce::Uuid().toString();impl->project=sequence.projectId;impl->results=juce::var();impl->pending=false;impl->progress.clear();}
    impl->stop=false;impl->stopTime=INT64_MAX;impl->origin=0;impl->worker=std::thread([this,sequence,position]{impl->run(sequence,position);});return {};
}
void RecordingCoordinator::stop(){if(impl->stopTime.load()==INT64_MAX)impl->stopTime=captureHostTime();impl->stop=true;}
bool RecordingCoordinator::active()const{std::lock_guard lock(impl->mutex);return impl->pending || impl->state=="preparing" || impl->state=="recording" || impl->state=="stopping";}
juce::var RecordingCoordinator::status()const
{std::lock_guard lock(impl->mutex);return object({{"state",impl->state},{"error",impl->error},{"takeId",impl->take},{"projectId",impl->project},{"elapsed",impl->origin.load()>0?std::max(0.0,((impl->stopTime.load()==INT64_MAX?captureHostTime():impl->stopTime.load())-impl->origin.load())/10000000.0):0.0},{"origin100ns",impl->origin.load()},{"tracks",impl->progress},{"results",impl->results},{"pendingPublication",impl->pending}});}
juce::var RecordingCoordinator::takeResults(){std::lock_guard lock(impl->mutex);if(!impl->pending)return {};impl->pending=false;return impl->results;}
}
