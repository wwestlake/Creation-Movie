#pragma once
#include <juce_gui_extra/juce_gui_extra.h>
#include "../ThirdParty/qrcodegen/qrcodegen.hpp"

// Invitations stay in this window's memory; never copy them to the clipboard,
// write them to project data, or send them to a QR rendering service.
class MovieWebCamPairing final : public juce::Component, private juce::Timer
{
public:
    using Issue = std::function<juce::var()>;
    MovieWebCamPairing(Issue issue, std::function<juce::String()> status,
                       std::function<void()> revoke)
        : issueInvitation(std::move(issue)), readStatus(std::move(status)), revokeInvitation(std::move(revoke))
    {
        addAndMakeVisible(instructions); addAndMakeVisible(state); addAndMakeVisible(refresh);
        instructions.setText("Open Djehuti WebCam on your phone and tap Scan QR.\nKeep the phone and PC on the same Wi-Fi. Scan, then tap Connect.", juce::dontSendNotification);
        instructions.setJustificationType(juce::Justification::centred);
        state.setJustificationType(juce::Justification::centred);
        refresh.setButtonText("New QR code"); refresh.onClick = [this] { renew(); };
        setSize(540, 640); renew();
    }
    ~MovieWebCamPairing() override
    {
        stopTimer();
        // Closing a pending invitation revokes it, but preserves a live session.
        if (readStatus() == "pairing") revokeInvitation();
    }
    void paint(juce::Graphics& g) override
    {
        if (!code) return;
        const int modules = code->getSize(), scale = 400 / (modules + 8);
        const int side = (modules + 8) * scale, left = (getWidth() - side) / 2;
        g.setColour(juce::Colours::white); g.fillRect(left, 100, side, side);
        g.setColour(juce::Colours::black);
        for (int y = 0; y < modules; ++y)
            for (int x = 0; x < modules; ++x)
                if (code->getModule(x, y)) g.fillRect(left + (x + 4) * scale, 100 + (y + 4) * scale, scale, scale);
    }
    void resized() override
    {
        instructions.setBounds(20, 10, getWidth() - 40, 80);
        state.setBounds(20, 510, getWidth() - 40, 65);
        refresh.setBounds(getWidth() / 2 - 85, 585, 170, 32);
    }
private:
    void renew()
    {
        startTimer(250);
        code.reset();
        try
        {
            const auto invitation = issueInvitation();
            const auto text = juce::JSON::toString(invitation, true);
            code = std::make_unique<qrcodegen::QrCode>(qrcodegen::QrCode::encodeText(text.toRawUTF8(), qrcodegen::QrCode::Ecc::MEDIUM));
            deadline = juce::Time::getMillisecondCounterHiRes() + 120000.0;
            timerCallback();
        }
        catch (const std::exception& e)
        {
            stopTimer();
            state.setText("Unable to pair: " + juce::String(e.what()), juce::dontSendNotification);
            refresh.setEnabled(true);
        }
        repaint();
    }
    void timerCallback() override
    {
        const auto current = readStatus();
        if (current != "pairing")
        {
            code.reset(); repaint(); refresh.setEnabled(current != "connected" && current != "negotiating");
            state.setText(current == "connected" ? "Phone connected. Close this window and select WebCam video/audio on your tracks."
                : current == "negotiating" ? "Connecting to your phone..." : "Connection ended. Choose New QR code to pair again.", juce::dontSendNotification);
            return;
        }
        if (!code) return;
        const int seconds = juce::jmax(0, (int)std::ceil((deadline - juce::Time::getMillisecondCounterHiRes()) / 1000.0));
        if (!seconds)
        {
            code.reset(); revokeInvitation(); repaint();
            state.setText("QR code expired. Choose New QR code to try again.", juce::dontSendNotification);
            stopTimer();
        }
        else state.setText("Private, one-use QR code. Expires in " + juce::String(seconds) + " seconds.", juce::dontSendNotification);
    }
    Issue issueInvitation;
    std::function<juce::String()> readStatus;
    std::function<void()> revokeInvitation;
    std::unique_ptr<qrcodegen::QrCode> code;
    juce::Label instructions, state;
    juce::TextButton refresh;
    double deadline = 0;
};
