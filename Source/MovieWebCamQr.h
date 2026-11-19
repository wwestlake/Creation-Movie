#pragma once
#include <juce_graphics/juce_graphics.h>
#include "../ThirdParty/qrcodegen/qrcodegen.hpp"

inline juce::Image movieWebCamQrImage(const juce::String& invitation)
{
    const auto qr = qrcodegen::QrCode::encodeText(invitation.toRawUTF8(), qrcodegen::QrCode::Ecc::MEDIUM);
    const int modules = qr.getSize(), scale = 400 / (modules + 8);
    if (scale < 1) throw std::runtime_error("Pairing invitation is too large for the QR display.");
    juce::Image image(juce::Image::RGB, 400, 400, true);
    juce::Graphics g(image);g.fillAll(juce::Colours::white);g.setColour(juce::Colours::black);
    const int edge = (400 - (modules + 8) * scale) / 2 + 4 * scale;
    for (int y = 0; y < modules; ++y)
        for (int x = 0; x < modules; ++x)
            if (qr.getModule(x, y)) g.fillRect(edge + x * scale, edge + y * scale, scale, scale);
    return image;
}
