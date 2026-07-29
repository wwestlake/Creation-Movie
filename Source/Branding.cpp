#include "Branding.h"

namespace creation_movie::branding
{
juce::Colour backgroundColour() noexcept { return juce::Colour(0xff0c1016); }
juce::Colour panelColour() noexcept { return juce::Colour(0xff162031); }
juce::Colour accentColour() noexcept { return juce::Colour(0xfff26d5b); }

juce::Image createAppLogoImage(int size)
{
    auto image = juce::Image(juce::Image::ARGB, size, size, true);
    juce::Graphics g(image);

    auto bounds = juce::Rectangle<float>(0.0f, 0.0f, (float) size, (float) size).reduced(size * 0.08f);
    g.setColour(juce::Colour(0xff111827));
    g.fillRoundedRectangle(bounds, size * 0.22f);

    g.setColour(accentColour().withAlpha(0.95f));
    g.drawRoundedRectangle(bounds, size * 0.22f, juce::jmax(2.0f, size * 0.05f));

    auto inner = bounds.reduced(size * 0.18f);
    juce::ColourGradient glow(accentColour().brighter(0.2f), inner.getTopLeft(),
                              juce::Colour(0xff59dfff), inner.getBottomRight(), false);
    g.setGradientFill(glow);

    juce::Path playShape;
    playShape.addTriangle(inner.getX() + inner.getWidth() * 0.20f, inner.getY() + inner.getHeight() * 0.14f,
                          inner.getX() + inner.getWidth() * 0.20f, inner.getBottom() - inner.getHeight() * 0.14f,
                          inner.getRight() - inner.getWidth() * 0.14f, inner.getCentreY());
    g.fillPath(playShape);

    g.setColour(juce::Colours::white.withAlpha(0.78f));
    g.drawRoundedRectangle(inner, size * 0.10f, juce::jmax(1.0f, size * 0.025f));

    return image;
}
}
