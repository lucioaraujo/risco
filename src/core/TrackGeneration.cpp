// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Lúcio de Araújo
#include "TrackGeneration.hpp"

#include "FormulaSeed.hpp"
#include "SongIO.hpp"
#include "Sound.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace risco {
namespace {

std::uint64_t mixSeed(std::uint64_t value) noexcept {
    value += 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31);
}

int stemForRole(VoiceRole role) noexcept {
    switch (role) {
        case VoiceRole::lead: return 0;
        case VoiceRole::harmony: return 1;
        case VoiceRole::bass: return 2;
    }
    return -1;
}

Sequence roleSequence(const Sequence& source, VoiceRole role) noexcept {
    Sequence result;
    result.end = source.end;
    for (int index = 0; index < std::clamp(source.count, 0, int(source.notes.size())); ++index) {
        const auto& note = source.notes[static_cast<size_t>(index)];
        if (note.role == role && result.count < int(result.notes.size())) {
            auto adjusted = note;
            if (!std::isfinite(adjusted.start) || !std::isfinite(adjusted.duration) ||
                adjusted.start < 0.0 || adjusted.start >= source.end) continue;
            adjusted.duration = std::min(adjusted.duration, source.end - adjusted.start);
            if (adjusted.duration <= 0.0) continue;
            const float roleGain = role == VoiceRole::harmony ? 3.0f :
                                   role == VoiceRole::bass ? 1.5f : 1.0f;
            adjusted.gain = std::min(0.9f, adjusted.gain * roleGain);
            result.notes[static_cast<size_t>(result.count++)] = adjusted;
        }
    }
    return result;
}

std::array<std::int16_t, Cell::waveformSize> waveformFor(const Sequence& sequence, WaveMode mode) noexcept {
    std::array<std::int16_t, Cell::waveformSize> result{};
    for (int index = 0; index < Cell::waveformSize; ++index) {
        const auto time = sequence.end * (double(index) + 0.5) / Cell::waveformSize;
        result[static_cast<size_t>(index)] = static_cast<std::int16_t>(std::lround(
            sampleAt(sequence, time, mode, nullptr, 16) * 32767.0f));
    }
    return result;
}

Sequence shapedPhrase(std::uint64_t seed, const SeedTrackOptions& options, int block,
                      int blocks, double bpm) {
    const auto motif = mixSeed(seed ^ 0x4d4f544946415243ULL);
    const int section = block * 4 / blocks;
    const int reprise = (blocks * 3 + 3) / 4;
    auto phraseSeed = motif;
    unsigned variation = static_cast<unsigned>(block % 4);
    unsigned version = 8;
    if (block == reprise) {
        variation = 0;
    } else if (options.form == SeedTrackForm::collage && block > 0) {
        const auto group = static_cast<std::uint64_t>(block / (3 - options.surprise));
        phraseSeed = mixSeed(motif ^ (0x434f4c4c414745ULL + group));
        version = 7u + static_cast<unsigned>(phraseSeed % 3);
    } else if (section == 2 && options.surprise > 0) {
        phraseSeed = mixSeed(motif ^ (block / (3 - options.surprise) + 0x434f4e5452415354ULL));
        version = 9;
    }
    return phraseFromSeed(phraseSeed, variation, version, bpm, 0x0f, 4, false, {});
}

void shapeTrackRole(Sequence& events, VoiceRole role, int block, int blocks,
                    std::uint64_t blockSeed, const SeedTrackOptions& options) {
    const int section = block * 4 / blocks;
    const bool ending = block == blocks - 1;
    const bool breakInMiddle = section == 2 && options.surprise > 0 &&
        blockSeed % 4 < static_cast<unsigned>(options.surprise);
    const float weight = ending ? 0.72f :
        section == 0 && role != VoiceRole::lead ? 0.60f :
        section == 2 && role == VoiceRole::lead ? 0.80f : 1.0f;
    const double cutoff = ending ? events.end * 0.75 :
                          breakInMiddle ? events.end * 0.62 : events.end;
    int kept = 0;
    for (int index = 0; index < events.count; ++index) {
        auto note = events.notes[static_cast<std::size_t>(index)];
        if (note.start >= cutoff) continue;
        note.duration = std::min(note.duration, cutoff - note.start);
        note.gain = std::clamp(note.gain * weight, 0.05f, 0.9f);
        events.notes[static_cast<std::size_t>(kept++)] = note;
    }
    if (kept > 0) events.count = kept;
}

