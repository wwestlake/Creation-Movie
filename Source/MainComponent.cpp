#include "MainComponent.h"
#include "Branding.h"
#include "Legal/EulaText.h"
#include <creation/assets/ProjectAssetService.h>
#include <creation/assets/ProjectContainerService.h>
#include <creation/assets/ProjectWorkspaceService.h>
#include <creation/assets/VfsEntryInputStream.h>
#include <creation/suite/SuiteSettings.h>
#include <creation/services/SuiteVfsJsonStore.h>
#include <creation/ui/SuiteEulaPanel.h>
#include <creation/ui/SuiteJUCEApplication.h>
#include <creation/ui/SuiteSettingsPanel.h>

namespace
{
constexpr const char* kMovieStateEntry = "Project/movie-state.json";
constexpr const char* kMovieLayoutEntry = "movie/layout.json";

class NonOwningHost final : public juce::Component
{
public:
    explicit NonOwningHost(juce::Component& c) : child(c) { addAndMakeVisible(child); }
    void resized() override { child.setBounds(getLocalBounds()); }
private:
    juce::Component& child;
};

juce::String formatTimecode(double seconds, int fpsNum, int fpsDen)
{
    const double fps = fpsDen > 0 && fpsNum > 0 ? (double) fpsNum / fpsDen : 30.0;
    const int safeFps = std::max(1, (int) std::lround(fps));
    const int64_t totalFrames = std::max<int64_t>(0, std::llround(std::max(0.0, seconds) * fps));
    const int frames = (int) (totalFrames % safeFps);
    const int64_t totalSecs = totalFrames / safeFps;
    const int secs = (int) (totalSecs % 60);
    const int mins = (int) ((totalSecs / 60) % 60);
    const int hours = (int) (totalSecs / 3600);
    return juce::String::formatted("%02d:%02d:%02d:%02d", hours, mins, secs, frames);
}

juce::var schemaObj(std::initializer_list<std::pair<juce::Identifier, juce::var>> props,
                    juce::StringArray required = {})
{
    juce::Array<juce::var> req;
    for (const auto& r : required) req.add(r);
    return movie::object({
        { "type", "object" },
        { "properties", movie::object(props) },
        { "required", req },
        { "additionalProperties", false }
    });
}
} // namespace

// =============================================================================
// Library Panel
// =============================================================================
class MainComponent::Library final : public juce::Component
{
public:
#if JUCE_DEBUG
    friend class MainComponent::TrackUiCheck;
#endif
    explicit Library(MainComponent& o) : owner(o)
    {
        importBtn.onClick = [this] { owner.chooseImport(); };
        placeBtn.onClick = [this]
        {
            if (owner.selectedMedia.isNotEmpty())
                owner.runUi("movie.timeline.place", movie::object({
                    { "assetId", owner.selectedMedia },
                    { "trackId", owner.selectedTrack },
                    { "start", owner.playback.position() }
                }));
        };
        titleBtn.onClick = [this] { owner.titleDialog(); };
        addAndMakeVisible(importBtn);
        addAndMakeVisible(placeBtn);
        addAndMakeVisible(titleBtn);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(creation_movie::branding::panelColour());
        auto area = getLocalBounds().reduced(10);
        area.removeFromTop(38);

        g.setColour(juce::Colours::white.withAlpha(0.75f));
        g.setFont(juce::FontOptions(13.0f, juce::Font::bold));
        g.drawText("Project Media (" + juce::String(owner.document.sequence.media.size()) + ")",
                   area.removeFromTop(22), juce::Justification::centredLeft);

        if (owner.document.sequence.media.empty())
        {
            g.setColour(juce::Colours::white.withAlpha(0.45f));
            g.setFont(juce::FontOptions(13.0f));
            g.drawFittedText("No media imported yet.\nUse Import Media or movie.media.import.",
                             area.reduced(8), juce::Justification::centred, 3);
            return;
        }

        int y = area.getY();
        for (const auto& m : owner.document.sequence.media)
        {
            juce::Rectangle<int> row(area.getX(), y, area.getWidth(), 54);
            if (row.getBottom() > getHeight() - 8)
                break;

            const bool selected = (m.id == owner.selectedMedia);
            g.setColour(selected ? creation_movie::branding::accentColour().withAlpha(0.28f)
                                 : juce::Colour(0xff182232));
            g.fillRoundedRectangle(row.toFloat(), 6.0f);
            g.setColour(selected ? creation_movie::branding::accentColour()
                                 : juce::Colours::white.withAlpha(0.14f));
            g.drawRoundedRectangle(row.toFloat(), 6.0f, 1.0f);

            g.setColour(juce::Colours::white);
            g.setFont(juce::FontOptions(13.5f, juce::Font::bold));
            g.drawText(m.name, row.reduced(8, 4).removeFromTop(20), juce::Justification::centredLeft, true);

            juce::String meta;
            if (m.info.hasVideo)
                meta << m.info.width << "x" << m.info.height << " " << m.info.videoCodec << " ";
            if (m.info.hasAudio)
                meta << m.info.sampleRate << "Hz " << m.info.channels << "ch " << m.info.audioCodec << " ";
            meta << juce::String(m.info.duration, 2) << "s";

            g.setColour(juce::Colours::white.withAlpha(0.65f));
            g.setFont(juce::FontOptions(11.5f));
            g.drawText(meta, row.reduced(8, 4).withTrimmedTop(22), juce::Justification::centredLeft, true);

            y += 60;
        }
    }

    void resized() override
    {
        auto top = getLocalBounds().reduced(10).removeFromTop(30);
        const int w = std::max(60, (top.getWidth() - 8) / 3);
        importBtn.setBounds(top.removeFromLeft(w));
        top.removeFromLeft(4);
        placeBtn.setBounds(top.removeFromLeft(w));
        top.removeFromLeft(4);
        titleBtn.setBounds(top);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        auto area = getLocalBounds().reduced(10);
        area.removeFromTop(60);
        const int idx = (e.y - area.getY()) / 60;
        if (idx >= 0 && idx < (int) owner.document.sequence.media.size())
        {
            const auto& m = owner.document.sequence.media[(size_t) idx];
            owner.selectMedia(m.id);
            if (e.mods.isPopupMenu())
            {
                juce::PopupMenu mMenu;
                mMenu.addItem(1, "Place at Playhead");
                mMenu.addItem(2, "Append to End of Timeline");
                mMenu.showMenuAsync(
                    juce::PopupMenu::Options().withTargetScreenArea({ e.getScreenX(), e.getScreenY(), 1, 1 }),
                    [this, id = m.id](int res)
                    {
                        if (res == 1)
                            owner.runUi("movie.timeline.place", movie::object({ { "assetId", id }, { "trackId", owner.selectedTrack }, { "start", owner.playback.position() } }));
                        else if (res == 2)
                            owner.runUi("movie.timeline.place", movie::object({ { "assetId", id }, { "trackId", owner.selectedTrack }, { "start", owner.document.sequence.duration() } }));
                    });
            }
        }
    }

    void mouseDoubleClick(const juce::MouseEvent& e) override
    {
        mouseDown(e);
        if (owner.selectedMedia.isNotEmpty())
            owner.runUi("movie.timeline.place", movie::object({
                { "assetId", owner.selectedMedia },
                { "trackId", owner.selectedTrack },
                { "start", owner.playback.position() }
            }));
    }

private:
    MainComponent& owner;
    juce::TextButton importBtn { "Import..." }, placeBtn { "Place" }, titleBtn { "+ Title" };
};

