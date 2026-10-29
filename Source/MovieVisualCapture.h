#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <vector>

namespace movie
{
struct VisualSource
{
    juce::String id, kind, name;
    int width=0, height=0;
    juce::var toJson() const;
};
struct VisualBinding
{
    juce::String trackId, sourceId;
    bool active=false;
};
struct VisualPreview
{
    juce::Image image;
    juce::String state{"stopped"}, message;
    juce::int64 frameNumber=0, timestamp100ns=0;
    int subscribers=0;
};
// Owns physical acquisition, independently of document track assignments.
// Discovery, D3D readback and session destruction occur on one MTA worker.
// Snapshots are immutable; only the latest image is retained (no growing queue).
class VisualCapture
{
public:
    VisualCapture();
    ~VisualCapture();
    std::vector<VisualSource> sources() const;
    void refresh();
    void setBindings(std::vector<VisualBinding>);
    VisualPreview preview(const juce::String& trackId) const;
    juce::var status() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
