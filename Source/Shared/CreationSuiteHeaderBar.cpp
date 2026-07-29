#include "CreationSuiteHeaderBar.h"

namespace
{
class TransportButtonLookAndFeel final : public juce::LookAndFeel_V4
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
        if (ring.getWidth() > 18.0f && ring.getHeight() > 18.0f)
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

        g.setColour(button.getToggleState() ? juce::Colours::white
                                            : (isButtonDown ? juce::Colour(0xffeaf6ff)
                                                            : isMouseOverButton ? juce::Colour(0xffdcecff)
                                                                                : juce::Colour(0xffb8c4d5)));
        drawTransportIcon(g, bounds, button.getButtonText());
    }

    void drawToggleButton(juce::Graphics& g,
                          juce::ToggleButton& button,
                          bool isMouseOverButton,
                          bool isButtonDown) override
    {
        drawButtonBackground(g,
                             button,
                             button.findColour(juce::TextButton::buttonColourId),
                             isMouseOverButton,
                             isButtonDown);

        auto bounds = button.getLocalBounds().toFloat().reduced(8.0f, 7.0f);
        g.setColour(button.getToggleState() ? juce::Colour(0xff5ce8ff) : juce::Colour(0xffb8c4d5));
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

        if (iconName == "pause")
        {
            auto barWidth = size * 0.16f;
            auto barHeight = size * 0.58f;
            auto leftBar = juce::Rectangle<float>(barWidth, barHeight).withCentre({ centre.x - size * 0.12f, centre.y });
            auto rightBar = juce::Rectangle<float>(barWidth, barHeight).withCentre({ centre.x + size * 0.12f, centre.y });
            g.fillRoundedRectangle(leftBar, 1.5f);
            g.fillRoundedRectangle(rightBar, 1.5f);
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

        if (iconName == "loop")
        {
            auto arc = bounds.reduced(size * 0.12f);
            g.drawEllipse(arc, 2.0f);
            juce::Path arrow;
            arrow.addTriangle(arc.getRight() - size * 0.02f, arc.getCentreY() - size * 0.20f,
                              arc.getRight() + size * 0.16f, arc.getCentreY() - size * 0.06f,
                              arc.getRight() - size * 0.02f, arc.getCentreY() + size * 0.08f);
            g.fillPath(arrow);
            return;
        }

        if (iconName == "click")
        {
            auto circle = bounds.reduced(size * 0.18f);
            g.drawEllipse(circle, 2.0f);
            auto point = circle.withSizeKeepingCentre(size * 0.16f, size * 0.16f);
            g.fillEllipse(point);
            return;
        }

        if (iconName == "prev" || iconName == "next")
        {
            const auto flip = iconName == "prev" ? -1.0f : 1.0f;
            juce::Path path;
            path.addTriangle(centre.x - flip * size * 0.04f, centre.y,
                             centre.x + flip * size * 0.20f, centre.y - size * 0.24f,
                             centre.x + flip * size * 0.20f, centre.y + size * 0.24f);
            path.addTriangle(centre.x - flip * size * 0.30f, centre.y,
                             centre.x - flip * size * 0.06f, centre.y - size * 0.24f,
                             centre.x - flip * size * 0.06f, centre.y + size * 0.24f);
            g.fillPath(path);
            return;
        }
    }
};

TransportButtonLookAndFeel& getTransportButtonLookAndFeel()
{
    static TransportButtonLookAndFeel lookAndFeel;
    return lookAndFeel;
}
}

