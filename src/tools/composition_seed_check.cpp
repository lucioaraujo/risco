// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Lúcio de Araújo
#include "core/CompositionSeed.hpp"
#include "core/SongIO.hpp"

#include <array>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>

namespace {
bool serialize(const risco::Song& song, std::string& text,
               const std::vector<risco::NamedSongVersionRecord>& versions = {}) {
    std::ostringstream out;
    if (!risco::saveSong(out, song, versions)) return false;
    text = out.str();
    return true;
}

bool reproducibleAndVersioned() {
    auto source = std::make_unique<risco::Song>();
    source->bpm = 112.0;
    const auto id = risco::storeGeneratedCell(*source, 2026, 2, "Motif", {}, 7);
    if (!id || !risco::placeCell(*source, {id, 0, risco::CellSpan::fourBars, 1, 0, 0, 0}))
        return false;

    auto first = std::make_unique<risco::Song>();
    auto repeat = std::make_unique<risco::Song>();
    auto other = std::make_unique<risco::Song>();
    constexpr std::uint64_t seed = 0x8f71ab02c3d4e5f6ULL;
    if (!risco::composeSongVariation(*source, seed, risco::CompositionSeedLevel::wide,
                                    risco::compositionSeedDefaultLocks, *first) ||
        !risco::composeSongVariation(*source, seed, risco::CompositionSeedLevel::wide,
                                    risco::compositionSeedDefaultLocks, *repeat) ||
        !risco::composeSongVariation(*source, seed + 1, risco::CompositionSeedLevel::wide,
                                    risco::compositionSeedDefaultLocks, *other))
        return false;

    std::string a, b, c;
    if (!serialize(*first, a) || !serialize(*repeat, b) || !serialize(*other, c) ||
        a != b || a == c || a.rfind("RISCO_SONG_V76\n", 0) != 0 ||
        first->compositionSeed != seed || first->compositionSeedLevel != 3 ||
        first->compositionSeedAlgorithm != risco::compositionSeedAlgorithmV1 ||
        first->compositionSeedLocks != risco::compositionSeedDefaultLocks ||
        first->masterGain != source->masterGain ||
        first->bank[0].phraseSeed == other->bank[0].phraseSeed)
        return false;

    auto loaded = std::make_unique<risco::Song>();
    std::istringstream in(a);
    if (!risco::loadSong(in, *loaded) || !loaded->compositionSeedRecipeEnabled ||
        loaded->compositionSeed != seed || loaded->compositionSeedLevel != 3 ||
        loaded->compositionSeedLocks != risco::compositionSeedDefaultLocks ||
        loaded->bank[0].phraseSeed != first->bank[0].phraseSeed ||
        loaded->stemRole != first->stemRole || loaded->stemGain != first->stemGain)
        return false;

    const auto recipeAt = a.find("\nSEED_RECIPE ");
    if (recipeAt == std::string::npos) return false;
    auto invalidText = a;
    const auto recipeEnd = invalidText.find('\n', recipeAt + 1);
    invalidText.replace(recipeAt + 1, recipeEnd - recipeAt - 1,
                        "SEED_RECIPE 99 1 3 0");
    std::istringstream invalid(invalidText);
    if (risco::loadSong(invalid, *loaded)) return false;
    return true;
}

bool levelsAndLocks() {
    auto source = std::make_unique<risco::Song>();
    source->bpm = 120.0;
    const auto id = risco::storeGeneratedCell(*source, 99, 1, "Phrase", {}, 7);
    if (!id || !risco::placeCell(*source, {id, 0, risco::CellSpan::fourBars, 1, 0, 0, 0}))
        return false;

    auto manual = std::make_unique<risco::Song>();
    auto assisted = std::make_unique<risco::Song>();
    auto ensemble = std::make_unique<risco::Song>();
    auto wide = std::make_unique<risco::Song>();
    auto locked = std::make_unique<risco::Song>();
    constexpr std::uint64_t seed = 1789;
    if (!risco::composeSongVariation(*source, seed, risco::CompositionSeedLevel::manual, 0, *manual) ||
        !risco::composeSongVariation(*source, seed, risco::CompositionSeedLevel::assisted, 0, *assisted) ||
        !risco::composeSongVariation(*source, seed, risco::CompositionSeedLevel::ensemble, 0, *ensemble) ||
        !risco::composeSongVariation(*source, seed, risco::CompositionSeedLevel::wide,
            risco::compositionSeedDefaultLocks, *wide) ||
        !risco::composeSongVariation(*source, seed, risco::CompositionSeedLevel::wide,
            risco::compositionSeedKnownLocks, *locked))
        return false;

    if (manual->bpm != source->bpm || manual->bank[0].phraseSeed != source->bank[0].phraseSeed ||
        assisted->bank[0].phraseSeed == source->bank[0].phraseSeed ||
        assisted->stemRole != source->stemRole || assisted->stemEffect != source->stemEffect ||
        assisted->stemGain != source->stemGain || ensemble->stemGain != source->stemGain ||
        wide->masterGain != source->masterGain || wide->bank[0].phraseSeed == source->bank[0].phraseSeed ||
        locked->bpm != source->bpm || locked->bank[0].phraseSeed != source->bank[0].phraseSeed ||
        locked->stemRole != source->stemRole || locked->timbreMode != source->timbreMode ||
        locked->stemEffect != source->stemEffect || locked->stemGain != source->stemGain ||
        locked->masterGain != source->masterGain || locked->compositionSeed != seed)
        return false;

    std::string originalFields, lockedFields;
    if (!serialize(*source, originalFields)) return false;
    locked->compositionSeedRecipeEnabled = false;
    locked->compositionSeed = 0;
    locked->compositionSeedAlgorithm = 0;
    locked->compositionSeedLocks = 0;
    locked->compositionSeedLevel = 0;
    if (!serialize(*locked, lockedFields) || originalFields != lockedFields)
        return false;

    // Tempo is its own field stream: locking it cannot alter phrase choices.
    auto tempoLocked = std::make_unique<risco::Song>();
    const auto locks = risco::compositionSeedLock(risco::CompositionSeedLock::tempoAndForm);
    if (!risco::composeSongVariation(*source, seed, risco::CompositionSeedLevel::assisted,
                                    locks, *tempoLocked) ||
        tempoLocked->bpm != source->bpm ||
        tempoLocked->bank[0].phraseSeed != assisted->bank[0].phraseSeed)
        return false;

    for (const auto badLevel : {4u, 255u}) {
        auto rejected = std::make_unique<risco::Song>();
        if (risco::composeSongVariation(*source, seed,
                static_cast<risco::CompositionSeedLevel>(badLevel), 0, *rejected))
            return false;
    }
    auto rejected = std::make_unique<risco::Song>();
    return !risco::composeSongVariation(*source, seed, risco::CompositionSeedLevel::wide,
        risco::compositionSeedKnownLocks | (1u << 20), *rejected);
}

bool v75DefaultsRemainManual() {
    auto source = std::make_unique<risco::Song>();
    std::string base;
    if (!serialize(*source, base)) return false;
    risco::NamedSongVersionRecord record{"Legacy", base, -1};
    std::string v75;
    if (!serialize(*source, v75, {record}) || v75.rfind("RISCO_SONG_V75\n", 0) != 0)
        return false;
    auto loaded = std::make_unique<risco::Song>();
    std::istringstream in(v75);
    return risco::loadSong(in, *loaded) && !loaded->compositionSeedRecipeEnabled &&
           loaded->compositionSeed == 0 && loaded->compositionSeedLevel == 0 &&
           loaded->compositionSeedLocks == 0;
}
}

int main() {
    if (!reproducibleAndVersioned()) {
        std::cerr << "composition seed reproducibility/V76 round-trip failed\n";
        return 1;
    }
    if (!levelsAndLocks()) {
        std::cerr << "composition seed levels/locks failed\n";
        return 2;
    }
    if (!v75DefaultsRemainManual()) {
        std::cerr << "V75 composition seed defaults failed\n";
        return 3;
    }
    std::cout << "Risco composition seed recipe passed\n";
    return 0;
}
