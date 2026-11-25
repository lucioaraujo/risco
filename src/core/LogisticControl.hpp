// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Lúcio de Araújo
#pragma once
#include <cstdint>

namespace risco {
// Original integer implementation of a quantized logistic map.
// x' = floor(r*x*(65536-x)/2^32), r = 258867/65536 (~3.95).
// State is Q0.16 in 1..65535. Finite precision permits cycles; no claim of
// continuous-system accuracy or aperiodicity. One bounded step, no allocation.
class LogisticControl final {
public:
    void reset(std::uint64_t seed) noexcept {
        seed ^= seed >> 30; seed *= 0xbf58476d1ce4e5b9ULL;
        seed ^= seed >> 27; seed *= 0x94d049bb133111ebULL;
        seed ^= seed >> 31;
        value = 1u + std::uint32_t(seed % 65534u);
    }
    std::uint32_t state() const noexcept { return value; }
    bool restore(std::uint32_t checkpoint) noexcept {
        if (checkpoint == 0 || checkpoint > 65535) return false;
        value = checkpoint; return true;
    }
    std::uint32_t next() noexcept {
        const auto result = (std::uint64_t(258867) * value * (65536u - value)) >> 32;
        value = result == 0 ? 1u : result > 65535 ? 65535u : std::uint32_t(result);
        return value;
    }
private:
    std::uint32_t value = 1;
};
}