CreationSuiteHeaderBar::CreationSuiteHeaderBar()
{
    setName("Transport");

    titleLabel.setText("Creation Movie", juce::dontSendNotification);
    titleLabel.setJustificationType(juce::Justification::centredLeft);
    titleLabel.setFont(juce::Font(28.0f).boldened());
    titleLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(titleLabel);

    midiStatusLabel.setVisible(false);
    midiStatusLabel.setColour(juce::Label::textColourId, juce::Colour(0xff8ea0b7));
    addAndMakeVisible(midiStatusLabel);

    projectButton.setButtonText("Project: Untitled Movie");
    projectButton.setTooltip("Open the project menu");
    addAndMakeVisible(projectButton);

    playButton.setLookAndFeel(&getTransportButtonLookAndFeel());
    pauseButton.setLookAndFeel(&getTransportButtonLookAndFeel());
    stopButton.setLookAndFeel(&getTransportButtonLookAndFeel());
    recordButton.setLookAndFeel(&getTransportButtonLookAndFeel());
    loopButton.setLookAndFeel(&getTransportButtonLookAndFeel());
    clickButton.setLookAndFeel(&getTransportButtonLookAndFeel());
    rewindButton.setLookAndFeel(&getTransportButtonLookAndFeel());
    fastForwardButton.setLookAndFeel(&getTransportButtonLookAndFeel());

    recordButton.setName("recordButton");
    playButton.setButtonText("play");
    pauseButton.setButtonText("pause");
    stopButton.setButtonText("stop");
    recordButton.setButtonText("record");
    loopButton.setButtonText("loop");
    clickButton.setButtonText("click");
    rewindButton.setButtonText("prev");
    fastForwardButton.setButtonText("next");

    playButton.setTooltip("Play");
    pauseButton.setTooltip("Pause");
    stopButton.setTooltip("Stop");
    recordButton.setTooltip("Record");
    loopButton.setTooltip("Loop");
    clickButton.setTooltip("Click");
    rewindButton.setTooltip("Rewind");
    fastForwardButton.setTooltip("Fast forward");

    auto configureTransportButton = [this](juce::Button& button)
    {
        button.setMouseClickGrabsKeyboardFocus(false);
        addAndMakeVisible(button);
    };

    configureTransportButton(playButton);
    configureTransportButton(pauseButton);
    configureTransportButton(stopButton);
    configureTransportButton(recordButton);
    configureTransportButton(loopButton);
    configureTransportButton(clickButton);
    configureTransportButton(rewindButton);
    configureTransportButton(fastForwardButton);

    playButton.addMouseListener(this, false);
    stopButton.addMouseListener(this, false);
    recordButton.addMouseListener(this, false);
    rewindButton.addMouseListener(this, false);
    fastForwardButton.addMouseListener(this, false);

    auto callback = [](auto& fn)
    {
        if (fn)
            fn();
    };

    playButton.onClick = [this, callback]
    {
        statusLabel.setText("Transport: play", juce::dontSendNotification);
        callback(onPlay);
    };
    pauseButton.onClick = [this, callback]
    {
        statusLabel.setText("Transport: pause", juce::dontSendNotification);
        callback(onPause);
    };
    stopButton.onClick = [this, callback]
    {
        statusLabel.setText("Transport: stop", juce::dontSendNotification);
        callback(onStop);
    };
    recordButton.onClick = [this, callback]
    {
        statusLabel.setText("Transport: record armed", juce::dontSendNotification);
        callback(onRecord);
    };
    rewindButton.onClick = [this, callback]
    {
        statusLabel.setText("Transport: rewind", juce::dontSendNotification);
        callback(onRewind);
    };
    fastForwardButton.onClick = [this, callback]
    {
        statusLabel.setText("Transport: fast forward", juce::dontSendNotification);
        callback(onFastForward);
    };
    loopButton.setClickingTogglesState(true);
    loopButton.onClick = [this]
    {
        if (onLoopChanged)
            onLoopChanged(loopButton.getToggleState());
    };
    clickButton.setClickingTogglesState(true);
    clickButton.onClick = [this]
    {
        if (onClickChanged)
            onClickChanged(clickButton.getToggleState());
    };

    signInButton.onClick = [this, callback] { callback(onSignInRequested); };
    signInButton.setTooltip("Sign in to sync your account");
    addAndMakeVisible(signInButton);

    projectButton.onClick = [this, callback] { callback(onProjectMenuRequested); };

    audioButton.onClick = [this, callback] { callback(onAudioRequested); };
    audioButton.setTooltip("Open audio settings");
    addAndMakeVisible(audioButton);

    suiteButton.onClick = [this, callback] { callback(onSuiteRequested); };
    suiteButton.setTooltip("Open suite settings");
    addAndMakeVisible(suiteButton);

    tourButton.onClick = [this, callback] { callback(onTourRequested); };
    tourButton.setTooltip("Open guidance");
    addAndMakeVisible(tourButton);

    profileNameLabel.setJustificationType(juce::Justification::centredLeft);
    profileNameLabel.setFont(juce::Font(17.0f).boldened());
    profileNameLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(profileNameLabel);

    profileDetailLabel.setJustificationType(juce::Justification::centredLeft);
    profileDetailLabel.setFont(juce::Font(13.0f));
    profileDetailLabel.setColour(juce::Label::textColourId, juce::Colour(0xff9fb0c8));
    addAndMakeVisible(profileDetailLabel);

    statusLabel.setText("Ready.", juce::dontSendNotification);
    statusLabel.setJustificationType(juce::Justification::centredRight);
    statusLabel.setColour(juce::Label::textColourId, juce::Colour(0xff99a6b8));
    addAndMakeVisible(statusLabel);

    scrubModeLabel.setText("SCRUB", juce::dontSendNotification);
    scrubModeLabel.setJustificationType(juce::Justification::centred);
    scrubModeLabel.setColour(juce::Label::textColourId, juce::Colour(0xff11151c));
    scrubModeLabel.setColour(juce::Label::backgroundColourId, juce::Colour(0xfff5c96a));
    scrubModeLabel.setVisible(false);
    addAndMakeVisible(scrubModeLabel);

    clearProfile();
    setScrubModeEnabled(false);
    setPlaybackVisualState(false, false);
}

