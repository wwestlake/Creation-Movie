#include "MainComponent.h"

#include "Branding.h"
#include "../Language/AppLanguagePolicy.h"

namespace
{
constexpr const char* kMovieStateEntryPath = "Project/movie-state.json";

juce::Colour panelOutlineColour()
{
    return creation_movie::branding::accentColour().withAlpha(0.32f);
}

juce::String formatTimecode(double seconds, double fps)
{
    const auto safeFps = juce::jmax(1, static_cast<int>(std::round(fps)));
    const auto totalFrames = juce::jmax(0, static_cast<int>(std::round(seconds * safeFps)));
    const auto frames = totalFrames % safeFps;
    const auto totalSeconds = totalFrames / safeFps;
    const auto secs = totalSeconds % 60;
    const auto mins = (totalSeconds / 60) % 60;
    const auto hours = totalSeconds / 3600;

    return juce::String::formatted("%02d:%02d:%02d:%02d", hours, mins, secs, frames);
}

juce::String formatShortDuration(double seconds)
{
    if (seconds <= 0.0)
        return "clip metadata pending";

    const auto totalSeconds = juce::jmax(0, static_cast<int>(std::round(seconds)));
    const auto secs = totalSeconds % 60;
    const auto mins = (totalSeconds / 60) % 60;
    const auto hours = totalSeconds / 3600;

    if (hours > 0)
        return juce::String::formatted("%d:%02d:%02d", hours, mins, secs);

    return juce::String::formatted("%02d:%02d", mins, secs);
}

juce::String assetKindLabel(MainComponent::AssetKind kind)
{
    switch (kind)
    {
        case MainComponent::AssetKind::video: return "Video";
        case MainComponent::AssetKind::audio: return "Audio";
        case MainComponent::AssetKind::image: return "Still";
    }

    return "Asset";
}

juce::String assetKindToken(MainComponent::AssetKind kind)
{
    switch (kind)
    {
        case MainComponent::AssetKind::video: return "video";
        case MainComponent::AssetKind::audio: return "audio";
        case MainComponent::AssetKind::image: return "image";
    }

    return "video";
}

MainComponent::AssetKind assetKindFromToken(const juce::String& token)
{
    if (token == "audio")
        return MainComponent::AssetKind::audio;
    if (token == "image")
        return MainComponent::AssetKind::image;
    return MainComponent::AssetKind::video;
}

juce::Colour colourForAssetKind(MainComponent::AssetKind kind)
{
    switch (kind)
    {
        case MainComponent::AssetKind::video: return juce::Colour(0xff4a95cf);
        case MainComponent::AssetKind::audio: return juce::Colour(0xffe28d57);
        case MainComponent::AssetKind::image: return juce::Colour(0xff8f63d6);
    }

    return juce::Colours::grey;
}

bool isVideoExtension(const juce::String& extension)
{
    static const juce::StringArray extensions { ".mov", ".mp4", ".m4v", ".avi", ".mkv", ".wmv", ".webm", ".mpeg", ".mpg" };
    return extensions.contains(extension.toLowerCase());
}

bool isImageExtension(const juce::String& extension)
{
    static const juce::StringArray extensions { ".png", ".jpg", ".jpeg", ".bmp", ".gif", ".tif", ".tiff", ".webp" };
    return extensions.contains(extension.toLowerCase());
}

int laneForAssetKind(MainComponent::AssetKind kind)
{
    switch (kind)
    {
        case MainComponent::AssetKind::video: return 0;
        case MainComponent::AssetKind::image: return 1;
        case MainComponent::AssetKind::audio: return 2;
    }

    return 0;
}

double defaultClipDurationForAsset(const MainComponent::AssetRecord& asset)
{
    if (asset.durationSeconds > 0.0)
        return asset.durationSeconds;

    switch (asset.kind)
    {
        case MainComponent::AssetKind::video: return 6.0;
        case MainComponent::AssetKind::image: return 4.0;
        case MainComponent::AssetKind::audio: return 3.0;
    }

    return 4.0;
}

const MainComponent::AssetRecord* findAssetById(const std::vector<MainComponent::AssetRecord>& assets, const juce::String& assetId)
{
    for (const auto& asset : assets)
        if (asset.id == assetId)
            return &asset;

    return nullptr;
}

void tryReloadImagePreview(MainComponent::AssetRecord& asset)
{
    if (asset.kind != MainComponent::AssetKind::image)
        return;

    const juce::File file(asset.sourcePath);
    if (! file.existsAsFile())
        return;

    asset.previewImage = juce::ImageFileFormat::loadFrom(file);
    if (asset.previewImage.isValid())
    {
        asset.width = asset.previewImage.getWidth();
        asset.height = asset.previewImage.getHeight();
    }
}

class ManagedDocumentWindow final : public juce::DocumentWindow
{
public:
    ManagedDocumentWindow(const juce::String& title,
                          juce::Colour backgroundColour,
                          int buttons,
                          std::function<void()> onCloseCallback)
        : juce::DocumentWindow(title, backgroundColour, buttons),
          onClose(std::move(onCloseCallback))
    {
    }

    void closeButtonPressed() override
    {
        setVisible(false);
        if (onClose)
            onClose();
    }

private:
    std::function<void()> onClose;
};

class StationStyleTransportLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    void drawButtonBackground(juce::Graphics& g,
                              juce::Button& button,
                              const juce::Colour& backgroundColour,
                              bool isMouseOverButton,
                              bool isButtonDown) override
    {
        juce::ignoreUnused(backgroundColour);

        auto bounds = button.getLocalBounds().toFloat().reduced(1.5f);
        auto isToggle = button.getToggleState();
        auto accent = juce::Colour(0xff59dfff);
        auto fill = juce::Colour(0xff17222c);

        if (button.getName() == "recordButton")
            accent = juce::Colour(0xffff5f73);
        else if (button.getName() == "scrubButton")
            accent = juce::Colour(0xff56f4ff);

        if (isToggle)
            fill = accent.withAlpha(0.25f).overlaidWith(juce::Colour(0xff13202b));
        else if (isButtonDown)
            fill = accent.withAlpha(0.20f).overlaidWith(fill);
        else if (isMouseOverButton)
            fill = accent.withAlpha(0.12f).overlaidWith(fill);

        g.setColour(accent.withAlpha(isToggle ? 0.35f : isMouseOverButton ? 0.35f : 0.14f));
        g.fillRoundedRectangle(bounds.expanded(2.0f), 13.0f);
        g.setColour(fill);
        g.fillRoundedRectangle(bounds, 11.0f);

        g.setColour(accent.withAlpha(isToggle ? 1.0f : 0.62f));
        g.drawRoundedRectangle(bounds, 11.0f, isToggle ? 2.0f : 1.3f);

        auto ring = bounds.reduced(7.0f, 5.0f);
        if (ring.getWidth() > 18.0f && ring.getHeight() > 18.0f && button.getButtonText() != "record")
        {
            auto diameter = juce::jmin(ring.getWidth(), ring.getHeight());
            auto circle = juce::Rectangle<float>(diameter, diameter).withCentre(ring.getCentre());
            g.setColour(accent.withAlpha(isToggle ? 0.96f : 0.36f));
            g.drawEllipse(circle, isToggle ? 2.4f : 2.0f);
        }
    }

    void drawButtonText(juce::Graphics& g,
                        juce::TextButton& button,
                        bool isMouseOverButton,
                        bool isButtonDown) override
    {
        auto bounds = button.getLocalBounds().toFloat().reduced(8.0f, 7.0f);
        auto accent = button.getName() == "recordButton" ? juce::Colour(0xffff6b7c)
                                                         : juce::Colour(0xffdcecff);

        g.setColour(button.getToggleState() ? juce::Colours::white
                                            : (isButtonDown ? accent
                                                            : isMouseOverButton ? juce::Colour(0xffdcecff)
                                                                                : juce::Colour(0xffb8c4d5)));
        drawTransportIcon(g, bounds, button.getButtonText());
    }

private:
    static void drawTransportIcon(juce::Graphics& g, juce::Rectangle<float> bounds, const juce::String& iconName)
    {
        auto centre = bounds.getCentre();
        auto size = juce::jmin(bounds.getWidth(), bounds.getHeight());

        if (iconName == "play")
        {
            juce::Path path;
            path.addTriangle(centre.x - size * 0.22f, centre.y - size * 0.32f,
                             centre.x - size * 0.22f, centre.y + size * 0.32f,
                             centre.x + size * 0.32f, centre.y);
            g.fillPath(path);
            return;
        }

        if (iconName == "stop")
        {
            auto square = juce::Rectangle<float>(size * 0.55f, size * 0.55f).withCentre(centre);
            g.fillRoundedRectangle(square, 2.0f);
            return;
        }

        if (iconName == "record")
        {
            auto circle = juce::Rectangle<float>(size * 0.48f, size * 0.48f).withCentre(centre);
            g.fillEllipse(circle);
            return;
        }

        if (iconName == "rewind" || iconName == "forward")
        {
            const auto direction = iconName == "rewind" ? -1.0f : 1.0f;
            juce::Path path;
            path.addTriangle(centre.x - direction * size * 0.02f, centre.y,
                             centre.x + direction * size * 0.22f, centre.y - size * 0.28f,
                             centre.x + direction * size * 0.22f, centre.y + size * 0.28f);
            path.addTriangle(centre.x - direction * size * 0.30f, centre.y,
                             centre.x - direction * size * 0.06f, centre.y - size * 0.28f,
                             centre.x - direction * size * 0.06f, centre.y + size * 0.28f);
            g.fillPath(path);
            return;
        }

        if (iconName == "scrub")
        {
            auto arc = bounds.reduced(size * 0.18f);
            g.drawEllipse(arc, 2.0f);
            g.drawLine(arc.getCentreX(), arc.getY(), arc.getCentreX(), arc.getBottom(), 2.0f);
            return;
        }
    }
};

