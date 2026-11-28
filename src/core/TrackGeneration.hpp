// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Lúcio de Araújo
#pragma once

#include "CompositionSeed.hpp"

#include <string_view>

namespace risco {

enum class SeedTrackForm { original, arc, collage };
enum class SeedTrackPulse { straight, swing, cut };
enum class SeedTrackPalette { original, resonant, chip, grain };
enum class SeedTrackMaterial { none, text, drawing };
enum class SeedTrackCue { none, breath, rise, fall };
enum class SeedTrackMotifReturn { automatic, literal, mirror, mirrorRetrograde };
enum class SeedTrackPartName { opening, development, breakSection, returnSection, ending };
enum class SeedTrackPartDynamics { automatic, steady, crescendo, diminuendo, arch };
inline constexpr int seedTrackMinBars = 16;
inline constexpr int seedTrackMaxBars = 84;
inline constexpr int seedTrackPartCount = 5;

struct SeedTrackPartEdit {
    int blocks = 0; // 0 keeps the generated duration.
    unsigned channels = 0; // 0 keeps the generated set.
    SeedTrackPartDynamics dynamics = SeedTrackPartDynamics::automatic;
    bool repeatFirst = false;

    bool automatic() const noexcept {
        return blocks == 0 && channels == 0 &&
               dynamics == SeedTrackPartDynamics::automatic && !repeatFirst;
    }
};

struct SeedTrackMotifNote {
    int tick = 0; // Half beats from the first attack.
    int semitones = 0; // Pitch relative to the first attack.
    friend bool operator==(const SeedTrackMotifNote& a, const SeedTrackMotifNote& b) noexcept {
        return a.tick == b.tick && a.semitones == b.semitones;
    }
};

struct SeedTrackMotif {
    std::array<SeedTrackMotifNote, 5> notes{};
    int count = 0;
    friend bool operator==(const SeedTrackMotif& a, const SeedTrackMotif& b) noexcept {
        return a.count == b.count && a.notes == b.notes;
    }
};

struct SeedTrackOptions {
    double bpm = 0.0; // 0 derives a stable tempo from the seed.
    int durationBars = 64; // 16..84, in four-bar phrase blocks.
    SeedTrackForm form = SeedTrackForm::original;
    int surprise = 1; // 0 restrained, 1 medium, 2 high.
    SeedTrackPulse pulse = SeedTrackPulse::straight;
    SeedTrackPalette palette = SeedTrackPalette::original;
    SeedTrackMaterial material = SeedTrackMaterial::none;
    std::uint64_t materialSeed = 0; // Fingerprint of source text/drawing, never the generated track seed.
    bool arrangeByParts = false;
    SeedTrackCue cue = SeedTrackCue::none;
    SeedTrackMotif motif;
    int motifStem = -1;
    int motifReturnStem = -1; // Use motifStem when a separate return voice is absent.
    std::array<SeedTrackPartEdit, seedTrackPartCount> partEdits{};
    int generatorVersion = 1; // V1 preserves the original motif; V2 develops it on return.
    SeedTrackMotifReturn motifReturn = SeedTrackMotifReturn::automatic;
};

struct SeedTrackPart {
    SeedTrackPartName name = SeedTrackPartName::opening;
    int firstBlock = 0;
    int endBlock = 0;
    unsigned channels = 0; // Bit per stem in grid order.
    float firstGain = 1.0f;
    float lastGain = 1.0f;
    bool repeatFirst = false;
    int sourceFirstBlock = 0;
    SeedTrackPartDynamics dynamics = SeedTrackPartDynamics::automatic;
};

SeedTrackCue seedTrackTextCue(std::string_view text) noexcept;
SeedTrackCue seedTrackDrawingCue(const Inscription& drawing) noexcept;
SeedTrackMotif seedTrackMotif(const Sequence& source) noexcept;
bool validSeedTrackMotif(const SeedTrackMotif& motif) noexcept;
int alternateMotifStem(std::uint64_t seed, int openingStem) noexcept;
std::array<SeedTrackPart, seedTrackPartCount> seedTrackParts(
    std::uint64_t seed, const SeedTrackOptions& options) noexcept;
// Restores the generated-track recipe from its saved project provenance.
// Returns false for hand-edited, legacy, incomplete or inconsistent projects.
bool seedTrackOptionsFromSong(const Song& song, SeedTrackOptions& options);
FormulaOrigin seedTrackOrigin(const SeedTrackOptions& options);
FormulaOrigin legacySeedTrackOrigin(const SeedTrackOptions& options);
// True for every versioned generated-track origin, including future generator revisions.
bool isSeedTrackOrigin(const FormulaOrigin& origin) noexcept;

// Builds an editable 16..84-bar project in four-bar blocks from a stable seed.
// The result is committed only when every phrase, stem and placement is valid.
bool generateSeedTrack(std::uint64_t seed, Song& destination);
bool generateSeedTrack(std::uint64_t seed, const SeedTrackOptions& options,
                       Song& destination);

} // namespace risco
