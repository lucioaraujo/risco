// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Lúcio de Araújo
#pragma once

#include "Composition.hpp"
#include "FormulaScaleMachine.hpp"
#include "StemAutomation.hpp"
#include <array>
#include <cstdint>
#include <string>

namespace risco {

struct FormulaOrigin {
    std::string author;
    std::string source;
    std::string sourceId;
    std::uint32_t parentA = 0;
    std::uint32_t parentB = 0;
    std::string license;
};

enum class CellRole { free, pulse, bass, harmony, melody, texture };

// The bank owns inscriptions; grid placements refer to stable IDs.
// All mutations happen outside the audio callback.
struct Cell {
    static constexpr int waveformSize = 128;
    static constexpr std::size_t maxTextBytes = 16384;
    std::uint32_t id = 0;
    std::uint32_t revision = 1;
    std::string name;
    std::string importProvenance; // Bounded chain of source projects/cell IDs, separate from authorship.
    Inscription inscription;
    bool generatedPhrase = false;
    bool bytebeatFormula = false;
    bool formulaEventPhrase = false;
    std::string formulaExpression;
    FormulaScalePreset formulaScale = FormulaScalePreset::major;
    std::uint8_t formulaTonic = 0; // Pitch class, C=0 through B=11.
    FormulaScaleMotion formulaMotion = FormulaScaleMotion::follow;
    Sequence formulaEvents;
    double formulaEventBpm = 120.0;
    int formulaProgression = 0; // 0 return, 1 I-vi-IV-V, 2 tonic pedal.
    int formulaHarmonicBars = 1;
    double formulaSwing = 0.0;
    int formulaSubdivision = 0; // 0 preserves legacy rhythm; 1/2/3/4/6 steps per quarter.
    bool formulaSyncopation = false;
    int formulaPulsesPerBar = 0; // 0 subdivision grid; otherwise 2/3/5/7 evenly spaced pulses.
    std::array<int, 4> formulaChordRoots{{-1, -1, -1, -1}}; // -1 auto, -2 silence, otherwise degree.
    SectionEdits sectionEdits{};
    bool favorite = false;
    int inputInterpretation = 0;
    float inputInterpretationStrength = 1.0f;
    TemporalGestureCurve inputGestureCurve;
    int inputGestureRangeSemitones = 12;
    FormulaOrigin formulaOrigin;
    std::uint64_t phraseSeed = 0;
    std::uint32_t phraseVariation = 0;
    std::uint32_t phraseVersion = 1; // Generator/form: V7 stateful, V8 arch, V9 spiral.
    std::uint8_t phraseSectionMask = 0x0f; // Bit per bar; at least one section is active.
    PhraseMeters phraseMeters{};
    PhraseSectionBars phraseSectionBars{{1, 1, 1, 1}};
    CellRole role = CellRole::free; // Compositional intent; does not change the cell audio.
    std::array<std::int16_t, waveformSize> waveform{};
    int waveformCount = 0;
};

struct Placement {
    std::uint32_t cellId = 0;
    int startBeat = 0;
    CellSpan span = CellSpan::bar;
    int repeats = 1;
    int sourceStartBeat = 0; // Início do trecho dentro da célula; 0 em músicas anteriores.
    int lengthBeats = 0; // 0 usa span × repeats; valor positivo é o recorte na grade.
    int stem = -1; // -1 pede distribuição automática; persistido como stem 0–2.
};

struct Song {
    static constexpr int stemCount = 3;
    bool compositionSeedRecipeEnabled = false;
    std::uint64_t compositionSeed = 0;
    std::uint32_t compositionSeedAlgorithm = 0;
    std::uint32_t compositionSeedLocks = 0;
    std::uint8_t compositionSeedLevel = 0; // 0 manual; 1 assisted; 2 ensemble; 3 wide.
    std::uint32_t preferredTakeId = 0; // Bank selection on project open; never replaces grid cells.
    double bpm = 120.0;
    int bitDepth = 8;
    int timbreMode = 0; // 0 original, 1 trio, 2..4 AY study, 5 AY-3-8910 historical, 6 YM2149 historical.
    int ayNoisePeriod = 16; // Study divider 1..31; no hardware register bus.
    int ayEnvelopeMode = 0; // 0 Risco original; 1 decay, 2 attack/hold, 3 triangle, 4 repeat decay.
    float ayEnvelopeRamp = 0.5f; // Requested duration of a 16-step ramp, 0.125..2 s.
    int historicalAyTexture = 0; // 0 tone, 1 tone+noise, 2 noise.
    int historicalAyNoisePeriod = 16; // Hardware divider 1..31.
    int historicalAyEnvelope = 0; // 0 fixed, 1 decay, 2 rise/hold, 3 triangle, 4 repeat decay.
    float historicalAyEnvelopeRamp = 0.5f; // Shared hardware-envelope ramp, 0.125..2 s.
    int interStemResponse = 0; // 0 off; 1–9 encode direction and a bounded pitch interval.
    int responsePolicy = 0; // 0 original, 1 free space, 2 role/register, 3 fixed harmony, 4 follow, 5 tension, 6 support, 7 internal rests.
    int responseMotif = 0; // 0 complete, 1 structural attacks, 2 essential contour.
    int responseDensity = 0; // 0=100%, 1=50%, 2=25%; deterministic and keeps the final selected attack.
    int responseMemory = 0; // 0 independent, 1 avoid repeated register, 2 connect previous ending.
    std::array<int, 4> responseSectionIntent{}; // 0 auto, 1 echo, 2 contrast, 3 tension, 4 resolve, 5 silence.
    std::array<int, stemCount> stemEffect{};
    std::array<GainAutomation, stemCount> stemAutomation{};
    std::array<PanAutomation, stemCount> stemPanAutomation{};
    std::array<FxAutomation, stemCount> stemFxAutomation{};
    std::array<float, stemCount> stemFilterCutoff{}; // 0: preset default; otherwise 80..16000 Hz.
    std::array<int, stemCount> stemBitcrushBits{}; // 0: preset default; otherwise 2..16 bits for Grão/Lo-fi.
    std::array<ParameterAutomation, stemCount> stemBitcrushAutomation{{
        ParameterAutomation{false, {{0,0,0}}}, ParameterAutomation{false, {{0,0,0}}}, ParameterAutomation{false, {{0,0,0}}}}};
    std::array<float, stemCount> stemReverbMix{{0.25f, 0.25f, 0.25f}};
    std::array<float, stemCount> stemReverbDecay{}; // 0: preset; 0.25..3 seconds, approximate low-frequency decay.
    std::array<float, stemCount> stemDelayMix{{0.3f, 0.3f, 0.3f}};
    std::array<float, stemCount> stemDelayFeedback{{0.35f, 0.35f, 0.35f}};
    int formulaMetricGrouping = -1; // -1 chooses a stable grouping from the formula.
    int shapeMode = 0; // 0: bypass, 1: tanh, 2: cubic, 3: fold
    float shapeDrive = 1.0f;
    float masterGain = 0.35f;
    int beatsPerBar = 4;
    bool compoundSixEight = false; // 6 eighths = 3 quarter units; BPM remains quarter notes.
    std::array<int, stemCount> stemMeter{}; // 0 inherits song; 2..7 quarter meter; 8 means 6/8.
    std::array<CellRole, stemCount> stemRole{{CellRole::melody, CellRole::harmony, CellRole::bass}};
    std::array<float, stemCount> stemPan{}; // -1: L, 0: C, +1: R.
    std::array<float, stemCount> stemGain{{1.0f, 1.0f, 1.0f}};
    std::array<Cell, 64> bank{};
    int cellCount = 0;
    std::uint32_t nextCellId = 1;
    std::array<Placement, 128> grid{};
    int placementCount = 0;
    std::array<ParameterAutomation, stemCount> stemFilterAutomation{};
    std::array<ParameterAutomation, stemCount> stemDelayMixAutomation{{
        ParameterAutomation{false, {{0.3f, 0.3f, 0.3f}}}, ParameterAutomation{false, {{0.3f, 0.3f, 0.3f}}}, ParameterAutomation{false, {{0.3f, 0.3f, 0.3f}}}}};
    std::array<ParameterAutomation, stemCount> stemDelayFeedbackAutomation{{
        ParameterAutomation{false, {{0.35f, 0.35f, 0.35f}}}, ParameterAutomation{false, {{0.35f, 0.35f, 0.35f}}}, ParameterAutomation{false, {{0.35f, 0.35f, 0.35f}}}}};
    std::array<ParameterAutomation, stemCount> stemReverbMixAutomation{{
        ParameterAutomation{false, {{0.25f, 0.25f, 0.25f}}}, ParameterAutomation{false, {{0.25f, 0.25f, 0.25f}}}, ParameterAutomation{false, {{0.25f, 0.25f, 0.25f}}}}};
    std::array<ParameterAutomation, stemCount> stemReverbDecayAutomation{{
        ParameterAutomation{false, {{0.0f, 0.0f, 0.0f}}}, ParameterAutomation{false, {{0.0f, 0.0f, 0.0f}}}, ParameterAutomation{false, {{0.0f, 0.0f, 0.0f}}}}};
};

struct CellAudioProfile {
    bool hasEvents = false;
    bool hasActivity = false;
    bool hasLevel = false;
    bool restLike = false;
    float eventsPerBar = 0.0f;
    float meanMidi = 0.0f;
    float activeFraction = 0.0f;
    float rms = 0.0f; // Relative 0–1 estimate from the archived preview waveform.
};

struct PlacementSuggestion {
    bool valid = false;
    int startBeat = 0;
    int stem = 0;
    int concurrentStems = 0;
    bool motifOnStem = false;
    bool roleOnStem = false;
    bool sameRoleConcurrent = false;
    bool stemRoleMatch = false;
    CellAudioProfile profile;
};

// Rejects incomplete strokes and values that cannot be saved in the project file.
bool validInscription(const Inscription& source) noexcept;
// Returns zero if the bank is full or the cell data is invalid.
std::uint32_t storeCell(Song& song, const Inscription& source, std::string name);
std::uint32_t storeGeneratedCell(Song& song, std::uint64_t seed,
                                 std::uint32_t variation, std::string name,
                                 const std::array<std::int16_t, Cell::waveformSize>& waveform,
                                 std::uint32_t phraseVersion = 2,
                                 std::uint8_t phraseSectionMask = 0x0f);
std::uint32_t storeFormulaCell(Song& song, std::string expression, std::string name,
                               const std::array<std::int16_t, Cell::waveformSize>& waveform,
                               FormulaOrigin origin = {});
std::uint32_t storeFormulaEventCell(Song& song, std::string expression, std::string name,
                                    const std::array<std::int16_t, Cell::waveformSize>& waveform,
                                    const Sequence& events, FormulaScalePreset scale,
                                    FormulaScaleMotion motion, int tonicSemitones, double bpm,
                                    FormulaOrigin origin = {}, int progression = 0, int harmonicBars = 1,
                                    double swing = 0.0, int subdivision = 0,
                                    const std::array<int, 4>& chordRoots = {{-1, -1, -1, -1}}, bool syncopation = false, int pulsesPerBar = 0);
// Updating a bank cell changes all grid occurrences that reference its ID.
bool updateCell(Song& song, std::uint32_t id, const Inscription& source);
const Cell* findCell(const Song& song, std::uint32_t id) noexcept;
CellAudioProfile describeCellAudio(const Cell& cell, double bpm, int beatsPerBar) noexcept;
bool placeCell(Song& song, Placement placement) noexcept;
// Suggests an aligned, free lane near fromBeat; never changes the song.
PlacementSuggestion suggestPlacement(const Song& song, std::uint32_t cellId,
                                     CellSpan span, int repeats, int fromBeat,
                                     bool balanceSound = false) noexcept;
double placementStartSeconds(const Song& song, const Placement& placement) noexcept;
double placementLengthSeconds(const Song& song, const Placement& placement) noexcept;

}