CreationSuiteHeaderBar::~CreationSuiteHeaderBar()
{
    playButton.setLookAndFeel(nullptr);
    pauseButton.setLookAndFeel(nullptr);
    stopButton.setLookAndFeel(nullptr);
    recordButton.setLookAndFeel(nullptr);
    loopButton.setLookAndFeel(nullptr);
    clickButton.setLookAndFeel(nullptr);
    rewindButton.setLookAndFeel(nullptr);
    fastForwardButton.setLookAndFeel(nullptr);
}

void CreationSuiteHeaderBar::setAppTitle(const juce::String& title)
{
    titleLabel.setText(title, juce::dontSendNotification);
}

void CreationSuiteHeaderBar::setLogoImage(const juce::Image& image)
{
    logoImage = image;
    repaint();
}

void CreationSuiteHeaderBar::setProfile(const ProfileData& profile)
{
    profileVisible = true;
    profileNameLabel.setVisible(true);
    profileDetailLabel.setVisible(true);
    signInButton.setVisible(false);

    profileNameLabel.setText(profile.displayName, juce::dontSendNotification);
    profileDetailLabel.setText(profile.detailText, juce::dontSendNotification);
    profileBadgeImage = profile.badgeImage;
    profileInitials = makeInitials(profile.displayName, profile.detailText);
    repaint();
}

void CreationSuiteHeaderBar::clearProfile()
{
    profileVisible = false;
    profileNameLabel.setText({}, juce::dontSendNotification);
    profileDetailLabel.setText({}, juce::dontSendNotification);
    profileBadgeImage = {};
    profileInitials.clear();
    profileNameLabel.setVisible(false);
    profileDetailLabel.setVisible(false);
    signInButton.setVisible(true);
    repaint();
}

void CreationSuiteHeaderBar::setProjectLabel(const juce::String& label)
{
    projectText = label;
    projectButton.setButtonText(label);
}

juce::Rectangle<int> CreationSuiteHeaderBar::getProjectButtonScreenBounds() const
{
    return localAreaToGlobal(projectButton.getBounds());
}

void CreationSuiteHeaderBar::setStatusText(const juce::String& text)
{
    statusLabel.setText(text, juce::dontSendNotification);
}

void CreationSuiteHeaderBar::setMidiStatusText(const juce::String& text)
{
    midiStatusLabel.setText(text, juce::dontSendNotification);
    midiStatusLabel.setVisible(text.isNotEmpty());
}

void CreationSuiteHeaderBar::setPlaybackVisualState(bool playing, bool recording)
{
    playbackIsPlaying = playing;
    playbackIsRecording = recording;
    refreshTransportButtonPresentation();
}

