// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Lúcio de Araújo
#pragma once

#include <array>
#include <cstdint>

namespace risco {

enum class FormulaScalePreset : std::uint8_t {
    major, naturalMinor, dorian, pentatonic,
    harmonicMinor, melodicMinor, phrygian, mixolydian,
    lydian, bluesMinor, wholeTone, chromatic,
    bebopDominant, bebopMajor, alteredDominant,
    diminishedHalfWhole, diminishedWholeHalf, lydianDominant,
    freygish, misheberakh,
    harmonicMajor, neapolitanMinor, hungarianMinor,
    doubleHarmonic, augmented, persian, prometheus
};
enum class FormulaScaleMotion : std::uint8_t { follow, counter, transfigure, connect, cadence, accompany, chordGuide };

struct FormulaMachineScale {
    double tonicCents = 0.0;
    std::array<double, 12> degreesCents{};
    std::uint8_t degreeCount = 0;
    std::uint8_t family = 0;
    double periodCents = 1200.0;
};

FormulaMachineScale makeFormulaScale(FormulaScalePreset preset) noexcept;
FormulaMachineScale evolveFormulaScale(const FormulaMachineScale& previous,
                                       FormulaScaleMotion motion,
                                       std::uint32_t gesture) noexcept;
double formulaScalePitchCents(const FormulaMachineScale& scale, int degree) noexcept;
const char* formulaScaleName(FormulaScalePreset preset) noexcept;
const char* formulaScaleMotionName(FormulaScaleMotion motion) noexcept;

}
