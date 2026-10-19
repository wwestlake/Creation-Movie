#include <JuceHeader.h>
#include "MainComponent.h"
#include <creation/ui/CreationSuiteLogos.h>
#include <creation/ui/SuiteJUCEApplication.h>

class CreationMovieApplication final : public creation::ui::SuiteJUCEApplication
{
public:
    CreationMovieApplication() : SuiteJUCEApplication(creation::ui::SuiteLogoId::movie) {}

    const juce::String getApplicationName() override { return "Djehuti Movie"; }
    const juce::String getApplicationVersion() override { return "0.0.1"; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void systemRequestedQuit() override
    {
        if (auto* window = getMainWindow())
            if (auto* content = dynamic_cast<MainComponent*>(window->getContentComponent()))
            {
                content->requestQuit();
                return;
            }
        quit();
    }

protected:
    std::unique_ptr<juce::DocumentWindow> createMainWindow() override
    {
        return std::make_unique<MainWindow>(getApplicationName());
    }

private:
    class MainWindow final : public juce::DocumentWindow
    {
    public:
        explicit MainWindow(juce::String name)
            : juce::DocumentWindow(name,
                                   juce::Desktop::getInstance().getDefaultLookAndFeel()
                                       .findColour(juce::ResizableWindow::backgroundColourId),
                                   juce::DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar(true);
            setIcon(creation::ui::getSuiteLogoImage(creation::ui::SuiteLogoId::movie));
            setContentOwned(new MainComponent(), true);
            centreWithSize(getWidth(), getHeight());
            setResizable(true, true);
            setVisible(true);
        }

        void closeButtonPressed() override
        {
            if (auto* c = dynamic_cast<MainComponent*>(getContentComponent()))
                c->requestQuit();
            else
                juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }
    };
};

START_JUCE_APPLICATION(CreationMovieApplication)
