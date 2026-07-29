#include "MainComponent.h"

#include "Branding.h"
#include "../Language/AppLanguagePolicy.h"

namespace
{
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

juce::String formatFileSize(int64 bytes)
{
    if (bytes <= 0)
        return "size unknown";

    constexpr double kb = 1024.0;
    constexpr double mb = kb * 1024.0;
    constexpr double gb = mb * 1024.0;

    if (bytes >= static_cast<int64>(gb))
        return juce::String(static_cast<double>(bytes) / gb, 2) + " GB";
    if (bytes >= static_cast<int64>(mb))
        return juce::String(static_cast<double>(bytes) / mb, 1) + " MB";
    if (bytes >= static_cast<int64>(kb))
        return juce::String(static_cast<double>(bytes) / kb, 1) + " KB";

    return juce::String(bytes) + " B";
}

juce::String formatSampleRateLabel(double sampleRate)
{
    if (sampleRate <= 0.0)
        return "unknown rate";

    if (sampleRate >= 1000.0)
        return juce::String(sampleRate / 1000.0, sampleRate < 10000.0 ? 1 : 0) + " kHz";

    return juce::String(static_cast<int>(std::round(sampleRate))) + " Hz";
}

juce::String formatChannelLabel(int channels)
{
    if (channels <= 0)
        return "unknown channels";
    if (channels == 1)
        return "Mono";
    if (channels == 2)
        return "Stereo";

    return juce::String(channels) + " ch";
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

juce::String assetReferenceModeToken(AssetReferenceMode mode)
{
    return creation::assets::toStorageToken(mode);
}

AssetReferenceMode assetReferenceModeFromToken(const juce::String& token)
{
    return creation::assets::assetReferenceModeFromStorageToken(token);
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

juce::String describeMediaSource(const MainComponent::AssetRecord& asset)
{
    if (asset.originalSourcePath.isNotEmpty() && asset.originalSourcePath != asset.sourcePath)
        return "Original: " + asset.originalSourcePath;

    return "Source: " + asset.sourcePath;
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
    enum class DragMode
    {
        none,
        move,
        trimLeft,
        trimRight
    };

    TimelineCanvas(const MainComponent::TransportState& stateToUse,
                   const std::vector<MainComponent::TimelineClip>& clipsToUse,
                   const std::vector<MainComponent::TimelineMarker>& markersToUse,
                   const std::vector<MainComponent::TimelineRegion>& regionsToUse,
                   const int& selectedClipIndexToUse,
                   std::function<void(int)> onClipSelectedToUse,
                   std::function<void(int, double, int)> onClipMovedToUse,
                   std::function<void(int, double, double)> onClipTrimmedToUse,
                   std::function<void(double)> onPlayheadMovedToUse,
                   std::function<void(double)> onTimelineScrolledToUse,
                   std::function<void(double)> onTimelineZoomedToUse)
        : WorkspacePanelBase("Video Tracker"),
          state(stateToUse),
          clips(clipsToUse),
          markers(markersToUse),
          regions(regionsToUse),
          selectedClipIndex(selectedClipIndexToUse),
          onClipSelected(std::move(onClipSelectedToUse)),
          onClipMoved(std::move(onClipMovedToUse)),
          onClipTrimmed(std::move(onClipTrimmedToUse)),
          onPlayheadMoved(std::move(onPlayheadMovedToUse)),
          onTimelineScrolled(std::move(onTimelineScrolledToUse)),
          onTimelineZoomed(std::move(onTimelineZoomedToUse))
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

        if (activeSnapTime >= 0.0)
        {
            const auto snapRatio = (activeSnapTime - state.visibleStartSeconds) / state.visibleLengthSeconds;
            if (snapRatio >= 0.0 && snapRatio <= 1.0)
            {
                const auto snapX = area.getX() + static_cast<int>(snapRatio * area.getWidth());
                g.setColour(juce::Colour(0xffffe066));
                g.drawVerticalLine(snapX, static_cast<float>(ruler.getY()), static_cast<float>(area.getBottom()));
            }
        }

        const auto playheadRatio = (state.playheadSeconds - state.visibleStartSeconds) / state.visibleLengthSeconds;
        const auto playheadX = area.getX() + static_cast<int>(playheadRatio * area.getWidth());
        g.setColour(creation_movie::branding::accentColour());
        g.drawVerticalLine(playheadX, static_cast<float>(ruler.getY()), static_cast<float>(area.getBottom()));
        g.fillEllipse(static_cast<float>(playheadX - 5), static_cast<float>(ruler.getY() + 12), 10.0f, 10.0f);
    }

    void mouseMove(const juce::MouseEvent& event) override
    {
        auto area = getTimelineArea();
        for (size_t i = 0; i < clips.size(); ++i)
        {
            auto clipBounds = getClipBounds(area, clips[i]);
            if (clipBounds.contains(event.getPosition()))
            {
                auto leftHandle = clipBounds.withWidth(12);
                auto rightHandle = clipBounds.withLeft(clipBounds.getRight() - 12);
                if (leftHandle.contains(event.getPosition()) || rightHandle.contains(event.getPosition()))
                    setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
                else
                    setMouseCursor(juce::MouseCursor::NormalCursor);
                return;
            }
        }
        setMouseCursor(juce::MouseCursor::NormalCursor);
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        dragClipIndex = -1;
        dragMode = DragMode::none;
        activeSnapTime = -1.0;
        repaint();
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        auto area = getTimelineArea();
        activeSnapTime = -1.0;

        for (size_t i = 0; i < clips.size(); ++i)
        {
            auto clipBounds = getClipBounds(area, clips[i]);
            if (clipBounds.contains(event.getPosition()))
            {
                if (onClipSelected)
                    onClipSelected(static_cast<int>(i));

                dragClipIndex = static_cast<int>(i);
                dragStartSeconds = clips[i].startSeconds;
                dragDurationSeconds = clips[i].durationSeconds;
                dragFixedEndSeconds = clips[i].startSeconds + clips[i].durationSeconds;

                auto leftHandle = clipBounds.withWidth(12);
                auto rightHandle = clipBounds.withLeft(clipBounds.getRight() - 12);

                if (leftHandle.contains(event.getPosition()))
                {
                    dragMode = DragMode::trimLeft;
                }
                else if (rightHandle.contains(event.getPosition()))
                {
                    dragMode = DragMode::trimRight;
                }
                else
                {
                    dragMode = DragMode::move;
                    dragOffsetSeconds = xToTime(area, event.x) - clips[i].startSeconds;
                }
                return;
            }
        }

        if (area.contains(event.getPosition()) && onPlayheadMoved)
            onPlayheadMoved(xToTime(area, event.x));
    }

    void mouseDrag(const juce::MouseEvent& event) override
    {
        if (dragClipIndex < 0 || dragClipIndex >= static_cast<int>(clips.size()))
            return;

        auto area = getTimelineArea();
        const auto minDuration = 1.0 / juce::jmax(1.0, state.framesPerSecond);
        const auto snapThresholdPixels = 8.0;
        const auto snapThresholdSeconds = state.snapEnabled
                                               ? (snapThresholdPixels / static_cast<double>(juce::jmax(1, area.getWidth()))) * state.visibleLengthSeconds
                                               : 0.0;

        if (dragMode == DragMode::move && onClipMoved)
        {
            const auto laneHeight = 68;
            auto newLane = juce::jlimit(0, 4, (event.y - area.getY()) / laneHeight);
            auto rawStart = xToTime(area, event.x) - dragOffsetSeconds;
            rawStart = juce::jmax(0.0, rawStart);

            double snappedStart = findSnapTarget(rawStart, snapThresholdSeconds);
            if (snappedStart >= 0.0)
                activeSnapTime = snappedStart;
            else
                activeSnapTime = -1.0;

            const auto finalStart = snappedStart >= 0.0 ? snappedStart : rawStart;
            onClipMoved(dragClipIndex, finalStart, newLane);
        }
        else if (dragMode == DragMode::trimLeft && onClipTrimmed)
        {
            auto rawMouseTime = xToTime(area, event.x);
            double snappedTime = findSnapTarget(rawMouseTime, snapThresholdSeconds);
            if (snappedTime >= 0.0)
                activeSnapTime = snappedTime;
            else
                activeSnapTime = -1.0;

            const auto targetStart = snappedTime >= 0.0 ? snappedTime : rawMouseTime;
            const auto maxStart = dragFixedEndSeconds - minDuration;
            const auto newStart = juce::jlimit(0.0, maxStart, targetStart);
            const auto newDuration = dragFixedEndSeconds - newStart;
            onClipTrimmed(dragClipIndex, newStart, newDuration);
        }
        else if (dragMode == DragMode::trimRight && onClipTrimmed)
        {
            auto rawMouseTime = xToTime(area, event.x);
            double snappedTime = findSnapTarget(rawMouseTime, snapThresholdSeconds);
            if (snappedTime >= 0.0)
                activeSnapTime = snappedTime;
            else
                activeSnapTime = -1.0;

            const auto targetEnd = snappedTime >= 0.0 ? snappedTime : rawMouseTime;
            const auto clipStart = clips[static_cast<size_t>(dragClipIndex)].startSeconds;
            const auto newDuration = juce::jmax(minDuration, targetEnd - clipStart);
            onClipTrimmed(dragClipIndex, clipStart, newDuration);
        }

        repaint();
    }

    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override
    {
        if (std::abs(wheel.deltaY) > std::abs(wheel.deltaX) && onTimelineZoomed)
        {
            const auto factor = wheel.deltaY > 0.0f ? 0.85 : 1.18;
            onTimelineZoomed(factor);
            return;
        }

        if (onTimelineScrolled)
        {
            const auto primaryDelta = std::abs(wheel.deltaX) > 0.0f ? wheel.deltaX : wheel.deltaY;
            const auto seconds = -primaryDelta * static_cast<float>(state.visibleLengthSeconds) * 0.32f;
            onTimelineScrolled(seconds);
        }
    }

private:
    double findSnapTarget(double targetTime, double threshold) const
    {
        if (! state.snapEnabled || threshold <= 0.0)
            return -1.0;

        double bestDist = threshold;
        double bestSnap = -1.0;

        auto checkCandidate = [&](double candidate)
        {
            const auto dist = std::abs(targetTime - candidate);
            if (dist < bestDist)
            {
                bestDist = dist;
                bestSnap = candidate;
            }
        };

        checkCandidate(0.0);
        checkCandidate(state.playheadSeconds);

        for (const auto& marker : markers)
            checkCandidate(marker.timeSeconds);

        for (const auto& region : regions)
        {
            checkCandidate(region.startSeconds);
            checkCandidate(region.endSeconds);
        }

        for (size_t i = 0; i < clips.size(); ++i)
        {
            if (static_cast<int>(i) == dragClipIndex)
                continue;
            checkCandidate(clips[i].startSeconds);
            checkCandidate(clips[i].startSeconds + clips[i].durationSeconds);
        }

        return bestSnap;
    }

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

        auto leftGrip = clipBounds.withWidth(6).reduced(1, 4);
        auto rightGrip = clipBounds.withLeft(clipBounds.getRight() - 6).reduced(1, 4);
        g.setColour(juce::Colours::white.withAlpha(0.5f));
        g.fillRoundedRectangle(leftGrip.toFloat(), 2.0f);
        g.fillRoundedRectangle(rightGrip.toFloat(), 2.0f);

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
    std::function<void(int, double, double)> onClipTrimmed;
    std::function<void(double)> onPlayheadMoved;
    std::function<void(double)> onTimelineScrolled;
    std::function<void(double)> onTimelineZoomed;

    DragMode dragMode = DragMode::none;
    int dragClipIndex = -1;
    double dragOffsetSeconds = 0.0;
    double dragStartSeconds = 0.0;
    double dragDurationSeconds = 0.0;
    double dragFixedEndSeconds = 0.0;
    double activeSnapTime = -1.0;
};

class AssetLibraryPanel final : public WorkspacePanelBase
{
public:
    AssetLibraryPanel(const std::vector<MainComponent::AssetRecord>& assetsToUse,
                      const int& selectedAssetIndexToUse,
                      std::function<void(int)> onAssetSelectedToUse,
                      std::function<void(int)> onAssetOpenedToUse)
        : WorkspacePanelBase("Media Library"),
          assets(assetsToUse),
          selectedAssetIndex(selectedAssetIndexToUse),
          onAssetSelected(std::move(onAssetSelectedToUse)),
          onAssetOpened(std::move(onAssetOpenedToUse))
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

    void mouseDoubleClick(const juce::MouseEvent& event) override
    {
        auto area = getPanelContentBounds();

        for (size_t i = 0; i < assets.size(); ++i)
        {
            if (getCardBoundsForIndex(area, static_cast<int>(i)).contains(event.getPosition()))
            {
                if (onAssetOpened)
                    onAssetOpened(static_cast<int>(i));
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
    std::function<void(int)> onAssetOpened;
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
                        asset->name + "\n" + asset->metadataLine + "\n" + describeMediaSource(*asset));
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

class SuiteControlPanel final : public juce::Component
{
public:
    SuiteControlPanel()
    {
        titleLabel.setText("Creation Suite Control", juce::dontSendNotification);
        titleLabel.setColour(juce::Label::textColourId, juce::Colours::white);
        titleLabel.setFont(juce::Font(24.0f).boldened());
        addAndMakeVisible(titleLabel);

        summaryLabel.setText("Suite-level paths and shared controls live here. This will keep growing across the apps.",
                             juce::dontSendNotification);
        summaryLabel.setColour(juce::Label::textColourId, juce::Colour(0xffb9c8db));
        addAndMakeVisible(summaryLabel);

        suiteRootLabel.setText("Suite Root", juce::dontSendNotification);
        suiteRootLabel.setColour(juce::Label::textColourId, juce::Colours::white);
        addAndMakeVisible(suiteRootLabel);

        suiteRootValue.setText("D:\\000 Creation Suite", juce::dontSendNotification);
        suiteRootValue.setColour(juce::Label::textColourId, juce::Colour(0xff8ea0b7));
        addAndMakeVisible(suiteRootValue);

        stationRootLabel.setText("Creation Station", juce::dontSendNotification);
        stationRootLabel.setColour(juce::Label::textColourId, juce::Colours::white);
        addAndMakeVisible(stationRootLabel);

        stationRootValue.setText("D:\\000 Creation Station", juce::dontSendNotification);
        stationRootValue.setColour(juce::Label::textColourId, juce::Colour(0xff8ea0b7));
        addAndMakeVisible(stationRootValue);

        movieRootLabel.setText("Creation Movie", juce::dontSendNotification);
        movieRootLabel.setColour(juce::Label::textColourId, juce::Colours::white);
        addAndMakeVisible(movieRootLabel);

        movieRootValue.setText("D:\\000 Creation Movie", juce::dontSendNotification);
        movieRootValue.setColour(juce::Label::textColourId, juce::Colour(0xff8ea0b7));
        addAndMakeVisible(movieRootValue);

        eulaButton.setButtonText("Read EULA");
        eulaButton.onClick = [this]
        {
            if (onReadEulaRequested)
                onReadEulaRequested();
        };
        addAndMakeVisible(eulaButton);
    }

    std::function<void()> onReadEulaRequested;

    void paint(juce::Graphics& g) override
    {
        g.fillAll(creation_movie::branding::backgroundColour());
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(18);
        titleLabel.setBounds(area.removeFromTop(32));
        summaryLabel.setBounds(area.removeFromTop(24));
        area.removeFromTop(12);

        auto placeRow = [&area](juce::Label& left, juce::Label& right)
        {
            auto row = area.removeFromTop(28);
            left.setBounds(row.removeFromLeft(160));
            right.setBounds(row);
            area.removeFromTop(8);
        };

        placeRow(suiteRootLabel, suiteRootValue);
        placeRow(stationRootLabel, stationRootValue);
        placeRow(movieRootLabel, movieRootValue);
        area.removeFromTop(10);
        eulaButton.setBounds(area.removeFromTop(32).removeFromLeft(110));
    }

private:
    juce::Label titleLabel;
    juce::Label summaryLabel;
    juce::Label suiteRootLabel;
    juce::Label suiteRootValue;
    juce::Label stationRootLabel;
    juce::Label stationRootValue;
    juce::Label movieRootLabel;
    juce::Label movieRootValue;
    juce::TextButton eulaButton;
};
}

MainComponent::DomainTabsBar::DomainTabsBar()
{
    titleLabel.setText("Creative Modes", juce::dontSendNotification);
    titleLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    titleLabel.setFont(juce::Font(18.0f).boldened());
    addAndMakeVisible(titleLabel);

    auto setupButton = [this](juce::TextButton& button, WorkspaceMode mode)
    {
        button.setClickingTogglesState(true);
        button.onClick = [this, mode]
        {
            setActiveMode(mode);
            if (onModeSelected)
                onModeSelected(mode);
        };
        addAndMakeVisible(button);
    };

    trackerButton.setButtonText("Editor");
    renderButton.setButtonText("Tools");

    setupButton(trackerButton, WorkspaceMode::tracker);
    setupButton(libraryButton, WorkspaceMode::library);
    setupButton(inspectorButton, WorkspaceMode::inspector);
    setupButton(renderButton, WorkspaceMode::render);
    setupButton(settingsButton, WorkspaceMode::settings);
    setActiveMode(WorkspaceMode::tracker);
}

void MainComponent::DomainTabsBar::setActiveMode(WorkspaceMode mode)
{
    activeMode = mode;
    trackerButton.setToggleState(mode == WorkspaceMode::tracker, juce::dontSendNotification);
    libraryButton.setToggleState(mode == WorkspaceMode::library, juce::dontSendNotification);
    inspectorButton.setToggleState(mode == WorkspaceMode::inspector, juce::dontSendNotification);
    renderButton.setToggleState(mode == WorkspaceMode::render, juce::dontSendNotification);
    settingsButton.setToggleState(mode == WorkspaceMode::settings, juce::dontSendNotification);
    repaint();
}

void MainComponent::DomainTabsBar::paint(juce::Graphics& g)
{
    g.setColour(juce::Colour(0xff8ea0b7));
    g.setFont(juce::Font(13.0f));

    juce::String modeName = "Non-Linear Editor";
    switch (activeMode)
    {
        case WorkspaceMode::tracker: modeName = "Non-Linear Editor"; break;
        case WorkspaceMode::library: modeName = "Library"; break;
        case WorkspaceMode::inspector: modeName = "Inspector"; break;
        case WorkspaceMode::render: modeName = "Movie Tools"; break;
        case WorkspaceMode::settings: modeName = "Settings"; break;
    }

    g.drawText(modeName + " active", getLocalBounds().reduced(12, 0), juce::Justification::centredRight, true);
}

void MainComponent::DomainTabsBar::resized()
{
    auto area = getLocalBounds().reduced(10, 6);
    titleLabel.setBounds(area.removeFromLeft(142));
    area.removeFromLeft(8);

    auto placeButton = [&area](juce::TextButton& button, int width)
    {
        button.setBounds(area.removeFromLeft(width));
        area.removeFromLeft(6);
    };

    placeButton(trackerButton, 84);
    placeButton(libraryButton, 84);
    placeButton(inspectorButton, 92);
    placeButton(renderButton, 78);
    placeButton(settingsButton, 90);
}

MainComponent::MainComponent()
{
    audioFormatManager.registerBasicFormats();

    juce::String suiteSettingsError;
    suiteSettings = suiteSettingsStore.load(suiteSettingsError);

    headerBar.setAppTitle("Creation Movie");
    headerBar.setLogoImage(creation_movie::branding::createAppLogoImage(72));
    headerBar.setProjectLabel("Project: " + projectName);
    headerBar.setMidiStatusText({});
    headerBar.setTransportButtonVisible(CreationSuiteHeaderBar::TransportButtonSlot::loop, false);
    headerBar.setTransportButtonVisible(CreationSuiteHeaderBar::TransportButtonSlot::click, false);
    headerBar.onPlay = [this] { togglePlayback(); };
    headerBar.onPause = [this]
    {
        transportState.isPlaying = false;
        updateStatusText();
    };
    headerBar.onStop = [this] { stopPlayback(); };
    headerBar.onRecord = [this] { toggleRecording(); };
    headerBar.onRewind = [this] { scrollTimeline(-(transportState.visibleLengthSeconds * 0.25)); };
    headerBar.onFastForward = [this] { scrollTimeline(transportState.visibleLengthSeconds * 0.25); };
    headerBar.onProjectMenuRequested = [this] { showProjectMenu(); };
    headerBar.onSuiteRequested = [this] { showSuiteSettingsWindow(); };
    headerBar.onAudioRequested = [this]
    {
        headerBar.setStatusText("Audio routing panel for Movie is next on the suite list.");
    };
    headerBar.onTourRequested = [this]
    {
        headerBar.setStatusText("Movie guidance panel is not wired yet.");
    };
    headerBar.onSignInRequested = [this]
    {
        headerBar.setStatusText("Suite sign-in wiring is not connected in Movie yet.");
    };
    addAndMakeVisible(headerBar);
    setWantsKeyboardFocus(true);
    addKeyListener(this);

    if (suiteSettingsError.isNotEmpty())
        headerBar.setStatusText(suiteSettingsError);

    runtimeLabel.setText(creation_movie::language::getLanguageRuntimeSummary(), juce::dontSendNotification);
    runtimeLabel.setColour(juce::Label::textColourId, creation_movie::branding::accentColour().brighter(0.15f));
    addAndMakeVisible(runtimeLabel);

    domainTabsBar.onModeSelected = [this](WorkspaceMode mode)
    {
        activeMode = mode;
        if (mode == WorkspaceMode::settings)
            showSuiteSettingsWindow();
        updateStatusText();
    };
    addAndMakeVisible(domainTabsBar);

    commandHintLabel.setText("Menus hold the actions. Tabs switch the workspace focus.", juce::dontSendNotification);
    commandHintLabel.setColour(juce::Label::textColourId, juce::Colour(0xffb9c8db));
    addAndMakeVisible(commandHintLabel);

    projectMenuButton.onClick = [this] { showProjectMenu(); };
    insertMenuButton.onClick = [this] { showInsertMenu(); };
    editMenuButton.onClick = [this] { showEditMenu(); };
    viewMenuButton.onClick = [this] { showViewMenu(); };
    windowMenuButton.onClick = [this] { showWindowMenu(); };
    snapButton.onClick = [this] { toggleSnapMode(); };

    addAndMakeVisible(projectMenuButton);
    addAndMakeVisible(insertMenuButton);
    addAndMakeVisible(editMenuButton);
    addAndMakeVisible(viewMenuButton);
    addAndMakeVisible(windowMenuButton);
    addAndMakeVisible(snapButton);

    seedDemoContent();

    previewSurface = std::make_unique<PreviewSurface>(transportState, assets, timelineClips, selectedAssetIndex, selectedClipIndex);
    timelineCanvas = std::make_unique<TimelineCanvas>(transportState,
                                                      timelineClips,
                                                      timelineMarkers,
                                                      timelineRegions,
                                                      selectedClipIndex,
                                                      [this](int clipIndex) { selectClip(clipIndex); },
                                                      [this](int clipIndex, double startSeconds, int laneIndex) { updateClipPlacement(clipIndex, startSeconds, laneIndex); },
                                                      [this](int clipIndex, double startSeconds, double durationSeconds) { updateClipTrim(clipIndex, startSeconds, durationSeconds); },
                                                      [this](double timeSeconds) { movePlayheadTo(timeSeconds); },
                                                      [this](double deltaSeconds) { scrollTimeline(deltaSeconds); },
                                                      [this](double factor) { zoomTimeline(factor); });
    assetLibraryPanel = std::make_unique<AssetLibraryPanel>(assets,
                                                            selectedAssetIndex,
                                                            [this](int assetIndex) { selectAsset(assetIndex); },
                                                            [this](int assetIndex)
                                                            {
                                                                selectAsset(assetIndex);
                                                                placeSelectedAssetOnTimeline();
                                                            });
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

    auto commandStrip = juce::Rectangle<float>(chrome.getX() + 16.0f, chrome.getY() + 102.0f, chrome.getWidth() - 32.0f, 100.0f);
    g.setColour(juce::Colour(0xff111a28));
    g.fillRoundedRectangle(commandStrip, 22.0f);
    g.setColour(creation_movie::branding::accentColour().withAlpha(0.22f));
    g.drawRoundedRectangle(commandStrip, 22.0f, 1.0f);
}

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced(30, 26);
    headerBar.setBounds(area.removeFromTop(78));
    area.removeFromTop(10);

    auto commandArea = area.removeFromTop(98);
    domainTabsBar.setBounds(commandArea.removeFromTop(42));
    commandArea.removeFromTop(6);

    auto commandTop = commandArea.removeFromTop(24);
    runtimeLabel.setBounds(commandTop.removeFromLeft(440));
    commandHintLabel.setBounds(commandTop);
    commandArea.removeFromTop(10);

    auto menuRow = commandArea.removeFromTop(32);
    projectMenuButton.setBounds(menuRow.removeFromLeft(92).reduced(4, 2));
    insertMenuButton.setBounds(menuRow.removeFromLeft(84).reduced(4, 2));
    editMenuButton.setBounds(menuRow.removeFromLeft(72).reduced(4, 2));
    viewMenuButton.setBounds(menuRow.removeFromLeft(72).reduced(4, 2));
    windowMenuButton.setBounds(menuRow.removeFromLeft(92).reduced(4, 2));
    snapButton.setBounds(menuRow.removeFromLeft(96).reduced(4, 2));

    area.removeFromTop(16);

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
    asset.originalSourcePath = asset.sourcePath;
    asset.importedAt = juce::Time::getCurrentTime();
    asset.fileSizeBytes = file.getSize();

    const auto extension = file.getFileExtension().toLowerCase();

    if (isImageExtension(extension))
    {
        asset.kind = AssetKind::image;
        asset.previewImage = juce::ImageFileFormat::loadFrom(file);
        asset.width = asset.previewImage.getWidth();
        asset.height = asset.previewImage.getHeight();
        asset.codecSummary = extension.fromFirstOccurrenceOf(".", false, false).toUpperCase();
        asset.metadataLine = "Still  |  " + juce::String(asset.width) + "x" + juce::String(asset.height)
                             + "  |  " + formatFileSize(file.getSize());
    }
    else if (isVideoExtension(extension))
    {
        asset.kind = AssetKind::video;
        asset.durationSeconds = 6.0;
        asset.frameRate = transportState.framesPerSecond;
        asset.codecSummary = extension.fromFirstOccurrenceOf(".", false, false).toUpperCase();
        asset.metadataLine = "Video  |  " + extension.fromFirstOccurrenceOf(".", false, false).toUpperCase()
                             + "  |  " + formatFileSize(file.getSize())
                             + "  |  proxy metadata pending";
    }
    else if (auto reader = std::unique_ptr<juce::AudioFormatReader>(audioFormatManager.createReaderFor(file)))
    {
        asset.kind = AssetKind::audio;
        asset.channels = static_cast<int>(reader->numChannels);
        asset.sampleRate = reader->sampleRate;
        asset.codecSummary = extension.fromFirstOccurrenceOf(".", false, false).toUpperCase();
        if (reader->sampleRate > 0.0)
            asset.durationSeconds = reader->lengthInSamples / reader->sampleRate;

        asset.metadataLine = "Audio  |  "
                             + formatChannelLabel(asset.channels) + "  |  "
                             + formatSampleRateLabel(asset.sampleRate) + "  |  "
                             + formatShortDuration(asset.durationSeconds);
    }
    else
    {
        return;
    }

    syncAssetReference(asset);
    juce::String assetStorageError;
    ensureAssetManagedInProject(asset, assetStorageError);
    assets.push_back(std::move(asset));
    markProjectDirty();
}

void MainComponent::openProject()
{
    auto startDirectory = currentProjectFile;
    if (startDirectory == juce::File())
        startDirectory = getMovieProjectsDirectory();
    if (startDirectory.isDirectory())
        startDirectory = startDirectory.getChildFile("*.creationmovie");

    openProjectChooser = std::make_unique<juce::FileChooser>("Open Creation Movie project",
                                                             startDirectory,
                                                             "*.creationmovie");
    openProjectChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                    [this](const juce::FileChooser& chooser)
                                    {
                                        const auto file = chooser.getResult();
                                        if (file.existsAsFile())
                                            loadProjectFromFile(file);

                                        openProjectChooser.reset();
                                    });
}

void MainComponent::saveProject()
{
    auto target = currentProjectFile;
    if (target == juce::File())
        target = getDefaultProjectFile();

    saveProjectChooser = std::make_unique<juce::FileChooser>("Save Creation Movie project",
                                                             target,
                                                             "*.creationmovie");
    saveProjectChooser->launchAsync(juce::FileBrowserComponent::saveMode
                                        | juce::FileBrowserComponent::canSelectFiles
                                        | juce::FileBrowserComponent::warnAboutOverwriting,
                                    [this](const juce::FileChooser& chooser)
                                    {
                                        const auto file = chooser.getResult();
                                        if (file != juce::File())
                                            saveProjectToFile(file);

                                        saveProjectChooser.reset();
                                    });
}

void MainComponent::showProjectMenu()
{
    juce::PopupMenu menu;
    menu.addItem(1, "New Movie Project");
    menu.addItem(2, "Open Project...");
    menu.addItem(3, "Save Project...");

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(headerBar.getProjectButtonScreenBounds()),
                       [this](int result)
                       {
                           if (result == 1)
                               resetProject();
                           else if (result == 2)
                               openProject();
                           else if (result == 3)
                               saveProject();
                       });
}

void MainComponent::resetProject()
{
    transportState = {};
    transportState.playheadSeconds = 12.0;
    transportState.visibleStartSeconds = 8.0;
    transportState.visibleLengthSeconds = 18.0;
    transportState.projectDurationSeconds = 154.0;
    transportState.framesPerSecond = 24.0;

    projectName = "Untitled Movie";
    currentProjectFile = {};
    isRecording = false;
    selectedAssetIndex = -1;
    selectedClipIndex = -1;
    assets.clear();
    timelineClips.clear();
    timelineMarkers.clear();
    timelineRegions.clear();
    seedDemoContent();
    markProjectDirty(false);
    updateStatusText();
    previewSurface->repaint();
    timelineCanvas->repaint();
    assetLibraryPanel->repaint();
    inspectorPanel->repaint();
    repaint();
}

void MainComponent::showInsertMenu()
{
    juce::PopupMenu menu;
    menu.addItem(1, "Import Media...");
    menu.addItem(2, "Place Selected Asset");
    menu.addItem(3, "Add Marker");
    menu.addItem(4, "Create Region From Latest Markers");

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&insertMenuButton),
                       [this](int result)
                       {
                           if (result == 1)
                               importMediaFiles();
                           else if (result == 2)
                               placeSelectedAssetOnTimeline();
                           else if (result == 3)
                               addMarkerAtPlayhead();
                           else if (result == 4)
                               createRegionFromMarkers();
                       });
}

