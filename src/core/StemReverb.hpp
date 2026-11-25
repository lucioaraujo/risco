// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Lúcio de Araújo
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace risco {
// Original small mono reverberator: four damped feedback lines and two diffusers.
// No external impulse responses or DSP code. Allocation only during prepare.
class StemReverb {
public:
    void prepare(double sampleRate) {
        const double rate = std::clamp(std::isfinite(sampleRate) ? sampleRate : 48000.0, 8000.0, 192000.0);
        constexpr double seconds[] = {0.0297, 0.0371, 0.0411, 0.0437, 0.0053, 0.0017};
        for (size_t i = 0; i < lines.size(); ++i)
            lines[i].assign(static_cast<size_t>(std::lround(rate * seconds[i])), 0);
        longestSeconds = double(lines[3].size()) / rate;
        updateDecayFeedback();
        smoothing = float(1 - std::exp(-1.0 / (0.01 * rate)));
        reset();
    }
    void reset() noexcept {
        for (auto& line : lines) std::fill(line.begin(), line.end(), 0);
        positions.fill(0); damped.fill(0); feedback = 0.68f;
    }
    void setDecaySeconds(float seconds) noexcept {
        const float next = !std::isfinite(seconds) || seconds == 0 ? 0 : std::clamp(seconds, 0.25f, 3.0f);
        if (next == decaySeconds) return;
        decaySeconds = next;
        updateDecayFeedback();
    }
    float process(float input, bool longDecay) noexcept {
        if (!std::isfinite(input)) input = 0;
        feedback += smoothing * ((decaySeconds > 0 ? decayFeedback : longDecay ? 0.82f : 0.68f) - feedback);
        float sum = 0;
        for (size_t i = 0; i < 4; ++i) {
            auto& line = lines[i];
            if (line.empty()) return 0;
            const float output = line[positions[i]];
            damped[i] += 0.4f * (output - damped[i]);
            if (std::abs(damped[i]) < 1e-20f) damped[i] = 0;
            float next = std::clamp(input + feedback * damped[i], -2.0f, 2.0f);
            if (std::abs(next) < 1e-20f) next = 0;
            line[positions[i]] = next;
            positions[i] = (positions[i] + 1) % line.size();
            sum += output * 0.25f;
        }
        for (size_t i = 4; i < lines.size(); ++i) {
            auto& line = lines[i];
            const float output = line[positions[i]] - 0.5f * sum;
            float next = sum + 0.5f * output;
            if (std::abs(next) < 1e-20f) next = 0;
            line[positions[i]] = next;
            positions[i] = (positions[i] + 1) % line.size();
            sum = output;
        }
        return std::clamp(sum, -2.0f, 2.0f);
    }
private:
    void updateDecayFeedback() noexcept {
        decayFeedback = decaySeconds > 0 ? float(std::min(0.91, std::pow(0.001, longestSeconds / decaySeconds))) : 0;
    }
    std::array<std::vector<float>, 6> lines;
    std::array<size_t, 6> positions{};
    std::array<float, 4> damped{};
    float smoothing = 0.002f, feedback = 0.68f;
    float decaySeconds = 0, decayFeedback = 0;
    double longestSeconds = 0.0437;
};
}