void shapePulse(Sequence& events, VoiceRole role, SeedTrackPulse pulse, double bpm) {
    if (pulse == SeedTrackPulse::straight) return;
    const double beatSeconds = 60.0 / bpm;
    const auto swingTime = [beatSeconds](double seconds) {
        const double beat = seconds / beatSeconds;
        const double whole = std::floor(beat);
        const double part = beat - whole;
        const double shaped = part < 0.5 ? part * 1.24 : 0.62 + (part - 0.5) * 0.76;
        return (whole + shaped) * beatSeconds;
    };
    for (int i = 0; i < events.count; ++i) {
        auto& note = events.notes[static_cast<std::size_t>(i)];
        if (pulse == SeedTrackPulse::swing) {
            const double end = std::min(events.end, swingTime(note.start + note.duration));
            note.start = swingTime(note.start);
            note.duration = end - note.start;
        } else {
            note.duration *= role == VoiceRole::harmony ? 0.83 : 0.66;
        }
    }
}

void applyTrackPalette(Song& song, SeedTrackPalette palette) {
    switch (palette) {
        case SeedTrackPalette::original: break;
        case SeedTrackPalette::resonant:
            song.timbreMode = 0;
            song.stemGain = {{0.76f, 0.74f, 0.70f}};
            song.stemPan = {{-0.22f, 0.26f, 0.0f}};
            song.stemEffect[1] = 8;
            song.stemReverbMix[1] = 0.21f;
            song.stemFxAutomation[1] = {};
            break;
        case SeedTrackPalette::chip:
            song.timbreMode = 2;
            song.stemGain = {{0.72f, 0.55f, 0.83f}};
            song.stemPan = {{0.22f, -0.18f, 0.0f}};
            song.stemEffect[0] = 5;
            song.stemDelayMix[0] = 0.14f;
            song.stemFxAutomation[0] = {true, {{false, true, false}}};
            break;
        case SeedTrackPalette::grain:
            song.stemGain = {{0.72f, 0.69f, 0.72f}};
            song.stemPan = {{-0.29f, 0.16f, 0.0f}};
            song.stemEffect[0] = 3;
            song.stemFxAutomation[0] = {true, {{true, false, true}}};
            song.stemEffect[1] = 2;
            song.stemFxAutomation[1] = {};
            break;
    }
}

} // namespace

FormulaOrigin seedTrackOrigin(const SeedTrackOptions& options) {
    const bool shaped = options.form != SeedTrackForm::original;
    auto description = shaped
        ? std::string(options.form == SeedTrackForm::arc ? "Form: arc" : "Form: collage") +
          "; surprise: " + std::to_string(options.surprise)
        : "Seed track generator: embedded generated note events";
    if (options.pulse != SeedTrackPulse::straight || options.palette != SeedTrackPalette::original)
        description += "; pulse: " + std::to_string(static_cast<unsigned>(options.pulse)) +
                       "; palette: " + std::to_string(static_cast<unsigned>(options.palette));
    return {"Risco", std::move(description), shaped ? "risco.track.seed.v2" : "risco.track.seed.v1",
            0, 0, "AGPL-3.0-or-later"};
}