void MainComponent::showViewMenu()
{
    juce::PopupMenu menu;
    menu.addItem(1, "Zoom In");
    menu.addItem(2, "Zoom Out");
    menu.addItem(3, "Scroll Left");
    menu.addItem(4, "Scroll Right");
    menu.addItem(5, "Pop Out Preview");

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&viewMenuButton),
                       [this](int result)
                       {
                           if (result == 1)
                               zoomTimeline(0.75);
                           else if (result == 2)
                               zoomTimeline(1.35);
                           else if (result == 3)
                               scrollTimeline(-(transportState.visibleLengthSeconds * 0.35));
                           else if (result == 4)
                               scrollTimeline(transportState.visibleLengthSeconds * 0.35);
                           else if (result == 5)
                               showPreviewWindow();
                       });
}

void MainComponent::showEditMenu()
{
    juce::PopupMenu menu;
    menu.addItem(1, "Split Selected Clip", selectedClipIndex >= 0);
    menu.addItem(2, "Step Back One Frame");
    menu.addItem(3, "Step Forward One Frame");

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&editMenuButton),
                       [this](int result)
                       {
                           if (result == 1)
                               splitSelectedClip();
                           else if (result == 2)
                               stepPlayheadByFrames(-1);
                           else if (result == 3)
                               stepPlayheadByFrames(1);
                       });
}

