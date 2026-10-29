#include "MovieVisualCapture.h"
#include <iostream>
#include <functional>
#if JUCE_WINDOWS
#include <windows.h>
#endif

static void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
static bool waitFor(const std::function<bool()>& condition,int timeout=10000)
{
    const auto end=juce::Time::getMillisecondCounterHiRes()+timeout;
    while(juce::Time::getMillisecondCounterHiRes()<end){if(condition())return true;juce::Thread::sleep(50);}return condition();
}
#if JUCE_WINDOWS
class ColourFixture final : public juce::Component, private juce::Timer
{
public:
    ColourFixture(){setSize(360,240);startTimer(300);}
    void paint(juce::Graphics& g) override {g.fillAll(red?juce::Colour(240,30,20):juce::Colour(20,210,40));}
private:
    void timerCallback() override {red=!red;repaint();}
    bool red=false;
};
class FixtureWindow final : public juce::DocumentWindow
{
public:
    FixtureWindow(const juce::String& title):DocumentWindow(title,juce::Colours::black,allButtons)
    {setUsingNativeTitleBar(true);setContentOwned(new ColourFixture,true);setTopLeftPosition(60,80);setAlwaysOnTop(true);setVisible(true);}
    void closeButtonPressed() override {juce::MessageManager::getInstance()->stopDispatchLoop();}
};
static int fixture(const juce::String& title)
{FixtureWindow window(title);juce::MessageManager::getInstance()->runDispatchLoop();return 0;}
#endif
int main(int argc,char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
#if JUCE_WINDOWS
    if(argc>=3 && juce::String(argv[1])=="--fixture")return fixture(argv[2]);
#endif
    juce::ChildProcess child;
    try
    {
        require(argc==2,"Supply an explicit output directory.");const juce::File output(argv[1]);output.createDirectory();
        const auto title="Movie capture validation "+juce::Uuid().toString();
        const auto self=juce::File::getSpecialLocation(juce::File::currentExecutableFile);
        require(child.start(juce::StringArray{self.getFullPathName(),"--fixture",title}),"Could not start owned capture fixture");
        movie::VisualCapture capture;juce::String windowId,displayId;
        require(waitFor([&]{for(const auto& s:capture.sources()){if(s.kind=="window" && s.name.contains(title))windowId=s.id;if(s.kind=="display")displayId=s.id;}return windowId.isNotEmpty() && displayId.isNotEmpty();}),"Display or owned application window not discovered");
        capture.setBindings({{"track-a",windowId,true},{"track-b",windowId,true}});
        require(waitFor([&]{return capture.preview("track-a").frameNumber>2;}),"Actual window capture did not deliver frames");
        auto first=capture.preview("track-a");require(first.state=="live" && first.image.isValid(),"Window capture returned no image");
        const auto pixel=first.image.getPixelAt(first.image.getWidth()/2,first.image.getHeight()/2);
        require((pixel.getRed()>180 && pixel.getGreen()<80) || (pixel.getGreen()>180 && pixel.getRed()<80),"Window image is not the fixture's actual colour");

        const bool changing=waitFor([&]{auto p=capture.preview("track-b");return p.image.isValid() && p.frameNumber>first.frameNumber && p.timestamp100ns>first.timestamp100ns
            && p.image.getPixelAt(p.image.getWidth()/2,p.image.getHeight()/2)!=pixel;});
        if(!changing)
        {const auto last=capture.preview("track-b");std::cerr<<"Window diagnostic: "<<last.state<<" "<<last.message<<" frames "<<first.frameNumber<<" -> "<<last.frameNumber<<" QPC "<<first.timestamp100ns<<" -> "<<last.timestamp100ns<<" first ARGB "<<pixel.toString()<<" last ARGB "<<last.image.getPixelAt(last.image.getWidth()/2,last.image.getHeight()/2).toString()<<"\n";
            auto stream=output.getChildFile("failed-window.png").createOutputStream();juce::PNGImageFormat().writeImageToStream(last.image,*stream);
        }
        require(changing,"Live window pixels did not update");
        require((int)capture.status()["activeAcquisitions"]==1 && capture.preview("track-b").subscribers==2,"Duplicate physical acquisition for shared source");
        auto save=[&](const char* name,const juce::Image& image){auto stream=output.getChildFile(name).createOutputStream();require(stream && juce::PNGImageFormat().writeImageToStream(image,*stream),"Could not save validation image");};
        save("window-live.png",first.image);
        capture.setBindings({{"track-b",windowId,true}});
        require(waitFor([&]{return capture.preview("track-b").subscribers==1;}),"Removing one subscriber invalidated the other");
        capture.setBindings({{"track-a",displayId,true},{"track-b",windowId,true}});
        require(waitFor([&]{return capture.preview("track-a").image.isValid() && (int)capture.status()["activeAcquisitions"]==2;}),"Independent display/window source switching failed");
        auto screen=capture.preview("track-a");int expectedWidth=0,expectedHeight=0;for(const auto& s:capture.sources())if(s.id==displayId){expectedWidth=s.width;expectedHeight=s.height;}
        require(screen.image.getWidth()==expectedWidth && screen.image.getHeight()==expectedHeight,"Display capture is not the individual monitor extent");save("display-live.png",screen.image);
        int displaysTested=0;
        for(const auto& display:capture.sources())if(display.kind=="display")
        {
            capture.setBindings({{"track-a",display.id,true},{"track-b",windowId,true}});
            require(waitFor([&]{auto p=capture.preview("track-a");return p.image.isValid() && p.image.getWidth()==display.width && p.image.getHeight()==display.height;}),"Individual display selection captured the wrong bounds");
            save(("display-"+juce::String(++displaysTested)+".png").toRawUTF8(),capture.preview("track-a").image);
        }
        capture.setBindings({{"track-a",windowId,true},{"track-b",windowId,false}});
        require(waitFor([&]{return (int)capture.status()["activeAcquisitions"]==1 && capture.preview("track-a").frameNumber>0;}),"Stopped track retained an acquisition");
        require(capture.preview("track-b").state=="stopped","Per-track preview stop was ignored");
#if JUCE_WINDOWS
        const auto window=FindWindowW(nullptr,title.toWideCharPointer());require(window!=nullptr,"Owned fixture disappeared prematurely");PostMessageW(window,WM_CLOSE,0,0);
#endif
        require(waitFor([&]{return !capture.preview("track-a").image.isValid() && capture.preview("track-a").state!="live";}),"Closed window retained stale live preview");
        capture.setBindings({});require(waitFor([&]{return (int)capture.status()["activeAcquisitions"]==0;}),"Last subscriber did not release capture resources");
        auto* report=new juce::DynamicObject;report->setProperty("ok",true);report->setProperty("nativeCapture",true);report->setProperty("displaysTested",displaysTested);report->setProperty("checks","actual changing window pixels and QPC timestamps; individual display extents; shared acquisition; independent stop/switch; closed-window invalidation; final cleanup");
        output.getChildFile("results.json").replaceWithText(juce::JSON::toString(juce::var(report)));
        child.waitForProcessToFinish(2000);std::cout<<"PASS: real WGC display/window pixels, live changes, timestamps, sharing, switch/stop/close and cleanup\n";return 0;
    }
    catch(const std::exception& e){child.kill();std::cerr<<"FAIL: "<<e.what()<<"\n"<<child.readAllProcessOutput().substring(0,1600)<<"\n";return 1;}
}