// =============================================================================
// Monitor Panel (Source Monitor & Program Monitor)
// =============================================================================
class MainComponent::Monitor final : public juce::Component
{
public:
    Monitor(MainComponent& o, movie::Playback& p, bool isSourceMonitor)
        : owner(o), player(p), sourceMode(isSourceMonitor)
    {
        playBtn.onClick = [this]
        {
            if (sourceMode)
                player.play(! player.playing());
            else
                owner.runUi(player.playing() ? "movie.transport.pause" : "movie.transport.play");
        };
        stopBtn.onClick = [this]
        {
            if (sourceMode)
            {
                player.play(false);
                player.seek(0.0);
            }
            else
            {
                owner.runUi("movie.transport.stop");
            }
        };
        stepBackBtn.onClick = [this]
        {
            if (sourceMode)
            {
                player.play(false);
                player.seek(std::max(0.0, player.position() - owner.document.sequence.frameSeconds()));
            }
            else
            {
                owner.runUi("movie.transport.step", movie::object({ { "frames", -1 } }));
            }
        };
        stepFwdBtn.onClick = [this]
        {
            if (sourceMode)
            {
                player.play(false);
                player.seek(player.position() + owner.document.sequence.frameSeconds());
            }
            else
            {
                owner.runUi("movie.transport.step", movie::object({ { "frames", 1 } }));
            }
        };
        addAndMakeVisible(playBtn);
        addAndMakeVisible(stopBtn);
        addAndMakeVisible(stepBackBtn);
        addAndMakeVisible(stepFwdBtn);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff0b0f17));
        auto bounds = getLocalBounds().reduced(8);
        auto controls = bounds.removeFromBottom(32);
        auto scrubBar = bounds.removeFromBottom(16).reduced(2, 4);

        const auto frame = player.preview();
        if (frame.isValid())
        {
            g.drawImageWithin(frame, bounds.getX(), bounds.getY(), bounds.getWidth(), bounds.getHeight(),
                              juce::RectanglePlacement::centred, false);
        }
        else
        {
            g.setColour(juce::Colours::white.withAlpha(0.35f));
            g.setFont(juce::FontOptions(14.0f));
            g.drawText(sourceMode ? "Source Monitor: Select a media asset to preview"
                                  : "Program Monitor: Place media on timeline to preview",
                       bounds, juce::Justification::centred);
        }

        // Overlay badge
        g.setColour(juce::Colours::black.withAlpha(0.6f));
        g.fillRoundedRectangle((float) bounds.getX() + 8.0f, (float) bounds.getY() + 8.0f, 220.0f, 24.0f, 4.0f);
        g.setColour(juce::Colours::white.withAlpha(0.9f));
        g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
        const auto& seq = owner.document.sequence;
        const auto tc = formatTimecode(player.position(), seq.fpsNum, seq.fpsDen);
        g.drawText((sourceMode ? "SRC  " : "PGM  ") + tc,
                   bounds.getX() + 14, bounds.getY() + 8, 208, 24, juce::Justification::centredLeft);

        // Scrub bar
        g.setColour(juce::Colour(0xff1e293b));
        g.fillRoundedRectangle(scrubBar.toFloat(), 3.0f);
        double dur = seq.duration();
        if (sourceMode)
        {
            if (const auto* m = seq.findMedia(owner.selectedMedia))
                dur = m->info.duration;
            else
                dur = 0.0;
        }
        if (dur > 0.0)
        {
            const float ratio = (float) juce::jlimit(0.0, 1.0, player.position() / dur);
            auto filled = scrubBar.withWidth((int) std::lround(scrubBar.getWidth() * ratio));
            g.setColour(creation_movie::branding::accentColour());
            g.fillRoundedRectangle(filled.toFloat(), 3.0f);
        }
        playBtn.setButtonText(player.playing() ? "Pause" : "Play");
        juce::ignoreUnused(controls);
    }

    void resized() override
    {
        auto row = getLocalBounds().reduced(8).removeFromBottom(28);
        stepBackBtn.setBounds(row.removeFromLeft(54));
        row.removeFromLeft(4);
        playBtn.setBounds(row.removeFromLeft(68));
        row.removeFromLeft(4);
        stopBtn.setBounds(row.removeFromLeft(58));
        row.removeFromLeft(4);
        stepFwdBtn.setBounds(row.removeFromLeft(54));
    }

    void mouseDown(const juce::MouseEvent& e) override { scrubAt(e); }
    void mouseDrag(const juce::MouseEvent& e) override { scrubAt(e); }

private:
    void scrubAt(const juce::MouseEvent& e)
    {
        auto bounds = getLocalBounds().reduced(8);
        bounds.removeFromBottom(32);
        auto scrubBar = bounds.removeFromBottom(16);
        if (! scrubBar.expanded(0, 6).contains(e.getPosition()))
            return;

        double dur = owner.document.sequence.duration();
        if (sourceMode)
        {
            if (const auto* m = owner.document.sequence.findMedia(owner.selectedMedia))
                dur = m->info.duration;
            else
                dur = 0.0;
        }
        if (dur <= 0.0)
            return;

        const double ratio = juce::jlimit(0.0, 1.0, (e.x - scrubBar.getX()) / (double) std::max(1, scrubBar.getWidth()));
        const double t = owner.document.sequence.snap(ratio * dur);
        if (sourceMode)
            player.seek(t);
        else
            owner.runUi("movie.transport.seek", movie::object({ { "time", t } }));
    }

    MainComponent& owner;
    movie::Playback& player;
    bool sourceMode = false;
    juce::TextButton playBtn { "Play" }, stopBtn { "Stop" }, stepBackBtn { "< 1f" }, stepFwdBtn { "1f >" };
};

// =============================================================================
// Timeline Panel
// =============================================================================
#include "TrackTimeline.inc"

// =============================================================================
// Inspector Panel
// =============================================================================
class MainComponent::Inspector final : public juce::Component
{
public:
    explicit Inspector(MainComponent& o) : owner(o)
    {
        auto setupSlider = [this](juce::Slider& s, double minV, double maxV, double stepV)
        {
            s.setSliderStyle(juce::Slider::LinearHorizontal);
            s.setTextBoxStyle(juce::Slider::TextBoxRight, false, 64, 22);
            s.setRange(minV, maxV, stepV);
            addAndMakeVisible(s);
        };
        setupSlider(gainSlider, 0.0, 4.0, 0.05);
        setupSlider(opacitySlider, 0.0, 1.0, 0.05);

        gainSlider.onDragEnd = [this] { applyLevels(); };
        opacitySlider.onDragEnd = [this] { applyLevels(); };

        applyTrimBtn.onClick = [this]
        {
            if (owner.selectedClip.isEmpty()) return;
            owner.runUi("movie.timeline.trim", movie::object({
                { "clipId", owner.selectedClip },
                { "sourceIn", sourceInEditor.getText().getDoubleValue() },
                { "duration", durationEditor.getText().getDoubleValue() }
            }));
        };
        deleteBtn.onClick = [this]
        {
            if (owner.selectedClip.isNotEmpty())
                owner.runUi("movie.timeline.remove", movie::object({ { "clipId", owner.selectedClip }, { "ripple", false } }));
        };
        rippleBtn.onClick = [this]
        {
            if (owner.selectedClip.isNotEmpty())
                owner.runUi("movie.timeline.remove", movie::object({ { "clipId", owner.selectedClip }, { "ripple", true } }));
        };

        addAndMakeVisible(sourceInEditor);
        addAndMakeVisible(durationEditor);
        addAndMakeVisible(applyTrimBtn);
        addAndMakeVisible(deleteBtn);
        addAndMakeVisible(rippleBtn);
    }

    void syncFromSelection()
    {
        const int idx = owner.document.sequence.findClip(owner.selectedClip);
        const bool hasClip = idx >= 0;
        gainSlider.setEnabled(hasClip);
        opacitySlider.setEnabled(hasClip);
        sourceInEditor.setEnabled(hasClip);
        durationEditor.setEnabled(hasClip);
        applyTrimBtn.setEnabled(hasClip);
        deleteBtn.setEnabled(hasClip);
        rippleBtn.setEnabled(hasClip);

        if (hasClip)
        {
            const auto& c = owner.document.sequence.clips[(size_t) idx];
            gainSlider.setValue((double) c.videoParams.getWithDefault("gain", 1.0), juce::dontSendNotification);
            opacitySlider.setValue((double) c.videoParams.getWithDefault("opacity", 1.0), juce::dontSendNotification);
            sourceInEditor.setText(juce::String(c.sourceStartSeconds, 3), juce::dontSendNotification);
            durationEditor.setText(juce::String(c.durationSeconds, 3), juce::dontSendNotification);
        }
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(creation_movie::branding::panelColour());
        auto area = getLocalBounds().reduced(12);

        g.setColour(juce::Colours::white);
        g.setFont(juce::FontOptions(14.0f, juce::Font::bold));
        g.drawText("Clip & Media Inspector", area.removeFromTop(24), juce::Justification::centredLeft);

        const int idx = owner.document.sequence.findClip(owner.selectedClip);
        if (idx >= 0)
        {
            const auto& c = owner.document.sequence.clips[(size_t) idx];
            g.setColour(juce::Colours::white.withAlpha(0.85f));
            g.setFont(juce::FontOptions(12.5f));
            juce::String info;
            info << "Clip: " << c.displayName << " (Track " << c.trackIndex << ")\n"
                 << "Timeline Start: " << juce::String(c.startSeconds, 3) << "s\n"
                 << "Source Range: " << juce::String(c.sourceStartSeconds, 3) << "s -> "
                 << juce::String(c.sourceStartSeconds + c.durationSeconds, 3) << "s";
            g.drawFittedText(info, area.removeFromTop(62), juce::Justification::topLeft, 4);
        }
        else
        {
            g.setColour(juce::Colours::white.withAlpha(0.45f));
            g.setFont(juce::FontOptions(12.5f));
            g.drawText("No timeline clip selected.", area.removeFromTop(62), juce::Justification::centredLeft);
        }

        g.setColour(juce::Colours::white.withAlpha(0.75f));
        g.setFont(juce::FontOptions(12.0f));
        g.drawText("Gain", 12, 102, 70, 24, juce::Justification::centredLeft);
        g.drawText("Opacity", 12, 132, 70, 24, juce::Justification::centredLeft);
        g.drawText("Source In (s)", 12, 166, 86, 24, juce::Justification::centredLeft);
        g.drawText("Duration (s)", 12, 196, 86, 24, juce::Justification::centredLeft);
    }

    void resized() override
    {
        const int w = std::max(80, getWidth() - 106);
        gainSlider.setBounds(94, 102, w, 24);
        opacitySlider.setBounds(94, 132, w, 24);
        sourceInEditor.setBounds(100, 166, w - 6, 24);
        durationEditor.setBounds(100, 196, w - 6, 24);
        applyTrimBtn.setBounds(12, 228, getWidth() - 24, 26);
        const int half = (getWidth() - 28) / 2;
        deleteBtn.setBounds(12, 260, half, 26);
        rippleBtn.setBounds(16 + half, 260, half, 26);
    }

private:
    void applyLevels()
    {
        if (owner.selectedClip.isEmpty()) return;
        owner.runUi("movie.timeline.levels", movie::object({
            { "clipId", owner.selectedClip },
            { "gain", gainSlider.getValue() },
            { "opacity", opacitySlider.getValue() }
        }));
    }