void MainComponent::showWindowMenu()
{
    juce::PopupMenu menu;
    menu.addItem(1, "Pop Out Preview");
    menu.addItem(2, "EULA");

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&windowMenuButton),
                       [this](int result)
                       {
                           if (result == 1)
                               showPreviewWindow();
                           else if (result == 2)
                               showEulaWindow();
                       });
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
    updateProjectDurationFromContent();
    markProjectDirty();
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
    updateProjectDurationFromContent();
    markProjectDirty();
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
    updateProjectDurationFromContent();
    markProjectDirty();
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
    updateProjectDurationFromContent();
    markProjectDirty();
    timelineCanvas->repaint();
    inspectorPanel->repaint();
    previewSurface->repaint();
    updateStatusText();
}

void MainComponent::toggleSnapMode()
{
    transportState.snapEnabled = !transportState.snapEnabled;
    snapButton.setButtonText(transportState.snapEnabled ? "Snap: ON" : "Snap: OFF");
    timelineCanvas->repaint();
    updateStatusText();
}

void MainComponent::updateClipTrim(int clipIndex, double startSeconds, double durationSeconds)
{
    if (clipIndex < 0 || clipIndex >= static_cast<int>(timelineClips.size()))
        return;

    auto& clip = timelineClips[static_cast<size_t>(clipIndex)];
    const auto minDuration = 1.0 / juce::jmax(1.0, transportState.framesPerSecond);
    clip.startSeconds = juce::jmax(0.0, startSeconds);
    clip.durationSeconds = juce::jmax(minDuration, durationSeconds);
    selectedClipIndex = clipIndex;
    updateProjectDurationFromContent();
    markProjectDirty();
    timelineCanvas->repaint();
    inspectorPanel->repaint();
    previewSurface->repaint();
    updateStatusText();
}