StationStyleTransportLookAndFeel& getStationStyleTransportLookAndFeel()
{
    static StationStyleTransportLookAndFeel lookAndFeel;
    return lookAndFeel;
}

class WorkspacePanelBase : public juce::Component
{
public:
    explicit WorkspacePanelBase(juce::String panelTitleToUse) : panelTitle(std::move(panelTitleToUse)) {}

protected:
    void paintPanelFrame(juce::Graphics& g, juce::Rectangle<int> bounds) const
    {
        auto area = bounds.toFloat();
        g.setColour(juce::Colour(0xff131c2a));
        g.fillRoundedRectangle(area, 18.0f);
        g.setColour(panelOutlineColour());
        g.drawRoundedRectangle(area, 18.0f, 1.0f);

        auto header = bounds.removeFromTop(32).toFloat();
        g.setColour(juce::Colour(0xff1d2940));
        g.fillRoundedRectangle(header, 16.0f);

        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(16.0f, juce::Font::bold));
        g.drawText(panelTitle, header.reduced(14.0f, 0.0f), juce::Justification::centredLeft, false);
    }

    juce::Rectangle<int> getPanelContentBounds() const
    {
        auto area = getLocalBounds().reduced(12);
        area.removeFromTop(42);
        return area;
    }

private:
    juce::String panelTitle;
};

class PreviewSurface final : public WorkspacePanelBase
{
public:
    PreviewSurface(const MainComponent::TransportState& stateToUse,
                   const std::vector<MainComponent::AssetRecord>& assetsToUse,
                   const std::vector<MainComponent::TimelineClip>& clipsToUse,
                   const int& selectedAssetIndexToUse,
                   const int& selectedClipIndexToUse)
        : WorkspacePanelBase("Program Monitor"),
          state(stateToUse),
          assets(assetsToUse),
          clips(clipsToUse),
          selectedAssetIndex(selectedAssetIndexToUse),
          selectedClipIndex(selectedClipIndexToUse)
    {
    }

    void paint(juce::Graphics& g) override
    {
        paintPanelFrame(g, getLocalBounds().reduced(2));

        auto area = getPanelContentBounds();
        g.setColour(juce::Colour(0xff0a0f18));
        g.fillRoundedRectangle(area.toFloat(), 14.0f);

        auto monitor = area.reduced(18);
        const auto frameHeight = static_cast<int>(monitor.getWidth() / 1.7777f);
        auto frame = juce::Rectangle<int>(monitor.getX(),
                                          monitor.getCentreY() - (frameHeight / 2),
                                          monitor.getWidth(),
                                          juce::jmin(frameHeight, monitor.getHeight()));

        g.setColour(juce::Colour(0xff05080e));
        g.fillRoundedRectangle(frame.toFloat(), 12.0f);

        const auto* focusedClip = (selectedClipIndex >= 0 && selectedClipIndex < static_cast<int>(clips.size())) ? &clips[static_cast<size_t>(selectedClipIndex)] : nullptr;
        const auto* focusedAsset = getFocusedAsset();

        if (focusedAsset != nullptr && focusedAsset->kind == MainComponent::AssetKind::image && focusedAsset->previewImage.isValid())
        {
            const auto imageArea = frame.reduced(8);
            g.drawImageWithin(focusedAsset->previewImage,
                              imageArea.getX(),
                              imageArea.getY(),
                              imageArea.getWidth(),
                              imageArea.getHeight(),
                              juce::RectanglePlacement::centred);
        }
        else
        {
            juce::ColourGradient gradient(juce::Colour(0xff113356),
                                          frame.getTopLeft().toFloat(),
                                          juce::Colour(0xff2f6777),
                                          frame.getBottomRight().toFloat(),
                                          false);
            gradient.addColour(0.55, juce::Colour(0xffd97c4c));
            g.setGradientFill(gradient);
            g.fillRoundedRectangle(frame.toFloat().reduced(8.0f), 8.0f);

            g.setColour(juce::Colours::white.withAlpha(0.08f));
            for (int i = 1; i < 3; ++i)
                g.drawLine(static_cast<float>(frame.getX() + (frame.getWidth() * i / 3)),
                           static_cast<float>(frame.getY()),
                           static_cast<float>(frame.getX() + (frame.getWidth() * i / 3)),
                           static_cast<float>(frame.getBottom()),
                           1.0f);

            for (int i = 1; i < 3; ++i)
                g.drawLine(static_cast<float>(frame.getX()),
                           static_cast<float>(frame.getY() + (frame.getHeight() * i / 3)),
                           static_cast<float>(frame.getRight()),
                           static_cast<float>(frame.getY() + (frame.getHeight() * i / 3)),
                           1.0f);
        }

        const auto title = focusedClip != nullptr ? focusedClip->title
                                                  : (focusedAsset != nullptr ? focusedAsset->name
                                                                             : "No media selected");
        const auto subtitle = focusedAsset != nullptr ? focusedAsset->metadataLine
                                                      : "Import media and place clips on the timeline to begin editing.";

        g.setColour(juce::Colours::white.withAlpha(0.90f));
        g.setFont(juce::Font(18.0f, juce::Font::bold));
        g.drawText(title, frame.reduced(18).removeFromTop(28), juce::Justification::topLeft, false);

        g.setFont(juce::Font(13.5f));
        g.drawText(subtitle,
                   frame.reduced(18).withTrimmedTop(34).removeFromTop(24),
                   juce::Justification::topLeft,
                   false);

        auto lower = frame.reduced(18);
        lower.removeFromTop(lower.getHeight() - 56);

        g.setColour(juce::Colour(0xaa000000));
        g.fillRoundedRectangle(lower.toFloat(), 10.0f);

        g.setColour(creation_movie::branding::accentColour());
        g.setFont(juce::Font(20.0f, juce::Font::bold));
        g.drawText(formatTimecode(state.playheadSeconds, state.framesPerSecond),
                   lower.removeFromLeft(220),
                   juce::Justification::centred,
                   false);

        g.setColour(juce::Colours::white.withAlpha(0.92f));
        g.setFont(juce::Font(14.0f, juce::Font::plain));
        g.drawText(state.scrubMode ? "Scrub armed" : (state.isPlaying ? "Rolling" : "Standing by"),
                   lower.removeFromLeft(140),
                   juce::Justification::centred,
                   false);

        const auto details = focusedAsset != nullptr
                                 ? assetKindLabel(focusedAsset->kind) + "  |  " + focusedAsset->metadataLine
                                 : "24 fps  |  preview shell ready";
        g.drawText(details, lower, juce::Justification::centredRight, false);
    }

private:
    const MainComponent::AssetRecord* getFocusedAsset() const
    {
        if (selectedAssetIndex >= 0 && selectedAssetIndex < static_cast<int>(assets.size()))
            return &assets[static_cast<size_t>(selectedAssetIndex)];

        if (selectedClipIndex >= 0 && selectedClipIndex < static_cast<int>(clips.size()))
            return findAssetById(assets, clips[static_cast<size_t>(selectedClipIndex)].assetId);

        return nullptr;
    }

    const MainComponent::TransportState& state;
    const std::vector<MainComponent::AssetRecord>& assets;
    const std::vector<MainComponent::TimelineClip>& clips;
    const int& selectedAssetIndex;
    const int& selectedClipIndex;
};

class TimelineCanvas final : public WorkspacePanelBase
{
public:
    TimelineCanvas(const MainComponent::TransportState& stateToUse,
                   const std::vector<MainComponent::TimelineClip>& clipsToUse,
                   const std::vector<MainComponent::TimelineMarker>& markersToUse,
                   const std::vector<MainComponent::TimelineRegion>& regionsToUse,
                   const int& selectedClipIndexToUse,
                   std::function<void(int)> onClipSelectedToUse,
                   std::function<void(int, double, int)> onClipMovedToUse,
                   std::function<void(double)> onPlayheadMovedToUse)
        : WorkspacePanelBase("Video Tracker"),
          state(stateToUse),
          clips(clipsToUse),
          markers(markersToUse),
          regions(regionsToUse),
          selectedClipIndex(selectedClipIndexToUse),
          onClipSelected(std::move(onClipSelectedToUse)),
          onClipMoved(std::move(onClipMovedToUse)),
          onPlayheadMoved(std::move(onPlayheadMovedToUse))
    {
    }

