// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Lúcio de Araújo
#pragma once

#include "Song.hpp"
#include <cstdint>

namespace risco {

enum class CompositionSeedLevel : std::uint8_t {
    manual = 0,
    assisted = 1,
    ensemble = 2,
    wide = 3
};

enum class CompositionSeedLock : std::uint32_t {
    phrases = 1u << 0,
    tempoAndForm = 1u << 1,
    ensemble = 1u << 2,
    timbreAndFx = 1u << 3,
    stemMix = 1u << 4,
    master = 1u << 5
};

constexpr std::uint32_t compositionSeedLock(CompositionSeedLock lock) noexcept {
    return static_cast<std::uint32_t>(lock);
}
constexpr std::uint32_t compositionSeedKnownLocks = (1u << 6) - 1u;
constexpr std::uint32_t compositionSeedDefaultLocks =
    compositionSeedLock(CompositionSeedLock::master);
constexpr std::uint32_t compositionSeedAlgorithmV1 = 1;

// Creates a candidate from source; source is never modified. Each stable field
// identifier has its own SplitMix64 substream, so adding a draw cannot shift
// the choices made for other fields. Level 0 is an exact copy without recipe
// metadata. Safety processing is not part of this recipe.
bool composeSongVariation(const Song& source, std::uint64_t seed,
                         CompositionSeedLevel level, std::uint32_t locks,
                         Song& candidate);

} // namespace risco
