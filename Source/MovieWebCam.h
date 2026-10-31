#pragma once
#include "MovieVisualCapture.h"

namespace movie {
// Local receiver owns transport/decode only; all recording remains in RecordingCoordinator.
class WebCamCapture {
public:
    WebCamCapture();
    ~WebCamCapture();
    juce::var invite(const juce::String& address,int port);
    void stop();
    std::vector<VisualSource> sources() const;
    void setBindings(std::vector<VisualBinding>,std::vector<RecordingSubscription>);
    VisualPreview preview(const juce::String& source) const;
    juce::var status() const;
    void renderAudio(float*const*,int channels,int frames,double rate);
#if MOVIE_WEBCAM_TESTS
    static void verifyTimingBoundaries();
#endif
private:
    struct Impl;
    std::shared_ptr<Impl> impl;
};
}
