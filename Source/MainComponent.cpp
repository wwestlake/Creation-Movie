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
                   const int& selectedClipIndexToUse,
                   std::function<void(int)> onClipSelectedToUse)
        : WorkspacePanelBase("Video Tracker"),
          state(stateToUse),
          clips(clipsToUse),
          selectedClipIndex(selectedClipIndexToUse),
          onClipSelected(std::move(onClipSelectedToUse))
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

        const auto playheadRatio = (state.playheadSeconds - state.visibleStartSeconds) / state.visibleLengthSeconds;
        const auto playheadX = area.getX() + static_cast<int>(playheadRatio * area.getWidth());
        g.setColour(creation_movie::branding::accentColour());
        g.drawVerticalLine(playheadX, static_cast<float>(ruler.getY()), static_cast<float>(area.getBottom()));
        g.fillEllipse(static_cast<float>(playheadX - 5), static_cast<float>(ruler.getY() + 12), 10.0f, 10.0f);
    }

    void mouseUp(const juce::MouseEvent& event) override
    {
        auto area = getPanelContentBounds();
        area.removeFromTop(42);
        area.removeFromLeft(148);

        for (size_t i = 0; i < clips.size(); ++i)
        {
            if (getClipBounds(area, clips[i]).contains(event.getPosition()))
            {
                if (onClipSelected)
                    onClipSelected(static_cast<int>(i));
                return;
            }
        }
    }

private:
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
    const int& selectedClipIndex;
    std::function<void(int)> onClipSelected;
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

    openButton.onClick = [this] { openProject(); };
    saveButton.onClick = [this] { saveProject(); };
    importButton.onClick = [this] { importMediaFiles(); };
    placeButton.onClick = [this] { placeSelectedAssetOnTimeline(); };
    stepBackButton.onClick = [this] { stepPlayheadByFrames(-1); };
    stepForwardButton.onClick = [this] { stepPlayheadByFrames(1); };
    playButton.onClick = [this] { togglePlayback(); };
    stopButton.onClick = [this] { stopPlayback(); };
    scrubButton.onClick = [this] { toggleScrubMode(); };
    previewButton.onClick = [this] { showPreviewWindow(); };
    eulaButton.onClick = [this] { showEulaWindow(); };

    addAndMakeVisible(openButton);
    addAndMakeVisible(saveButton);
    addAndMakeVisible(importButton);
    addAndMakeVisible(placeButton);
    addAndMakeVisible(stepBackButton);
    addAndMakeVisible(stepForwardButton);
    addAndMakeVisible(playButton);
    addAndMakeVisible(stopButton);
    addAndMakeVisible(scrubButton);
    addAndMakeVisible(previewButton);
    addAndMakeVisible(eulaButton);

    seedDemoContent();

    previewSurface = std::make_unique<PreviewSurface>(transportState, assets, timelineClips, selectedAssetIndex, selectedClipIndex);
    timelineCanvas = std::make_unique<TimelineCanvas>(transportState, timelineClips, selectedClipIndex, [this](int clipIndex) { selectClip(clipIndex); });
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

    auto centerHeader = header.removeFromLeft(280);
    projectChipLabel.setBounds(centerHeader.removeFromTop(42).reduced(10, 4));
    statusLabel.setBounds(centerHeader.removeFromTop(26).reduced(10, 0));

    auto buttonStrip = header;
    auto topButtons = buttonStrip.removeFromTop(40);
    openButton.setBounds(topButtons.removeFromLeft(118).reduced(4, 2));
    saveButton.setBounds(topButtons.removeFromLeft(118).reduced(4, 2));
    importButton.setBounds(topButtons.removeFromLeft(118).reduced(4, 2));
    placeButton.setBounds(topButtons.removeFromLeft(138).reduced(4, 2));
    stepBackButton.setBounds(topButtons.removeFromLeft(92).reduced(4, 2));
    stepForwardButton.setBounds(topButtons.removeFromLeft(92).reduced(4, 2));
    playButton.setBounds(topButtons.removeFromLeft(78).reduced(4, 2));
    stopButton.setBounds(topButtons.removeFromLeft(78).reduced(4, 2));
    scrubButton.setBounds(topButtons.removeFromLeft(88).reduced(4, 2));
    previewButton.setBounds(topButtons.removeFromLeft(156).reduced(4, 2));
    eulaButton.setBounds(topButtons.removeFromLeft(68).reduced(4, 2));

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
    openProjectChooser = std::make_unique<juce::FileChooser>("Open Creation Movie project",
                                                             currentProjectFile,
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
        target = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile(projectName + ".creationmovie");

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

    selectedAssetIndex = -1;
    selectedClipIndex = timelineClips.empty() ? -1 : 0;
    currentProjectFile = file;
    projectChipLabel.setText("Project: " + projectName, juce::dontSendNotification);

    assetLibraryPanel->repaint();
    timelineCanvas->repaint();
    inspectorPanel->repaint();
    previewSurface->repaint();
    repaint();
}

void MainComponent::saveProjectToFile(const juce::File& file)
{
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

    file.replaceWithText(juce::JSON::toString(juce::var(root.get()), true));
    currentProjectFile = file;
    projectName = file.getFileNameWithoutExtension();
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
    playButton.setButtonText(transportState.isPlaying ? "Pause" : "Play");
    scrubButton.setToggleState(transportState.scrubMode, juce::dontSendNotification);
    scrubButton.setColour(juce::TextButton::buttonOnColourId, creation_movie::branding::accentColour());
    placeButton.setEnabled(selectedAssetIndex >= 0);
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
