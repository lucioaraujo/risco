// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Lúcio de Araújo
#pragma once

#include <array>
#include <cstdint>

namespace risco {
struct HarmonicPalette {
    std::array<int, 7> intervals;
    std::array<int, 4> progression;
};

// Shared by seed-generated phrases and gesture-derived melodies so both
// composition paths use the same four locally authored harmonic vocabularies.
inline constexpr std::array<HarmonicPalette, 4> harmonicPalettes{{
    {{{0, 2, 4, 5, 7, 9, 11}}, {{0, 5, 3, 4}}},
    {{{0, 2, 3, 5, 7, 8, 10}}, {{0, 5, 6, 4}}},
    {{{0, 2, 3, 5, 7, 9, 10}}, {{0, 3, 4, 0}}},
    {{{0, 3, 5, 7, 10, 12, 14}}, {{0, 4, 1, 3}}}
}};

inline unsigned paletteForDrawingSeed(std::uint64_t seed) noexcept {
    return static_cast<unsigned>(seed % harmonicPalettes.size());
}

inline int rootForDrawingSeed(std::uint64_t seed) noexcept {
    return 48 + static_cast<int>((seed >> 8u) % 9u);
}

inline const char* paletteName(unsigned index) noexcept {
    constexpr const char* names[] = {"maior", "menor natural", "dórico", "pentatônica"};
    return names[index % harmonicPalettes.size()];
}

inline const char* pitchClassName(int midiNote) noexcept {
    constexpr const char* names[] = {
        "Dó", "Dó sustenido", "Ré", "Mi bemol", "Mi", "Fá",
        "Fá sustenido", "Sol", "Lá bemol", "Lá", "Si bemol", "Si"
    };
    const int pitchClass = ((midiNote % 12) + 12) % 12;
    return names[pitchClass];
}
}
