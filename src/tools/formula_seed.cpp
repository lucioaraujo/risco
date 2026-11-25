// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Lúcio de Araújo
#include "core/BytebeatExpression.hpp"
#include "core/FormulaSeed.hpp"
#include <charconv>
#include <cstdint>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: risco_formula_seed DECIMAL_SEED\n";
        return 2;
    }
    const std::string input(argv[1]);
    std::uint64_t seed = 0;
    const auto parsed = std::from_chars(input.data(), input.data() + input.size(), seed);
    if (input.empty() || parsed.ec != std::errc{} || parsed.ptr != input.data() + input.size()) {
        std::cerr << "invalid decimal seed\n";
        return 2;
    }
    for (unsigned variation = 0; variation < 6; ++variation) {
        const auto formula = risco::formulaFromSeedV3(seed, variation);
        risco::BytebeatProgram program;
        std::string error;
        if (!risco::compileBytebeat(formula, program, error)) {
            std::cerr << "generated formula is invalid: " << error << '\n';
            return 1;
        }
        std::cout << "v3 seed=" << seed << " stage=" << (variation + 1)
                  << " " << formula << '\n';
    }
}