    void paint(juce::Graphics& g) override
    {
        paintPanelFrame(g, getLocalBounds().reduced(2));

        auto area = getPanelContentBounds();
        auto ruler = area.removeFromTop(42);
        auto labels = area.removeFromLeft(148);

        g.setColour(juce::Colour(0xff0e1521));
        g.fillRoundedRectangle(ruler.toFloat(), 10.0f);
        g.setColour(juce::Colour(0xff0f1824));
        g.fillRoundedRectangle(labels.toFloat(), 10.0f);
        g.setColour(juce::Colour(0xff0b111b));
        g.fillRoundedRectangle(area.toFloat(), 10.0f);

        for (const auto& region : regions)
            drawRegion(g, area, region);

        const auto end = state.visibleStartSeconds + state.visibleLengthSeconds;
        for (int second = static_cast<int>(std::floor(state.visibleStartSeconds)); second <= static_cast<int>(std::ceil(end)); ++second)
        {
            const auto ratio = (second - state.visibleStartSeconds) / state.visibleLengthSeconds;
            const auto x = area.getX() + static_cast<int>(ratio * area.getWidth());
            g.setColour(juce::Colours::white.withAlpha(0.12f));
            g.drawVerticalLine(x, static_cast<float>(area.getY()), static_cast<float>(area.getBottom()));

            g.setColour(juce::Colours::white.withAlpha(0.7f));
            g.setFont(juce::Font(13.0f, juce::Font::bold));
            g.drawText(formatTimecode(static_cast<double>(second), state.framesPerSecond),
                       x + 6,
                       ruler.getY() + 3,
                       112,
                       18,
                       juce::Justification::left,
                       false);

            g.setColour(juce::Colours::white.withAlpha(0.55f));
            g.setFont(juce::Font(11.0f));
            g.drawText(juce::String(second) + " s",
                       x + 6,
                       ruler.getY() + 20,
                       56,
                       16,
                       juce::Justification::left,
                       false);
        }

        const auto laneHeight = 68;
        const juce::StringArray laneNames { "V1 Picture", "V2 Graphics", "A1 Dialogue", "A2 Music", "A3 FX" };
        for (int laneIndex = 0; laneIndex < laneNames.size(); ++laneIndex)
        {
            const auto y = area.getY() + laneIndex * laneHeight;
            auto laneBounds = juce::Rectangle<int>(area.getX(), y, area.getWidth(), laneHeight - 6);
            auto labelBounds = juce::Rectangle<int>(labels.getX(), y, labels.getWidth(), laneHeight - 6);

            g.setColour(juce::Colour(0xff162235));
            g.fillRoundedRectangle(labelBounds.toFloat(), 10.0f);
            g.setColour(juce::Colours::white.withAlpha(0.88f));
            g.setFont(juce::Font(14.0f, juce::Font::bold));
            g.drawText(laneNames[laneIndex], labelBounds.reduced(12, 8), juce::Justification::topLeft, false);
            g.setFont(juce::Font(11.0f));
            g.setColour(juce::Colours::white.withAlpha(0.56f));
            g.drawText(laneIndex < 2 ? "picture lane" : "audio lane",
                       labelBounds.reduced(12, 8).withTrimmedTop(22),
                       juce::Justification::topLeft,
                       false);

            g.setColour(juce::Colours::white.withAlpha(0.05f));
            g.fillRoundedRectangle(laneBounds.toFloat(), 10.0f);
        }

        for (size_t i = 0; i < clips.size(); ++i)
            drawClip(g, area, static_cast<int>(i), clips[i], static_cast<int>(i) == selectedClipIndex);

        for (const auto& marker : markers)
            drawMarker(g, ruler, area, marker);

        const auto playheadRatio = (state.playheadSeconds - state.visibleStartSeconds) / state.visibleLengthSeconds;
        const auto playheadX = area.getX() + static_cast<int>(playheadRatio * area.getWidth());
        g.setColour(creation_movie::branding::accentColour());
        g.drawVerticalLine(playheadX, static_cast<float>(ruler.getY()), static_cast<float>(area.getBottom()));
        g.fillEllipse(static_cast<float>(playheadX - 5), static_cast<float>(ruler.getY() + 12), 10.0f, 10.0f);
    }

    void mouseUp(const juce::MouseEvent& event) override
    {
        dragClipIndex = -1;
        auto area = getTimelineArea();

        for (size_t i = 0; i < clips.size(); ++i)
        {
            if (getClipBounds(area, clips[i]).contains(event.getPosition()))
            {
                if (onClipSelected)
                    onClipSelected(static_cast<int>(i));
                return;
            }
        }

        if (area.contains(event.getPosition()) && onPlayheadMoved)
            onPlayheadMoved(xToTime(area, event.x));
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        auto area = getTimelineArea();
        for (size_t i = 0; i < clips.size(); ++i)
        {
            auto clipBounds = getClipBounds(area, clips[i]);
            if (clipBounds.contains(event.getPosition()))
            {
                dragClipIndex = static_cast<int>(i);
                dragOffsetSeconds = xToTime(area, event.x) - clips[i].startSeconds;
                return;
            }
        }
    }

    void mouseDrag(const juce::MouseEvent& event) override
    {
        if (dragClipIndex < 0 || dragClipIndex >= static_cast<int>(clips.size()) || ! onClipMoved)
            return;

        auto area = getTimelineArea();
        const auto laneHeight = 68;
        auto newLane = juce::jlimit(0, 4, (event.y - area.getY()) / laneHeight);
        auto newStart = xToTime(area, event.x) - dragOffsetSeconds;
        newStart = juce::jmax(0.0, newStart);
        onClipMoved(dragClipIndex, newStart, newLane);
    }

private:
    juce::Rectangle<int> getTimelineArea() const
    {
        auto area = getPanelContentBounds();
        area.removeFromTop(42);
        area.removeFromLeft(148);
        return area;
    }

    double xToTime(juce::Rectangle<int> laneArea, int x) const
    {
        const auto ratio = juce::jlimit(0.0, 1.0, (x - laneArea.getX()) / static_cast<double>(juce::jmax(1, laneArea.getWidth())));
        return state.visibleStartSeconds + (ratio * state.visibleLengthSeconds);
    }

    void drawMarker(juce::Graphics& g, juce::Rectangle<int> ruler, juce::Rectangle<int> laneArea, const MainComponent::TimelineMarker& marker) const
    {
        const auto ratio = (marker.timeSeconds - state.visibleStartSeconds) / state.visibleLengthSeconds;
        if (ratio < 0.0 || ratio > 1.0)
            return;

        const auto x = laneArea.getX() + static_cast<int>(ratio * laneArea.getWidth());
        g.setColour(juce::Colour(0xfff0d36a));
        g.drawVerticalLine(x, static_cast<float>(ruler.getBottom()), static_cast<float>(laneArea.getBottom()));
        juce::Path flag;
        flag.startNewSubPath(static_cast<float>(x), static_cast<float>(ruler.getY() + 4));
        flag.lineTo(static_cast<float>(x + 14), static_cast<float>(ruler.getY() + 10));
        flag.lineTo(static_cast<float>(x), static_cast<float>(ruler.getY() + 16));
        flag.closeSubPath();
        g.fillPath(flag);
        g.setColour(juce::Colours::white.withAlpha(0.92f));
        g.setFont(juce::Font(11.0f, juce::Font::bold));
        g.drawText(marker.title, x + 18, ruler.getY() + 2, 120, 16, juce::Justification::left, false);
    }

    void drawRegion(juce::Graphics& g, juce::Rectangle<int> laneArea, const MainComponent::TimelineRegion& region) const
    {
        const auto startRatio = (region.startSeconds - state.visibleStartSeconds) / state.visibleLengthSeconds;
        const auto endRatio = (region.endSeconds - state.visibleStartSeconds) / state.visibleLengthSeconds;
        const auto clampedStart = juce::jlimit(0.0, 1.0, startRatio);
        const auto clampedEnd = juce::jlimit(0.0, 1.0, endRatio);
        if (clampedEnd <= clampedStart)
            return;

        auto bounds = juce::Rectangle<int>(laneArea.getX() + static_cast<int>(clampedStart * laneArea.getWidth()),
                                           laneArea.getY(),
                                           static_cast<int>((clampedEnd - clampedStart) * laneArea.getWidth()),
                                           laneArea.getHeight());
        g.setColour(region.colour.withAlpha(0.12f));
        g.fillRoundedRectangle(bounds.toFloat(), 8.0f);
        g.setColour(region.colour.withAlpha(0.28f));
        g.drawRoundedRectangle(bounds.toFloat(), 8.0f, 1.0f);
        g.setColour(region.colour.brighter(0.3f));
        g.setFont(juce::Font(12.0f, juce::Font::bold));
        g.drawText(region.title, bounds.reduced(8, 4).removeFromTop(18), juce::Justification::centredLeft, false);
    }

    juce::Rectangle<int> getClipBounds(juce::Rectangle<int> laneArea, const MainComponent::TimelineClip& clip) const
    {
        const auto laneHeight = 68;
        const auto y = laneArea.getY() + clip.laneIndex * laneHeight + 10;
        const auto x = laneArea.getX() + static_cast<int>(((clip.startSeconds - state.visibleStartSeconds) / state.visibleLengthSeconds) * laneArea.getWidth());
        const auto width = static_cast<int>((clip.durationSeconds / state.visibleLengthSeconds) * laneArea.getWidth());
        return { x, y, juce::jmax(110, width), laneHeight - 24 };
    }

    void drawClip(juce::Graphics& g,
                  juce::Rectangle<int> laneArea,
                  int clipIndex,
                  const MainComponent::TimelineClip& clip,
                  bool isSelected) const
    {
        auto clipBounds = getClipBounds(laneArea, clip);

        g.setColour(clip.colour);
        g.fillRoundedRectangle(clipBounds.toFloat(), 10.0f);
        g.setColour(juce::Colours::white.withAlpha(isSelected ? 1.0f : 0.9f));
        g.drawRoundedRectangle(clipBounds.toFloat(), 10.0f, isSelected ? 2.0f : 1.0f);
        g.setFont(juce::Font(14.0f, juce::Font::bold));
        g.drawText(clip.title, clipBounds.reduced(12, 8).removeFromTop(20), juce::Justification::centredLeft, false);
        g.setFont(juce::Font(11.0f));
        g.drawText(formatTimecode(clip.startSeconds, state.framesPerSecond) + "  to  "
                       + formatTimecode(clip.startSeconds + clip.durationSeconds, state.framesPerSecond),
                   clipBounds.reduced(12, 8).withTrimmedTop(20),
                   juce::Justification::centredLeft,
                   false);

        if (isSelected)
        {
            g.setColour(juce::Colours::white.withAlpha(0.12f));
            g.drawText("selected", clipBounds.removeFromRight(68), juce::Justification::centred, false);
        }
    }

