#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "MovieWebCamQr.h"

// A temporary view of the existing receiver. It never owns an established peer.
class MovieWebCamPairing final : public juce::Component, private juce::Timer
{
public:
    using Issue = std::function<juce::var()>;
    MovieWebCamPairing(Issue issue, std::function<juce::var()> status,
                       std::function<void()> revoke, std::function<void()> dismiss)
        : issueInvitation(std::move(issue)), readStatus(std::move(status)),
          revokeInvitation(std::move(revoke)), dismissWindow(std::move(dismiss))
    {
        for(auto* child:std::initializer_list<juce::Component*>{&instructions,&state,&refresh,&close})addAndMakeVisible(child);
        instructions.setText("Open Djehuti WebCam on your phone and tap Scan QR.\nScanning connects automatically. Movie stays usable while you pair.",juce::dontSendNotification);
        instructions.setJustificationType(juce::Justification::centred);
        state.setJustificationType(juce::Justification::centred);
        close.setButtonText("Close");close.onClick=[this]{requestClose();};
        refresh.setButtonText("New QR code");refresh.onClick=[this]{renew();};
        setSize(540,640);
        if(readStatus()["state"].toString()=="connected")updateStatus();else renew();
        startTimer(100);
    }
    ~MovieWebCamPairing() override
    {
        stopTimer();
        const auto current=readStatus()["state"].toString();
        if(ownsInvitation&&!established&&current!="connected"&&current!="stopped")revokeInvitation();
    }
    void requestClose()
    {
        if(closing)return;
        closing=true;stopTimer();
        const juce::Component::SafePointer<MovieWebCamPairing> safe(this);
        // Defer owner deletion until button/timer callbacks have unwound.
        juce::MessageManager::callAsync([safe]{if(safe){auto dismiss=safe->dismissWindow;dismiss();}});
    }
    void paint(juce::Graphics& g) override
    {
        if(code.isValid())g.drawImageAt(code,(getWidth()-400)/2,100);
    }
    void resized() override
    {
        instructions.setBounds(20,10,getWidth()-40,80);
        state.setBounds(20,510,getWidth()-40,65);
        refresh.setBounds(getWidth()/2-175,585,170,32);
        close.setBounds(getWidth()/2+5,585,170,32);
    }
private:
    void renew()
    {
        // A reconnect click must never replace a live connection.
        if(readStatus()["state"].toString()=="connected"){updateStatus();return;}
        code={};ownsInvitation=true;setupError={};
        try{
            const auto invitation=issueInvitation();
            code=movieWebCamQrImage(juce::JSON::toString(invitation,true));
            deadline=juce::Time::getMillisecondCounterHiRes()+120000.0;
        }catch(const std::exception& e){setupError=e.what();}
        updateStatus();repaint();
    }
    void timerCallback() override {updateStatus();}
    void updateStatus()
    {
        const auto status=readStatus();const auto current=status["state"].toString();
        close.setButtonText(current=="connected"?"Done":"Close");
        refresh.setVisible(current!="connected");refresh.setEnabled(current!="negotiating");
        if(current=="connected"){
            established=true;code={};
            state.setText("Connected. Your phone is available in Movie's video/audio source selectors.",juce::dontSendNotification);
            if(ownsInvitation)requestClose();
        }else if(setupError.isNotEmpty()){
            state.setText("Failed: "+setupError,juce::dontSendNotification);refresh.setButtonText("Retry");
        }else if(current=="pairing"){
            const int seconds=juce::jmax(0,(int)std::ceil((deadline-juce::Time::getMillisecondCounterHiRes())/1000.0));
            if(seconds)state.setText("Waiting for phone. QR code expires in "+juce::String(seconds)+" seconds.",juce::dontSendNotification);
            else{code={};revokeInvitation();setupError="QR code expired. Retry to generate a fresh code.";state.setText(setupError,juce::dontSendNotification);refresh.setButtonText("Retry");}
        }else if(current=="negotiating"){
            code={};state.setText("Connecting to your phone... You can close this window to cancel.",juce::dontSendNotification);
        }else{
            code={};refresh.setButtonText("Retry");
            state.setText("Failed: "+status["error"].toString()+" Retry pairing or close this window.",juce::dontSendNotification);
        }
        repaint();
    }
    Issue issueInvitation;
    std::function<juce::var()> readStatus;
    std::function<void()> revokeInvitation,dismissWindow;
    juce::Image code;
    juce::Label instructions,state;
    juce::TextButton refresh,close;
    double deadline=0;
    bool ownsInvitation=false,established=false,closing=false;
    juce::String setupError;
};

// Deliberately modeless: pairing can never block editing or recording controls.
class MovieWebCamPairingWindow final : public juce::DocumentWindow
{
public:
    MovieWebCamPairingWindow(std::unique_ptr<MovieWebCamPairing> panel,juce::Component* owner,bool visible=true)
        : juce::DocumentWindow("Pair Phone",juce::Colour(0xff263238),juce::DocumentWindow::closeButton)
    {
        setUsingNativeTitleBar(true);setResizable(false,false);
        setContentOwned(panel.release(),true);centreAroundComponent(owner,getWidth(),getHeight());
        setVisible(visible);
    }
    void closeButtonPressed() override {if(auto* panel=dynamic_cast<MovieWebCamPairing*>(getContentComponent()))panel->requestClose();}
    bool keyPressed(const juce::KeyPress& key) override
    {
        if(key==juce::KeyPress::escapeKey){closeButtonPressed();return true;}
        return juce::DocumentWindow::keyPressed(key);
    }
};
