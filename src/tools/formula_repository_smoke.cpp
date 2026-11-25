// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Lúcio de Araújo
#include "core/BytebeatExpression.hpp"
#include "core/FormulaLibrary.hpp"
#include "core/FormulaSeed.hpp"
#include "core/SongIO.hpp"
#include <iostream>
#include <memory>
#include <sstream>

int main() {
    for (std::uint64_t seed = 0; seed < 64; ++seed) {
        for (unsigned stage = 0; stage < 6; ++stage) {
            const auto expression = risco::formulaFromSeedV3(seed, stage);
            risco::BytebeatProgram program;
            std::string error;
            if (!risco::compileBytebeat(expression, program, error)) {
                std::cerr << "generator seed=" << seed << " stage=" << stage << ": " << error << '\n'
                          << expression << '\n';
                return 1;
            }
        }
    }
    const auto first = risco::formulaFromSeedV3(42, 0);
    const auto second = risco::formulaFromSeedV3(42, 1);
    const auto combined = risco::recombineFormulaV1(first, second, 3);
    risco::BytebeatProgram program;
    std::string error;
    if (combined.empty() || !risco::compileBytebeat(combined, program, error)) return 2;

    risco::FormulaLibrary library;
    if (!risco::addFormula(library, {"Estudo 1", first, "Risco", "risco:machine-v3", "42:0", "AGPL-3.0-or-later"}) ||
        !risco::addFormula(library, {"Estudo 2", second, "Risco", "risco:machine-v3", "42:1", "AGPL-3.0-or-later"}))
        return 3;
    std::stringstream libraryFile;
    if (!risco::saveFormulaLibrary(libraryFile, library)) return 4;
    risco::FormulaLibrary restoredLibrary;
    if (!risco::loadFormulaLibrary(libraryFile, restoredLibrary) || restoredLibrary.count != 2 ||
        restoredLibrary.records[1].sourceId != "42:1" ||
        restoredLibrary.records[1].license != "AGPL-3.0-or-later") return 5;

    auto song = std::make_unique<risco::Song>();
    std::array<std::int16_t, risco::Cell::waveformSize> wave{};
    const auto id1 = risco::storeFormulaCell(*song, first, "Estudo 1", wave,
        {"Risco", "risco:machine-v3", "42:0", 0, 0, "AGPL-3.0-or-later"});
    const auto id2 = risco::storeFormulaCell(*song, second, "Estudo 2", wave,
        {"Risco", "risco:machine-v3", "42:1", 0, 0, "AGPL-3.0-or-later"});
    const auto child = risco::storeFormulaCell(*song, combined, "Recombinação", wave,
        {"Risco", "risco:recombine-v1", "42:2", id1, id2, "AGPL-3.0-or-later"});
    if (id1 == 0 || id2 == 0 || child == 0 ||
        !risco::placeCell(*song, {child, 0, risco::CellSpan::fourBars, 1})) return 6;
    std::stringstream songFile;
    if (!risco::saveSong(songFile, *song)) return 7;
    auto restoredSong = std::make_unique<risco::Song>();
    if (!risco::loadSong(songFile, *restoredSong) || restoredSong->cellCount != 3 ||
        restoredSong->bank[2].formulaOrigin.parentA != id1 ||
        restoredSong->bank[2].formulaOrigin.parentB != id2 ||
        restoredSong->bank[2].formulaExpression != combined ||
        restoredSong->bank[2].formulaOrigin.license != "AGPL-3.0-or-later") return 8;
    std::cout << "formula repository smoke passed\n";
}
