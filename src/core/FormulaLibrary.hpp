// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Lúcio de Araújo
#pragma once

#include <array>
#include <iosfwd>
#include <string>

namespace risco {

struct FormulaRecord {
    std::string name;
    std::string expression;
    std::string author;
    std::string source;
    std::string sourceId;
    std::string license;
};

struct FormulaLibrary {
    std::array<FormulaRecord, 512> records{};
    int count = 0;
};

bool addFormula(FormulaLibrary& library, FormulaRecord record);
bool saveFormulaLibrary(std::ostream& out, const FormulaLibrary& library);
bool loadFormulaLibrary(std::istream& in, FormulaLibrary& destination);

}
