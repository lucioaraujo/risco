// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Lúcio de Araújo
#include "FormulaLibrary.hpp"
#include "BytebeatExpression.hpp"
#include <iomanip>
#include <istream>
#include <ostream>
#include <sstream>
#include <utility>

namespace risco {
namespace {
bool safe(const std::string& value, size_t max) {
    return value.size() <= max && value.find_first_of("\r\n\0", 0, 3) == std::string::npos;
}
bool valid(const FormulaRecord& item) {
    if (item.expression.empty() || !safe(item.expression, 128) ||
        !safe(item.name, 128) || !safe(item.author, 128) ||
        !safe(item.source, 256) || !safe(item.sourceId, 64) ||
        !safe(item.license, 64)) return false;
    BytebeatProgram program;
    std::string error;
    return compileBytebeat(item.expression, program, error);
}
bool readLine(std::istream& in, std::string& value) {
    value.clear();
    for (int i = 0; i < 2048; ++i) {
        const int next = in.get();
        if (next == std::char_traits<char>::eof()) return !value.empty() && in.eof();
        if (next == '\n') return true;
        if (next == '\0') return false;
        value.push_back(static_cast<char>(next));
    }
    return false;
}
}

bool addFormula(FormulaLibrary& library, FormulaRecord record) {
    if (library.count < 0 || library.count >= int(library.records.size()) || !valid(record))
        return false;
    for (int i = 0; i < library.count; ++i) {
        const auto& existing = library.records[static_cast<size_t>(i)];
        if (existing.expression == record.expression &&
            existing.sourceId == record.sourceId) return false;
    }
    library.records[static_cast<size_t>(library.count++)] = std::move(record);
    return true;
}

bool saveFormulaLibrary(std::ostream& out, const FormulaLibrary& library) {
    if (library.count < 0 || library.count > int(library.records.size())) return false;
    out << "RISCO_FORMULAS_V2 " << library.count << '\n';
    for (int i = 0; i < library.count; ++i) {
        const auto& item = library.records[static_cast<size_t>(i)];
        if (!valid(item)) return false;
        out << "F " << std::quoted(item.name) << ' ' << std::quoted(item.expression)
            << ' ' << std::quoted(item.author) << ' ' << std::quoted(item.source)
            << ' ' << std::quoted(item.sourceId) << ' '
            << std::quoted(item.license) << '\n';
    }
    return bool(out);
}

bool loadFormulaLibrary(std::istream& in, FormulaLibrary& destination) {
    std::string line;
    if (!readLine(in, line)) return false;
    int count = 0;
    bool version2 = false;
    {
        std::istringstream parser(line);
        std::string tag;
        if (!(parser >> tag >> count) ||
            (tag != "RISCO_FORMULAS_V1" && tag != "RISCO_FORMULAS_V2") ||
            count < 0 || count > int(destination.records.size()) ||
            !(parser >> std::ws).eof()) return false;
        version2 = tag == "RISCO_FORMULAS_V2";
    }
    FormulaLibrary next;
    for (int i = 0; i < count; ++i) {
        if (!readLine(in, line)) return false;
        std::istringstream parser(line);
        std::string tag;
        FormulaRecord item;
        if (!(parser >> tag >> std::quoted(item.name) >> std::quoted(item.expression)
                     >> std::quoted(item.author) >> std::quoted(item.source)
                     >> std::quoted(item.sourceId)) ||
            (version2 && !(parser >> std::quoted(item.license))) || tag != "F" ||
            !(parser >> std::ws).eof() || !addFormula(next, std::move(item))) return false;
    }
    if (readLine(in, line) || !in.eof()) return false;
    destination = std::move(next);
    return true;
}
}
