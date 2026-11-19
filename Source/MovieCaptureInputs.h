#pragma once
#include "MovieDocument.h"

namespace movie
{
inline bool isCaptureInput(const Track& track)
{
    const auto category=track.source["category"].toString();
    return (track.type=="video" && (category=="camera" || category=="display" || category=="window"))
        || (track.type=="audio" && category=="microphone");
}
inline bool recordsCaptureInput(const Track& track)
{
    // Capture participation is an input's Enabled flag. Viewer selection and
    // the historical arm/monitor preferences never route or gate recording.
    return track.enabled && isCaptureInput(track);
}
}