    MainComponent& owner;
    juce::Slider gainSlider, opacitySlider;
    juce::TextEditor sourceInEditor, durationEditor;
    juce::TextButton applyTrimBtn { "Apply Source In / Duration" };
    juce::TextButton deleteBtn { "Delete Clip" }, rippleBtn { "Ripple Delete" };
};

#include "EffectsPanel.inc"
#include "TrackInspector.inc"
#include "TrackUiCheck.inc"

// =============================================================================
// MainComponent Implementation
// =============================================================================
MainComponent::MainComponent()
{
    setLookAndFeel(&look);
    setWantsKeyboardFocus(true);

    juce::String settingsErr;
    creation::suite::SuiteSettingsStore settingsStore;
    settings = settingsStore.load(settingsErr);

    const auto audioErr = playback.initialiseAudio();
    sourcePlayback.initialiseAudio();
    if (audioErr.isNotEmpty())
        lastStatus = "Audio output unavailable (" + audioErr + "); using wall-clock preview transport.";

    header.setAppTitle("Djehuti Movie");
    header.setLogoImage(creation::ui::getSuiteLogoImage(creation::ui::SuiteLogoId::movie));
    header.setSelectedLogoId(creation::ui::SuiteLogoId::movie);
    header.setSeparatePauseButtonVisible(true);
    header.setTransportButtonVisible(CreationSuiteHeaderBar::TransportButtonSlot::record, false);
    header.setTransportButtonVisible(CreationSuiteHeaderBar::TransportButtonSlot::click, false);
    header.setTransportButtonVisible(CreationSuiteHeaderBar::TransportButtonSlot::loop, false);
    header.onPlay = [this] { runUi("movie.transport.play"); };
    header.onPause = [this] { runUi("movie.transport.pause"); };
    header.onStop = [this] { runUi("movie.transport.stop"); };
    header.onRewind = [this] { runUi("movie.transport.step", movie::object({ { "frames", -1 } })); };
    header.onFastForward = [this] { runUi("movie.transport.step", movie::object({ { "frames", 1 } })); };
    header.onRewindToStart = [this] { runUi("movie.transport.seek", movie::object({ { "time", 0.0 } })); };
    header.onFastForwardToEnd = [this] { runUi("movie.transport.seek", movie::object({ { "time", document.sequence.duration() } })); };
    header.onProjectMenuRequested = [this] { openProjectMenu(); };
    header.onSuiteRequested = [this] { showSettings(); };
    addAndMakeVisible(header);

    addAndMakeVisible(menu);
    addAndMakeVisible(status);
    addAndMakeVisible(timecode);
    addAndMakeVisible(split);
    addAndMakeVisible(title);
    addAndMakeVisible(fit);
    addAndMakeVisible(cancelJob);
    addAndMakeVisible(zoom);

    timecode.setFont(juce::FontOptions(14.0f, juce::Font::bold));
    timecode.setColour(juce::Label::textColourId, juce::Colours::white);
    status.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.85f));

    split.onClick = [this]
    {
        if (selectedClip.isEmpty()) { warn("Select a timeline clip first."); return; }
        runUi("movie.timeline.split", movie::object({ { "clipId", selectedClip }, { "time", playback.position() } }));
    };
    title.onClick = [this] { titleDialog(); };
    fit.onClick = [this]
    {
        visibleStart = 0.0;
        const double dur = std::max(5.0, document.sequence.duration());
        const int availW = std::max(200, (timeline ? timeline->getWidth() : 800) - 260);
        pixelsPerSecond = juce::jlimit(5.0, 200.0, availW / dur);
        zoom.setValue(pixelsPerSecond, juce::dontSendNotification);
        if (timeline) timeline->repaint();
    };
    cancelJob.onClick = [this] { runUi("movie.job.cancel"); };

    zoom.setSliderStyle(juce::Slider::LinearHorizontal);
    zoom.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    zoom.setRange(5.0, 200.0, 1.0);
    zoom.setValue(pixelsPerSecond, juce::dontSendNotification);
    zoom.onValueChange = [this]
    {
        pixelsPerSecond = zoom.getValue();
        if (timeline) timeline->repaint();
    };

    library = std::make_unique<Library>(*this);
    monitor = std::make_unique<Monitor>(*this, playback, false);
    sourceMonitor = std::make_unique<Monitor>(*this, sourcePlayback, true);
    timeline = std::make_unique<Timeline>(*this);
    inspector = std::make_unique<Inspector>(*this);
    trackInspector = std::make_unique<TrackInspector>(*this);
    effectsPanel = std::make_unique<EffectsPanel>(*this);
    chat = std::make_unique<creation::agent::EngineerChat>(engineer);
    cards = std::make_unique<creation::agent::CardsPanel>(engineer);

    dock = std::make_unique<CreationDock::DockManager>(*this);
    using Zone = CreationDock::DockTargetZone;
    dock->registerPanel("library", "Media Library", std::make_unique<NonOwningHost>(*library), Zone::Left);
    dock->registerPanel("source", "Source Monitor", std::make_unique<NonOwningHost>(*sourceMonitor), Zone::CenterTab);
    dock->registerPanel("program", "Program Monitor", std::make_unique<NonOwningHost>(*monitor), Zone::CenterTab);
    dock->registerPanel("timeline", "Timeline", std::make_unique<NonOwningHost>(*timeline), Zone::Bottom);
    dock->registerPanel("track-inspector", "Track Inspector", std::make_unique<NonOwningHost>(*trackInspector), Zone::Right);
    dock->registerPanel("effects", "Effects", std::make_unique<NonOwningHost>(*effectsPanel), Zone::Right);
    dock->registerPanel("inspector", "Inspector", std::make_unique<NonOwningHost>(*inspector), Zone::Right);
    dock->registerPanel("engineer", "Virtual Engineer", std::make_unique<NonOwningHost>(*chat), Zone::Right);
    dock->registerPanel("cards", "Cards", std::make_unique<NonOwningHost>(*cards), Zone::Right);
    dock->activatePanel("program");
    dock->activatePanel("inspector");
    addAndMakeVisible(*dock);

    {
        juce::String layoutErr;
        const auto savedLayout = creation::services::SuiteVfsJsonStore::loadJson(kMovieLayoutEntry, layoutErr);
        if (savedLayout.isObject())
            dock->applyLayout(savedLayout);
    }

    document.changed = [this] { changed(); };

    // Ensure an active VFS project is open or created
    {
        juce::String projErr;
        auto projects = creation::assets::ProjectContainerService::listProjects(settings, projErr);
        bool opened = false;
        for (const auto& summary : projects)
        {
            if (creation::assets::ProjectWorkspaceService::openProject(settings, summary.projectId, session, projErr))
            {
                juce::MemoryBlock block;
                if (session.readEntry(kMovieStateEntry, block))
                {
                    movie::Sequence loaded;
                    juce::String loadErr;
                    if (movie::Sequence::fromJson(juce::JSON::parse(block.toString()), loaded, loadErr))
                    {
                        loaded.projectId = session.getProjectId();
                        document.replace(std::move(loaded), true);
                        opened = true;
                        break;
                    }
                }
            }
        }
        if (! opened)
        {
            if (creation::assets::ProjectWorkspaceService::createProject(
                    settings, creation::assets::SuiteAppDomain::movie,
                    "Untitled Movie", "0.0.1", "0.0.1", session, projErr))
            {
                movie::Sequence fresh;
                fresh.projectId = session.getProjectId();
                fresh.name = "Untitled Movie";
                document.replace(std::move(fresh), true);
            }
            else if (projErr.isNotEmpty())
            {
                lastStatus = "Project init note: " + projErr;
            }
        }
    }

    registerCommands();
    engineer.setProjectId(session.isValid() ? session.getProjectId() : juce::String());
    cards->refresh();

    juce::String apiErr;
    if (! api.start(apiErr))
        lastStatus = "Agent API did not start: " + apiErr;
    else if (lastStatus.isEmpty())
        lastStatus = "Ready (FFmpeg " + movie::codecVersion() + ", API port " + juce::String(api.getPort()) + ")";

    changed();
    setSize(1520, 940);
    startTimerHz(30);
#if JUCE_DEBUG
    const auto arguments=juce::StringArray::fromTokens(juce::JUCEApplication::getCommandLineParameters(),true);
    const int testArgument=arguments.indexOf("--track-ui-check");
    if(testArgument>=0 && testArgument+1<arguments.size())
        trackUiCheck=std::make_unique<TrackUiCheck>(*this,juce::File(arguments[testArgument+1].unquoted()),
            testArgument+2<arguments.size()?juce::File(arguments[testArgument+2].unquoted()):juce::File(),arguments.contains("--effects-ui-check"));
#endif
}

MainComponent::~MainComponent()
{
#if JUCE_DEBUG
    trackUiCheck.reset();
#endif
    closing = true;
    stopTimer();
    api.stop();
    engineer.cancel();
    if (job)
        job->cancel = true;
    if (worker.joinable())
        worker.join();
    if (dock)
    {
        juce::String layoutErr;
        creation::services::SuiteVfsJsonStore::saveJson(kMovieLayoutEntry, dock->captureLayout(), layoutErr);
    }
    setLookAndFeel(nullptr);
}

void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(creation_movie::branding::backgroundColour());
}

