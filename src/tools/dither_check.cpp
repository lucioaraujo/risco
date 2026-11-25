// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Lúcio de Araújo
#include "core/TpdfDither.hpp"
#include <cmath>
#include <cstdint>
#include <iostream>

namespace {
bool require(bool condition, const char* message) {
    if (!condition) std::cerr << message << '\n';
    return condition;
}
}

int main() {
    bool ok = true;
    risco::TpdfDither repeatA(16, 12345u), repeatB(16, 12345u);
    risco::TpdfDither otherChannel(16, 67890u);
    bool distinctChannels = false;
    for (int i = 0; i < 4096; ++i) {
        const auto a = repeatA.quantize(0.00001f);
        const auto b = repeatB.quantize(0.00001f);
        const auto c = otherChannel.quantize(0.00001f);
        ok &= require(a == b, "same seed must reproduce the same PCM sequence");
        distinctChannels |= a != c;
    }
    ok &= require(distinctChannels, "channel seeds must produce independent sequences");

    risco::TpdfDither silence(16, 20261123u);
    std::int64_t sum = 0;
    std::int64_t absoluteSum = 0;
    constexpr int count = 200000;
    for (int i = 0; i < count; ++i) {
        const auto value = silence.quantize(0.0f);
        sum += value;
        absoluteSum += std::abs(value);
    }
    ok &= require(absoluteSum > count / 4, "silence must receive dither instead of quantizing to zero");
    ok &= require(std::abs(sum) < count / 100, "zero-input dither must have near-zero mean");

    risco::TpdfDither pcm16(16, 1u), pcm24(24, 1u);
    ok &= require(pcm16.quantize(-1.0f) == -32768, "PCM16 negative full scale must map to -32768");
    ok &= require(pcm16.quantize(1.0f) == 32767, "PCM16 positive full scale must be bounded");
    ok &= require(pcm24.quantize(-1.0f) == -8388608, "PCM24 negative full scale must map to -8388608");
    ok &= require(pcm24.quantize(1.0f) == 8388607, "PCM24 positive full scale must be bounded");
    ok &= require(pcm24.quantize(NAN) == 0 && pcm24.quantize(INFINITY) == 0,
                  "non-finite input must be sanitized");

    if (ok) std::cout << "TPDF dither checks passed\n";
    return ok ? 0 : 1;
}
