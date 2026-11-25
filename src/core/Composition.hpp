// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Lúcio de Araújo
#pragma once

#include <array>
#include <string>

namespace risco {
struct Point { float x = 0.0f, y = 0.0f; };
struct Stroke {
    std::array<Point, 128> points{};
    int count = 0;
    bool crossed = false;
};
struct Inscription {
    std::array<Stroke, 12> strokes{};
    int count = 0;
    std::string ascii;
};
enum class VoiceRole { lead, bass, harmony };
struct Note {
    double start = 0.0, duration = 0.0, frequency = 0.0;
    float gain = 0.0f;
    VoiceRole role = VoiceRole::lead;
    float pulseDuty = 0.25f;
};
struct Sequence {
    std::array<Note, 64> notes{};
    int count = 0;
    double end = 0.0;
    double repeatPeriod = 0.0; // Optional live preview loop; song placements repeat independently.
};
struct TemporalGestureCurve {
    std::array<float, 3> values{{0.0f, 0.5f, 1.0f}};
    bool enabled = false;
};
struct SectionEdit {
    float density = 1.0f;
    float gain = 1.0f;
    int octaves = 0;
};
using SectionEdits = std::array<SectionEdit, 4>;
using PhraseMeters = std::array<int, 4>; // 0 inherit, 2..7 quarter meters, 8 means 6/8.
using PhraseSectionBars = std::array<int, 4>; // 1..4 reference bars occupied by each section.
bool validPhraseMeters(const PhraseMeters& meters) noexcept;
bool validPhraseSectionBars(const PhraseSectionBars& bars) noexcept;
std::array<int, 4> phraseMeterWeights(const PhraseMeters& meters, int beatsPerBar) noexcept;
std::array<int, 4> phraseSectionWeights(const PhraseMeters& meters,
                                        const PhraseSectionBars& bars,
                                        int beatsPerBar) noexcept;
bool validSectionEdits(const SectionEdits& edits) noexcept;
bool validTemporalGestureCurve(const TemporalGestureCurve& curve) noexcept;
bool validTemporalGestureRange(int semitones) noexcept;
// Stretches each existing section to 1..4 bars without adding attacks.
Sequence stretchSections(const Sequence& source,
                         const std::array<int, 4>& sourceWeights,
                         const PhraseSectionBars& bars) noexcept;
// A reversible interpretation of a source sequence. Does not change its end or timing.
Sequence editSections(const Sequence& source, const SectionEdits& edits, int sections = 4,
                      const std::array<int, 4>& weights = {{1,1,1,1}}) noexcept;
// 0 original; 1 pitch contour, 2 rhythm, 3 dynamics, 4 reverse time;
// 5 accelerate, 6 decelerate, 7 breathe, 8 authorial time,
// 9 authorial crescendo, 10 authorial diminuendo, 11/12 rising/falling pitch,
// 13 authorial pitch arch that returns to the source register.
// Gestures preserve the source extent.
Sequence interpretInput(const Sequence& source, int mode, float strength = 1.0f,
                        const TemporalGestureCurve& curve = {}, int rangeSemitones = 12) noexcept;
enum class CellSpan { beat, bar, fourBars, twoBars };
struct CellPlan {
    double bpm = 120.0;
    int beatsPerBar = 4;
    CellSpan span = CellSpan::bar;
    int repeats = 2;
};
Sequence compose(const Inscription& inscription) noexcept;
Sequence composeText(const std::string& text) noexcept;
Sequence composeInput(const Inscription& inscription) noexcept;
Sequence arrange(const Sequence& source, CellPlan plan) noexcept;
}