void MainComponent::resized()
{
    auto area = getLocalBounds();
    header.setBounds(area.removeFromTop(CreationSuiteHeaderBar::preferredHeight));

    auto topBar = area.removeFromTop(30);
    menu.setBounds(topBar.removeFromLeft(300));
    split.setBounds(topBar.removeFromLeft(66).reduced(2));
    title.setBounds(topBar.removeFromLeft(66).reduced(2));
    fit.setBounds(topBar.removeFromLeft(90).reduced(2));
    cancelJob.setBounds(topBar.removeFromLeft(86).reduced(2));
    zoom.setBounds(topBar.removeFromLeft(130).reduced(4));
    timecode.setBounds(topBar.removeFromLeft(130).reduced(4, 0));
    status.setBounds(topBar.reduced(6, 0));

    if (dock)
        dock->setBounds(area);
}

bool MainComponent::keyPressed(const juce::KeyPress& key)
{
    if(key.getKeyCode()==juce::KeyPress::F2Key){renameTrack();return true;}
    if(key.getModifiers().isAltDown() && (key.getKeyCode()==juce::KeyPress::upKey || key.getKeyCode()==juce::KeyPress::downKey))
    {
        const int index=document.sequence.findTrack(selectedTrack);
        const int dest=index+(key.getKeyCode()==juce::KeyPress::upKey?-1:1);
        if(index>=0 && dest>=0 && dest<(int)document.sequence.tracks.size())
            runUi("movie.track.reorder",movie::object({{"trackId",selectedTrack},{"order",dest}}));
        return true;
    }
    if (key == juce::KeyPress::spaceKey)
    {
        runUi(playback.playing() ? "movie.transport.pause" : "movie.transport.play");
        return true;
    }
    if (key.getModifiers().isCommandDown() && (key.getKeyCode() == 'z' || key.getKeyCode() == 'Z'))
    {
        runUi(key.getModifiers().isShiftDown() ? "movie.timeline.redo" : "movie.timeline.undo");
        return true;
    }
    if (key.getModifiers().isCommandDown() && (key.getKeyCode() == 's' || key.getKeyCode() == 'S'))
    {
        runUi("movie.project.save");
        return true;
    }
    return false;
}

void MainComponent::requestQuit()
{
    if (job && job->status == "running")
    {
        warn("A background job is still running. Wait for completion, or cancel it before closing.");
        return;
    }
    if (document.dirty)
    {
        quitAfterSave = true;
        const auto result = execute("movie.project.save", movie::object({}));
        if (! result.ok)
        {
            quitAfterSave = false;
            warn("Movie remains open: " + result.message);
        }
        return;
    }
    juce::JUCEApplication::getInstance()->quit();
}

MainComponent::Result MainComponent::startJob(const juce::String& type,
                                              std::function<juce::var(Job&)> work,
                                              std::function<void(const juce::var&)> completed)
{
    if (job && job->status == "running")
        return Result::failure("job_busy", "A background media job (" + job->type + ") is already running.",
                               "Wait for the active job to complete or cancel it with movie.job.cancel.");

    if (worker.joinable())
        worker.join();

    auto active = std::make_shared<Job>();
    active->id = juce::Uuid().toString();
    active->type = type;
    active->projectId = session.isValid() ? session.getProjectId() : juce::String();
    active->status = "running";
    job = active;
    refresh();

    const juce::Component::SafePointer<MainComponent> safe(this);
    worker = std::thread([safe, active, work = std::move(work), completed = std::move(completed)]
    {
        juce::var out;
        juce::String error;
        try
        {
            out = work(*active);
        }
        catch (const std::exception& e)
        {
            error = e.what();
        }
        catch (...) { error = "Unexpected background job failure."; }
        // Apply the result first, then publish completion. No worker reads UI state
        // or writes strings/vars concurrently with the API and UI timers.
        juce::MessageManager::callAsync([safe, active, out, error, completed]() mutable
        {
            if (! safe || safe->closing || safe->job != active)
                return;
            if (error.isEmpty())
            {
                try { if (completed) completed(out); }
                catch (const std::exception& e) { error = e.what(); }
                catch (...) { error = "Could not apply the background result."; }
            }
            active->result = out;
            active->error = error;
            if (error.isNotEmpty())
            {
                active->status = active->cancel.load() ? "cancelled" : "failed";
                safe->quitAfterSave = false;
                safe->afterSave = {};
                safe->lastStatus = error;
            }
            else
            {
                // A cancellation that arrives after the operation committed does
                // not turn a successful write into a falsely reported cancellation.
                active->status = "completed";
                active->progress = 1.0;
                safe->lastStatus = "Completed " + active->type + " job.";
            }
            safe->refresh();
            if (error.isEmpty() && active->type == "save")
            {
                if (safe->quitAfterSave)
                {
                    safe->quitAfterSave = false;
                    juce::JUCEApplication::getInstance()->quit();
                }
                else if (safe->afterSave)
                {
                    auto next = std::move(safe->afterSave);
                    safe->afterSave = {};
                    next();
                }
            }
        });
    });

    return Result::success(
        movie::object({
            { "jobId", active->id },
            { "type", active->type },
            { "status", "scheduled" }
        }),
        { "Scheduled background " + type + " job " + active->id });
}