void MainComponent::movePlayheadTo(double timeSeconds)
{
    transportState.playheadSeconds = juce::jlimit(0.0, transportState.projectDurationSeconds, timeSeconds);

    if (transportState.playheadSeconds < transportState.visibleStartSeconds
        || transportState.playheadSeconds > transportState.visibleStartSeconds + transportState.visibleLengthSeconds)
    {
        transportState.visibleStartSeconds = juce::jlimit(0.0,
                                                          juce::jmax(0.0, transportState.projectDurationSeconds - transportState.visibleLengthSeconds),
                                                          transportState.playheadSeconds - (transportState.visibleLengthSeconds * 0.5));
    }

    previewSurface->repaint();
    timelineCanvas->repaint();
    inspectorPanel->repaint();
    updateStatusText();
}

void MainComponent::loadProjectFromFile(const juce::File& file)
{
    const auto jsonText = file.loadFileAsString();
    if (jsonText.isEmpty())
        return;

    const auto parsed = juce::JSON::parse(jsonText);
    if (! parsed.isObject())
        return;

    const auto* root = parsed.getDynamicObject();
    if (root == nullptr)
        return;

    projectName = root->getProperty("projectName").toString().isNotEmpty()
                      ? root->getProperty("projectName").toString()
                      : file.getFileNameWithoutExtension();

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
                asset.codecSummary = object->getProperty("codecSummary").toString();
                asset.sourceTool = object->getProperty("sourceTool").toString();
                asset.sourceApp = object->getProperty("sourceApp").toString();
                asset.sourceVersion = object->getProperty("sourceVersion").toString();
                asset.durationSeconds = static_cast<double>(object->getProperty("durationSeconds"));
                asset.frameRate = static_cast<double>(object->getProperty("frameRate"));
                asset.sampleRate = static_cast<double>(object->getProperty("sampleRate"));
                asset.width = static_cast<int>(object->getProperty("width"));
                asset.height = static_cast<int>(object->getProperty("height"));
                asset.channels = static_cast<int>(object->getProperty("channels"));
                asset.fileSizeBytes = static_cast<int64>(object->getProperty("fileSizeBytes"));
                asset.originalSourcePath = object->getProperty("originalSourcePath").toString();
                asset.logicalPath = object->getProperty("logicalPath").toString();
                asset.assetId = object->getProperty("assetId").toString();
                asset.versionId = object->getProperty("versionId").toString();
                asset.importedAt = juce::Time(static_cast<int64>(object->getProperty("importedAtMs")));
                asset.ref.id = asset.assetId;
                asset.ref.versionId = asset.versionId;
                const auto assetReferenceModeVar = object->hasProperty("assetReferenceMode")
                                                     ? object->getProperty("assetReferenceMode")
                                                     : juce::var("exact");
                asset.ref.mode = assetReferenceModeFromToken(assetReferenceModeVar.toString());
                asset.ref.logicalPath = asset.logicalPath;
                asset.ref.displayName = asset.name;
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
                clip.assetVersionId = object->getProperty("assetVersionId").toString();
                const auto clipReferenceModeVar = object->hasProperty("assetReferenceMode")
                                                    ? object->getProperty("assetReferenceMode")
                                                    : juce::var("exact");
                clip.assetReferenceMode = assetReferenceModeFromToken(clipReferenceModeVar.toString());
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
    currentProjectFile = file;
    markProjectDirty(false);

    assetLibraryPanel->repaint();
    timelineCanvas->repaint();
    inspectorPanel->repaint();
    previewSurface->repaint();
    repaint();
}

