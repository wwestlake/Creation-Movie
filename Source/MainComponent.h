#pragma once

#include <JuceHeader.h>

class MainComponent final : public juce::Component
{
public:
    MainComponent();

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    juce::Label titleLabel;
    juce::Label subtitleLabel;
    juce::Label runtimeLabel;
    juce::GroupComponent timelineGroup;
    juce::GroupComponent previewGroup;
    juce::GroupComponent libraryGroup;
    juce::TextEditor notesBox;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};