MainComponent::Result MainComponent::execute(const juce::String& name, const juce::var& a)
{
    if (name == "movie.state.get")
        return Result::success(state());
    if (name == "movie.jobs.get")
        return Result::success(jobs());
    if(name=="movie.effects.catalog")return Result::success(movie::effectsCatalogJson());
    if (name == "movie.track.list")
        return Result::success(document.sequence.toJson()["tracks"]);
    if (name == "movie.track.get")
    {
        const int index=document.sequence.findTrack(a["trackId"].toString());
        if(index<0) return Result::failure("unknown_track","Unknown track identity.");
        return Result::success(document.sequence.tracks[(size_t)index].toJson());
    }
    if (name == "movie.app.close")
    {
        if (job && job->status == "running")
            return Result::failure("job_busy", "Wait for the current job or cancel it before closing.");
        requestQuit();
        return Result::success(movie::object({ { "savePending", quitAfterSave } }));
    }
    if (name == "movie.job.cancel")
    {
        if (job && job->status == "running")
        {
            job->cancel = true;
            lastStatus = "Cancelling " + job->type + " job...";
            refresh();
            return Result::success(jobs(), { "Signalled cancellation for job " + job->id });
        }
        return Result::success(jobs());
    }

    const bool transport = name.startsWith("movie.transport.") || name == "movie.select";
    if (job && job->status == "running" && ! transport)
        return Result::failure("job_busy", "Wait for the background job to finish or cancel it. Transport and state inspection remain available.");
    if ((name == "movie.project.new" || name == "movie.project.open") && document.dirty
        && ! static_cast<bool>(a.getProperty("discardChanges", false)))
        return Result::failure("unsaved_changes", "Save the current project first, or explicitly set discardChanges to true.");

    if (name == "movie.project.new" || name == "movie.project.open")
    {
        const bool create = name == "movie.project.new";
        auto nextSession = std::make_shared<creation::assets::ProjectSession>();
        auto nextSequence = std::make_shared<movie::Sequence>();
        const auto config = settings;
        if (create)
        {
            nextSequence->name = a.getProperty("name", "Untitled Movie").toString().trim();
            if (nextSequence->name.isEmpty())
                return Result::failure("invalid_name", "Project name cannot be empty.");
            if (a.hasProperty("width")) nextSequence->width = (int)a["width"];
            if (a.hasProperty("height")) nextSequence->height = (int)a["height"];
            if (a.hasProperty("fpsNum")) nextSequence->fpsNum = (int)a["fpsNum"];
            if (a.hasProperty("fpsDen")) nextSequence->fpsDen = (int)a["fpsDen"];
            movie::Sequence checked;
            juce::String validation;
            if (!movie::Sequence::fromJson(nextSequence->toJson(), checked, validation))
                return Result::failure("invalid_sequence", validation);
        }
        else if (a["projectId"].toString().trim().isEmpty())
            return Result::failure("missing_project_id", "projectId is required.");
        playback.play(false);
        sourcePlayback.play(false);
        return startJob(create ? "new" : "open",
            [create, config, a, nextSession, nextSequence](Job& j)
            {
                juce::String error;
                if (j.cancel.load()) throw std::runtime_error("Project operation cancelled.");
                if (create)
                {
                    if (!creation::assets::ProjectWorkspaceService::createProject(config,
                            creation::assets::SuiteAppDomain::movie, nextSequence->name,
                            "0.0.1", "0.0.1", *nextSession, error))
                        throw std::runtime_error(error.toStdString());
                    nextSequence->projectId = nextSession->getProjectId();
                    const auto json = juce::JSON::toString(nextSequence->toJson());
                    if (!nextSession->writeEntry(kMovieStateEntry, juce::MemoryBlock(json.toRawUTF8(), json.getNumBytesAsUTF8())))
                        throw std::runtime_error(nextSession->getLastWriteError().toStdString());
                    if (!nextSession->commit(error)) throw std::runtime_error(error.toStdString());
                }
                else
                {
                    if (!creation::assets::ProjectWorkspaceService::openProject(config, a["projectId"].toString(), *nextSession, error))
                        throw std::runtime_error(error.toStdString());
                    juce::MemoryBlock block;
                    if (!nextSession->readEntry(kMovieStateEntry, block))
                        throw std::runtime_error("The selected project has no readable saved Movie sequence.");
                    if (!movie::Sequence::fromJson(juce::JSON::parse(block.toString()), *nextSequence, error))
                        throw std::runtime_error(error.toStdString());
                    nextSequence->projectId = nextSession->getProjectId();
                    if (j.cancel.load()) throw std::runtime_error("Opening project cancelled.");
                }
                return movie::object({{"projectId",nextSession->getProjectId()},{"name",nextSequence->name}});
            },
            [this, nextSession, nextSequence](const juce::var&)
            {
                // The current session is replaced only after the new one is valid.
                session = *nextSession;
                selectedClip = {}; selectedMedia = {};
                document.replace(*nextSequence, true);
                playback.seek(0);
                sourcePlayback.setSequence(movie::Sequence{});
                engineer.setProjectId(session.getProjectId());
                cards->refresh();
            });
    }

    if (name == "movie.project.save")
    {
        if (!session.isValid())
            return Result::failure("no_project", "No active VFS project session.");
        const auto snapshot = document.sequence;
        auto target = session;
        return startJob("save", [snapshot, target](Job& j) mutable
            {
                if (j.cancel.load()) throw std::runtime_error("Save cancelled before writing.");
                const auto json = juce::JSON::toString(snapshot.toJson());
                if (!target.writeEntry(kMovieStateEntry, juce::MemoryBlock(json.toRawUTF8(), json.getNumBytesAsUTF8())))
                    throw std::runtime_error(target.getLastWriteError().toStdString());
                juce::String error;
                if (!target.commit(error)) throw std::runtime_error(error.toStdString());
                return movie::object({{"projectId",target.getProjectId()}});
            },
            [this, snapshot](const juce::var&)
            {
                if (session.getProjectId() != snapshot.projectId)
                    throw std::runtime_error("Save completed for a different project; current edits remain unsaved.");
                if (juce::JSON::toString(document.sequence.toJson()) != juce::JSON::toString(snapshot.toJson()))
                    throw std::runtime_error("The saved snapshot is older than the current edits. Movie remains open; save again.");
                document.dirty = false;
            });
    }

    if (name == "movie.media.import")
    {
        if (! session.isValid())
            return Result::failure("no_project", "Create or open a project before importing media.");

        const juce::File file(a["path"].toString());
        if (! file.existsAsFile())
            return Result::failure("file_not_found", "Media file does not exist: " + file.getFullPathName());

        const juce::String projId = session.getProjectId();
        const juce::String mediaId = juce::Uuid().toString();
        const juce::String safeName = file.getFileName().replaceCharacter('\\', '_').replaceCharacter('/', '_');
        const juce::String logicalPath = "Media/" + mediaId + "_" + safeName;

        return startJob("import",
            [file, projId, mediaId, logicalPath](Job& j) -> juce::var
            {
                // 1. Probe external file first
                auto fileStream = file.createInputStream();
                if (! fileStream)
                    throw std::runtime_error("Cannot read source file: " + file.getFullPathName().toStdString());
                movie::MediaReader fileProbe(std::move(fileStream), file.getFileName(), movie::MediaReader::Kind::probe, &j.cancel);

                // 2. Upload directly into VFS container without compression overhead for media streams
                creation::suite::SuiteSettingsStore store;
                juce::String err;
                creation::assets::ProjectSession workerSession;
                if (! creation::assets::ProjectSession::open(store.load(err), projId, workerSession, err))
                    throw std::runtime_error(("Could not open VFS project for import: " + err).toStdString());

                if (! workerSession.writeEntryFromFile(logicalPath, file, err, 0,
                        [&j](double p) { j.progress = p * 0.85; return ! j.cancel.load(); }))
                {
                    throw std::runtime_error((err.isNotEmpty() ? err : "VFS media upload failed.").toStdString());
                }

                // 3. Verify VFS stream demuxing directly through custom AVIO
                auto vfsStream = creation::assets::openVfsEntryStream(projId, logicalPath);
                movie::MediaReader vfsProbe(std::move(vfsStream), file.getFileName(), movie::MediaReader::Kind::probe, &j.cancel);
                j.progress = 1.0;

                return movie::object({
                    { "id", mediaId },
                    { "versionId", mediaId },
                    { "name", file.getFileName() },
                    { "path", logicalPath },
                    { "info", vfsProbe.info().toJson() }
                });
            },
            [this, projId](const juce::var& res)
            {
                if (session.getProjectId() != projId || document.sequence.projectId != projId)
                    throw std::runtime_error("Import belongs to a different project; it was not applied here.");
                movie::Media m;
                m.id = res["id"].toString();
                m.versionId = res["versionId"].toString();
                m.name = res["name"].toString();
                m.path = res["path"].toString();
                m.info = movie::MediaInfo::fromJson(res["info"]);

                auto next = document.sequence;
                next.media.push_back(m);
                document.restore(next.toJson());
                selectMedia(m.id);
            });
    }

    if(name.startsWith("movie.effect."))
    {
        juce::String error;
        if(!document.edit(name.fromFirstOccurrenceOf("movie.",false,false),a,error))return Result::failure("edit_rejected",error);
        return Result::success(state());
    }
    if(name.startsWith("movie.track."))
    {
        juce::String error;
        if(!document.edit(name.fromFirstOccurrenceOf("movie.",false,false),a,error))
            return Result::failure("edit_rejected",error);
        return Result::success(state());
    }

    if (name == "movie.timeline.place" || name == "movie.timeline.title"
        || name == "movie.timeline.split" || name == "movie.timeline.trim"
        || name == "movie.timeline.move" || name == "movie.timeline.remove"
        || name == "movie.timeline.levels")
    {
        const juce::String shortCmd = name.fromLastOccurrenceOf(".", false, false);
        juce::var args = a;
        if (shortCmd != "place" && shortCmd != "title" && args["clipId"].toString().isEmpty() && selectedClip.isNotEmpty())
        {
            auto* cloned = new juce::DynamicObject();
            if (auto* existing = a.getDynamicObject())
                cloned->getProperties() = existing->getProperties();
            cloned->setProperty("clipId", selectedClip);
            if (shortCmd == "split" && ! movie::finiteNumber(cloned->getProperty("time")))
                cloned->setProperty("time", playback.position());
            args = juce::var(cloned);
        }

        juce::String err;
        if (! document.edit(shortCmd, args, err))
            return Result::failure("edit_rejected", err);

        if (! document.sequence.clips.empty() && document.sequence.findClip(selectedClip) < 0)
            selectedClip = document.sequence.clips.back().id;
        changed();
        return Result::success(state(), { "Applied timeline " + shortCmd });
    }

    if (name == "movie.timeline.undo")
    {
        if (! document.undo.canUndo())
            return Result::failure("nothing_to_undo", "There is no timeline action to undo.");
        document.undo.undo();
        changed();
        return Result::success(state(), { "Undid timeline edit" });
    }

    if (name == "movie.timeline.redo")
    {
        if (! document.undo.canRedo())
            return Result::failure("nothing_to_redo", "There is no timeline action to redo.");
        document.undo.redo();
        changed();
        return Result::success(state(), { "Redid timeline edit" });
    }

    if (name == "movie.transport.play")
    {
        playback.play(true);
        refresh();
        return Result::success(state(), { "Started playback" });
    }
    if (name == "movie.transport.pause")
    {
        playback.play(false);
        refresh();
        return Result::success(state(), { "Paused playback" });
    }
    if (name == "movie.transport.stop")
    {
        playback.play(false);
        playback.seek(0.0);
        refresh();
        return Result::success(state(), { "Stopped playback" });
    }
    if (name == "movie.transport.seek")
    {
        if (! movie::finiteNumber(a["time"]))
            return Result::failure("invalid_time", "A finite 'time' in seconds is required.");
        playback.seek((double) a["time"]);
        refresh();
        return Result::success(state(), { "Seeked playhead" });
    }
    if (name == "movie.transport.step")
    {
        const int frames = (int) a.getProperty("frames", 1);
        playback.play(false);
        playback.seek(document.sequence.snap(playback.position() + frames * document.sequence.frameSeconds()));
        refresh();
        return Result::success(state(), { "Stepped playhead" });
    }

    if (name == "movie.select")
    {
        if(a["trackId"].toString().isNotEmpty())selectTrack(a["trackId"].toString(),false);
        if (a["clipId"].toString().isNotEmpty())
            selectClip(a["clipId"].toString());
        if (a["assetId"].toString().isNotEmpty())
            selectMedia(a["assetId"].toString());
        return Result::success(state());
    }

    if (name == "movie.export")
    {
        if (document.sequence.duration() <= 0.0)
            return Result::failure("empty_timeline", "Cannot export an empty timeline.");

        movie::ExportOptions opts;
        opts.destination = juce::File(a["path"].toString());
        opts.preset = a.getProperty("preset", "mp4").toString();
        opts.width = (int) a.getProperty("width", 0);
        opts.height = (int) a.getProperty("height", 0);
        opts.overwrite = (bool) a.getProperty("overwrite", false);

        if (opts.destination.getFullPathName().trim().isEmpty())
            return Result::failure("invalid_destination", "Export destination path is required.");
        if (! opts.destination.getParentDirectory().isDirectory())
            return Result::failure("missing_folder", "Export destination directory does not exist.");
        if (opts.destination.exists() && ! opts.overwrite)
            return Result::failure("destination_exists", "Destination file already exists and overwrite is false.");

        const auto seqSnapshot = document.sequence;
        return startJob("export",
            [seqSnapshot, opts](Job& j) -> juce::var
            {
                movie::exportSequence(seqSnapshot, opts, j.cancel, [&j](double p) { j.progress = p; });
                auto verifyStream = opts.destination.createInputStream();
                if (! verifyStream)
                    throw std::runtime_error("Exported file could not be reopened for verification.");
                movie::MediaReader verifyProbe(std::move(verifyStream), opts.destination.getFileName(),
                                               movie::MediaReader::Kind::probe, &j.cancel);
                return movie::object({
                    { "path", opts.destination.getFullPathName() },
                    { "preset", opts.preset },
                    { "bytes", (juce::int64) opts.destination.getSize() },
                    { "info", verifyProbe.info().toJson() }
                });
            });
    }

    return Result::failure("unknown_command", "Unknown Movie command: " + name);
}