void MainComponent::saveProjectToFile(const juce::File& file)
{
    if (currentProjectFile == juce::File())
        currentProjectFile = file;

    ensureAllAssetsManagedInProject();

    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    root->setProperty("projectName", projectName);
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
        object->setProperty("assetId", asset.assetId);
        object->setProperty("versionId", asset.versionId);
        object->setProperty("name", asset.name);
        object->setProperty("sourcePath", asset.sourcePath);
        object->setProperty("originalSourcePath", asset.originalSourcePath);
        object->setProperty("kind", assetKindToken(asset.kind));
        object->setProperty("metadataLine", asset.metadataLine);
        object->setProperty("codecSummary", asset.codecSummary);
        object->setProperty("sourceTool", asset.sourceTool);
        object->setProperty("sourceApp", asset.sourceApp);
        object->setProperty("sourceVersion", asset.sourceVersion);
        object->setProperty("durationSeconds", asset.durationSeconds);
        object->setProperty("frameRate", asset.frameRate);
        object->setProperty("sampleRate", asset.sampleRate);
        object->setProperty("width", asset.width);
        object->setProperty("height", asset.height);
        object->setProperty("channels", asset.channels);
        object->setProperty("fileSizeBytes", asset.fileSizeBytes);
        object->setProperty("logicalPath", asset.logicalPath);
        object->setProperty("assetReferenceMode", assetReferenceModeToken(asset.ref.mode));
        object->setProperty("importedAtMs", static_cast<int64>(asset.importedAt.toMilliseconds()));
        assetArray.add(juce::var(object.get()));
    }
    root->setProperty("assets", juce::var(assetArray));

    juce::Array<juce::var> clipArray;
    for (const auto& clip : timelineClips)
    {
        juce::DynamicObject::Ptr object = new juce::DynamicObject();
        object->setProperty("assetId", clip.assetId);
        object->setProperty("assetVersionId", clip.assetVersionId);
        object->setProperty("assetReferenceMode", assetReferenceModeToken(clip.assetReferenceMode));
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

    file.replaceWithText(juce::JSON::toString(juce::var(root.get()), true));
    currentProjectFile = file;
    projectName = file.getFileNameWithoutExtension();
    markProjectDirty(false);
}

