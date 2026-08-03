#pragma once

#include <JuceHeader.h>
#include "Legal/EulaText.h"

#include <creation/assets/ProjectContainerService.h>
#include <creation/assets/ProjectSession.h>
#include <creation/assets/ProjectWorkspaceService.h>
#include <creation/suite/SuiteSettings.h>
#include <creation/suite/SuiteStoragePaths.h>

class MainComponent final : public juce::Component,
                            private juce::Timer
{
public:
    enum class AssetKind
    {
        video,
        audio,
        image
    };

    struct AssetRecord
    {
        juce::String id;
        juce::String name;
        juce::String sourcePath;
        AssetKind kind = AssetKind::video;
        juce::String metadataLine;
        double durationSeconds = 0.0;
        double frameRate = 0.0;
        double sampleRate = 0.0;
        int width = 0;
        int height = 0;
        int channels = 0;
        juce::Image previewImage;
    };

    struct TimelineClip
    {
        juce::String assetId;
        juce::String title;
        int laneIndex = 0;
        double startSeconds = 0.0;
        double durationSeconds = 0.0;
        juce::Colour colour;
    };

    struct TimelineMarker
    {
        juce::String title;
        double timeSeconds = 0.0;
    };

    struct TimelineRegion
    {
        juce::String title;
        double startSeconds = 0.0;
        double endSeconds = 0.0;
        juce::Colour colour;
    };

    struct TransportState
    {
        bool isPlaying = false;
        bool scrubMode = false;
        double playheadSeconds = 12.0;
        double visibleStartSeconds = 8.0;
        double visibleLengthSeconds = 18.0;
        double projectDurationSeconds = 154.0;
        double framesPerSecond = 24.0;
    };

    MainComponent();
    ~MainComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void importMediaFiles();
    void ingestMediaFile(const juce::File& file);
    void openProject();
    void saveProject();
    void createNewProject(const juce::String& name);
    void loadProjectFromSession();
    void saveProjectToSession();
    void zoomTimeline(double factor);
    void scrollTimeline(double deltaSeconds);
    void addMarkerAtPlayhead();
    void createRegionFromMarkers();
    void splitSelectedClip();
    void updateClipPlacement(int clipIndex, double startSeconds, int laneIndex);
    void movePlayheadTo(double timeSeconds);
    void placeSelectedAssetOnTimeline();
    void selectAsset(int assetIndex);
    void selectClip(int clipIndex);
    void seedDemoContent();

    void timerCallback() override;

    void updateTransportButtons();
    void updateStatusText();
    void showEulaWindow();
    void closeEulaWindow();
    void showPreviewWindow();
    void closePreviewWindow();
    void togglePlayback();
    void stopPlayback();
    void toggleRecording();
    void toggleScrubMode();
    void stepPlayheadByFrames(int frameDelta);

    TransportState transportState;
    juce::String projectName { "Untitled Movie" };
    bool isRecording = false;

    juce::Label titleLabel;
    juce::Label subtitleLabel;
    juce::Label projectChipLabel;
    juce::Label runtimeLabel;
    juce::Label statusLabel;
    juce::TextButton rewindButton { "<<" };
    juce::TextButton fastForwardButton { ">>" };
    juce::TextButton recordButton { "Record" };
    juce::TextButton openButton { "Open Project" };
    juce::TextButton saveButton { "Save Project" };
    juce::TextButton importButton { "Import Media" };
    juce::TextButton placeButton { "Place On Timeline" };
    juce::TextButton zoomOutButton { "Zoom -" };
    juce::TextButton zoomInButton { "Zoom +" };
    juce::TextButton scrollLeftButton { "< Scroll" };
    juce::TextButton scrollRightButton { "Scroll >" };
    juce::TextButton markerButton { "+ Marker" };
    juce::TextButton regionButton { "+ Region" };
    juce::TextButton splitButton { "Split Clip" };
    juce::TextButton stepBackButton { "< Frame" };
    juce::TextButton stepForwardButton { "Frame >" };
    juce::TextButton playButton { "Play" };
    juce::TextButton stopButton { "Stop" };
    juce::TextButton scrubButton { "Scrub" };
    juce::TextButton previewButton { "Pop Out Preview" };
    juce::TextButton eulaButton { "EULA" };

    juce::AudioFormatManager audioFormatManager;
    std::vector<AssetRecord> assets;
    std::vector<TimelineClip> timelineClips;
    std::vector<TimelineMarker> timelineMarkers;
    std::vector<TimelineRegion> timelineRegions;
    int selectedAssetIndex = -1;
    int selectedClipIndex = -1;
    std::unique_ptr<juce::FileChooser> importChooser;
    std::unique_ptr<juce::FileChooser> openProjectChooser;

    creation::suite::SuiteSettings suiteSettings;
    creation::assets::ProjectSession projectSession;

    std::unique_ptr<juce::Component> previewSurface;
    std::unique_ptr<juce::Component> timelineCanvas;
    std::unique_ptr<juce::Component> assetLibraryPanel;
    std::unique_ptr<juce::Component> inspectorPanel;
    std::unique_ptr<juce::DocumentWindow> previewWindow;
    std::unique_ptr<juce::DocumentWindow> eulaWindow;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
