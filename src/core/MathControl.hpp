// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Lúcio de Araújo
#pragma once

#include <algorithm>
#include <cstdint>

namespace risco {
enum class BoundedMathMap { logistic, tent, triangle, saw, cantor };
enum class MathDestination { degree, rest, duration, gain, registerOffset, pulseDuty };

inline std::uint32_t boundedMathMap(std::uint32_t state, BoundedMathMap map) noexcept {
    state = std::min(state, 65535u);
    switch (map) {
        case BoundedMathMap::tent: return state < 32768u ? state * 2u : (65535u - state) * 2u;
        case BoundedMathMap::triangle: return state < 16384u ? state * 4u : state < 49152u ? 65535u - ((state - 16384u) * 2u) : (state - 49152u) * 4u;
        case BoundedMathMap::saw: return (state * 5u + 8191u) & 65535u;
        case BoundedMathMap::cantor: return std::min(65535u, ((state ^ (state >> 1)) * 3u) / 2u);
        case BoundedMathMap::logistic: default: return state;
    }
}
inline int applyMathDestination(std::uint32_t state, MathDestination destination) noexcept {
    state = std::min(state, 65535u);
    switch (destination) {
        case MathDestination::degree: return int(state * 7u / 65536u);
        case MathDestination::rest: return int(state * 5u / 65536u) == 0 ? 1 : 0;
        case MathDestination::duration: return 1 + int(state * 4u / 65536u);
        case MathDestination::gain: return 55 + int(state * 46u / 65536u);
        case MathDestination::registerOffset: return int(state * 5u / 65536u) - 2;
        case MathDestination::pulseDuty: return 1 + int(state * 4u / 65536u);
    }
    return 0;
}
}
