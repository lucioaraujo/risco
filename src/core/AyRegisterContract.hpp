// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Lúcio de Araújo
#pragma once

#include <array>
#include <cmath>
#include <cstdint>

namespace risco::ay_contract {

// Register-domain contract used by the isolated AY/YM evaluation bench.
// This does not synthesize audio and is not a chip emulator.
constexpr int registerCount = 16;
constexpr int channelCount = 3;

constexpr std::array<std::uint8_t, registerCount> writeMasks{
    0xff, 0x0f, 0xff, 0x0f, 0xff, 0x0f, 0x1f, 0xff,
    0x1f, 0x1f, 0x1f, 0xff, 0xff, 0x0f, 0xff, 0xff
};

constexpr std::uint8_t maskedWrite(int reg, std::uint8_t value) noexcept {
    return reg >= 0 && reg < registerCount ? std::uint8_t(value & writeMasks[static_cast<std::size_t>(reg)]) : 0;
}

constexpr int rawTonePeriod(const std::array<std::uint8_t, registerCount>& regs, int channel) noexcept {
    if (channel < 0 || channel >= channelCount) return 0;
    const int base = channel * 2;
    return int(regs[static_cast<std::size_t>(base)]) |
           (int(regs[static_cast<std::size_t>(base + 1)] & 0x0f) << 8);
}

constexpr int effectiveTonePeriod(const std::array<std::uint8_t, registerCount>& regs, int channel) noexcept {
    const int period = rawTonePeriod(regs, channel);
    return period == 0 ? 1 : period;
}

constexpr int rawNoisePeriod(const std::array<std::uint8_t, registerCount>& regs) noexcept {
    return int(regs[6] & 0x1f);
}

constexpr int effectiveNoisePeriod(const std::array<std::uint8_t, registerCount>& regs) noexcept {
    const int period = rawNoisePeriod(regs);
    return period == 0 ? 1 : period;
}

constexpr int envelopePeriod(const std::array<std::uint8_t, registerCount>& regs) noexcept {
    return int(regs[11]) | (int(regs[12]) << 8);
}

constexpr int envelopeShape(const std::array<std::uint8_t, registerCount>& regs) noexcept {
    return int(regs[13] & 0x0f);
}

constexpr bool toneEnabled(const std::array<std::uint8_t, registerCount>& regs, int channel) noexcept {
    return channel >= 0 && channel < channelCount && (regs[7] & (1u << channel)) == 0;
}

constexpr bool noiseEnabled(const std::array<std::uint8_t, registerCount>& regs, int channel) noexcept {
    return channel >= 0 && channel < channelCount && (regs[7] & (1u << (channel + 3))) == 0;
}

constexpr bool envelopeEnabled(const std::array<std::uint8_t, registerCount>& regs, int channel) noexcept {
    return channel >= 0 && channel < channelCount && (regs[static_cast<std::size_t>(8 + channel)] & 0x10) != 0;
}

constexpr int fixedLevel(const std::array<std::uint8_t, registerCount>& regs, int channel) noexcept {
    return channel >= 0 && channel < channelCount ? int(regs[static_cast<std::size_t>(8 + channel)] & 0x0f) : 0;
}

inline double toneFrequency(double clockHz, const std::array<std::uint8_t, registerCount>& regs,
                            int channel) noexcept {
    if (!std::isfinite(clockHz) || clockHz <= 0) return 0;
    return clockHz / (16.0 * effectiveTonePeriod(regs, channel));
}

} // namespace risco::ay_contract
