#pragma once

#include <JuceHeader.h>
#include "Shared/CreationSuiteHeaderBar.h"
#include <creation/assets/AssetTypes.h>
#include <creation/suite/SuiteSettings.h>
#include <creation/ui/SuiteSettingsPanel.h>
#include "Legal/EulaText.h"

using SuiteSettings = creation::suite::SuiteSettings;
using SuiteSettingsStore = creation::suite::SuiteSettingsStore;
using AssetId = creation::assets::AssetId;
using AssetVersionId = creation::assets::AssetVersionId;
using AssetReferenceMode = creation::assets::AssetReferenceMode;
using AssetRef = creation::assets::AssetRef;

class MainComponent final : public juce::Component,
                            private juce::Timer,
                            private juce::KeyListener
{
public:
    enum class WorkspaceMode
    {
        tracker,
        library,
        inspector,
        render,
        settings
    };

    enum class AssetKind
    {
        video,
        audio,
        image
    };

    struct AssetRecord
    {
        AssetId assetId;
        AssetVersionId versionId;
        AssetRef ref;
        juce::String id;
        juce::String name;
        juce::String sourcePath;
        juce::String originalSourcePath;
        juce::String logicalPath;
        AssetKind kind = AssetKind::video;
        juce::String metadataLine;
        juce::String codecSummary;
        juce::String sourceTool { "Creation Movie" };
        juce::String sourceApp { "Creation Movie" };
        juce::String sourceVersion { "1" };
        double durationSeconds = 0.0;
        double frameRate = 0.0;
        double sampleRate = 0.0;
        int width = 0;
        int height = 0;
        int channels = 0;
        int64 fileSizeBytes = 0;
        juce::Time importedAt;
        juce::Image previewImage;
    };

    struct TimelineClip
    {
        juce::String assetId;
        juce::String assetVersionId;
        AssetReferenceMode assetReferenceMode = AssetReferenceMode::exact;
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
        bool snapEnabled = true;
    };

    MainComponent();
    ~MainComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    class DomainTabsBar final : public juce::Component
    {
    public:
        DomainTabsBar();

        std::function<void(WorkspaceMode)> onModeSelected;

        void setActiveMode(WorkspaceMode mode);
        void resized() override;
        void paint(juce::Graphics&) override;

    private:
        WorkspaceMode activeMode = WorkspaceMode::tracker;
        juce::Label titleLabel;
        juce::TextButton trackerButton { "Tracker" };
        juce::TextButton libraryButton { "Library" };
        juce::TextButton inspectorButton { "Inspector" };
        juce::TextButton renderButton { "Render" };
        juce::TextButton settingsButton { "Settings" };
    };

    void importMediaFiles();
    void ingestMediaFile(const juce::File& file);
    void openProject();
    void saveProject();
    void loadProjectFromFile(const juce::File& file);
    void saveProjectToFile(const juce::File& file);
    void zoomTimeline(double factor);
    void scrollTimeline(double deltaSeconds);
    void addMarkerAtPlayhead();
    void createRegionFromMarkers();
    void splitSelectedClip();
    void updateClipPlacement(int clipIndex, double startSeconds, int laneIndex);
    void updateClipTrim(int clipIndex, double startSeconds, double durationSeconds);
    void movePlayheadTo(double timeSeconds);
    void placeSelectedAssetOnTimeline();
    void selectAsset(int assetIndex);
    void selectClip(int clipIndex);
    void seedDemoContent();
    void markProjectDirty(bool dirty = true);
    void refreshProjectHeader();
    void updateProjectDurationFromContent();
    juce::File getMovieProjectsDirectory() const;
    juce::File getDefaultProjectFile() const;
    juce::File getProjectAssetDirectory() const;
    juce::String makeAssetSlug(const juce::String& name) const;
    juce::String makeAssetVersionSuffix(const juce::File& file) const;
    void syncAssetReference(AssetRecord& asset);
    bool ensureAssetManagedInProject(AssetRecord& asset, juce::String& errorMessage);
    void ensureAllAssetsManagedInProject();

    void timerCallback() override;
    bool keyPressed(const juce::KeyPress& key, juce::Component*) override;

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
    void toggleSnapMode();
    void stepPlayheadByFrames(int frameDelta);
    void showProjectMenu();
    void resetProject();
    void showInsertMenu();
    void showViewMenu();
    void showEditMenu();
    void showWindowMenu();
    void showSuiteSettingsWindow();
    void closeSuiteSettingsWindow();
    void chooseSuiteDirectory(const juce::String& fieldId);
    void applySuiteSettings(const SuiteSettings& settings);

    TransportState transportState;
    juce::String projectName { "Untitled Movie" };
    bool isRecording = false;
    bool projectDirty = false;
    WorkspaceMode activeMode = WorkspaceMode::tracker;

    CreationSuiteHeaderBar headerBar;
    DomainTabsBar domainTabsBar;
    juce::Label runtimeLabel;
    juce::Label commandHintLabel;
    juce::TextButton projectMenuButton { "Project" };
    juce::TextButton insertMenuButton { "Insert" };
    juce::TextButton editMenuButton { "Edit" };
    juce::TextButton viewMenuButton { "View" };
    juce::TextButton windowMenuButton { "Window" };
    juce::TextButton snapButton { "Snap: ON" };

    juce::AudioFormatManager audioFormatManager;
    std::vector<AssetRecord> assets;
    std::vector<TimelineClip> timelineClips;
    std::vector<TimelineMarker> timelineMarkers;
    std::vector<TimelineRegion> timelineRegions;
    int selectedAssetIndex = -1;
    int selectedClipIndex = -1;
    juce::File currentProjectFile;
    std::unique_ptr<juce::FileChooser> importChooser;
    std::unique_ptr<juce::FileChooser> openProjectChooser;
    std::unique_ptr<juce::FileChooser> saveProjectChooser;

    std::unique_ptr<juce::Component> previewSurface;
    std::unique_ptr<juce::Component> timelineCanvas;
    std::unique_ptr<juce::Component> assetLibraryPanel;
    std::unique_ptr<juce::Component> inspectorPanel;
    std::unique_ptr<juce::DocumentWindow> previewWindow;
    std::unique_ptr<juce::DocumentWindow> eulaWindow;
    std::unique_ptr<juce::DocumentWindow> suiteSettingsWindow;
    juce::Component::SafePointer<SuiteSettingsPanel> suiteSettingsPanel;
    std::unique_ptr<juce::FileChooser> suiteDirectoryChooser;
    SuiteSettingsStore suiteSettingsStore;
    SuiteSettings suiteSettings;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