void CreationSuiteHeaderBar::setScrubModeEnabled(bool enabled)
{
    scrubModeEnabled = enabled;
    repaint();
}

void CreationSuiteHeaderBar::setTransportButtonVisible(TransportButtonSlot slot, bool shouldBeVisible)
{
    auto& config = getTransportButtonConfig(slot);
    config.visible = shouldBeVisible;
    refreshTransportButtonPresentation();
    resized();
}

void CreationSuiteHeaderBar::setTransportButtonEnabled(TransportButtonSlot slot, bool shouldBeEnabled)
{
    auto& config = getTransportButtonConfig(slot);
    config.enabled = shouldBeEnabled;
    refreshTransportButtonPresentation();
}

void CreationSuiteHeaderBar::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff0f1115));
    if (logoImage.isValid())
        g.drawImageWithin(logoImage, 14, 9, 44, 44, juce::RectanglePlacement::centred, false);

    g.setColour(juce::Colour(0xff242a36));
    g.drawLine(0.0f, (float) getHeight() - 1.0f, (float) getWidth(), (float) getHeight() - 1.0f, 1.0f);

    if (! transportControlBounds.isEmpty())
    {
        auto panel = transportControlBounds.toFloat().expanded(12.0f, 9.0f);
        auto accentGlowTop = scrubModeEnabled ? juce::Colour(0x55ff5b6e) : juce::Colour(0x4426d9ff);
        auto accentGlowBottom = scrubModeEnabled ? juce::Colour(0x001f0f14) : juce::Colour(0x00101820);
        juce::ColourGradient glow(accentGlowTop, panel.getCentreX(), panel.getY(),
                                  accentGlowBottom, panel.getCentreX(), panel.getBottom(), false);
        g.setGradientFill(glow);
        g.fillRoundedRectangle(panel.expanded(4.0f), 18.0f);

        g.setColour(juce::Colour(0xff151b23));
        g.fillRoundedRectangle(panel, 16.0f);
        g.setColour(scrubModeEnabled ? juce::Colour(0xffff5b6e) : juce::Colour(0xff36506b));
        g.drawRoundedRectangle(panel, 16.0f, scrubModeEnabled ? 2.6f : 1.4f);
    }

    if (profileVisible)
    {
        auto chip = profileChipBounds.toFloat();
        g.setColour(juce::Colour(0xff151b23));
        g.fillRoundedRectangle(chip, 16.0f);
        g.setColour(juce::Colour(0xff2c394c));
        g.drawRoundedRectangle(chip, 16.0f, 1.0f);

        auto avatar = chip.removeFromLeft(44).reduced(4);
        g.setColour(juce::Colour(0xff223041));
        g.fillEllipse(avatar);
        g.setColour(juce::Colour(0xff8ea0b7));
        g.setFont(juce::Font(15.0f).boldened());
        g.drawText(profileInitials, avatar.toNearestInt(), juce::Justification::centred, false);

        auto badgeArea = chip.removeFromRight(38).withSizeKeepingCentre(28.0f, 28.0f);
        if (profileBadgeImage.isValid())
            g.drawImageWithin(profileBadgeImage, (int) badgeArea.getX(), (int) badgeArea.getY(), (int) badgeArea.getWidth(), (int) badgeArea.getHeight(),
                              juce::RectanglePlacement::centred, false);
    }
}