void MainComponent::registerCommands()
{
    using creation::agent::Effect;
    using creation::agent::ToolDefinition;

    engineer.addStateDomain({
        "movie.sequence",
        [this] { return document.sequence.toJson(); },
        [this](const juce::var& v) { document.restore(v); }
    });

    engineer.appContext = [this](const juce::String&)
    {
        return "Project: " + document.sequence.name
             + " | Media: " + juce::String(document.sequence.media.size())
             + " | Clips: " + juce::String(document.sequence.clips.size())
             + " | Duration: " + juce::String(document.sequence.duration(), 2) + "s"
             + " | Playhead: " + juce::String(playback.position(), 2) + "s"
             + " | Job: " + (job ? (job->type + ":" + job->status) : "idle");
    };

    engineer.approver = [this](const creation::agent::ApprovalRequest& req, std::function<void(bool)> decide)
    {
        juce::AlertWindow::showOkCancelBox(
            juce::MessageBoxIconType::QuestionIcon,
            "Confirm " + req.toolTitle,
            req.description + "\n\nArguments:\n" + req.arguments,
            "Allow", "Cancel", this,
            juce::ModalCallbackFunction::create([decide = std::move(decide)](int r) { decide(r == 1); }));
    };

    auto reg = [this](const juce::String& name, const juce::String& titleText, const juce::String& desc,
                      Effect eff, juce::var schema)
    {
        juce::String err;
        auto handler = [this, name](const juce::var& args, std::function<void(Result)> done)
        {
            done(execute(name, args));
        };
        engineer.addTool({ name, titleText, desc, schema, eff }, handler, err);
        api.addAppCommand(name, desc, schema, handler);
    };

    reg("movie.state.get", "Get Movie State", "Returns sequence, transport, selection, and background job state.",
        Effect::read, schemaObj({}));
    reg("movie.jobs.get", "Get Background Jobs", "Returns the current or most recent background media job status.",
        Effect::read, schemaObj({}));
    reg("movie.track.list", "List Tracks", "Returns tracks in display order.", Effect::read, schemaObj({}));
    reg("movie.effects.catalog", "Effects Catalog", "Returns audio/video effect and stable parameter metadata, ranges, units, defaults and automation eligibility.",Effect::read,schemaObj({}));
    auto effectSchema=schemaObj({{"trackId",movie::object({{"type","string"}})},{"effectId",movie::object({{"type","string"}})},
        {"type",movie::object({{"type","string"}})},{"parameterId",movie::object({{"type","string"}})},
        {"value",movie::object({{"type",juce::Array<juce::var>{"number","boolean"}}})},{"order",movie::object({{"type","integer"}})},
        {"gestureId",movie::object({{"type","string"}})}},{"trackId"});
    for(const auto* action:{"add","remove","reorder","parameter","enabled","bypass"})
        reg("movie.effect."+juce::String(action),"Effect "+juce::String(action),"Undoable track effect operation using stable owner/effect/parameter identities.",Effect::write,effectSchema);
    reg("movie.track.get", "Get Track", "Returns a track by stable identity.", Effect::read,
        schemaObj({{"trackId",movie::object({{"type","string"}})}},{"trackId"}));
    auto trackSchema=schemaObj({
        {"trackId",movie::object({{"type","string"}})}, {"name",movie::object({{"type","string"}})},
        {"type",movie::object({{"type","string"},{"enum",juce::var(juce::Array<juce::var>{"video","audio","image","effects","automation"})}})},
        {"order",movie::object({{"type","integer"}})}, {"enabled",movie::object({{"type","boolean"}})},
        {"removePlacements",movie::object({{"type","boolean"}})},
        {"source",movie::object({{"type",juce::var(juce::Array<juce::var>{"object","null"})}})},
        {"armed",movie::object({{"type","boolean"}})}, {"monitoring",movie::object({{"type","boolean"}})},
        {"height",movie::object({{"type","integer"}})}, {"automation",movie::object({{"type","object"}})}
    });
    for(const auto* operation:{"create","remove","rename","reorder","type","enabled","source","preferences","automation"})
        reg("movie.track."+juce::String(operation),"Track "+juce::String(operation),
            "Undoable track document operation. Sources and recording preferences configure the document only. See docs/TRACK_DOCUMENT.md.",
            juce::String(operation)=="remove"?Effect::destructive:Effect::write,trackSchema);
    reg("movie.app.close", "Close Movie", "Closes Movie after saving dirty edits. Refuses while a job is active; a failed save keeps the app open and the document dirty.",
        Effect::external, schemaObj({}));
    reg("movie.job.cancel", "Cancel Background Job", "Requests cancellation of the active job. Inspect its terminal status: already committed writes remain completed.",
        Effect::write, schemaObj({}));

    reg("movie.project.new", "New Movie Project", "Creates a new VFS Movie project in a background job. Wait for job completion; unsaved edits require saving or explicit discardChanges.",
        Effect::destructive, schemaObj({
            { "name", movie::object({ { "type", "string" } }) },
            { "width", movie::object({ { "type", "integer" } }) },
            { "height", movie::object({ { "type", "integer" } }) },
            { "fpsNum", movie::object({ { "type", "integer" } }) },
            { "fpsDen", movie::object({ { "type", "integer" } }) },
            { "discardChanges", movie::object({ { "type", "boolean" } }) }
        }));
    reg("movie.project.open", "Open Movie Project", "Opens a VFS Movie project in a background job. Wait for completion; a failed open preserves the current document. Unsaved edits require saving or explicit discardChanges.",
        Effect::destructive, schemaObj({ { "projectId", movie::object({ { "type", "string" } }) },
            { "discardChanges", movie::object({ { "type", "boolean" } }) } }, { "projectId" }));
    reg("movie.project.save", "Save Movie Project", "Saves the sequence in a background job. Wait for movie.jobs.get to report completed; a failed save keeps the document dirty and the app open.",
        Effect::external, schemaObj({}));

    reg("movie.media.import", "Import Media File",
        "Schedules a background job to probe and import an external media file into the project VFS.",
        Effect::external, schemaObj({ { "path", movie::object({ { "type", "string" } }) } }, { "path" }));

    reg("movie.timeline.place", "Place Media on Timeline", "Places an imported media asset onto the sequence timeline.",
        Effect::write, schemaObj({
            { "assetId", movie::object({ { "type", "string" } }) },
            { "start", movie::object({ { "type", "number" } }) },
            { "duration", movie::object({ { "type", "number" } }) },
            { "sourceIn", movie::object({ { "type", "number" } }) },
            { "track", movie::object({ { "type", "integer" } }) },
            { "trackId", movie::object({ { "type", "string" } }) }
        }, { "assetId" }));

    reg("movie.timeline.title", "Add Title Clip", "Adds a title overlay clip onto the timeline.",
        Effect::write, schemaObj({
            { "text", movie::object({ { "type", "string" } }) },
            { "start", movie::object({ { "type", "number" } }) },
            { "duration", movie::object({ { "type", "number" } }) },
            { "track", movie::object({ { "type", "integer" } }) },
            { "trackId", movie::object({ { "type", "string" } }) }
        }, { "text" }));

    reg("movie.timeline.split", "Split Timeline Clip",
        "Splits a timeline clip at the specified sequence time while preserving exact source media offsets.",
        Effect::write, schemaObj({
            { "clipId", movie::object({ { "type", "string" } }) },
            { "time", movie::object({ { "type", "number" } }) }
        }, { "clipId", "time" }));

    reg("movie.timeline.trim", "Trim Timeline Clip", "Updates a clip's sourceIn and duration.",
        Effect::write, schemaObj({
            { "clipId", movie::object({ { "type", "string" } }) },
            { "sourceIn", movie::object({ { "type", "number" } }) },
            { "duration", movie::object({ { "type", "number" } }) }
        }, { "clipId" }));

    reg("movie.timeline.move", "Move Timeline Clip", "Moves a clip to a new start time and/or track.",
        Effect::write, schemaObj({
            { "clipId", movie::object({ { "type", "string" } }) },
            { "start", movie::object({ { "type", "number" } }) },
            { "track", movie::object({ { "type", "integer" } }) },
            { "trackId", movie::object({ { "type", "string" } }) }
        }, { "clipId", "start" }));

    reg("movie.timeline.remove", "Remove Timeline Clip", "Removes a clip (with optional ripple delete).",
        Effect::destructive, schemaObj({
            { "clipId", movie::object({ { "type", "string" } }) },
            { "ripple", movie::object({ { "type", "boolean" } }) }
        }, { "clipId" }));

    reg("movie.timeline.levels", "Set Clip Gain and Opacity", "Adjusts audio gain (0..4) and video opacity (0..1).",
        Effect::write, schemaObj({
            { "clipId", movie::object({ { "type", "string" } }) },
            { "gain", movie::object({ { "type", "number" } }) },
            { "opacity", movie::object({ { "type", "number" } }) }
        }, { "clipId" }));

    reg("movie.timeline.undo", "Undo Timeline Edit", "Undoes the last timeline edit.", Effect::write, schemaObj({}));
    reg("movie.timeline.redo", "Redo Timeline Edit", "Redoes the last undone timeline edit.", Effect::write, schemaObj({}));

    reg("movie.transport.play", "Play Transport", "Starts synchronized sequence playback.", Effect::write, schemaObj({}));
    reg("movie.transport.pause", "Pause Transport", "Pauses sequence playback.", Effect::write, schemaObj({}));
    reg("movie.transport.stop", "Stop Transport", "Stops playback and rewinds to 0.", Effect::write, schemaObj({}));
    reg("movie.transport.seek", "Seek Transport", "Moves the playhead to a position in seconds.",
        Effect::write, schemaObj({ { "time", movie::object({ { "type", "number" } }) } }, { "time" }));
    reg("movie.transport.step", "Step Frames", "Steps the playhead by a number of frames.",
        Effect::write, schemaObj({ { "frames", movie::object({ { "type", "integer" } }) } }));

    reg("movie.export", "Export Sequence",
        "Schedules a background job to render and encode the sequence to mp4, prores (.mov), or ffv1 (.mkv).",
        Effect::external, schemaObj({
            { "path", movie::object({ { "type", "string" } }) },
            { "preset", movie::object({ { "type", "string" } }) },
            { "width", movie::object({ { "type", "integer" } }) },
            { "height", movie::object({ { "type", "integer" } }) },
            { "overwrite", movie::object({ { "type", "boolean" } }) }
        }, { "path" }));

    api.addAppEndpoint("state", "Full Movie state including sequence, transport, and active job", [this] { return state(); });
    api.addAppEndpoint("jobs", "Background project/import/export job status; completed means its result has been applied", [this] { return jobs(); });
    api.addAppEndpoint("sequence", "Current Movie sequence JSON", [this] { return document.sequence.toJson(); });
}

