// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Lúcio de Araújo
#pragma once

#include "BytebeatExpression.hpp"
#include "Composition.hpp"
#include "FormulaScaleMachine.hpp"

namespace risco {

struct FormulaComposition {
    Sequence sequence;
    int bars = 0;
    int steps = 0;
    int rests = 0;
    int machineSections = 0;
    int groupingVariant = 0; // Stable choice derived from the expression's first byte.
};

const char* formulaMetricGrouping(int beatsPerBar, int variant) noexcept;
int formulaMetricGroupingCount(int beatsPerBar) noexcept;

// Maps an unsigned byte (normalized from Floatbeat where applicable) at each
// selected subdivision. Zero subdivision preserves the legacy sixteenth grid,
// every other position in accompany mode. At most 64 notes including harmony.
// to a pitch in the selected machine scale, gate length and bounded accent. The output is a
// deterministic, four-bar-or-shorter phrase and allocates nothing.
FormulaComposition composeFormulaEvents(const BytebeatProgram& formula,
                                        double bpm,
                                        int beatsPerBar,
                                        FormulaScalePreset scale,
                                        FormulaScaleMotion motion,
                                        int tonicSemitones = 0,
                                        int requestedGrouping = -1,
                                        int progression = 0, int barsPerChord = 1, double swing = 0.0,
                                        int subdivision = 0, bool compoundSixEight = false,
                                        const std::array<int, 4>& chordRoots = {{-1, -1, -1, -1}}, bool syncopation = false, int pulsesPerBar = 0) noexcept;

}