    const MainComponent::TransportState& state;
    const std::vector<MainComponent::TimelineClip>& clips;
    const std::vector<MainComponent::TimelineMarker>& markers;
    const std::vector<MainComponent::TimelineRegion>& regions;
    const int& selectedClipIndex;
    std::function<void(int)> onClipSelected;
    std::function<void(int, double, int)> onClipMoved;
    std::function<void(double)> onPlayheadMoved;
    int dragClipIndex = -1;
    double dragOffsetSeconds = 0.0;
};

class AssetLibraryPanel final : public WorkspacePanelBase
{
public:
    AssetLibraryPanel(const std::vector<MainComponent::AssetRecord>& assetsToUse,
                      const int& selectedAssetIndexToUse,
                      std::function<void(int)> onAssetSelectedToUse)
        : WorkspacePanelBase("Media Library"),
          assets(assetsToUse),
          selectedAssetIndex(selectedAssetIndexToUse),
          onAssetSelected(std::move(onAssetSelectedToUse))
    {
    }

    void paint(juce::Graphics& g) override
    {
        paintPanelFrame(g, getLocalBounds().reduced(2));
        auto area = getPanelContentBounds();

        if (assets.empty())
        {
            g.setColour(juce::Colours::white.withAlpha(0.55f));
            g.setFont(juce::Font(14.0f));
            g.drawFittedText("Import media to begin building the sequence library.",
                             area.reduced(16),
                             juce::Justification::centred,
                             4);
            return;
        }

        for (size_t i = 0; i < assets.size(); ++i)
        {
            auto card = getCardBoundsForIndex(area, static_cast<int>(i));
            const auto& asset = assets[i];
            const auto isSelected = static_cast<int>(i) == selectedAssetIndex;

            g.setColour(isSelected ? juce::Colour(0xff27405f) : juce::Colour(0xff18263a));
            g.fillRoundedRectangle(card.toFloat(), 12.0f);
            g.setColour(isSelected ? creation_movie::branding::accentColour() : juce::Colours::white.withAlpha(0.08f));
            g.drawRoundedRectangle(card.toFloat(), 12.0f, isSelected ? 1.5f : 1.0f);

            auto text = card.reduced(12, 10);
            g.setColour(juce::Colours::white.withAlpha(0.94f));
            g.setFont(juce::Font(14.0f, juce::Font::bold));
            g.drawText(asset.name, text.removeFromTop(22), juce::Justification::centredLeft, false);

            g.setColour(juce::Colours::white.withAlpha(0.62f));
            g.setFont(juce::Font(11.5f));
            g.drawText(asset.metadataLine, text.removeFromTop(18), juce::Justification::centredLeft, false);
            g.drawText(asset.sourcePath, text, juce::Justification::bottomLeft, true);
        }
    }

    void mouseUp(const juce::MouseEvent& event) override
    {
        auto area = getPanelContentBounds();

        for (size_t i = 0; i < assets.size(); ++i)
        {
            if (getCardBoundsForIndex(area, static_cast<int>(i)).contains(event.getPosition()))
            {
                if (onAssetSelected)
                    onAssetSelected(static_cast<int>(i));
                return;
            }
        }
    }

private:
    juce::Rectangle<int> getCardBoundsForIndex(juce::Rectangle<int> area, int index) const
    {
        const auto cardHeight = 74;
        const auto gap = 10;
        area.removeFromTop(index * (cardHeight + gap));
        return area.removeFromTop(cardHeight);
    }

    const std::vector<MainComponent::AssetRecord>& assets;
    const int& selectedAssetIndex;
    std::function<void(int)> onAssetSelected;
};

class InspectorPanel final : public WorkspacePanelBase
{
public:
    InspectorPanel(const MainComponent::TransportState& stateToUse,
                   const std::vector<MainComponent::AssetRecord>& assetsToUse,
                   const std::vector<MainComponent::TimelineClip>& clipsToUse,
                   const int& selectedAssetIndexToUse,
                   const int& selectedClipIndexToUse)
        : WorkspacePanelBase("Inspector"),
          state(stateToUse),
          assets(assetsToUse),
          clips(clipsToUse),
          selectedAssetIndex(selectedAssetIndexToUse),
          selectedClipIndex(selectedClipIndexToUse)
    {
    }

    void paint(juce::Graphics& g) override
    {
        paintPanelFrame(g, getLocalBounds().reduced(2));
        auto area = getPanelContentBounds();

        drawSection(g, area.removeFromTop(120), "Sequence",
                    juce::String("Project: Untitled Movie\nFrame rate: ")
                        + juce::String(state.framesPerSecond, 0)
                        + " fps\nVisible range: "
                        + formatTimecode(state.visibleStartSeconds, state.framesPerSecond)
                        + " - "
                        + formatTimecode(state.visibleStartSeconds + state.visibleLengthSeconds, state.framesPerSecond));
        area.removeFromTop(10);

        if (selectedClipIndex >= 0 && selectedClipIndex < static_cast<int>(clips.size()))
        {
            const auto& clip = clips[static_cast<size_t>(selectedClipIndex)];
            drawSection(g, area.removeFromTop(116), "Selected Clip",
                        clip.title + "\nIn: " + formatTimecode(clip.startSeconds, state.framesPerSecond)
                            + "\nDuration: " + formatTimecode(clip.durationSeconds, state.framesPerSecond)
                            + "\nLane: " + juce::String(clip.laneIndex + 1));
            area.removeFromTop(10);
        }

        const auto* asset = getFocusedAsset();
        if (asset != nullptr)
        {
            drawSection(g, area.removeFromTop(132), "Focused Asset",
                        asset->name + "\n" + asset->metadataLine + "\nSource: " + asset->sourcePath);
            area.removeFromTop(10);
        }

        drawSection(g, area.removeFromTop(108), "Routing",
                    "Video FX lane: pending\nAudio FX lane: VST3 ready\nSuite asset binding: planned next step");
    }

private:
    const MainComponent::AssetRecord* getFocusedAsset() const
    {
        if (selectedAssetIndex >= 0 && selectedAssetIndex < static_cast<int>(assets.size()))
            return &assets[static_cast<size_t>(selectedAssetIndex)];

        if (selectedClipIndex >= 0 && selectedClipIndex < static_cast<int>(clips.size()))
            return findAssetById(assets, clips[static_cast<size_t>(selectedClipIndex)].assetId);

        return nullptr;
    }

    void drawSection(juce::Graphics& g, juce::Rectangle<int> bounds, const juce::String& title, const juce::String& body) const
    {
        g.setColour(juce::Colour(0xff18263a));
        g.fillRoundedRectangle(bounds.toFloat(), 12.0f);

        auto text = bounds.reduced(12, 10);
        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(14.0f, juce::Font::bold));
        g.drawText(title, text.removeFromTop(22), juce::Justification::centredLeft, false);
        g.setColour(juce::Colours::white.withAlpha(0.68f));
        g.setFont(juce::Font(12.0f));
        g.drawFittedText(body, text, juce::Justification::topLeft, 6);
    }

    const MainComponent::TransportState& state;
    const std::vector<MainComponent::AssetRecord>& assets;
    const std::vector<MainComponent::TimelineClip>& clips;
    const int& selectedAssetIndex;
    const int& selectedClipIndex;
};

class EulaPanel final : public juce::Component
{
public:
    explicit EulaPanel(const juce::String& text)
    {
        title.setText("End User License Agreement", juce::dontSendNotification);
        title.setFont(juce::Font(20.0f).boldened());
        title.setColour(juce::Label::textColourId, juce::Colours::white);
        addAndMakeVisible(title);

        body.setMultiLine(true);
        body.setReadOnly(true);
        body.setScrollbarsShown(true);
        body.setCaretVisible(false);
        body.setText(text, juce::dontSendNotification);
        addAndMakeVisible(body);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(creation_movie::branding::backgroundColour());
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(16);
        title.setBounds(area.removeFromTop(28));
        area.removeFromTop(8);
        body.setBounds(area);
    }

private:
    juce::Label title;
    juce::TextEditor body;
};
}

