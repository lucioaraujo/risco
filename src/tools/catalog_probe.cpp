// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Lúcio de Araújo
#include "app/DollchanCatalog.hpp"
#include <iostream>

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    std::vector<risco::FormulaRecord> formulas;
    const auto result = risco::loadDollchanCatalog(juce::File(argv[1]), formulas);
    if (result.failed()) {
        std::cerr << result.getErrorMessage() << '\n';
        return 1;
    }
    std::cout << formulas.size() << " compatible formulas; first source id: "
              << formulas.front().sourceId << '\n';
}