void CreationSuiteHeaderBar::mouseDown(const juce::MouseEvent& event)
{
    if (event.mods.isPopupMenu())
    {
        juce::String targetId;
        juce::String label;

        if (event.eventComponent == &rewindButton) { targetId = "transport_rewind"; label = "Rewind"; }
        else if (event.eventComponent == &fastForwardButton) { targetId = "transport_fast_forward"; label = "Fast Forward"; }
        else if (event.eventComponent == &playButton) { targetId = "transport_play"; label = "Play"; }
        else if (event.eventComponent == &stopButton) { targetId = "transport_stop"; label = "Stop"; }
        else if (event.eventComponent == &recordButton) { targetId = "transport_record"; label = "Record"; }

        if (targetId.isNotEmpty())
        {
            juce::PopupMenu menu;
            menu.addItem(1, "Learn MIDI Binding...");
            menu.showMenuAsync(juce::PopupMenu::Options(), [this, targetId, label](int result)
            {
                if (result == 1 && onLearnMidiRequested)
                    onLearnMidiRequested(targetId, label);
            });
            return;
        }
    }

    if (profileVisible && profileChipBounds.contains(event.getPosition()))
    {
        juce::PopupMenu menu;
        menu.addItem(1, "View profile page");
        menu.addSeparator();
        menu.addItem(2, "Log out");
        menu.showMenuAsync(juce::PopupMenu::Options(), [this](int result)
        {
            if (result == 1 && onOpenProfilePageRequested)
                onOpenProfilePageRequested();
            else if (result == 2 && onLogoutRequested)
                onLogoutRequested();
        });
    }
}

void CreationSuiteHeaderBar::resized()
{
    auto area = getLocalBounds().reduced(18, 10);
    area.removeFromLeft(56);
    auto profileArea = area.removeFromRight(268);

    auto topRow = area.removeFromTop(30);
    auto bottomRow = area;

    titleLabel.setBounds(topRow.removeFromLeft(520));
    midiStatusLabel.setBounds(topRow.withTrimmedLeft(10));

    auto transportRow = bottomRow.removeFromLeft(662);
    projectButton.setBounds(bottomRow.removeFromLeft(260));
    audioButton.setBounds(bottomRow.removeFromLeft(82));
    suiteButton.setBounds(bottomRow.removeFromLeft(54));
    tourButton.setBounds(bottomRow.removeFromLeft(78));
    statusLabel.setBounds(bottomRow.removeFromRight(220));

    auto placeTransportButton = [&transportRow](juce::Button& button, int width, bool isVisible)
    {
        if (! isVisible)
        {
            button.setBounds({});
            button.setVisible(false);
            return;
        }

        button.setBounds(transportRow.removeFromLeft(width));
        transportRow.removeFromLeft(7);
        button.setVisible(true);
    };

    placeTransportButton(rewindButton, 62, rewindButtonConfig.visible);
    placeTransportButton(fastForwardButton, 62, fastForwardButtonConfig.visible);
    placeTransportButton(stopButton, 66, stopButtonConfig.visible);
    placeTransportButton(playButton, 82, playPauseButtonConfig.visible);
    pauseButton.setBounds(playPauseButtonConfig.visible ? playButton.getBounds() : juce::Rectangle<int>());
    pauseButton.setVisible(playPauseButtonConfig.visible && playbackIsPlaying);
    placeTransportButton(loopButton, 64, loopButtonConfig.visible);
    placeTransportButton(clickButton, 64, clickButtonConfig.visible);
    placeTransportButton(recordButton, 82, recordButtonConfig.visible);

    auto combineBounds = [](juce::Rectangle<int> left, juce::Rectangle<int> right, bool useRight)
    {
        if (left.isEmpty())
            return useRight ? right : juce::Rectangle<int>();

        return useRight ? left.getUnion(right) : left;
    };

    transportControlBounds = {};
    transportControlBounds = combineBounds(transportControlBounds, rewindButton.getBounds(), rewindButtonConfig.visible);
    transportControlBounds = combineBounds(transportControlBounds, fastForwardButton.getBounds(), fastForwardButtonConfig.visible);
    transportControlBounds = combineBounds(transportControlBounds, stopButton.getBounds(), stopButtonConfig.visible);
    transportControlBounds = combineBounds(transportControlBounds, playButton.getBounds(), playPauseButtonConfig.visible);
    transportControlBounds = combineBounds(transportControlBounds, loopButton.getBounds(), loopButtonConfig.visible);
    transportControlBounds = combineBounds(transportControlBounds, clickButton.getBounds(), clickButtonConfig.visible);
    transportControlBounds = combineBounds(transportControlBounds, recordButton.getBounds(), recordButtonConfig.visible);

    auto profileContent = profileArea.reduced(12, 6);
    signInButton.setBounds(profileContent);

    auto profileTextArea = profileContent.withTrimmedLeft(48).withTrimmedRight(42);
    profileNameLabel.setBounds(profileTextArea.removeFromTop(24));
    profileDetailLabel.setBounds(profileTextArea.removeFromTop(18));
    profileChipBounds = profileArea;
    signInButton.setVisible(! profileVisible);
    profileNameLabel.setVisible(profileVisible);
    profileDetailLabel.setVisible(profileVisible);
}

