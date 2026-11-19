#pragma once
#include "MovieDocument.h"

namespace movie::detail
{
// Keep the registration operation testable at its metadata I/O boundary. Plan
// all additions before changing flags or the session's in-memory catalog.
template <typename Session, typename ListEntries, typename Describe>
bool registerMediaReferences(Session& session, Sequence& sequence,
                             ListEntries&& listEntries, Describe&& describe,
                             juce::String& error)
{
    if (!session.refreshManifest(error)) return false;
    auto next = sequence.media;
    std::vector<Media> additions;
    juce::StringArray paths;
    bool listed = false;
    for (auto& media : next)
    {
        const auto* asset = session.getManifest().assetCatalog.findByVersionId(media.versionId);
        if (asset != nullptr)
        {
            if (asset->id != media.id || asset->logicalPath != media.path)
            { error = "Movie's media reference disagrees with the shared catalog: " + media.id; return false; }
            media.catalogued = true;
        }
        else if (!media.catalogued)
        {
            if (!listed)
            {
                if (!listEntries(paths, error)) return false;
                listed = true;
            }
            if (!paths.contains(media.path))
            { error = "Movie's media source is missing from the project: " + media.id; return false; }
            additions.push_back(media);
            media.catalogued = true;
        }
        // A previously catalogued, subsequently removed version stays unresolved.
        // Do not resurrect it when saving an older native edit.
    }
    for (const auto& media : additions) session.upsertAssetDescriptor(describe(media));
    sequence.media = std::move(next);
    return true;
}
}
