#pragma once
#include <juce_gui_extra/juce_gui_extra.h>
#include "MovieWebCamQr.h"

// Invitations stay in this window's memory; never copy them to the clipboard,
// write them to project data, or send them to a QR rendering service.
class MovieWebCamPairing final : public juce::Component, private juce::Timer
{
public:
    using Issue = std::function<juce::var()>;
    MovieWebCamPairing(Issue issue, std::function<juce::var()> status,
                       std::function<void()> revoke)
        : issueInvitation(std::move(issue)), readStatus(std::move(status)), revokeInvitation(std::move(revoke))
    {
        addAndMakeVisible(instructions); addAndMakeVisible(state); addAndMakeVisible(refresh); addAndMakeVisible(disconnect);
        disconnect.setButtonText("Disconnect");disconnect.onClick=[this]{revokeInvitation();};
        instructions.setText("Open Djehuti WebCam on your phone and tap Scan QR.\nScanning connects automatically. Your phone must be able to reach this PC over the local network.", juce::dontSendNotification);
        instructions.setJustificationType(juce::Justification::centred);
        state.setJustificationType(juce::Justification::centred);
        refresh.setButtonText("New QR code"); refresh.onClick = [this] { renew(); };
        setSize(540, 640); renew();
    }
    ~MovieWebCamPairing() override
    {
        stopTimer();
        // Closing a pending invitation revokes it, but preserves a live session.
        if (readStatus()["state"].toString() != "connected") revokeInvitation();
    }
    void paint(juce::Graphics& g) override
    {
        if(code.isValid())g.drawImageAt(code,(getWidth()-400)/2,100);
    }
    void resized() override
    {
        instructions.setBounds(20, 10, getWidth() - 40, 80);
        state.setBounds(20, 510, getWidth() - 40, 65);
        refresh.setBounds(getWidth() / 2 - 175, 585, 170, 32);
        disconnect.setBounds(getWidth() / 2 + 5, 585, 170, 32);
    }
private:
    void renew()
    {
        startTimer(250);
        code = {};
        try
        {
            const auto invitation = issueInvitation();
            const auto text = juce::JSON::toString(invitation, true);
            code = movieWebCamQrImage(text);
            deadline = juce::Time::getMillisecondCounterHiRes() + 120000.0;
            timerCallback();
        }
        catch (const std::exception& e)
        {
            stopTimer();
            state.setText("Failed: " + juce::String(e.what()), juce::dontSendNotification);
            refresh.setEnabled(true);
        }
        repaint();
    }
    void timerCallback() override
    {
        const auto status = readStatus();
        const auto current = status["state"].toString();
        if (current != "pairing")
        {
            code = {}; repaint(); refresh.setEnabled(current != "connected" && current != "negotiating");
            state.setText(current == "connected" ? "Connected. Select WebCam video/audio on your tracks."
                : current == "negotiating" ? "Connecting to your phone..." : "Failed. " + status["error"].toString() + " Choose New QR code to try again.", juce::dontSendNotification);
            return;
        }
        if (!code.isValid()) return;
        const int seconds = juce::jmax(0, (int)std::ceil((deadline - juce::Time::getMillisecondCounterHiRes()) / 1000.0));
        if (!seconds)
        {
            code = {}; revokeInvitation(); repaint();
            state.setText("QR code expired. Choose New QR code to try again.", juce::dontSendNotification);
            stopTimer();
        }
        else state.setText("Waiting for phone. QR code expires in " + juce::String(seconds) + " seconds.", juce::dontSendNotification);
    }
    Issue issueInvitation;
    std::function<juce::var()> readStatus;
    std::function<void()> revokeInvitation;
    juce::Image code;
    juce::Label instructions, state;
    juce::TextButton refresh, disconnect;
    double deadline = 0;
};