juce::String CreationSuiteHeaderBar::makeInitials(const juce::String& displayName, const juce::String& detailText)
{
    auto source = displayName.trim();
    if (source.isEmpty())
        source = detailText.upToFirstOccurrenceOf("@", false, false);

    juce::StringArray parts;
    parts.addTokens(source, " _-", {});
    juce::String initials;
    for (auto part : parts)
    {
        if (part.isNotEmpty())
            initials << part.substring(0, 1).toUpperCase();
        if (initials.length() >= 2)
            break;
    }

    if (initials.isEmpty() && source.isNotEmpty())
        initials = source.substring(0, 1).toUpperCase();

    return initials;
}

CreationSuiteHeaderBar::TransportButtonConfig& CreationSuiteHeaderBar::getTransportButtonConfig(TransportButtonSlot slot)
{
    switch (slot)
    {
        case TransportButtonSlot::rewind: return rewindButtonConfig;
        case TransportButtonSlot::fastForward: return fastForwardButtonConfig;
        case TransportButtonSlot::stop: return stopButtonConfig;
        case TransportButtonSlot::playPause: return playPauseButtonConfig;
        case TransportButtonSlot::loop: return loopButtonConfig;
        case TransportButtonSlot::click: return clickButtonConfig;
        case TransportButtonSlot::record: return recordButtonConfig;
    }

    return playPauseButtonConfig;
}

const CreationSuiteHeaderBar::TransportButtonConfig& CreationSuiteHeaderBar::getTransportButtonConfig(TransportButtonSlot slot) const
{
    switch (slot)
    {
        case TransportButtonSlot::rewind: return rewindButtonConfig;
        case TransportButtonSlot::fastForward: return fastForwardButtonConfig;
        case TransportButtonSlot::stop: return stopButtonConfig;
        case TransportButtonSlot::playPause: return playPauseButtonConfig;
        case TransportButtonSlot::loop: return loopButtonConfig;
        case TransportButtonSlot::click: return clickButtonConfig;
        case TransportButtonSlot::record: return recordButtonConfig;
    }

    return playPauseButtonConfig;
}

void CreationSuiteHeaderBar::refreshTransportButtonPresentation()
{
    rewindButton.setEnabled(rewindButtonConfig.enabled);
    fastForwardButton.setEnabled(fastForwardButtonConfig.enabled);
    stopButton.setEnabled(stopButtonConfig.enabled);
    playButton.setEnabled(playPauseButtonConfig.enabled);
    pauseButton.setEnabled(playPauseButtonConfig.enabled);
    loopButton.setEnabled(loopButtonConfig.enabled);
    clickButton.setEnabled(clickButtonConfig.enabled);
    recordButton.setEnabled(recordButtonConfig.enabled);

    playButton.setVisible(playPauseButtonConfig.visible && ! playbackIsPlaying);
    pauseButton.setVisible(playPauseButtonConfig.visible && playbackIsPlaying);
    stopButton.setVisible(stopButtonConfig.visible);
    rewindButton.setVisible(rewindButtonConfig.visible);
    fastForwardButton.setVisible(fastForwardButtonConfig.visible);
    loopButton.setVisible(loopButtonConfig.visible);
    clickButton.setVisible(clickButtonConfig.visible);
    recordButton.setVisible(recordButtonConfig.visible);

    playButton.setToggleState(playbackIsPlaying && ! playbackIsRecording, juce::dontSendNotification);
    pauseButton.setToggleState(false, juce::dontSendNotification);
    stopButton.setToggleState(! playbackIsPlaying && ! playbackIsRecording, juce::dontSendNotification);
    recordButton.setToggleState(playbackIsRecording, juce::dontSendNotification);

    repaint();
}