void MainComponent::placeSelectedAssetOnTimeline()
{
    if (selectedAssetIndex < 0 || selectedAssetIndex >= static_cast<int>(assets.size()))
        return;

    const auto& asset = assets[static_cast<size_t>(selectedAssetIndex)];

    TimelineClip clip;
    clip.assetId = asset.assetId;
    clip.assetVersionId = asset.versionId;
    clip.assetReferenceMode = AssetReferenceMode::exact;
    clip.title = juce::File(asset.name).getFileNameWithoutExtension();
    clip.laneIndex = laneForAssetKind(asset.kind);
    clip.startSeconds = transportState.playheadSeconds;
    clip.durationSeconds = defaultClipDurationForAsset(asset);
    clip.colour = colourForAssetKind(asset.kind);

    if (asset.kind == AssetKind::audio && timelineClips.size() % 2 == 1)
        clip.laneIndex = 3;

    timelineClips.push_back(std::move(clip));
    updateProjectDurationFromContent();
    markProjectDirty();
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
    currentProjectFile = {};

    AssetRecord video;
    video.id = juce::Uuid().toString();
    video.name = "Interview_CamA_4k.mov";
    video.sourcePath = "Demo source";
    video.kind = AssetKind::video;
    video.durationSeconds = 6.4;
    video.frameRate = 24.0;
    video.metadataLine = "Video  |  3840x2160  |  24 fps  |  03:42";
    syncAssetReference(video);
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
    syncAssetReference(audio);
    assets.push_back(std::move(audio));

    AssetRecord image;
    image.id = juce::Uuid().toString();
    image.name = "City_Skyline_Still.png";
    image.sourcePath = "Demo source";
    image.kind = AssetKind::image;
    image.width = 4096;
    image.height = 2160;
    image.metadataLine = "Still  |  4096x2160  |  imported graphic";
    syncAssetReference(image);
    assets.push_back(std::move(image));

    AssetRecord title;
    title.id = juce::Uuid().toString();
    title.name = "LowerThird_01";
    title.sourcePath = "Demo source";
    title.kind = AssetKind::image;
    title.metadataLine = "Still  |  title template  |  suite asset";
    syncAssetReference(title);
    assets.push_back(std::move(title));

    TimelineClip sceneA;
    sceneA.assetId = assets[0].assetId;
    sceneA.assetVersionId = assets[0].versionId;
    sceneA.assetReferenceMode = AssetReferenceMode::exact;
    sceneA.title = "Scene A";
    sceneA.laneIndex = 0;
    sceneA.startSeconds = 9.2;
    sceneA.durationSeconds = 6.4;
    sceneA.colour = colourForAssetKind(AssetKind::video);
    timelineClips.push_back(std::move(sceneA));

    TimelineClip lowerThird;
    lowerThird.assetId = assets[3].assetId;
    lowerThird.assetVersionId = assets[3].versionId;
    lowerThird.assetReferenceMode = AssetReferenceMode::exact;
    lowerThird.title = "Lower Third";
    lowerThird.laneIndex = 1;
    lowerThird.startSeconds = 13.8;
    lowerThird.durationSeconds = 3.8;
    lowerThird.colour = colourForAssetKind(AssetKind::image);
    timelineClips.push_back(std::move(lowerThird));

    TimelineClip dialogue;
    dialogue.assetId = assets[1].assetId;
    dialogue.assetVersionId = assets[1].versionId;
    dialogue.assetReferenceMode = AssetReferenceMode::exact;
    dialogue.title = "Dialogue";
    dialogue.laneIndex = 2;
    dialogue.startSeconds = 9.2;
    dialogue.durationSeconds = 6.4;
    dialogue.colour = colourForAssetKind(AssetKind::audio);
    timelineClips.push_back(std::move(dialogue));

    TimelineClip themeBed;
    themeBed.assetId = assets[1].assetId;
    themeBed.assetVersionId = assets[1].versionId;
    themeBed.assetReferenceMode = AssetReferenceMode::exact;
    themeBed.title = "Theme Bed";
    themeBed.laneIndex = 3;
    themeBed.startSeconds = 8.6;
    themeBed.durationSeconds = 11.1;
    themeBed.colour = juce::Colour(0xff2f9f7f);
    timelineClips.push_back(std::move(themeBed));
    timelineMarkers.push_back({ "Intro", 9.0 });
    timelineMarkers.push_back({ "Verse", 17.0 });
    timelineRegions.push_back({ "Act 1", 8.8, 18.0, juce::Colour(0xff6a9bd8) });

    selectedClipIndex = 0;
    selectedAssetIndex = -1;
    updateProjectDurationFromContent();
    refreshProjectHeader();
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
    headerBar.setPlaybackVisualState(transportState.isPlaying, isRecording);
    headerBar.setScrubModeEnabled(transportState.scrubMode);
    insertMenuButton.setEnabled(true);
    editMenuButton.setEnabled(true);
    viewMenuButton.setEnabled(true);
    windowMenuButton.setEnabled(true);
}

