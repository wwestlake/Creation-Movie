#include "MovieProjectMedia.h"

namespace movie
{
creation::assets::AssetDescriptor describeProjectMedia(const Media& media)
{
    using namespace creation::assets;
    AssetDescriptor asset;
    asset.id = media.id;
    asset.versionId = media.versionId;
    asset.displayName = media.name;
    asset.logicalPath = media.path;
    asset.kind = media.info.hasVideo ? AssetKind::video : AssetKind::audio;
    asset.sourceApp = "Djehuti Movie";
    asset.sourceTool = "Movie media";
    asset.createdAt = asset.modifiedAt = juce::Time::getCurrentTime();
    asset.revision = 1;
    asset.details.set("durationSeconds", juce::String(media.info.duration, 12));
    asset.details.set("hasAudio", media.info.hasAudio ? "1" : "0");
    asset.details.set("hasVideo", media.info.hasVideo ? "1" : "0");
    asset.details.set("width", juce::String(media.info.width));
    asset.details.set("height", juce::String(media.info.height));
    asset.details.set("frameRate", juce::String((double)media.info.fpsNum / std::max(1, media.info.fpsDen), 12));
    asset.details.set("sampleRate", juce::String(media.info.sampleRate));
    asset.details.set("channels", juce::String(media.info.channels));
    return asset;
}

bool registerSequenceMedia(creation::assets::ProjectSession& session, Sequence& sequence, juce::String& error)
{
    if (!session.refreshManifest(error)) return false;
    for (auto& media : sequence.media)
    {
        const auto* asset = session.getManifest().assetCatalog.findByVersionId(media.versionId);
        if (asset != nullptr)
        {
            if (asset->id != media.id || asset->logicalPath != media.path)
            { error = "Movie's media reference disagrees with the shared catalog: " + media.id; return false; }
        }
        else if (!media.catalogued && session.containsEntry(media.path))
        {
            // Register already-stored Movie media by reference. Never duplicate bytes.
            session.upsertAssetDescriptor(describeProjectMedia(media));
        }
        // A previously registered, subsequently removed version stays unresolved;
        // saving an old edit must never resurrect another app's catalog deletion.
        media.catalogued = true;
    }
    return true;
}

juce::StringArray resolveProjectMedia(const creation::assets::ProjectSession& session,
                                     Sequence& sequence, std::atomic<bool>* cancel)
{
    using namespace creation::assets;
    juce::StringArray warnings;
    auto assets = session.getManifest().assetCatalog.assets;
    std::stable_sort(assets.begin(), assets.end(), [](const auto& a, const auto& b)
    { return a.revision != b.revision ? a.revision > b.revision : a.modifiedAt > b.modifiedAt; });
    std::vector<Media> resolved;
    for (const auto& asset : assets)
    {
        if (cancel != nullptr && cancel->load()) throw std::runtime_error("Media resolution cancelled.");
        if (asset.kind != AssetKind::audio && asset.kind != AssetKind::video && asset.kind != AssetKind::render) continue;
        if (asset.versionId.isEmpty()) { warnings.add("Media has no version identity: " + asset.displayName); continue; }
        if (const auto* existing = sequence.findMedia(asset.id, asset.versionId);
            existing != nullptr && existing->path == asset.logicalPath)
        { auto media = *existing; media.name = asset.displayName; media.catalogued = true; resolved.push_back(std::move(media)); continue; }
        try
        {
            MediaReader probe(session.openEntryStream(asset.logicalPath), asset.logicalPath, MediaReader::Kind::probe, cancel);
            if ((!probe.info().hasAudio && !probe.info().hasVideo) || probe.info().duration <= 0)
                throw std::runtime_error("No usable audio/video stream.");
            resolved.push_back({asset.id, asset.versionId, asset.displayName, asset.logicalPath, probe.info(), true});
        }
        catch (const std::exception& e)
        {
            if (cancel != nullptr && cancel->load()) throw;
            warnings.add(asset.displayName + ": " + e.what());
        }
    }
    // Retain exact references used by existing edits even if their source is
    // missing. Resolution must never silently retarget a clip to a newer version.
    for (const auto& media : sequence.media)
        if (std::none_of(resolved.begin(), resolved.end(), [&](const auto& m)
            { return m.id == media.id && m.versionId == media.versionId; })) resolved.push_back(media);
    sequence.media = std::move(resolved);
    return warnings;
}

void loadProjectSequence(const creation::assets::ProjectSession& session, Sequence& sequence,
                         juce::StringArray& warnings, std::atomic<bool>* cancel)
{
    juce::MemoryBlock bytes;
    juce::String error;
    bool found = false;
    if (!session.readOptionalEntry("Project/movie-state.json", bytes, found, error))
        throw std::runtime_error(error.toStdString());
    Sequence loaded;
    if (found)
    {
        if (!Sequence::fromJson(juce::JSON::parse(bytes.toString()), loaded, error))
            throw std::runtime_error(error.toStdString());
    }
    else loaded.name = session.getManifest().projectName;
    loaded.projectId = session.getProjectId();
    warnings = resolveProjectMedia(session, loaded, cancel);
    sequence = std::move(loaded);
}
}