void MainComponent::runUi(const juce::String& name, const juce::var& a)
{
    if ((name == "movie.project.new" || name == "movie.project.open") && document.dirty
        && ! static_cast<bool>(a.getProperty("discardChanges", false)) && job->status != "running")
    {
        confirmProjectChange(name, a);
        return;
    }
    const auto res = execute(name, a);
    if (! res.ok)
        warn(res.message);
    else if (! res.changes.isEmpty())
        lastStatus = res.changes[0];
    refresh();
}

void MainComponent::confirmProjectChange(const juce::String& name, const juce::var& a)
{
    const juce::Component::SafePointer<MainComponent> safe(this);
    juce::AlertWindow::showYesNoCancelBox(juce::MessageBoxIconType::QuestionIcon,
        "Unsaved Movie edits", "Save the current project before switching?",
        "Save", "Discard", "Cancel", this,
        juce::ModalCallbackFunction::create([safe, name, a](int choice)
        {
            if (!safe || safe->closing || choice == 0) return;
            if (choice == 1)
            {
                safe->afterSave = [safe, name, a] { if (safe) safe->runUi(name, a); };
                const auto result = safe->execute("movie.project.save", movie::object({}));
                if (!result.ok) { safe->afterSave = {}; safe->warn(result.message); }
            }
            else
            {
                auto* args = new juce::DynamicObject();
                if (auto* original = a.getDynamicObject()) args->getProperties() = original->getProperties();
                args->setProperty("discardChanges", true);
                safe->runUi(name, juce::var(args));
            }
        }));
}

void MainComponent::changed()
{
    if(document.sequence.findTrack(selectedTrack)<0)
        selectedTrack=document.sequence.tracks.empty()?juce::String():document.sequence.tracks.front().id;
    if(trackInspector)trackInspector->sync();
    if(effectsPanel)effectsPanel->sync();
    if(timeline)timeline->sync();
    playback.setSequence(document.sequence);
    lastPreviewTime = -1.0;
    playback.requestPreview(playback.position());
    if (inspector)
        inspector->syncFromSelection();
    refresh();
}

void MainComponent::refresh()
{
    const auto& seq = document.sequence;
    header.setProjectLabel("Project: " + seq.name + (document.dirty ? " *" : ""));
    header.setPlaybackVisualState(playback.playing(), false);
    timecode.setText(formatTimecode(playback.position(), seq.fpsNum, seq.fpsDen)
                     + " / " + formatTimecode(seq.duration(), seq.fpsNum, seq.fpsDen),
                     juce::dontSendNotification);

    juce::String statusLine = lastStatus;
    if (job && job->status == "running")
        statusLine = "Running " + job->type + " (" + juce::String((int) std::lround(job->progress.load() * 100.0)) + "%)";
    else if (playback.error().isNotEmpty())
        statusLine = "Playback error: " + playback.error();

    status.setText(statusLine, juce::dontSendNotification);
    header.setStatusText(statusLine);
    cancelJob.setEnabled(job && job->status == "running");

    if (library)       library->repaint();
    if (monitor)       monitor->repaint();
    if (sourceMonitor) sourceMonitor->repaint();
    if (timeline)      timeline->repaint();
    if (inspector)     inspector->repaint();
}

void MainComponent::selectClip(const juce::String& id)
{
    selectedClip = id;
    const int selectedIndex=document.sequence.findClip(id);
    if(selectedIndex>=0)selectedTrack=document.sequence.owner(document.sequence.clips[(size_t)selectedIndex]);
    if(trackInspector)trackInspector->sync();
    if(effectsPanel)effectsPanel->sync();
    if (inspector)
        inspector->syncFromSelection();
    if (timeline)
        timeline->repaint();
}

void MainComponent::selectMedia(const juce::String& id)
{
    selectedMedia = id;
    if (const auto* m = document.sequence.findMedia(id))
    {
        movie::Sequence single;
        single.projectId = document.sequence.projectId;
        single.name = m->name;
        single.width = m->info.width > 0 ? m->info.width : 1920;
        single.height = m->info.height > 0 ? m->info.height : 1080;
        single.fpsNum = m->info.fpsNum > 0 ? m->info.fpsNum : 30;
        single.fpsDen = m->info.fpsDen > 0 ? m->info.fpsDen : 1;
        single.media.push_back(*m);

        movie::Clip c;
        c.id = "source-preview";
        c.assetId = m->id;
        c.assetVersionId = m->versionId;
        c.displayName = m->name;
        c.trackIndex = 0;
        c.kind = m->info.hasVideo ? creation::timeline::ClipKind::video : creation::timeline::ClipKind::audio;
        c.startSeconds = 0.0;
        c.durationSeconds = m->info.duration;
        c.sourceStartSeconds = 0.0;
        c.sourceDurationSeconds = m->info.duration;
        single.clips.push_back(c);

        sourcePlayback.setSequence(single);
        sourcePlayback.requestPreview(0.0);
    }
    refresh();
}

juce::var MainComponent::state() const
{
    return movie::object({
        { "projectId", session.isValid() ? session.getProjectId() : juce::String() },
        { "dirty", document.dirty },
        { "canUndo", document.undo.canUndo() },
        { "canRedo", document.undo.canRedo() },
        { "selectedClip", selectedClip },
        { "selectedTrack", selectedTrack },
        { "selectedMedia", selectedMedia },
        { "codecVersion", movie::codecVersion() },
        { "transport", movie::object({
            { "playing", playback.playing() },
            { "position", playback.position() },
            { "duration", document.sequence.duration() },
            { "audioReady", playback.audioReady() },
            { "underruns", (juce::int64) playback.underruns() },
            { "error", playback.error() }
        }) },
        { "job", jobs() },
        { "sequence", document.sequence.toJson() }
    });
}

juce::var MainComponent::jobs() const
{
    if (! job)
        return movie::object({ { "status", "idle" } });
    return movie::object({
        { "id", job->id },
        { "type", job->type },
        { "projectId", job->projectId },
        { "status", job->status },
        { "progress", job->progress.load() },
        { "error", job->error },
        { "result", job->result }
    });
}

void MainComponent::chooseImport()
{
    chooser = std::make_unique<juce::FileChooser>(
        "Import Media Asset into Project VFS",
        juce::File(),
        "*.mp4;*.mov;*.mkv;*.webm;*.avi;*.wav;*.mp3;*.flac;*.ogg;*.aac;*.png;*.jpg;*.jpeg;*.bmp;*.webp");
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& fc)
        {
            const auto file = fc.getResult();
            if (file.existsAsFile())
                runUi("movie.media.import", movie::object({ { "path", file.getFullPathName() } }));
        });
}

void MainComponent::chooseExport()
{
    chooser = std::make_unique<juce::FileChooser>(
        "Export Movie Sequence",
        juce::File(),
        "*.mp4;*.mov;*.mkv");
    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (file == juce::File())
                return;
            juce::String preset = "mp4";
            if (file.hasFileExtension(".mov")) preset = "prores";
            else if (file.hasFileExtension(".mkv")) preset = "ffv1";
            else if (! file.hasFileExtension(".mp4")) file = file.withFileExtension(".mp4");

            runUi("movie.export", movie::object({
                { "path", file.getFullPathName() },
                { "preset", preset },
                { "overwrite", true }
            }));
        });
}

