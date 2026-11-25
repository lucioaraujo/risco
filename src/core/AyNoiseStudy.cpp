// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Lúcio de Araújo
#include "AyToneStudy.hpp"
#include <cstdint>

namespace risco {
namespace {
constexpr auto makeNoiseBits() {
    std::array<std::uint8_t, 131071> bits{};
    std::uint32_t state = 1;
    for (auto& bit : bits) {
        bit = std::uint8_t(state & 1);
        state = (state >> 1) | (((state ^ (state >> 3)) & 1) << 16);
    }
    return bits;
}
constexpr auto noiseBits = makeNoiseBits(); // Immutable ROM, no first-use work in audio.
float noiseFrame(std::uint64_t frame, int period) noexcept {
    const double ratio = 125000.0 / (period * 8000.0);
    const auto first = static_cast<std::uint64_t>(std::floor(frame * ratio));
    const auto end = static_cast<std::uint64_t>(std::floor((frame + 1) * ratio));
    const int count = int(std::clamp<std::uint64_t>(end - first, 1, 16));
    float sum = 0;
    for (int i = 0; i < count; ++i) sum += ayNoiseBitAt(first + i) ? 1.0f : -1.0f;
    return sum / count;
}
}
int ayNoiseBitAt(std::uint64_t tick) noexcept { return noiseBits[tick % noiseBits.size()]; }
float ayNoiseStudySampleAt(double seconds, int period) noexcept {
    if (!std::isfinite(seconds) || seconds < 0 || seconds > 1e9) return 0;
    period = std::clamp(period, 1, 31);
    const double position = seconds * 8000;
    const auto frame = static_cast<std::uint64_t>(std::floor(position));
    const float fraction = float(position - frame);
    const float a = noiseFrame(frame, period), b = noiseFrame(frame + 1, period);
    return a + fraction * (b - a);
}
}
