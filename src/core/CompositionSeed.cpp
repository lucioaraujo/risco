// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Lúcio de Araújo
#include "CompositionSeed.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace risco {
namespace {
constexpr std::uint64_t golden = 0x9e3779b97f4a7c15ULL;

std::uint64_t mix64(std::uint64_t value) noexcept {
    value += golden;
    value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31);
}

std::uint64_t draw(std::uint64_t seed, std::uint64_t field) noexcept {
    return mix64(seed ^ mix64(field ^ 0x524953434f534545ULL));
}

float unit(std::uint64_t seed, std::uint64_t field) noexcept {
    return float(draw(seed, field) >> 40) / float(1u << 24);
}

int pick(std::uint64_t seed, std::uint64_t field, int count) noexcept {
    return count > 0 ? int(draw(seed, field) % std::uint64_t(count)) : 0;
}

bool unlocked(std::uint32_t locks, CompositionSeedLock lock) noexcept {
    return (locks & compositionSeedLock(lock)) == 0;
}
}

bool composeSongVariation(const Song& source, std::uint64_t seed,
                          CompositionSeedLevel level, std::uint32_t locks,
                          Song& destination) {
    const auto levelValue = static_cast<unsigned>(level);
    if (levelValue > static_cast<unsigned>(CompositionSeedLevel::wide) ||
        (locks & ~compositionSeedKnownLocks) != 0 || source.cellCount < 0 ||
        source.cellCount > int(source.bank.size()) || source.placementCount < 0 ||
        source.placementCount > int(source.grid.size()) || !std::isfinite(source.bpm) ||
        source.bpm < 40.0 || source.bpm > 240.0)
        return false;

    Song result = source;
    if (level == CompositionSeedLevel::manual) {
        destination = std::move(result);
        return true;
    }

    if (unlocked(locks, CompositionSeedLock::phrases)) {
        for (int index = 0; index < result.cellCount; ++index) {
            auto& cell = result.bank[static_cast<std::size_t>(index)];
            if (!cell.generatedPhrase) continue;
            const std::uint64_t field = 0x100000000ULL | cell.id;
            cell.phraseSeed = draw(seed, field);
            cell.phraseVariation = std::uint32_t(draw(seed, field ^ 0x564152494154494fULL) % 8u);
        }
    }

    if (unlocked(locks, CompositionSeedLock::tempoAndForm)) {
        const float tempoFactor = 0.92f + unit(seed, 0x42504d5f52495343ULL) * 0.16f;
        result.bpm = std::clamp(double(source.bpm * tempoFactor), 40.0, 240.0);
    }

    if (levelValue >= static_cast<unsigned>(CompositionSeedLevel::ensemble) &&
        unlocked(locks, CompositionSeedLock::ensemble)) {
        for (int stem = 0; stem < Song::stemCount; ++stem) {
            const auto field = 0x5354454d00000000ULL | std::uint64_t(stem);
            result.stemRole[static_cast<std::size_t>(stem)] =
                static_cast<CellRole>(pick(seed, field, 6));
            const int meterPick = pick(seed, field ^ 0x4d45544552ULL, 8);
            result.stemMeter[static_cast<std::size_t>(stem)] = meterPick == 0 ? 0 : meterPick + 1;
        }
        result.interStemResponse = pick(seed, 0x524553504f4e5345ULL, 10);
        result.responsePolicy = pick(seed, 0x52455350504f4c49ULL, 8);
        result.responseMotif = pick(seed, 0x524553504d4f5449ULL, 3);
        result.responseDensity = pick(seed, 0x5245535044454e53ULL, 3);
        result.responseMemory = pick(seed, 0x524553504d454d4fULL, 3);
        for (int section = 0; section < 4; ++section)
            result.responseSectionIntent[static_cast<std::size_t>(section)] =
                pick(seed, 0x53454354494f4e00ULL | std::uint64_t(section), 6);
    }

    if (level == CompositionSeedLevel::wide &&
        unlocked(locks, CompositionSeedLock::timbreAndFx)) {
        result.timbreMode = pick(seed, 0x54494d425245ULL, 7);
        for (int stem = 0; stem < Song::stemCount; ++stem) {
            const auto index = static_cast<std::size_t>(stem);
            const auto field = 0x4658000000000000ULL | std::uint64_t(stem);
            result.stemEffect[index] = pick(seed, field, 10);
            result.stemFilterCutoff[index] = pick(seed, field ^ 0x46494c544552ULL, 4) == 0
                ? 0.0f : 250.0f + unit(seed, field ^ 0x4355544f4646ULL) * 11750.0f;
            constexpr int bitChoices[] = {0, 4, 6, 8, 12, 16};
            result.stemBitcrushBits[index] = bitChoices[pick(seed, field ^ 0x42495453ULL, 6)];
            result.stemReverbMix[index] = 0.05f + unit(seed, field ^ 0x5245564d4958ULL) * 0.5f;
            result.stemReverbDecay[index] = 0.25f + unit(seed, field ^ 0x524556444543ULL) * 2.75f;
            result.stemDelayMix[index] = 0.05f + unit(seed, field ^ 0x44454c4d4958ULL) * 0.5f;
            result.stemDelayFeedback[index] = unit(seed, field ^ 0x44454c46454544ULL) * 0.55f;
        }
    }

    if (level == CompositionSeedLevel::wide &&
        unlocked(locks, CompositionSeedLock::stemMix)) {
        result.shapeMode = pick(seed, 0x53484150454d4f44ULL, 4);
        result.shapeDrive = 0.7f + unit(seed, 0x5348415045445249ULL) * 1.8f;
        for (int stem = 0; stem < Song::stemCount; ++stem) {
            const auto index = static_cast<std::size_t>(stem);
            const auto field = 0x4d49580000000000ULL | std::uint64_t(stem);
            result.stemGain[index] = 0.65f + unit(seed, field ^ 0x4741494eULL) * 0.7f;
            result.stemPan[index] = -0.55f + unit(seed, field ^ 0x50414eULL) * 1.1f;
        }
    }

    if (level == CompositionSeedLevel::wide &&
        unlocked(locks, CompositionSeedLock::master))
        result.masterGain = 0.25f + unit(seed, 0x4d41535445524741ULL) * 0.45f;

    result.compositionSeedRecipeEnabled = true;
    result.compositionSeed = seed;
    result.compositionSeedAlgorithm = compositionSeedAlgorithmV1;
    result.compositionSeedLocks = locks;
    result.compositionSeedLevel = static_cast<std::uint8_t>(level);
    destination = std::move(result);
    return true;
}
} // namespace risco