MainComponent::MainComponent()
{
    juce::String suiteSettingsError;
    suiteSettings = creation::suite::SuiteSettingsStore().load(suiteSettingsError);

    audioFormatManager.registerBasicFormats();

    titleLabel.setText("Creation Movie", juce::dontSendNotification);
    titleLabel.setFont(juce::Font(33.0f, juce::Font::bold));
    titleLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(titleLabel);

    subtitleLabel.setText("Suite-native editing, preview, and picture assembly with room for CEL and render workflows.",
                          juce::dontSendNotification);
    subtitleLabel.setColour(juce::Label::textColourId, juce::Colour(0xffb9c8db));
    addAndMakeVisible(subtitleLabel);

    projectChipLabel.setText("Project: " + projectName, juce::dontSendNotification);
    projectChipLabel.setJustificationType(juce::Justification::centred);
    projectChipLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(projectChipLabel);

    runtimeLabel.setText(creation_movie::language::getLanguageRuntimeSummary(), juce::dontSendNotification);
    runtimeLabel.setColour(juce::Label::textColourId, creation_movie::branding::accentColour().brighter(0.15f));
    addAndMakeVisible(runtimeLabel);

    statusLabel.setColour(juce::Label::textColourId, juce::Colour(0xffd9e1ec));
    addAndMakeVisible(statusLabel);

    rewindButton.setLookAndFeel(&getStationStyleTransportLookAndFeel());
    fastForwardButton.setLookAndFeel(&getStationStyleTransportLookAndFeel());
    stopButton.setLookAndFeel(&getStationStyleTransportLookAndFeel());
    playButton.setLookAndFeel(&getStationStyleTransportLookAndFeel());
    scrubButton.setLookAndFeel(&getStationStyleTransportLookAndFeel());
    recordButton.setLookAndFeel(&getStationStyleTransportLookAndFeel());

    rewindButton.setButtonText("rewind");
    fastForwardButton.setButtonText("forward");
    stopButton.setButtonText("stop");
    playButton.setButtonText("play");
    scrubButton.setButtonText("scrub");
    recordButton.setButtonText("record");
    recordButton.setName("recordButton");
    scrubButton.setName("scrubButton");
    rewindButton.setTooltip("Move the visible timeline earlier");
    fastForwardButton.setTooltip("Move the visible timeline later");
    stopButton.setTooltip("Stop playback");
    playButton.setTooltip("Play from the current playhead");
    scrubButton.setTooltip("Toggle scrub mode");
    recordButton.setTooltip("Arm transport recording mode");

    openButton.onClick = [this] { openProject(); };
    saveButton.onClick = [this] { saveProject(); };
    rewindButton.onClick = [this] { scrollTimeline(-(transportState.visibleLengthSeconds * 0.25)); };
    fastForwardButton.onClick = [this] { scrollTimeline(transportState.visibleLengthSeconds * 0.25); };
    recordButton.onClick = [this] { toggleRecording(); };
    importButton.onClick = [this] { importMediaFiles(); };
    placeButton.onClick = [this] { placeSelectedAssetOnTimeline(); };
    zoomOutButton.onClick = [this] { zoomTimeline(1.35); };
    zoomInButton.onClick = [this] { zoomTimeline(0.75); };
    scrollLeftButton.onClick = [this] { scrollTimeline(-(transportState.visibleLengthSeconds * 0.35)); };
    scrollRightButton.onClick = [this] { scrollTimeline(transportState.visibleLengthSeconds * 0.35); };
    markerButton.onClick = [this] { addMarkerAtPlayhead(); };
    regionButton.onClick = [this] { createRegionFromMarkers(); };
    splitButton.onClick = [this] { splitSelectedClip(); };
    stepBackButton.onClick = [this] { stepPlayheadByFrames(-1); };
    stepForwardButton.onClick = [this] { stepPlayheadByFrames(1); };
    playButton.onClick = [this] { togglePlayback(); };
    stopButton.onClick = [this] { stopPlayback(); };
    scrubButton.onClick = [this] { toggleScrubMode(); };
    previewButton.onClick = [this] { showPreviewWindow(); };
    eulaButton.onClick = [this] { showEulaWindow(); };

    addAndMakeVisible(rewindButton);
    addAndMakeVisible(fastForwardButton);
    addAndMakeVisible(recordButton);
    addAndMakeVisible(openButton);
    addAndMakeVisible(saveButton);
    addAndMakeVisible(importButton);
    addAndMakeVisible(placeButton);
    addAndMakeVisible(zoomOutButton);
    addAndMakeVisible(zoomInButton);
    addAndMakeVisible(scrollLeftButton);
    addAndMakeVisible(scrollRightButton);
    addAndMakeVisible(markerButton);
    addAndMakeVisible(regionButton);
    addAndMakeVisible(splitButton);
    addAndMakeVisible(stepBackButton);
    addAndMakeVisible(stepForwardButton);
    addAndMakeVisible(playButton);
    addAndMakeVisible(stopButton);
    addAndMakeVisible(scrubButton);
    addAndMakeVisible(previewButton);
    addAndMakeVisible(eulaButton);

    seedDemoContent();

    previewSurface = std::make_unique<PreviewSurface>(transportState, assets, timelineClips, selectedAssetIndex, selectedClipIndex);
    timelineCanvas = std::make_unique<TimelineCanvas>(transportState,
                                                      timelineClips,
                                                      timelineMarkers,
                                                      timelineRegions,
                                                      selectedClipIndex,
                                                      [this](int clipIndex) { selectClip(clipIndex); },
                                                      [this](int clipIndex, double startSeconds, int laneIndex) { updateClipPlacement(clipIndex, startSeconds, laneIndex); },
                                                      [this](double timeSeconds) { movePlayheadTo(timeSeconds); });
    assetLibraryPanel = std::make_unique<AssetLibraryPanel>(assets, selectedAssetIndex, [this](int assetIndex) { selectAsset(assetIndex); });
    inspectorPanel = std::make_unique<InspectorPanel>(transportState, assets, timelineClips, selectedAssetIndex, selectedClipIndex);

    addAndMakeVisible(*previewSurface);
    addAndMakeVisible(*timelineCanvas);
    addAndMakeVisible(*assetLibraryPanel);
    addAndMakeVisible(*inspectorPanel);

    updateTransportButtons();
    updateStatusText();

    setSize(1480, 920);
    startTimerHz(30);
}

MainComponent::~MainComponent()
{
    rewindButton.setLookAndFeel(nullptr);
    fastForwardButton.setLookAndFeel(nullptr);
    stopButton.setLookAndFeel(nullptr);
    playButton.setLookAndFeel(nullptr);
    scrubButton.setLookAndFeel(nullptr);
    recordButton.setLookAndFeel(nullptr);
    closePreviewWindow();
    closeEulaWindow();
}

void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(creation_movie::branding::backgroundColour());

    auto chrome = getLocalBounds().toFloat().reduced(14.0f);
    g.setColour(juce::Colour(0xff111827));
    g.fillRoundedRectangle(chrome, 26.0f);
    g.setColour(panelOutlineColour());
    g.drawRoundedRectangle(chrome, 26.0f, 1.4f);

    auto header = juce::Rectangle<float>(chrome.getX() + 16.0f, chrome.getY() + 16.0f, chrome.getWidth() - 32.0f, 108.0f);
    g.setColour(juce::Colour(0xff0d141f));
    g.fillRoundedRectangle(header, 22.0f);
    g.setColour(creation_movie::branding::accentColour().withAlpha(0.30f));
    g.drawRoundedRectangle(header, 22.0f, 1.0f);

    auto transportGlow = juce::Rectangle<float>(chrome.getX() + 446.0f, chrome.getY() + 30.0f, 468.0f, 52.0f);
    g.setColour(isRecording ? juce::Colour(0x33ff5f73) : juce::Colour(0x2259dfff));
    g.fillRoundedRectangle(transportGlow, 18.0f);
    g.setColour(isRecording ? juce::Colour(0xffff6b7c) : juce::Colour(0xff59dfff).withAlpha(0.65f));
    g.drawRoundedRectangle(transportGlow, 18.0f, isRecording ? 1.8f : 1.0f);
}

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced(30, 26);
    auto header = area.removeFromTop(112);
    area.removeFromTop(16);

    auto titleArea = header.removeFromLeft(430);
    titleLabel.setBounds(titleArea.removeFromTop(42));
    subtitleLabel.setBounds(titleArea.removeFromTop(26));
    runtimeLabel.setBounds(titleArea.removeFromTop(22));

    auto transportHeader = header.removeFromLeft(490);
    auto transportRow = transportHeader.removeFromTop(52);
    rewindButton.setBounds(transportRow.removeFromLeft(62).reduced(4, 2));
    fastForwardButton.setBounds(transportRow.removeFromLeft(62).reduced(4, 2));
    stopButton.setBounds(transportRow.removeFromLeft(62).reduced(4, 2));
    playButton.setBounds(transportRow.removeFromLeft(78).reduced(4, 2));
    scrubButton.setBounds(transportRow.removeFromLeft(84).reduced(4, 2));
    recordButton.setBounds(transportRow.removeFromLeft(92).reduced(4, 2));

    auto centerHeader = header.removeFromLeft(260);
    projectChipLabel.setBounds(centerHeader.removeFromTop(42).reduced(10, 4));
    statusLabel.setBounds(centerHeader.removeFromTop(26).reduced(10, 0));

    auto controlRow = header.removeFromTop(42);
    openButton.setBounds(controlRow.removeFromLeft(102).reduced(4, 2));
    saveButton.setBounds(controlRow.removeFromLeft(102).reduced(4, 2));
    importButton.setBounds(controlRow.removeFromLeft(106).reduced(4, 2));
    placeButton.setBounds(controlRow.removeFromLeft(124).reduced(4, 2));
    zoomOutButton.setBounds(controlRow.removeFromLeft(80).reduced(4, 2));
    zoomInButton.setBounds(controlRow.removeFromLeft(80).reduced(4, 2));
    scrollLeftButton.setBounds(controlRow.removeFromLeft(86).reduced(4, 2));
    scrollRightButton.setBounds(controlRow.removeFromLeft(86).reduced(4, 2));
    markerButton.setBounds(controlRow.removeFromLeft(84).reduced(4, 2));
    regionButton.setBounds(controlRow.removeFromLeft(84).reduced(4, 2));
    splitButton.setBounds(controlRow.removeFromLeft(84).reduced(4, 2));
    stepBackButton.setBounds(controlRow.removeFromLeft(80).reduced(4, 2));
    stepForwardButton.setBounds(controlRow.removeFromLeft(80).reduced(4, 2));
    previewButton.setBounds(controlRow.removeFromLeft(150).reduced(4, 2));
    eulaButton.setBounds(controlRow.removeFromLeft(64).reduced(4, 2));

    auto upperRow = area.removeFromTop(352);
    auto leftColumn = upperRow.removeFromLeft(322);
    upperRow.removeFromLeft(14);
    auto rightColumn = upperRow.removeFromRight(316);
    upperRow.removeFromRight(14);

    assetLibraryPanel->setBounds(leftColumn);
    inspectorPanel->setBounds(rightColumn);
    previewSurface->setBounds(upperRow);

    area.removeFromTop(14);
    timelineCanvas->setBounds(area);
}

