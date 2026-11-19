#pragma once
#include "MovieMedia.h"
#include <creation/assets/ProjectSession.h>

namespace movie
{
// The sequence's media records are resolved references/decoder metadata, not a
// second asset registry. Identity and storage always come from the Suite catalog.
juce::StringArray resolveProjectMedia(const creation::assets::ProjectSession&, Sequence&,
                                     std::atomic<bool>* cancel = nullptr);
void loadProjectSequence(const creation::assets::ProjectSession&, Sequence&,
                         juce::StringArray& warnings, std::atomic<bool>* cancel = nullptr);
bool registerSequenceMedia(creation::assets::ProjectSession&, Sequence&, juce::String& error);
creation::assets::AssetDescriptor describeProjectMedia(const Media&);
}
