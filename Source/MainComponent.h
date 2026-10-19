#pragma once
#include <JuceHeader.h>
#include "MoviePlayback.h"
#include <creation/assets/ProjectSession.h>
#include <creation/agent/AgentApi.h>
#include <creation/agent/EngineerChat.h>
#include <creation/agent/CardsPanel.h>
#include <creation/ui/CreationSuiteHeaderBar.h>
#include <creation/ui/SuiteDesktopAuthSession.h>
#include <CreationDock/DockManager.h>

class MainComponent final : public juce::Component,private juce::Timer,private juce::MenuBarModel
{
public:
    MainComponent();~MainComponent() override;
    void paint(juce::Graphics&)override;void resized()override;
    bool keyPressed(const juce::KeyPress&)override;
    void requestQuit();
private:
    class Library;class Monitor;class Timeline;class Inspector;
    struct Job
    {
        juce::String id,type,status{"idle"},error;
        juce::var result;
        std::atomic<double> progress{0};std::atomic<bool> cancel{false};
    };
    using Result=creation::agent::ToolResult;
    Result execute(const juce::String&,const juce::var&);
    Result startJob(const juce::String&,std::function<juce::var(Job&)>,std::function<void(const juce::var&)> completed = {});
    void registerCommands();void runUi(const juce::String&,const juce::var& = movie::object({}));
    void changed();void refresh();void selectClip(const juce::String&);void selectMedia(const juce::String&);
    juce::var state()const;juce::var jobs()const;
    void chooseImport();void chooseExport();void newProjectDialog();void openProjectMenu();void titleDialog();void showSettings();void showHelp();
    void warn(const juce::String&);void timerCallback()override;
    juce::StringArray getMenuBarNames()override;
    juce::PopupMenu getMenuForIndex(int,const juce::String&)override;
    void menuItemSelected(int,int)override;

    movie::Document document;
    movie::Playback playback,sourcePlayback;
    creation::assets::ProjectSession session;
    creation::suite::SuiteSettings settings;
    creation::agent::VirtualEngineer engineer{creation::assets::SuiteAppDomain::movie};
    creation::agent::AgentApi api{engineer};
    creation::ui::SuiteDesktopAuthSession auth{"movie"};
    CreationSuiteHeaderBar header;
    juce::LookAndFeel_V4 look;
    juce::MenuBarComponent menu{this};
    juce::Label status,timecode;
    juce::TextButton split{"Split"},title{"Title"},fit{"Fit timeline"},cancelJob{"Cancel job"};
    juce::Slider zoom;
    std::unique_ptr<Library> library;std::unique_ptr<Monitor> monitor,sourceMonitor;
    std::unique_ptr<Timeline> timeline;std::unique_ptr<Inspector> inspector;
    std::unique_ptr<creation::agent::EngineerChat> chat;
    std::unique_ptr<creation::agent::CardsPanel> cards;
    std::unique_ptr<CreationDock::DockManager> dock;
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<juce::DocumentWindow> settingsWindow,helpWindow;
    juce::String selectedClip,selectedMedia,lastStatus;
    double visibleStart=0,pixelsPerSecond=35,lastPreviewTime=-1;
    std::shared_ptr<Job> job=std::make_shared<Job>();std::thread worker;
    bool closing=false,quitAfterSave=false;int autosaveTicks=0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