void MainComponent::newProjectDialog()
{
    auto* w = new juce::AlertWindow("New Movie Project", "Enter a name for the new VFS project:", juce::MessageBoxIconType::NoIcon);
    w->addTextEditor("name", "Untitled Movie", "Project Name:");
    w->addButton("Create", 1, juce::KeyPress(juce::KeyPress::returnKey));
    w->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    w->enterModalState(true, juce::ModalCallbackFunction::create([this, w](int r)
    {
        if (r == 1)
            runUi("movie.project.new", movie::object({ { "name", w->getTextEditorContents("name") } }));
        delete w;
    }), false);
}

void MainComponent::openProjectMenu()
{
    juce::PopupMenu m;
    m.addItem(1, "New Project...");
    m.addItem(2, "Save Project", session.isValid());
    m.addSeparator();

    juce::String err;
    const auto list = creation::assets::ProjectContainerService::listProjects(settings, err);
    for (int i = 0; i < list.size(); ++i)
    {
        const auto& p = list.getReference(i);
        m.addItem(100 + i, p.manifest.projectName + " (" + p.projectId.substring(0, 8) + ")",
                  true, session.isValid() && session.getProjectId() == p.projectId);
    }

    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(header.projectButton),
        [this, list](int res)
        {
            if (res == 1) newProjectDialog();
            else if (res == 2) runUi("movie.project.save");
            else if (res >= 100 && res - 100 < list.size())
                runUi("movie.project.open", movie::object({ { "projectId", list[res - 100].projectId } }));
        });
}

void MainComponent::titleDialog()
{
    auto* w = new juce::AlertWindow("Add Title Overlay", "Enter title text:", juce::MessageBoxIconType::NoIcon);
    w->addTextEditor("text", "Title Text", "Text:");
    w->addTextEditor("duration", "3.0", "Duration (s):");
    w->addButton("Add", 1, juce::KeyPress(juce::KeyPress::returnKey));
    w->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    w->enterModalState(true, juce::ModalCallbackFunction::create([this, w](int r)
    {
        if (r == 1)
        {
            runUi("movie.timeline.title", movie::object({
                { "text", w->getTextEditorContents("text") },
                { "trackId", selectedTrack },
                { "duration", w->getTextEditorContents("duration").getDoubleValue() },
                { "start", playback.position() }
            }));
        }
        delete w;
    }), false);
}

void MainComponent::showSettings()
{
    if (settingsWindow != nullptr)
    {
        settingsWindow->toFront(true);
        return;
    }

    class ClosableWindow final : public juce::DocumentWindow
    {
    public:
        ClosableWindow(const juce::String& t, std::function<void()> onClose)
            : juce::DocumentWindow(t, juce::Colour(0xff0f172a), juce::DocumentWindow::closeButton),
              closeFn(std::move(onClose))
        {
            setUsingNativeTitleBar(true);
        }
        void closeButtonPressed() override { closeFn(); }
    private:
        std::function<void()> closeFn;
    };

    auto panel = std::make_unique<SuiteSettingsPanel>();
    panel->setSettings(settings);
    panel->onApplyRequested = [this](const creation::suite::SuiteSettings& updated)
    {
        juce::String err;
        creation::suite::SuiteSettingsStore store;
        if (store.save(updated, err))
            settings = updated;
    };
    panel->onReadEulaRequested = [this] { showHelp(); };

    settingsWindow = std::make_unique<ClosableWindow>("Djehuti Suite Settings", [this] { settingsWindow.reset(); });
    settingsWindow->setContentOwned(panel.release(), true);
    settingsWindow->centreWithSize(760, 560);
    settingsWindow->setResizable(true, true);
    settingsWindow->setVisible(true);
}

void MainComponent::showHelp()
{
    if (helpWindow != nullptr)
    {
        helpWindow->toFront(true);
        return;
    }

    class ClosableWindow final : public juce::DocumentWindow
    {
    public:
        ClosableWindow(const juce::String& t, std::function<void()> onClose)
            : juce::DocumentWindow(t, juce::Colour(0xff0f172a), juce::DocumentWindow::closeButton),
              closeFn(std::move(onClose))
        {
            setUsingNativeTitleBar(true);
        }
        void closeButtonPressed() override { closeFn(); }
    private:
        std::function<void()> closeFn;
    };

    auto panel = std::make_unique<SuiteEulaPanel>();

    helpWindow = std::make_unique<ClosableWindow>("Djehuti Movie EULA & Licensing", [this] { helpWindow.reset(); });
    helpWindow->setContentOwned(panel.release(), true);
    helpWindow->centreWithSize(720, 540);
    helpWindow->setResizable(true, true);
    helpWindow->setVisible(true);
}

void MainComponent::warn(const juce::String& msg)
{
    lastStatus = msg;
    refresh();
}

void MainComponent::timerCallback()
{
    const double pos = playback.position();
    if (std::abs(pos - lastPreviewTime) > 1e-4)
    {
        lastPreviewTime = pos;
        const int w = monitor ? std::max(320, monitor->getWidth() * 2) : 960;
        const int h = monitor ? std::max(180, monitor->getHeight() * 2) : 540;
        playback.requestPreview(pos, w, h);
    }
    if (sourcePlayback.playing())
        sourcePlayback.requestPreview(sourcePlayback.position());

    if (document.dirty && session.isValid() && !playback.playing()
        && job->status != "running" && (++autosaveTicks >= 1800))
    {
        autosaveTicks = 0;
        execute("movie.project.save", movie::object({}));
    }
    refresh();
}

juce::StringArray MainComponent::getMenuBarNames()
{
    return { "File", "Edit", "Timeline", "View", "Help" };
}

juce::PopupMenu MainComponent::getMenuForIndex(int, const juce::String& menuName)
{
    juce::PopupMenu m;
    if (menuName == "File")
    {
        m.addItem(1, "New Project...");
        m.addItem(2, "Open Project...");
        m.addItem(3, "Save Project", session.isValid());
        m.addSeparator();
        m.addItem(4, "Import Media...", session.isValid());
        m.addItem(5, "Export Sequence...", document.sequence.duration() > 0.0);
        m.addSeparator();
        m.addItem(6, "Suite Settings...");
        m.addSeparator();
        m.addItem(7, "Quit");
    }
    else if (menuName == "Edit")
    {
        m.addItem(10, "Undo", document.undo.canUndo());
        m.addItem(11, "Redo", document.undo.canRedo());
    }
    else if (menuName == "Timeline")
    {
        m.addItem(20, "Place Selected Media at Playhead", selectedMedia.isNotEmpty());
        m.addItem(21, "Add Title Overlay...");
        m.addItem(22, "Split Selected Clip at Playhead", selectedClip.isNotEmpty());
        m.addItem(23, "Delete Selected Clip", selectedClip.isNotEmpty());
        m.addItem(24, "Ripple Delete Selected Clip", selectedClip.isNotEmpty());
    }
    else if (menuName == "View")
    {
        m.addItem(30, "Media Library");
        m.addItem(31, "Source Monitor");
        m.addItem(32, "Program Monitor");
        m.addItem(33, "Timeline");
        m.addItem(34, "Inspector");
        m.addItem(35, "Virtual Engineer");
        m.addItem(36, "Cards");
        m.addItem(38, "Track Inspector");
        m.addItem(39, "Effects Browser / Inspector");
        m.addSeparator();
        m.addItem(37, "Reset Workspace Layout");
    }
    else if (menuName == "Help")
    {
        m.addItem(40, "EULA & FFmpeg Licensing...");
    }
    return m;
}

void MainComponent::menuItemSelected(int id, int)
{
    switch (id)
    {
        case 1: newProjectDialog(); break;
        case 2: openProjectMenu(); break;
        case 3: runUi("movie.project.save"); break;
        case 4: chooseImport(); break;
        case 5: chooseExport(); break;
        case 6: showSettings(); break;
        case 7: requestQuit(); break;
        case 10: runUi("movie.timeline.undo"); break;
        case 11: runUi("movie.timeline.redo"); break;
        case 20: runUi("movie.timeline.place", movie::object({ { "assetId", selectedMedia }, { "trackId", selectedTrack }, { "start", playback.position() } })); break;
        case 21: titleDialog(); break;
        case 22: runUi("movie.timeline.split", movie::object({ { "clipId", selectedClip }, { "time", playback.position() } })); break;
        case 23: runUi("movie.timeline.remove", movie::object({ { "clipId", selectedClip }, { "ripple", false } })); break;
        case 24: runUi("movie.timeline.remove", movie::object({ { "clipId", selectedClip }, { "ripple", true } })); break;
        case 30: if (dock) dock->activatePanel("library"); break;
        case 31: if (dock) dock->activatePanel("source"); break;
        case 32: if (dock) dock->activatePanel("program"); break;
        case 33: if (dock) dock->activatePanel("timeline"); break;
        case 34: if (dock) dock->activatePanel("inspector"); break;
        case 35: if (dock) dock->activatePanel("engineer"); break;
        case 36: if (dock) dock->activatePanel("cards"); break;
        case 38: if (dock) dock->activatePanel("track-inspector"); break;
        case 39: if (dock) dock->activatePanel("effects"); break;
        case 37: if (dock) dock->resetLayout(); break;
        case 40: showHelp(); break;
        default: break;
    }
}