void MainComponent::updateStatusText()
{
    juce::String modeName = "Non-Linear Editor";
    switch (activeMode)
    {
        case WorkspaceMode::tracker: modeName = "Non-Linear Editor"; break;
        case WorkspaceMode::library: modeName = "Library"; break;
        case WorkspaceMode::inspector: modeName = "Inspector"; break;
        case WorkspaceMode::render: modeName = "Movie Tools"; break;
        case WorkspaceMode::settings: modeName = "Settings"; break;
    }

    const auto status = transportState.scrubMode ? "Scrub ready" : (transportState.isPlaying ? "Rolling timeline" : "Idle");
    headerBar.setStatusText(modeName + "  |  " + juce::String(status) + "  |  "
                            + formatTimecode(transportState.playheadSeconds, transportState.framesPerSecond)
                            + "  |  assets: " + juce::String(static_cast<int>(assets.size()))
                            + "  |  clips: " + juce::String(static_cast<int>(timelineClips.size())));
    refreshProjectHeader();
    domainTabsBar.setActiveMode(activeMode);
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

void MainComponent::showSuiteSettingsWindow()
{
    if (suiteSettingsWindow != nullptr)
    {
        suiteSettingsWindow->toFront(true);
        return;
    }

    auto panel = std::make_unique<SuiteSettingsPanel>();
    panel->setSettings(suiteSettings);
    panel->onBrowseRequested = [this](const juce::String& fieldId)
    {
        chooseSuiteDirectory(fieldId);
    };
    panel->onApplyRequested = [this](const SuiteSettings& settings)
    {
        applySuiteSettings(settings);
    };
    panel->onReadEulaRequested = [this]
    {
        showEulaWindow();
    };

    auto* panelRaw = panel.get();

    auto window = std::make_unique<ManagedDocumentWindow>("Creation Suite Control",
                                                          creation_movie::branding::backgroundColour(),
                                                          juce::DocumentWindow::allButtons,
                                                          [this] { closeSuiteSettingsWindow(); });
    window->setUsingNativeTitleBar(true);
    window->setResizable(true, true);
    window->setContentOwned(panel.release(), true);
    window->centreWithSize(940, 560);
    window->setVisible(true);
    suiteSettingsPanel = panelRaw;
    suiteSettingsWindow = std::move(window);
}

void MainComponent::closeSuiteSettingsWindow()
{
    suiteSettingsPanel = nullptr;
    suiteSettingsWindow.reset();
}

void MainComponent::chooseSuiteDirectory(const juce::String& fieldId)
{
    juce::String currentPath = suiteSettings.suiteVfsRoot;
    if (fieldId == "shared_resources_root")
        currentPath = suiteSettings.sharedResourcesRoot;
    else if (fieldId == "creation_station_projects_root")
        currentPath = suiteSettings.creationStationProjectsRoot;
    else if (fieldId == "creation_engine_projects_root")
        currentPath = suiteSettings.creationEngineProjectsRoot;
    else if (fieldId == "creation_movie_projects_root")
        currentPath = suiteSettings.creationMovieProjectsRoot;
    else if (fieldId == "creation_live_projects_root")
        currentPath = suiteSettings.creationLiveProjectsRoot;

    suiteDirectoryChooser = std::make_unique<juce::FileChooser>("Choose a folder for the Creation Suite",
                                                                currentPath.isNotEmpty()
                                                                    ? juce::File(currentPath)
                                                                    : juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
                                                                "*",
                                                                true);
    auto chooser = suiteDirectoryChooser.get();
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                         [this, chooser, fieldId](const juce::FileChooser& result)
                         {
                             auto selected = result.getResult();
                             if (chooser == suiteDirectoryChooser.get())
                                 suiteDirectoryChooser.reset();

                             if (selected == juce::File())
                                 return;

                             auto selectedPath = selected.getFullPathName();
                             if (fieldId == "suite_vfs_root")
                                 suiteSettings.suiteVfsRoot = selectedPath;
                             else if (fieldId == "shared_resources_root")
                                 suiteSettings.sharedResourcesRoot = selectedPath;
                             else if (fieldId == "creation_station_projects_root")
                                 suiteSettings.creationStationProjectsRoot = selectedPath;
                             else if (fieldId == "creation_engine_projects_root")
                                 suiteSettings.creationEngineProjectsRoot = selectedPath;
                             else if (fieldId == "creation_movie_projects_root")
                                 suiteSettings.creationMovieProjectsRoot = selectedPath;
                             else if (fieldId == "creation_live_projects_root")
                                 suiteSettings.creationLiveProjectsRoot = selectedPath;

                             if (suiteSettingsPanel != nullptr)
                                 suiteSettingsPanel->setSettings(suiteSettings);
                         });
}

