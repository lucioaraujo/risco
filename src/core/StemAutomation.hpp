// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Lúcio de Araújo
#pragma once
#include <algorithm>
#include <array>
#include <cmath>

namespace risco {
struct GainAutomation {
    bool enabled = false;
    std::array<float, 3> levels{{1,1,1}}; // Start, midpoint, end of the complete grid.
};
struct PanAutomation {
    bool enabled = false;
    std::array<float,3> levels{{0,0,0}}; // Absolute pan at start/mid/end, -1..+1.
};
struct FxAutomation {
    bool enabled = false;
    std::array<bool,3> active{{true,true,true}}; // Nearest point; transitions at 25% and 75%.
};
struct ParameterAutomation {
    bool enabled = false;
    std::array<float, 3> values{{1200, 1200, 1200}};
};
inline float automationReverbDecayAt(const ParameterAutomation& curve, double seconds, double duration,
                                     float fallback) noexcept {
    if (!curve.enabled || !std::isfinite(seconds) || !std::isfinite(duration) || duration <= 0) return fallback;
    const double position = std::clamp(seconds / duration, 0.0, 1.0) * 2;
    const int section = position < 1 ? 0 : 1;
    const float u = float(position - section), smooth = u * u * (3 - 2 * u);
    const auto point = [&](int i) {
        const float value = curve.values[i];
        return value == 0.0f ? 0.0f : std::clamp(std::isfinite(value) ? value : fallback, 0.25f, 3.0f);
    };
    return point(section) + (point(section + 1) - point(section)) * smooth;
}
inline int automationBitcrushBitsAt(const ParameterAutomation& curve, double seconds, double duration,
                                     int fallback) noexcept {
    if (!curve.enabled || !std::isfinite(seconds) || !std::isfinite(duration) || duration <= 0) return fallback;
    const double position = std::clamp(seconds / duration, 0.0, 1.0) * 2;
    const int section = position < 1 ? 0 : 1;
    const int point = int(std::lround(curve.values[section + (position - section >= 0.5 ? 1 : 0)]));
    return point == 0 ? 0 : std::clamp(point, 2, 16);
}
inline float automationParameterAt(const ParameterAutomation& curve, double seconds, double duration,
                                   float fallback, float minimum, float maximum) noexcept {
    if (!curve.enabled || !std::isfinite(seconds) || !std::isfinite(duration) || duration <= 0) return fallback;
    const double position = std::clamp(seconds / duration, 0.0, 1.0) * 2;
    const int section = position < 1 ? 0 : 1;
    const float u = float(position - section), smooth = u * u * (3 - 2 * u);
    const auto point = [&](int i) { return std::clamp(std::isfinite(curve.values[i]) ? curve.values[i] : fallback, minimum, maximum); };
    return point(section) + (point(section + 1) - point(section)) * smooth;
}
inline float automationGainAt(const GainAutomation& curve, double seconds, double duration) noexcept {
    if (!curve.enabled || !std::isfinite(seconds) || !std::isfinite(duration) || duration <= 0) return 1;
    const double position = std::clamp(seconds / duration, 0.0, 1.0) * 2;
    const int section = position < 1 ? 0 : 1;
    const float u = float(position - section);
    const float smooth = u * u * (3 - 2 * u);
    const auto level = [&](int i) { return std::clamp(std::isfinite(curve.levels[i]) ? curve.levels[i] : 1.0f, 0.0f, 1.0f); };
    const float a = level(section), b = level(section + 1);
    return a + (b - a) * smooth;
}
inline float automationPanAt(const PanAutomation& curve, double seconds, double duration, float basePan = 0) noexcept {
    basePan = std::clamp(std::isfinite(basePan) ? basePan : 0.0f,-1.0f,1.0f);
    if (!curve.enabled || !std::isfinite(seconds) || !std::isfinite(duration) || duration <= 0) return basePan;
    const double position = std::clamp(seconds / duration,0.0,1.0) * 2;
    const int section = position < 1 ? 0 : 1;
    const float u = float(position - section), smooth = u*u*(3-2*u);
    const auto point = [&](int i) { return std::clamp(std::isfinite(curve.levels[i]) ? curve.levels[i] : 0.0f,-1.0f,1.0f); };
    return point(section) + (point(section + 1) - point(section)) * smooth;
}
inline bool automationFxActiveAt(const FxAutomation& curve, double seconds, double duration) noexcept {
    if (!curve.enabled || !std::isfinite(seconds) || !std::isfinite(duration) || duration <= 0) return true;
    const double position = std::clamp(seconds / duration,0.0,1.0);
    const int point = position < 0.25 ? 0 : position < 0.75 ? 1 : 2;
    return curve.active[static_cast<size_t>(point)];
}
class StemAutomation {
public:
    void prepare(double rate) noexcept {
        const double safeRate = std::clamp(std::isfinite(rate) ? rate : 48000.0, 8000.0, 192000.0);
        smoothing = float(1 - std::exp(-1.0 / (0.01 * safeRate)));
        reset();
    }
    void reset() noexcept { initialized = false; gain = 1; }
    float process(const GainAutomation& curve, double seconds, double duration) noexcept {
        const float target = automationGainAt(curve, seconds, duration);
        if (!initialized) { gain = target; initialized = true; }
        else gain += smoothing * (target - gain);
        if (std::abs(gain - target) < 1e-6f) gain = target;
        return float(gain);
    }
private:
    bool initialized = false;
    float smoothing = 0.002f;
    double gain = 1;
};
class StemPanAutomation {
public:
    void prepare(double rate) noexcept {
        const double safeRate = std::clamp(std::isfinite(rate) ? rate : 48000.0,8000.0,192000.0);
        smoothing = float(1 - std::exp(-1.0 / (0.01 * safeRate))); reset();
    }
    void reset() noexcept { initialized = false; pan = 0; }
    float process(const PanAutomation& curve, double seconds, double duration, float basePan) noexcept {
        const float target = automationPanAt(curve,seconds,duration,basePan);
        if (!initialized) { pan = target; initialized = true; }
        else pan += smoothing * (target-pan);
        if (std::abs(pan-target) < 1e-6f) pan = target;
        return float(pan);
    }
private:
    bool initialized = false;
    float smoothing = 0.002f;
    double pan = 0;
};
}
