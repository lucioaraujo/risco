// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Lúcio de Araújo
#pragma once
#include <algorithm>
#include <cmath>
#include <vector>
#include "StemReverb.hpp"

namespace risco {
// Original bounded one-pole low-pass / amplitude quantizer; no external DSP.
// Presets 0..4: bypass, warm, dark, grain (8 bits), lo-fi (4 bits).
// Presets 5..7: eighth / dotted-eighth / quarter delay; mix/feedback 0..60%.
// Allocation only in prepare(); reset/process/setters allocate nothing.
class StemEffect {
public:
    void prepare(double rate) {
        sampleRate = std::clamp(std::isfinite(rate) ? rate : 48000.0, 8000.0, 192000.0);
        delay.assign(static_cast<size_t>(std::ceil(sampleRate * 1.5)) + 2, 0.0f);
        smoothing = float(1.0 - std::exp(-1.0 / (0.01 * sampleRate)));
        reverb.prepare(sampleRate);
        reset();
    }
    // No allocation: transport restart only clears the preallocated history.
    void reset() noexcept {
        std::fill(delay.begin(), delay.end(), 0.0f); write = 0; delayWet = 0;
        delayFrames = oldFrames = queuedFrames = 1; crossfade = 1; delayEnabled = 0;
        feedbackSmoothed = targetFeedback;
        reverb.reset(); reverbWet = reverbEnabled = 0;
        low = wet = 0; coefficient = targetCoefficient = 1;
        levels = targetLevels = 32767; preset = -1;
        setPreset(0);
    }
    void setPreset(int value) noexcept {
        value = std::clamp(value, 0, 9);
        if (value == preset) return;
        preset = value;
        updateFilterCoefficient();
        targetLevels = explicitBitcrushBits > 0 && value >= 3 && value <= 4
            ? float((1u << std::clamp(explicitBitcrushBits, 2, 15)) - 1u)
            : value == 4 ? 7 : value == 3 ? 127 : 32767;
        setTempo(tempo);
    }
    void setBitcrushBits(int bits) noexcept {
        explicitBitcrushBits = bits <= 0 ? 0 : std::clamp(bits, 2, 16);
        if (preset >= 3 && preset <= 4)
            targetLevels = explicitBitcrushBits > 0
                ? float((1u << std::clamp(explicitBitcrushBits, 2, 15)) - 1u)
                : preset == 4 ? 7.0f : 127.0f;
    }
    void setTempo(double bpm) noexcept {
        tempo = std::clamp(std::isfinite(bpm) ? bpm : 120.0, 40.0, 240.0);
        if (preset >= 5 && preset <= 7 && !delay.empty()) {
            constexpr double ratios[] = {0.5, 0.75, 1.0};
            queuedFrames = std::clamp(sampleRate * 60.0 / tempo * ratios[preset - 5], 1.0, double(delay.size() - 2));
            if (crossfade >= 1 && std::abs(queuedFrames - delayFrames) > 0.01) {
                oldFrames = delayFrames; delayFrames = queuedFrames; crossfade = 0;
            }
        }
    }
    void setDelayParameters(float mix, float feedback) noexcept {
        targetMix = std::clamp(std::isfinite(mix) ? mix : 0.3f, 0.0f, 0.6f);
        targetFeedback = std::clamp(std::isfinite(feedback) ? feedback : 0.35f, 0.0f, 0.6f);
    }
    void setFilterCutoff(float hz) noexcept {
        const float next = !std::isfinite(hz) || hz == 0 ? 0 : std::clamp(hz, 80.0f, 16000.0f);
        if (next == filterCutoff) return;
        filterCutoff = next;
        updateFilterCoefficient();
    }
    void setReverbMix(float mix) noexcept {
        targetReverbMix = std::clamp(std::isfinite(mix) ? mix : 0.25f, 0.0f, 0.6f);
    }
    void setReverbDecay(float seconds) noexcept { reverb.setDecaySeconds(seconds); }
    float process(float input) noexcept {
        if (!std::isfinite(input)) input = 0;
        wet += smoothing * ((preset > 0 && preset <= 4 ? 1.0f : 0.0f) - wet);
        coefficient += smoothing * (targetCoefficient - coefficient);
        levels += smoothing * (targetLevels - levels);
        low += coefficient * (input - low);
        if (std::abs(low) < 1e-20f) low = 0;
        const float quantized = std::round(std::clamp(low, -1.0f, 1.0f) * levels) / levels;
        const float filtered = wet < 1e-6f && (preset == 0 || preset >= 5) ? input : input + wet * (quantized - input);
        if (delay.empty()) return filtered;
        const auto tap = [&](double frames) {
            double position = double(write) - frames;
            if (position < 0) position += delay.size();
            const auto first = static_cast<size_t>(position);
            const auto second = (first + 1) % delay.size();
            const float fraction = float(position - first);
            return delay[first] + fraction * (delay[second] - delay[first]);
        };
        const float echo = tap(oldFrames) * (1.0f - crossfade) + tap(delayFrames) * crossfade;
        crossfade = std::min(1.0f, crossfade + float(1.0 / (0.02 * sampleRate)));
        if (crossfade >= 1 && std::abs(queuedFrames - delayFrames) > 0.01) {
            oldFrames = delayFrames; delayFrames = queuedFrames; crossfade = 0;
        }
        const bool isDelay = preset >= 5 && preset <= 7;
        delayWet += smoothing * ((isDelay ? targetMix : 0.0f) - delayWet);
        delayEnabled += smoothing * ((isDelay ? 1.0f : 0.0f) - delayEnabled);
        feedbackSmoothed += smoothing * (targetFeedback - feedbackSmoothed);
        float feedback = std::clamp(delayEnabled * (filtered + echo * feedbackSmoothed), -2.0f, 2.0f);
        if (std::abs(feedback) < 1e-20f) feedback = 0;
        delay[write] = feedback; write = (write + 1) % delay.size();
        const float delayed = !isDelay && delayWet < 1e-6f ? filtered : filtered * (1.0f - delayWet) + echo * delayWet;
        reverbEnabled += smoothing * ((preset >= 8 ? 1.0f : 0.0f) - reverbEnabled);
        reverbWet += smoothing * ((preset >= 8 ? targetReverbMix : 0.0f) - reverbWet);
        const float room = reverb.process(filtered * reverbEnabled, preset == 9);
        if (preset < 8 && reverbWet < 1e-6f) return delayed;
        return delayed * (1.0f - reverbWet) + room * reverbWet;
    }
private:
    void updateFilterCoefficient() noexcept {
        constexpr double cutoffs[] = {20000, 5000, 1200, 5000, 2500, 20000, 20000, 20000, 20000, 20000};
        const double hz = preset >= 1 && preset <= 4 && filterCutoff > 0 ? filterCutoff : cutoffs[std::clamp(preset, 0, 9)];
        targetCoefficient = float(1.0 - std::exp(-6.283185307179586 * std::min(hz, sampleRate * 0.45) / sampleRate));
    }
    double sampleRate = 48000;
    float smoothing = 0.002f, low = 0, wet = 0, coefficient = 1, targetCoefficient = 1;
    float levels = 32767, targetLevels = 32767;
    int explicitBitcrushBits = 0;
    int preset = 0;
    float filterCutoff = 0;
    std::vector<float> delay;
    size_t write = 0;
    double tempo = 120, delayFrames = 1, oldFrames = 1, queuedFrames = 1;
    float delayWet = 0, crossfade = 1;
    float targetMix = 0.3f, targetFeedback = 0.35f, feedbackSmoothed = 0.35f, delayEnabled = 0;
    StemReverb reverb;
    float reverbWet = 0, reverbEnabled = 0;
    float targetReverbMix = 0.25f;
};
}