bool generateSeedTrack(std::uint64_t seed, const SeedTrackOptions& options,
                       Song& destination) {
    if (seed == 0 || options.durationBars < 16 || options.durationBars > 84 ||
        options.durationBars % 4 != 0 ||
        options.form < SeedTrackForm::original || options.form > SeedTrackForm::collage ||
        options.surprise < 0 || options.surprise > 2 ||
        options.pulse < SeedTrackPulse::straight || options.pulse > SeedTrackPulse::cut ||
        options.palette < SeedTrackPalette::original || options.palette > SeedTrackPalette::grain ||
        (options.bpm != 0.0 && (!std::isfinite(options.bpm) || options.bpm < 40.0 || options.bpm > 240.0)))
        return false;
    Song candidate;
    candidate.bpm = options.bpm > 0.0 ? options.bpm :
        88.0 + double(mixSeed(seed ^ 0x545241434b54454dULL) % 45u);
    candidate.beatsPerBar = 4;
    candidate.timbreMode = 1;
    candidate.masterGain = 0.28f;
    candidate.stemGain = {{0.82f, 0.62f, 0.78f}};
    candidate.stemPan = {{-0.16f, 0.12f, 0.0f}};
    candidate.stemRole = {{CellRole::melody, CellRole::harmony, CellRole::bass}};
    candidate.compositionSeedRecipeEnabled = true;
    candidate.compositionSeed = seed;
    candidate.compositionSeedAlgorithm = compositionSeedAlgorithmV1;
    candidate.compositionSeedLocks = compositionSeedDefaultLocks;
    candidate.compositionSeedLevel = static_cast<std::uint8_t>(CompositionSeedLevel::wide);
    const bool shaped = options.form != SeedTrackForm::original;
    if (shaped && options.surprise > 0) {
        candidate.stemEffect[1] = 8;
        candidate.stemReverbMix[1] = 0.13f;
        candidate.stemFxAutomation[1] = {true, {{false, true, false}}};
        if (options.form == SeedTrackForm::collage && options.surprise == 2) {
            candidate.stemEffect[0] = (mixSeed(seed) & 1u) != 0 ? 3 : 6;
            candidate.stemDelayMix[0] = 0.18f;
            candidate.stemFxAutomation[0] = {true, {{false, true, false}}};
        }
    }
    applyTrackPalette(candidate, options.palette);

    const int phraseBlocks = options.durationBars / 4;
    constexpr std::array<VoiceRole, Song::stemCount> roles{{
        VoiceRole::lead, VoiceRole::harmony, VoiceRole::bass}};
    constexpr std::array<const char*, Song::stemCount> names{{"Melodia", "Harmonia", "Baixo"}};
    const auto origin = seedTrackOrigin(options);
    const auto thumbnailMode = options.palette == SeedTrackPalette::original
        ? WaveMode::quantizedSine : noteWaveMode(candidate.timbreMode);

    for (int block = 0; block < phraseBlocks; ++block) {
        const auto blockSeed = mixSeed(seed ^ (0x424c4f434b000001ULL + std::uint64_t(block)));
        const unsigned version = 7u + static_cast<unsigned>(block % 3);
        const auto phrase = shaped ? shapedPhrase(seed, options, block, phraseBlocks, candidate.bpm) :
            phraseFromSeed(blockSeed, static_cast<unsigned>(block % 7), version,
                                           candidate.bpm, 0x0f, candidate.beatsPerBar,
                                           candidate.compoundSixEight, {});
        if (phrase.count < 1 || phrase.end <= 0.0) return false;

        for (int roleIndex = 0; roleIndex < Song::stemCount; ++roleIndex) {
            const auto role = roles[static_cast<size_t>(roleIndex)];
            auto events = roleSequence(phrase, role);
            if (events.count < 1) return false;
            if (shaped) shapeTrackRole(events, role, block, phraseBlocks, blockSeed, options);
            shapePulse(events, role, options.pulse, candidate.bpm);
            const auto label = std::string(names[static_cast<size_t>(roleIndex)]) + " " +
                               std::to_string(block + 1);
            const auto cellId = storeFormulaEventCell(candidate, "0", label,
                waveformFor(events, thumbnailMode), events, FormulaScalePreset::major,
                FormulaScaleMotion::follow, 0, candidate.bpm, origin, 0, 1, 0.0, 0,
                {{-1, -1, -1, -1}}, false, 0);
            if (cellId == 0) return false;
            auto* cell = const_cast<Cell*>(findCell(candidate, cellId));
            if (cell == nullptr) return false;
            cell->role = roleIndex == 0 ? CellRole::melody :
                         roleIndex == 1 ? CellRole::harmony : CellRole::bass;
            const int startBeat = block * candidate.beatsPerBar * 4;
            if (!placeCell(candidate, {cellId, startBeat, CellSpan::fourBars, 1, 0, 0,
                                       stemForRole(roles[static_cast<size_t>(roleIndex)])}))
                return false;
        }
    }

    if (candidate.cellCount != phraseBlocks * Song::stemCount ||
        candidate.placementCount != phraseBlocks * Song::stemCount) return false;
    destination = std::move(candidate);
    return true;
}

bool generateSeedTrack(std::uint64_t seed, Song& destination) {
    return generateSeedTrack(seed, SeedTrackOptions{}, destination);
}

} // namespace risco
