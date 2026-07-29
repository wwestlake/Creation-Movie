#pragma once

#include <JuceHeader.h>

namespace creation_movie::branding
{
juce::Colour backgroundColour() noexcept;
juce::Colour panelColour() noexcept;
juce::Colour accentColour() noexcept;
juce::Image createAppLogoImage(int size);
}
