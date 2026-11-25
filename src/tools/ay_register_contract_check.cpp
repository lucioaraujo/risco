// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Lúcio de Araújo
#include "core/AyRegisterContract.hpp"

#include <array>
#include <cmath>
#include <iostream>

int main() {
    using namespace risco::ay_contract;
    int failures = 0;
    const auto require = [&](bool condition, const char* name) {
        if (!condition) { std::cerr << "FAIL: " << name << '\n'; ++failures; }
    };

    require(maskedWrite(1, 0xff) == 0x0f, "tone coarse mask");
    require(maskedWrite(6, 0xff) == 0x1f, "noise mask");
    require(maskedWrite(13, 0xff) == 0x0f, "shape mask");
    require(maskedWrite(-1, 0xff) == 0 && maskedWrite(16, 0xff) == 0, "register bounds");

    std::array<std::uint8_t, registerCount> regs{};
    regs[0] = 0x34; regs[1] = 0x02;
    regs[2] = 0xff; regs[3] = 0x0f;
    require(rawTonePeriod(regs, 0) == 0x234, "tone A assembly");
    require(rawTonePeriod(regs, 1) == 0x0fff, "tone B assembly");
    require(effectiveTonePeriod(regs, 2) == 1, "zero tone aliases one");
    require(std::abs(toneFrequency(2'000'000.0, regs, 0) - 2'000'000.0 / (16.0 * 0x234)) < 1e-12,
            "tone frequency");

    regs[6] = 0;
    require(rawNoisePeriod(regs) == 0 && effectiveNoisePeriod(regs) == 1, "zero noise aliases one");
    regs[6] = 31;
    require(effectiveNoisePeriod(regs) == 31, "maximum noise period");

    regs[7] = 0b00010001; // tone A off; noise B off; tone/noise C on.
    require(!toneEnabled(regs, 0) && toneEnabled(regs, 1) && toneEnabled(regs, 2), "tone mixer bits");
    require(noiseEnabled(regs, 0) && !noiseEnabled(regs, 1) && noiseEnabled(regs, 2), "noise mixer bits");

    regs[8] = 0x1d; regs[9] = 0x07;
    require(envelopeEnabled(regs, 0) && fixedLevel(regs, 0) == 13, "channel A level/envelope");
    require(!envelopeEnabled(regs, 1) && fixedLevel(regs, 1) == 7, "channel B fixed level");
    regs[11] = 0x78; regs[12] = 0x56; regs[13] = 0x0e;
    require(envelopePeriod(regs) == 0x5678 && envelopeShape(regs) == 14, "envelope registers");

    if (failures == 0) std::cout << "AY register contract: PASS (decode only; no audio fidelity claim)\n";
    return failures == 0 ? 0 : 1;
}
