// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Lúcio de Araújo
#pragma once
#include <cstdint>
#include <string>
#include "Composition.hpp"

namespace risco {
// Version 1: one numeric seed yields four related, editable expressions.
// The generated strings are original Risco material and fit BytebeatExpression.
std::string formulaFromSeedV1(std::uint64_t seed, unsigned variation);
// Version 2: four related stages, from one rhythmic voice to a modulated
// two-layer expression. The series seed fixes the shared musical material.
std::string formulaFromSeedV2(std::uint64_t seed, unsigned stage);
// Version 3 chooses one of eight calculation families from the seed, then
// adds bounded rhythmic, modular and bitwise layers over six stages.
std::string formulaFromSeedV3(std::uint64_t seed, unsigned stage);
// Returns an empty string when the parents cannot fit the bounded parser.
std::string recombineFormulaV1(const std::string& first, const std::string& second,
                               std::uint64_t seed);
// Version 2 explores arithmetic and bitwise relations, returning empty when
// the bounded Bytebeat compiler cannot represent the combined expression.
std::string recombinationOperatorV2(std::uint64_t seed);
std::string recombineFormulaV2(const std::string& first, const std::string& second,
                               std::uint64_t seed);
struct FormulaGlitch {
    std::string expression;
    std::string kind;
    std::uint32_t parameterA = 0;
    std::uint32_t parameterB = 0;
};
enum class FormulaGlitchKind { mask, xorPulse, wrap };
// Makes a bounded, deterministic derivative; the parent formula is untouched.
FormulaGlitch glitchFormulaV1(const std::string& source, std::uint64_t seed);
// Explicit glitch family and bounded 1–100 intensity for direct user control.
FormulaGlitch glitchFormulaV1(const std::string& source, std::uint64_t seed,
                              FormulaGlitchKind kind, unsigned intensity);
Sequence phraseFromSeedV1(std::uint64_t seed, unsigned variation,
                          double bpm = 120.0) noexcept;
Sequence phraseFromSeedV2(std::uint64_t seed, unsigned variation,
                          double bpm = 120.0) noexcept;
Sequence phraseFromSeedV3(std::uint64_t seed, unsigned variation,
                          double bpm = 120.0) noexcept;
Sequence phraseFromSeedV4(std::uint64_t seed, unsigned variation,
                          double bpm = 120.0) noexcept;
Sequence phraseFromSeedV5(std::uint64_t seed, unsigned variation,
                          double bpm = 120.0) noexcept;
Sequence phraseFromSeed(std::uint64_t seed, unsigned variation,
                        unsigned version, double bpm = 120.0,
                        unsigned sectionMask = 0x0f, int beatsPerBar = 4,
                        bool compoundSixEight = false, const PhraseMeters& meters = {}) noexcept;
std::uint64_t formulaSeedFromText(const std::string& text) noexcept;
std::uint64_t formulaSeedFromDrawing(const Inscription& inscription) noexcept;
}
