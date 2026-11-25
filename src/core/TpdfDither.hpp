// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Lúcio de Araújo
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace risco {

// Deterministic triangular-probability-density dither for integer PCM exports.
// One instance per channel keeps the two noise streams independent.
class TpdfDither final {
public:
    explicit TpdfDither(int bits, std::uint32_t seed = 0x6d2b79f5u) noexcept
        : bitDepth(bits == 24 ? 24 : 16), state(seed ? seed : 0x6d2b79f5u),
          scale(bitDepth == 24 ? 8388608.0 : 32768.0),
          maximum(bitDepth == 24 ? 8388607 : 32767) {}

    std::int32_t quantize(float sample) noexcept {
        if (!std::isfinite(sample)) return 0;
        const double bounded = std::clamp(static_cast<double>(sample), -1.0, 1.0);
        const double dither = uniform() - uniform();
        const auto quantized = static_cast<std::int64_t>(std::llround(bounded * scale + dither));
        const auto minimum = -static_cast<std::int64_t>(scale);
        return static_cast<std::int32_t>(std::clamp(quantized, minimum,
                                                     static_cast<std::int64_t>(maximum)));
    }

private:
    std::uint32_t next() noexcept {
        std::uint32_t value = state;
        value ^= value << 13;
        value ^= value >> 17;
        value ^= value << 5;
        state = value;
        return value;
    }

    double uniform() noexcept {
        return static_cast<double>(next()) / 4294967296.0;
    }

    int bitDepth;
    std::uint32_t state;
    double scale;
    std::int32_t maximum;
};

} // namespace risco