void MainComponent::applySuiteSettings(const SuiteSettings& settings)
{
    juce::String errorMessage;
    if (! suiteSettingsStore.save(settings, errorMessage))
    {
        headerBar.setStatusText(errorMessage);
        if (suiteSettingsPanel != nullptr)
            suiteSettingsPanel->setStatusText(errorMessage);
        return;
    }

    suiteSettings = settings;
    headerBar.setStatusText("Saved Creation Suite settings.");
    if (suiteSettingsPanel != nullptr)
        suiteSettingsPanel->setStatusText("Saved suite-wide settings for all Creation apps.");
}

void MainComponent::markProjectDirty(bool dirty)
{
    projectDirty = dirty;
    refreshProjectHeader();
}

void MainComponent::refreshProjectHeader()
{
    headerBar.setProjectLabel("Project: " + projectName + (projectDirty ? " *" : ""));
}

void MainComponent::updateProjectDurationFromContent()
{
    double maxEnd = 30.0;

    for (const auto& clip : timelineClips)
        maxEnd = juce::jmax(maxEnd, clip.startSeconds + clip.durationSeconds + 2.0);

    for (const auto& marker : timelineMarkers)
        maxEnd = juce::jmax(maxEnd, marker.timeSeconds + 2.0);

    for (const auto& region : timelineRegions)
        maxEnd = juce::jmax(maxEnd, region.endSeconds + 2.0);

    transportState.projectDurationSeconds = juce::jmax(maxEnd, transportState.visibleLengthSeconds + 4.0);
    transportState.visibleStartSeconds = juce::jlimit(0.0,
                                                      juce::jmax(0.0, transportState.projectDurationSeconds - transportState.visibleLengthSeconds),
                                                      transportState.visibleStartSeconds);
    transportState.playheadSeconds = juce::jlimit(0.0, transportState.projectDurationSeconds, transportState.playheadSeconds);
}

juce::File MainComponent::getMovieProjectsDirectory() const
{
    auto configured = suiteSettings.creationMovieProjectsRoot.trim();
    if (configured.isNotEmpty())
        return juce::File(configured);

    auto suiteVfsRoot = suiteSettings.suiteVfsRoot.trim();
    if (suiteVfsRoot.isNotEmpty())
        return juce::File(suiteVfsRoot).getChildFile("CreationMovieProjects");

    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("CreationMovieProjects");
}

juce::File MainComponent::getDefaultProjectFile() const
{
    auto projectDirectory = getMovieProjectsDirectory();
    if (! projectDirectory.exists())
        projectDirectory.createDirectory();

    auto safeName = projectName.trim();
    if (safeName.isEmpty())
        safeName = "Untitled Movie";

    return projectDirectory.getChildFile(safeName + ".creationmovie");
}

juce::File MainComponent::getProjectAssetDirectory() const
{
    auto baseProjectFile = currentProjectFile != juce::File() ? currentProjectFile : getDefaultProjectFile();
    auto projectFolderName = baseProjectFile.getFileNameWithoutExtension() + "_Assets";
    return baseProjectFile.getSiblingFile(projectFolderName);
}

juce::String MainComponent::makeAssetSlug(const juce::String& name) const
{
    auto slug = name.trim().toLowerCase();
    slug = slug.replaceCharacter('\\', '-').replaceCharacter('/', '-').replaceCharacter(' ', '-');
    slug = slug.retainCharacters("abcdefghijklmnopqrstuvwxyz0123456789-_.");
    while (slug.contains("--"))
        slug = slug.replace("--", "-");
    slug = slug.trimCharactersAtStart("-").trimCharactersAtEnd("-");
    return slug.isNotEmpty() ? slug : "asset";
}

juce::String MainComponent::makeAssetVersionSuffix(const juce::File& file) const
{
    return juce::String::toHexString(static_cast<int64>(file.getSize()))
           + "-"
           + juce::String(file.getLastModificationTime().toMilliseconds());
}

void MainComponent::syncAssetReference(AssetRecord& asset)
{
    if (asset.assetId.isEmpty())
        asset.assetId = "movie:" + makeAssetSlug(asset.name) + ":" + asset.id.substring(0, 8);

    if (asset.versionId.isEmpty())
        asset.versionId = asset.assetId + "@1";

    asset.ref.id = asset.assetId;
    asset.ref.versionId = asset.versionId;
    asset.ref.mode = AssetReferenceMode::exact;
    asset.ref.logicalPath = asset.logicalPath;
    asset.ref.displayName = asset.name;
}

bool MainComponent::ensureAssetManagedInProject(AssetRecord& asset, juce::String& errorMessage)
{
    auto sourceFile = juce::File(asset.sourcePath);
    if (! sourceFile.existsAsFile())
        return false;

    auto assetDirectory = getProjectAssetDirectory();
    if (! assetDirectory.exists() && ! assetDirectory.createDirectory())
    {
        errorMessage = "Could not create the Movie asset folder.";
        return false;
    }

    auto extension = sourceFile.getFileExtension();
    auto targetName = makeAssetSlug(sourceFile.getFileNameWithoutExtension()) + extension;
    auto destination = assetDirectory.getChildFile(targetName);

    int suffix = 2;
    while (destination.existsAsFile() && destination.getFullPathName() != sourceFile.getFullPathName())
    {
        destination = assetDirectory.getChildFile(makeAssetSlug(sourceFile.getFileNameWithoutExtension())
                                                  + "-" + juce::String(suffix) + extension);
        ++suffix;
    }

    if (destination.getFullPathName() != sourceFile.getFullPathName())
    {
        if (! destination.existsAsFile() && ! sourceFile.copyFileTo(destination))
        {
            errorMessage = "Could not copy imported media into the Movie asset folder.";
            return false;
        }
    }

    asset.sourcePath = destination.getFullPathName();
    asset.logicalPath = destination.getFileName();
    asset.fileSizeBytes = destination.getSize();
    asset.versionId = asset.assetId + "@" + makeAssetVersionSuffix(destination);
    syncAssetReference(asset);
    return true;
}

void MainComponent::ensureAllAssetsManagedInProject()
{
    juce::String ignoredError;
    for (auto& asset : assets)
        ensureAssetManagedInProject(asset, ignoredError);
}

void MainComponent::togglePlayback()
{
    transportState.isPlaying = ! transportState.isPlaying;
    updateTransportButtons();
    updateStatusText();
}

bool MainComponent::keyPressed(const juce::KeyPress& key, juce::Component*)
{
    if (key == juce::KeyPress::spaceKey)
    {
        togglePlayback();
        return true;
    }

    if (key == juce::KeyPress::escapeKey)
    {
        stopPlayback();
        return true;
    }

    if (key == juce::KeyPress::leftKey)
    {
        stepPlayheadByFrames(-1);
        return true;
    }

    if (key == juce::KeyPress::rightKey)
    {
        stepPlayheadByFrames(1);
        return true;
    }

    if (key.getTextCharacter() == 'm' || key.getTextCharacter() == 'M')
    {
        addMarkerAtPlayhead();
        return true;
    }

    return false;
}

void MainComponent::stopPlayback()
{
    transportState.isPlaying = false;
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
