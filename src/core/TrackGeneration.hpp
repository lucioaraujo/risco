// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Lúcio de Araújo
#pragma once

#include "CompositionSeed.hpp"

namespace risco {

enum class SeedTrackForm { original, arc, collage };
enum class SeedTrackPulse { straight, swing, cut };
enum class SeedTrackPalette { original, resonant, chip, grain };

struct SeedTrackOptions {
    double bpm = 0.0; // 0 derives a stable tempo from the seed.
    int durationBars = 64; // 16..84, in four-bar phrase blocks.
    SeedTrackForm form = SeedTrackForm::original;
    int surprise = 1; // 0 restrained, 1 medium, 2 high.
    SeedTrackPulse pulse = SeedTrackPulse::straight;
    SeedTrackPalette palette = SeedTrackPalette::original;
};

FormulaOrigin seedTrackOrigin(const SeedTrackOptions& options);

// Builds an editable 16..84-bar project in four-bar blocks from a stable seed.
// The result is committed only when every phrase, stem and placement is valid.
bool generateSeedTrack(std::uint64_t seed, Song& destination);
bool generateSeedTrack(std::uint64_t seed, const SeedTrackOptions& options,
                       Song& destination);

} // namespace risco
