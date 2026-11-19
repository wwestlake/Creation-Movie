#pragma once
#include "MovieWebCamPairing.h"

inline void verifyPairingLifecycle()
{
    auto require=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    auto pump=[](int ms){juce::MessageManager::getInstance()->runDispatchLoopUntil(ms);};
    auto status=[](const char* state){auto* o=new juce::DynamicObject;o->setProperty("state",state);o->setProperty("error","Test failure");return juce::var(o);};
    auto press=[&](juce::DocumentWindow& window,const juce::String& label){
        for(auto* child:window.getContentComponent()->getChildren())
            if(auto* button=dynamic_cast<juce::TextButton*>(child);button&&button->getButtonText()==label){button->triggerClick();pump(30);return;}
        throw std::runtime_error("Expected pairing action not found");
    };
    auto current=status("stopped");int issued=0,revoked=0,dismissed=0;bool reject=false;
    juce::Component workspace;workspace.setSize(900,700);
    std::unique_ptr<MovieWebCamPairingWindow> window;
    auto open=[&]{
        auto panel=std::make_unique<MovieWebCamPairing>([&]{
            ++issued;if(reject)throw std::runtime_error("Test setup failure");current=status("pairing");
            return juce::JSON::parse("{\"protocol\":\"djehuti-webcam-feasibility-v1\",\"session\":\"test\",\"secret\":\"not-a-live-invitation\",\"endpoint\":\"ws://192.168.1.2:8765/signal\"}");
        },[&]{return current;},[&]{++revoked;current=status("stopped");},[&]{++dismissed;window.reset();});
        window=std::make_unique<MovieWebCamPairingWindow>(std::move(panel),&workspace,false);
        require(!window->isCurrentlyModal()&&!workspace.isCurrentlyBlockedByAnotherModalComponent(),"Pairing blocked workspace");
    };
    open();require(issued==1,"Waiting setup did not issue invitation");press(*window,"Close");
    require(!window&&revoked==1&&dismissed==1,"Close did not release waiting window/invitation");
    open();current=status("connected");pump(180);
    require(!window&&revoked==1&&current["state"].toString()=="connected","Success did not dismiss while preserving connection");
    const int issuedBefore=issued;open();pump(180);
    require(window&&issued==issuedBefore,"Reopen replaced live connection");press(*window,"Done");
    require(!window&&revoked==1,"Done disconnected established phone");
    current=status("stopped");reject=true;open();require(window!=nullptr,"Error left no dismissible window");
    reject=false;press(*window,"Retry");require(current["state"].toString()=="pairing","Retry failed to return to pairing");
    current=status("failed");pump(180);press(*window,"Close");require(!window&&revoked==2,"Failed setup could not close/revoke");
    open();window->closeButtonPressed();window.reset();pump(30);
    require(revoked==3,"Owner teardown did not cancel pending setup safely");
    open();current=status("stopped");window->closeButtonPressed();pump(30);
    require(!window&&revoked==3,"Explicit receiver disconnect caused redundant teardown/disallowed closing");
}
