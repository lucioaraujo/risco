// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Lúcio de Araújo
#pragma once

#include "core/FormulaLibrary.hpp"
#include <juce_core/juce_core.h>
#include <vector>

namespace risco {
// Reads a user-selected Dollchan all.gz file outside the audio callback.
// Only bounded, 8 kHz, unsigned, mono formulas accepted by our parser survive.
juce::Result loadDollchanCatalog(const juce::File& file,
                                std::vector<FormulaRecord>& destination);
}