void MainComponent::importMediaFiles()
{
    importChooser = std::make_unique<juce::FileChooser>("Import media into Creation Movie");
    importChooser->launchAsync(juce::FileBrowserComponent::openMode
                                   | juce::FileBrowserComponent::canSelectFiles
                                   | juce::FileBrowserComponent::canSelectMultipleItems,
                               [this](const juce::FileChooser& chooser)
                               {
                                   for (const auto& file : chooser.getResults())
                                       ingestMediaFile(file);

                                   if (! assets.empty())
                                       selectAsset(static_cast<int>(assets.size()) - 1);

                                   assetLibraryPanel->repaint();
                                   inspectorPanel->repaint();
                                   previewSurface->repaint();
                                   repaint();
                                   importChooser.reset();
                               });
}

void MainComponent::ingestMediaFile(const juce::File& file)
{
    AssetRecord asset;
    asset.id = juce::Uuid().toString();
    asset.name = file.getFileName();
    asset.sourcePath = file.getFullPathName();

    const auto extension = file.getFileExtension().toLowerCase();

    if (isImageExtension(extension))
    {
        asset.kind = AssetKind::image;
        asset.previewImage = juce::ImageFileFormat::loadFrom(file);
        asset.width = asset.previewImage.getWidth();
        asset.height = asset.previewImage.getHeight();
        asset.metadataLine = "Still  |  " + juce::String(asset.width) + "x" + juce::String(asset.height)
                             + "  |  " + formatShortDuration(defaultClipDurationForAsset(asset));
    }
    else if (isVideoExtension(extension))
    {
        asset.kind = AssetKind::video;
        asset.durationSeconds = 6.0;
        asset.frameRate = transportState.framesPerSecond;
        asset.metadataLine = "Video  |  imported clip  |  metadata expansion pending";
    }
    else if (auto reader = std::unique_ptr<juce::AudioFormatReader>(audioFormatManager.createReaderFor(file)))
    {
        asset.kind = AssetKind::audio;
        asset.channels = static_cast<int>(reader->numChannels);
        asset.sampleRate = reader->sampleRate;
        if (reader->sampleRate > 0.0)
            asset.durationSeconds = reader->lengthInSamples / reader->sampleRate;

        asset.metadataLine = "Audio  |  "
                             + juce::String(asset.channels) + " ch  |  "
                             + juce::String(static_cast<int>(std::round(asset.sampleRate))) + " Hz  |  "
                             + formatShortDuration(asset.durationSeconds);
    }
    else
    {
        return;
    }

    assets.push_back(std::move(asset));
}

void MainComponent::openProject()
{
    // Projects are picked from the framework's own catalog, never an OS file browser (there's
    // no loose .csproj file to browse for anymore -- see docs/architecture/
    // Suite-Shared-Project-Model.md). A full shared project-browser integration
    // (SuiteShellController, as Station/Engine use) is its own scope beyond this migration;
    // this is the minimal correct replacement, matching CreationStation's own project-list
    // popup menu pattern.
    juce::String listError;
    auto availableProjects = creation::assets::ProjectContainerService::listProjects(
        suiteSettings, creation::assets::SuiteAppDomain::movie, listError);

    if (availableProjects.isEmpty())
    {
        juce::NativeMessageBox::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,
                                                    "No Projects Found",
                                                    listError.isNotEmpty() ? listError : "No Creation Movie projects were found.");
        return;
    }

    juce::PopupMenu menu;
    for (int index = 0; index < availableProjects.size(); ++index)
        menu.addItem(index + 1, availableProjects.getReference(index).manifest.projectName);

    menu.showMenuAsync(juce::PopupMenu::Options(), [this, availableProjects](int result)
    {
        if (result < 1 || result > availableProjects.size())
            return;

        const auto& selected = availableProjects.getReference(result - 1);
        juce::String errorMessage;
        if (creation::assets::ProjectWorkspaceService::openProject(suiteSettings, selected.projectId, projectSession, errorMessage))
        {
            loadProjectFromSession();
        }
        else
        {
            juce::NativeMessageBox::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                                                        "Could Not Open Project",
                                                        errorMessage);
        }
    });
}

void MainComponent::saveProject()
{
    if (projectSession.isValid())
    {
        saveProjectToSession();
        return;
    }

    auto* alertWindow = new juce::AlertWindow("New Movie Project",
                                              "Name this project:",
                                              juce::MessageBoxIconType::NoIcon);
    alertWindow->addTextEditor("projectName", projectName, "Project name:");
    alertWindow->addButton("Create", 1, juce::KeyPress(juce::KeyPress::returnKey));
    alertWindow->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

    alertWindow->enterModalState(true,
                                 juce::ModalCallbackFunction::create([this, alertWindow](int result)
                                 {
                                     if (result == 1)
                                     {
                                         const auto name = alertWindow->getTextEditorContents("projectName").trim();
                                         if (name.isNotEmpty())
                                             createNewProject(name);
                                     }
                                 }),
                                 true /* deleteWhenDismissed */);
}

void MainComponent::createNewProject(const juce::String& name)
{
    juce::String errorMessage;
    if (! creation::assets::ProjectWorkspaceService::createProject(suiteSettings,
                                                                   creation::assets::SuiteAppDomain::movie,
                                                                   name,
                                                                   "1.0.0",
                                                                   "1.0.0",
                                                                   projectSession,
                                                                   errorMessage))
    {
        juce::NativeMessageBox::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                                                    "Could Not Create Project",
                                                    errorMessage);
        return;
    }

    projectName = name;
    projectChipLabel.setText("Project: " + projectName, juce::dontSendNotification);
    saveProjectToSession();
}

void MainComponent::zoomTimeline(double factor)
{
    transportState.visibleLengthSeconds = juce::jlimit(4.0, 240.0, transportState.visibleLengthSeconds * factor);
    transportState.visibleStartSeconds = juce::jlimit(0.0,
                                                      juce::jmax(0.0, transportState.projectDurationSeconds - transportState.visibleLengthSeconds),
                                                      transportState.playheadSeconds - (transportState.visibleLengthSeconds * 0.5));
    timelineCanvas->repaint();
    inspectorPanel->repaint();
    updateStatusText();
}

void MainComponent::scrollTimeline(double deltaSeconds)
{
    transportState.visibleStartSeconds = juce::jlimit(0.0,
                                                      juce::jmax(0.0, transportState.projectDurationSeconds - transportState.visibleLengthSeconds),
                                                      transportState.visibleStartSeconds + deltaSeconds);
    timelineCanvas->repaint();
    inspectorPanel->repaint();
    updateStatusText();
}

void MainComponent::addMarkerAtPlayhead()
{
    TimelineMarker marker;
    marker.title = "M" + juce::String(static_cast<int>(timelineMarkers.size()) + 1);
    marker.timeSeconds = transportState.playheadSeconds;
    timelineMarkers.push_back(std::move(marker));
    std::sort(timelineMarkers.begin(), timelineMarkers.end(), [](const auto& a, const auto& b) { return a.timeSeconds < b.timeSeconds; });
    timelineCanvas->repaint();
    inspectorPanel->repaint();
}

void MainComponent::createRegionFromMarkers()
{
    if (timelineMarkers.size() < 2)
        return;

    const auto& first = timelineMarkers[timelineMarkers.size() - 2];
    const auto& second = timelineMarkers[timelineMarkers.size() - 1];
    TimelineRegion region;
    region.title = "Region " + juce::String(static_cast<int>(timelineRegions.size()) + 1);
    region.startSeconds = juce::jmin(first.timeSeconds, second.timeSeconds);
    region.endSeconds = juce::jmax(first.timeSeconds, second.timeSeconds);
    region.colour = creation_movie::branding::accentColour();
    timelineRegions.push_back(std::move(region));
    timelineCanvas->repaint();
}

void MainComponent::splitSelectedClip()
{
    if (selectedClipIndex < 0 || selectedClipIndex >= static_cast<int>(timelineClips.size()))
        return;

    auto& clip = timelineClips[static_cast<size_t>(selectedClipIndex)];
    if (transportState.playheadSeconds <= clip.startSeconds + (1.0 / transportState.framesPerSecond)
        || transportState.playheadSeconds >= clip.startSeconds + clip.durationSeconds - (1.0 / transportState.framesPerSecond))
        return;

    const auto splitTime = transportState.playheadSeconds;
    const auto secondDuration = (clip.startSeconds + clip.durationSeconds) - splitTime;
    clip.durationSeconds = splitTime - clip.startSeconds;

    TimelineClip splitClip = clip;
    splitClip.startSeconds = splitTime;
    splitClip.durationSeconds = secondDuration;
    splitClip.title = clip.title + " B";
    timelineClips.insert(timelineClips.begin() + selectedClipIndex + 1, splitClip);
    selectClip(selectedClipIndex + 1);
}

