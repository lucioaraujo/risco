// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Lúcio de Araújo
#pragma once

#include <juce_graphics/juce_graphics.h>
#include <array>
#include <cstdint>

namespace risco::app {

inline constexpr std::array<std::array<std::uint8_t, 7>, 5> pixelLetters{{
    {{0b11110, 0b10001, 0b10001, 0b11110, 0b10100, 0b10010, 0b10001}}, // R
    {{0b01110, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b01110}}, // I
    {{0b01111, 0b10000, 0b10000, 0b01110, 0b00001, 0b00001, 0b11110}}, // S
    {{0b01111, 0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b01111}}, // C
    {{0b01110, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01110}}  // O
}};

inline void paintPixelGlyph(juce::Graphics& g, float x, float y, float pixel,
                            float horizontalScale,
                            const std::array<std::uint8_t, 7>& glyph,
                            juce::Colour colour) {
    g.setColour(colour);
    for (int row = 0; row < 7; ++row) {
        for (int col = 0; col < 5; ++col) {
            if ((glyph[static_cast<std::size_t>(row)] & (1u << (4 - col))) != 0)
                g.fillRect(x + float(col) * pixel * horizontalScale,
                           y + float(row) * pixel, pixel * horizontalScale, pixel);
        }
    }
}

inline void paintRiscoPixelLogo(juce::Graphics& g, float x, float y,
                               float horizontalScale = 1.0f) {
    const auto cyan = juce::Colour(0xff55d6e8);
    const auto amber = juce::Colour(0xffffc65c);
    constexpr std::array<float, 5> letterOffsets{{0.0f, 20.0f, 40.0f, 63.0f, 86.0f}};
    for (std::size_t i = 0; i < pixelLetters.size(); ++i) {
        paintPixelGlyph(g, x + letterOffsets[i] * horizontalScale, y, 3.0f,
                        horizontalScale,
                        pixelLetters[i], i == pixelLetters.size() - 1 ? amber : cyan);
    }
}

inline void paintRiscoWaveIcon(juce::Graphics& g, float x, float y, float size,
                               float horizontalScale = 1.0f) {
    const float unit = size / 64.0f;
    const auto scaledX = [=](float value) { return x + value * unit * horizontalScale; };
    const auto scaledY = [=](float value) { return y + value * unit; };
    const auto fill = [&](float rx, float ry, float width, float height, juce::Colour colour) {
        g.setColour(colour);
        g.fillRect(scaledX(rx), scaledY(ry),
                   width * unit * horizontalScale, height * unit);
    };

    const auto cyan = juce::Colour(0xff55d6e8);
    fill(4, 36, 8, 4, cyan);
    fill(12, 18, 4, 22, cyan);
    fill(16, 18, 12, 4, cyan);
    fill(28, 18, 4, 28, cyan);
    fill(32, 42, 8, 4, cyan);
    fill(40, 24, 4, 22, cyan);
    fill(44, 24, 4, 4, cyan);
    fill(48, 24, 4, 16, cyan);
    fill(52, 36, 2, 4, cyan);
    fill(54, 35, 6, 6, juce::Colour(0xffffc65c));
}

} // namespace risco::app