void MainComponent::updateClipPlacement(int clipIndex, double startSeconds, int laneIndex)
{
    if (clipIndex < 0 || clipIndex >= static_cast<int>(timelineClips.size()))
        return;

    auto& clip = timelineClips[static_cast<size_t>(clipIndex)];
    clip.startSeconds = juce::jlimit(0.0, juce::jmax(0.0, transportState.projectDurationSeconds - clip.durationSeconds), startSeconds);
    clip.laneIndex = juce::jlimit(0, 4, laneIndex);
    selectedClipIndex = clipIndex;
    transportState.playheadSeconds = clip.startSeconds;
    timelineCanvas->repaint();
    inspectorPanel->repaint();
    previewSurface->repaint();
    updateStatusText();
}

void MainComponent::movePlayheadTo(double timeSeconds)
{
    transportState.playheadSeconds = juce::jlimit(0.0, transportState.projectDurationSeconds, timeSeconds);
    previewSurface->repaint();
    timelineCanvas->repaint();
    inspectorPanel->repaint();
    updateStatusText();
}

void MainComponent::loadProjectFromSession()
{
    juce::MemoryBlock stateData;
    if (! projectSession.readEntry(kMovieStateEntryPath, stateData))
        return;

    const auto jsonText = juce::String::fromUTF8(static_cast<const char*>(stateData.getData()), static_cast<int>(stateData.getSize()));
    const auto parsed = juce::JSON::parse(jsonText);
    if (! parsed.isObject())
        return;

    const auto* root = parsed.getDynamicObject();
    if (root == nullptr)
        return;

    projectName = projectSession.getManifest().projectName;

    transportState.framesPerSecond = static_cast<double>(root->getProperty("framesPerSecond"));
    transportState.projectDurationSeconds = static_cast<double>(root->getProperty("projectDurationSeconds"));
    transportState.visibleStartSeconds = static_cast<double>(root->getProperty("visibleStartSeconds"));
    transportState.visibleLengthSeconds = static_cast<double>(root->getProperty("visibleLengthSeconds"));
    transportState.playheadSeconds = static_cast<double>(root->getProperty("playheadSeconds"));
    transportState.isPlaying = false;
    transportState.scrubMode = false;

    assets.clear();
    timelineClips.clear();
    timelineMarkers.clear();
    timelineRegions.clear();

    if (const auto* assetArray = root->getProperty("assets").getArray())
    {
        for (const auto& entry : *assetArray)
        {
            if (const auto* object = entry.getDynamicObject())
            {
                AssetRecord asset;
                asset.id = object->getProperty("id").toString();
                asset.name = object->getProperty("name").toString();
                asset.sourcePath = object->getProperty("sourcePath").toString();
                asset.kind = assetKindFromToken(object->getProperty("kind").toString());
                asset.metadataLine = object->getProperty("metadataLine").toString();
                asset.durationSeconds = static_cast<double>(object->getProperty("durationSeconds"));
                asset.frameRate = static_cast<double>(object->getProperty("frameRate"));
                asset.sampleRate = static_cast<double>(object->getProperty("sampleRate"));
                asset.width = static_cast<int>(object->getProperty("width"));
                asset.height = static_cast<int>(object->getProperty("height"));
                asset.channels = static_cast<int>(object->getProperty("channels"));
                tryReloadImagePreview(asset);
                assets.push_back(std::move(asset));
            }
        }
    }

    if (const auto* clipArray = root->getProperty("clips").getArray())
    {
        for (const auto& entry : *clipArray)
        {
            if (const auto* object = entry.getDynamicObject())
            {
                TimelineClip clip;
                clip.assetId = object->getProperty("assetId").toString();
                clip.title = object->getProperty("title").toString();
                clip.laneIndex = static_cast<int>(object->getProperty("laneIndex"));
                clip.startSeconds = static_cast<double>(object->getProperty("startSeconds"));
                clip.durationSeconds = static_cast<double>(object->getProperty("durationSeconds"));
                clip.colour = juce::Colour::fromString(object->getProperty("colour").toString());
                timelineClips.push_back(std::move(clip));
            }
        }
    }

    if (const auto* markerArray = root->getProperty("markers").getArray())
    {
        for (const auto& entry : *markerArray)
        {
            if (const auto* object = entry.getDynamicObject())
            {
                TimelineMarker marker;
                marker.title = object->getProperty("title").toString();
                marker.timeSeconds = static_cast<double>(object->getProperty("timeSeconds"));
                timelineMarkers.push_back(std::move(marker));
            }
        }
    }

    if (const auto* regionArray = root->getProperty("regions").getArray())
    {
        for (const auto& entry : *regionArray)
        {
            if (const auto* object = entry.getDynamicObject())
            {
                TimelineRegion region;
                region.title = object->getProperty("title").toString();
                region.startSeconds = static_cast<double>(object->getProperty("startSeconds"));
                region.endSeconds = static_cast<double>(object->getProperty("endSeconds"));
                region.colour = juce::Colour::fromString(object->getProperty("colour").toString());
                timelineRegions.push_back(std::move(region));
            }
        }
    }

    selectedAssetIndex = -1;
    selectedClipIndex = timelineClips.empty() ? -1 : 0;
    projectChipLabel.setText("Project: " + projectName, juce::dontSendNotification);

    assetLibraryPanel->repaint();
    timelineCanvas->repaint();
    inspectorPanel->repaint();
    previewSurface->repaint();
    repaint();
}

void MainComponent::saveProjectToSession()
{
    if (! projectSession.isValid())
        return;

    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    root->setProperty("framesPerSecond", transportState.framesPerSecond);
    root->setProperty("projectDurationSeconds", transportState.projectDurationSeconds);
    root->setProperty("visibleStartSeconds", transportState.visibleStartSeconds);
    root->setProperty("visibleLengthSeconds", transportState.visibleLengthSeconds);
    root->setProperty("playheadSeconds", transportState.playheadSeconds);

    juce::Array<juce::var> assetArray;
    for (const auto& asset : assets)
    {
        juce::DynamicObject::Ptr object = new juce::DynamicObject();
        object->setProperty("id", asset.id);
        object->setProperty("name", asset.name);
        object->setProperty("sourcePath", asset.sourcePath);
        object->setProperty("kind", assetKindToken(asset.kind));
        object->setProperty("metadataLine", asset.metadataLine);
        object->setProperty("durationSeconds", asset.durationSeconds);
        object->setProperty("frameRate", asset.frameRate);
        object->setProperty("sampleRate", asset.sampleRate);
        object->setProperty("width", asset.width);
        object->setProperty("height", asset.height);
        object->setProperty("channels", asset.channels);
        assetArray.add(juce::var(object.get()));
    }
    root->setProperty("assets", juce::var(assetArray));

    juce::Array<juce::var> clipArray;
    for (const auto& clip : timelineClips)
    {
        juce::DynamicObject::Ptr object = new juce::DynamicObject();
        object->setProperty("assetId", clip.assetId);
        object->setProperty("title", clip.title);
        object->setProperty("laneIndex", clip.laneIndex);
        object->setProperty("startSeconds", clip.startSeconds);
        object->setProperty("durationSeconds", clip.durationSeconds);
        object->setProperty("colour", clip.colour.toString());
        clipArray.add(juce::var(object.get()));
    }
    root->setProperty("clips", juce::var(clipArray));

    juce::Array<juce::var> markerArray;
    for (const auto& marker : timelineMarkers)
    {
        juce::DynamicObject::Ptr object = new juce::DynamicObject();
        object->setProperty("title", marker.title);
        object->setProperty("timeSeconds", marker.timeSeconds);
        markerArray.add(juce::var(object.get()));
    }
    root->setProperty("markers", juce::var(markerArray));

    juce::Array<juce::var> regionArray;
    for (const auto& region : timelineRegions)
    {
        juce::DynamicObject::Ptr object = new juce::DynamicObject();
        object->setProperty("title", region.title);
        object->setProperty("startSeconds", region.startSeconds);
        object->setProperty("endSeconds", region.endSeconds);
        object->setProperty("colour", region.colour.toString());
        regionArray.add(juce::var(object.get()));
    }
    root->setProperty("regions", juce::var(regionArray));

    const auto jsonText = juce::JSON::toString(juce::var(root.get()), true);
    const juce::MemoryBlock stateData(jsonText.toRawUTF8(), jsonText.getNumBytesAsUTF8());

    if (! projectSession.writeEntry(kMovieStateEntryPath, stateData))
    {
        juce::NativeMessageBox::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                                                    "Could Not Save Project",
                                                    "Could not write the project state into the container.");
        return;
    }

    juce::String errorMessage;
    if (! projectSession.commit(errorMessage))
    {
        juce::NativeMessageBox::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                                                    "Could Not Save Project",
                                                    errorMessage);
        return;
    }

    projectChipLabel.setText("Project: " + projectName, juce::dontSendNotification);
}

void MainComponent::placeSelectedAssetOnTimeline()
{
    if (selectedAssetIndex < 0 || selectedAssetIndex >= static_cast<int>(assets.size()))
        return;

    const auto& asset = assets[static_cast<size_t>(selectedAssetIndex)];

    TimelineClip clip;
    clip.assetId = asset.id;
    clip.title = juce::File(asset.name).getFileNameWithoutExtension();
    clip.laneIndex = laneForAssetKind(asset.kind);
    clip.startSeconds = transportState.playheadSeconds;
    clip.durationSeconds = defaultClipDurationForAsset(asset);
    clip.colour = colourForAssetKind(asset.kind);

    if (asset.kind == AssetKind::audio && timelineClips.size() % 2 == 1)
        clip.laneIndex = 3;

    timelineClips.push_back(std::move(clip));
    selectClip(static_cast<int>(timelineClips.size()) - 1);
}

void MainComponent::selectAsset(int assetIndex)
{
    if (assetIndex < 0 || assetIndex >= static_cast<int>(assets.size()))
        return;

    selectedAssetIndex = assetIndex;
    selectedClipIndex = -1;
    assetLibraryPanel->repaint();
    inspectorPanel->repaint();
    previewSurface->repaint();
    timelineCanvas->repaint();
}

void MainComponent::selectClip(int clipIndex)
{
    if (clipIndex < 0 || clipIndex >= static_cast<int>(timelineClips.size()))
        return;

    selectedClipIndex = clipIndex;
    selectedAssetIndex = -1;
    const auto& clip = timelineClips[static_cast<size_t>(clipIndex)];
    transportState.playheadSeconds = clip.startSeconds;

    const auto visibleEnd = transportState.visibleStartSeconds + transportState.visibleLengthSeconds;
    if (clip.startSeconds < transportState.visibleStartSeconds || clip.startSeconds > visibleEnd)
        transportState.visibleStartSeconds = juce::jlimit(0.0,
                                                          transportState.projectDurationSeconds - transportState.visibleLengthSeconds,
                                                          clip.startSeconds - 2.0);

    timelineCanvas->repaint();
    inspectorPanel->repaint();
    previewSurface->repaint();
    assetLibraryPanel->repaint();
}

void MainComponent::seedDemoContent()
{
    assets.clear();
    timelineClips.clear();
    timelineMarkers.clear();
    timelineRegions.clear();
    projectName = "Untitled Movie";

    AssetRecord video;
    video.id = juce::Uuid().toString();
    video.name = "Interview_CamA_4k.mov";
    video.sourcePath = "Demo source";
    video.kind = AssetKind::video;
    video.durationSeconds = 6.4;
    video.frameRate = 24.0;
    video.metadataLine = "Video  |  3840x2160  |  24 fps  |  03:42";
    assets.push_back(std::move(video));

    AssetRecord audio;
    audio.id = juce::Uuid().toString();
    audio.name = "Ink3_Master.wav";
    audio.sourcePath = "Demo source";
    audio.kind = AssetKind::audio;
    audio.durationSeconds = 179.0;
    audio.channels = 2;
    audio.sampleRate = 48000.0;
    audio.metadataLine = "Audio  |  Stereo  |  48 kHz  |  02:59";
    assets.push_back(std::move(audio));

    AssetRecord image;
    image.id = juce::Uuid().toString();
    image.name = "City_Skyline_Still.png";
    image.sourcePath = "Demo source";
    image.kind = AssetKind::image;
    image.width = 4096;
    image.height = 2160;
    image.metadataLine = "Still  |  4096x2160  |  imported graphic";
    assets.push_back(std::move(image));

    AssetRecord title;
    title.id = juce::Uuid().toString();
    title.name = "LowerThird_01";
    title.sourcePath = "Demo source";
    title.kind = AssetKind::image;
    title.metadataLine = "Still  |  title template  |  suite asset";
    assets.push_back(std::move(title));

    timelineClips.push_back({ assets[0].id, "Scene A", 0, 9.2, 6.4, colourForAssetKind(AssetKind::video) });
    timelineClips.push_back({ assets[3].id, "Lower Third", 1, 13.8, 3.8, colourForAssetKind(AssetKind::image) });
    timelineClips.push_back({ assets[1].id, "Dialogue", 2, 9.2, 6.4, colourForAssetKind(AssetKind::audio) });
    timelineClips.push_back({ assets[1].id, "Theme Bed", 3, 8.6, 11.1, juce::Colour(0xff2f9f7f) });
    timelineMarkers.push_back({ "Intro", 9.0 });
    timelineMarkers.push_back({ "Verse", 17.0 });
    timelineRegions.push_back({ "Act 1", 8.8, 18.0, juce::Colour(0xff6a9bd8) });

    selectedClipIndex = 0;
    selectedAssetIndex = -1;
    projectChipLabel.setText("Project: " + projectName, juce::dontSendNotification);
}

void MainComponent::timerCallback()
{
    if (transportState.isPlaying)
    {
        transportState.playheadSeconds += 1.0 / 30.0;
        if (transportState.playheadSeconds >= transportState.projectDurationSeconds)
            transportState.playheadSeconds = 0.0;

        const auto visibleEnd = transportState.visibleStartSeconds + transportState.visibleLengthSeconds;
        if (transportState.playheadSeconds > visibleEnd - 2.0)
            transportState.visibleStartSeconds = juce::jlimit(0.0,
                                                              transportState.projectDurationSeconds - transportState.visibleLengthSeconds,
                                                              transportState.playheadSeconds - (transportState.visibleLengthSeconds * 0.5));
    }

    updateStatusText();
    previewSurface->repaint();
    timelineCanvas->repaint();

    if (previewWindow != nullptr)
        previewWindow->repaint();
}

void MainComponent::updateTransportButtons()
{
    playButton.setButtonText("play");
    playButton.setToggleState(transportState.isPlaying, juce::dontSendNotification);
    scrubButton.setToggleState(transportState.scrubMode, juce::dontSendNotification);
    recordButton.setToggleState(isRecording, juce::dontSendNotification);
    placeButton.setEnabled(selectedAssetIndex >= 0);
    splitButton.setEnabled(selectedClipIndex >= 0);
    regionButton.setEnabled(timelineMarkers.size() >= 2);
}

void MainComponent::updateStatusText()
{
    const auto status = transportState.scrubMode ? "Scrub ready" : (transportState.isPlaying ? "Rolling timeline" : "Idle");
    statusLabel.setText(juce::String(status) + "  |  "
                            + formatTimecode(transportState.playheadSeconds, transportState.framesPerSecond)
                            + "  |  assets: " + juce::String(static_cast<int>(assets.size()))
                            + "  |  clips: " + juce::String(static_cast<int>(timelineClips.size())),
                        juce::dontSendNotification);
    updateTransportButtons();
}

void MainComponent::showEulaWindow()
{
    if (eulaWindow != nullptr)
    {
        eulaWindow->toFront(true);
        return;
    }

    auto window = std::make_unique<ManagedDocumentWindow>("Creation Movie EULA",
                                                          creation_movie::branding::backgroundColour(),
                                                          juce::DocumentWindow::allButtons,
                                                          [this] { closeEulaWindow(); });
    window->setUsingNativeTitleBar(true);
    window->setResizable(true, true);
    window->setContentOwned(new EulaPanel(creation_movie::legal::getEulaText()), true);
    window->centreWithSize(820, 640);
    window->setVisible(true);
    eulaWindow = std::move(window);
}

void MainComponent::closeEulaWindow()
{
    eulaWindow.reset();
}

void MainComponent::showPreviewWindow()
{
    if (previewWindow != nullptr)
    {
        previewWindow->toFront(true);
        return;
    }

    auto window = std::make_unique<ManagedDocumentWindow>("Creation Movie Preview",
                                                          creation_movie::branding::backgroundColour(),
                                                          juce::DocumentWindow::allButtons,
                                                          [this] { closePreviewWindow(); });
    window->setUsingNativeTitleBar(true);
    window->setResizable(true, true);
    window->setContentOwned(new PreviewSurface(transportState, assets, timelineClips, selectedAssetIndex, selectedClipIndex), true);
    window->centreWithSize(960, 620);
    window->setVisible(true);
    previewWindow = std::move(window);
}

void MainComponent::closePreviewWindow()
{
    previewWindow.reset();
}

void MainComponent::togglePlayback()
{
    transportState.isPlaying = ! transportState.isPlaying;
    updateTransportButtons();
    updateStatusText();
}

void MainComponent::stopPlayback()
{
    transportState.isPlaying = false;

    if (selectedClipIndex >= 0 && selectedClipIndex < static_cast<int>(timelineClips.size()))
        transportState.playheadSeconds = timelineClips[static_cast<size_t>(selectedClipIndex)].startSeconds;
    else
        transportState.playheadSeconds = transportState.visibleStartSeconds;

    updateTransportButtons();
    updateStatusText();
    previewSurface->repaint();
    timelineCanvas->repaint();
}

void MainComponent::toggleRecording()
{
    isRecording = ! isRecording;
    if (isRecording)
        transportState.isPlaying = false;

    updateTransportButtons();
    updateStatusText();
    repaint();
}

void MainComponent::toggleScrubMode()
{
    transportState.scrubMode = ! transportState.scrubMode;
    updateTransportButtons();
    updateStatusText();
    previewSurface->repaint();
}

void MainComponent::stepPlayheadByFrames(int frameDelta)
{
    const auto secondsPerFrame = 1.0 / juce::jmax(1.0, transportState.framesPerSecond);
    transportState.playheadSeconds = juce::jlimit(0.0,
                                                  transportState.projectDurationSeconds,
                                                  transportState.playheadSeconds + (secondsPerFrame * frameDelta));

    if (transportState.playheadSeconds < transportState.visibleStartSeconds)
        transportState.visibleStartSeconds = juce::jmax(0.0, transportState.playheadSeconds - secondsPerFrame);

    const auto visibleEnd = transportState.visibleStartSeconds + transportState.visibleLengthSeconds;
    if (transportState.playheadSeconds > visibleEnd)
        transportState.visibleStartSeconds = juce::jlimit(0.0,
                                                          transportState.projectDurationSeconds - transportState.visibleLengthSeconds,
                                                          transportState.playheadSeconds - transportState.visibleLengthSeconds + (secondsPerFrame * 2.0));

    updateStatusText();
    previewSurface->repaint();
    timelineCanvas->repaint();
    inspectorPanel->repaint();
}
