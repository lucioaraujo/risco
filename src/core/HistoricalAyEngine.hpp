// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Lúcio de Araújo
#pragma once

#include "Composition.hpp"

extern "C" {
#include "ayumi.h"
}

#include <array>

namespace risco {

struct SongPlayback;
enum class HistoricalAyModel { ay38910, ym2149 };

inline bool isHistoricalAyTimbre(int timbreMode) noexcept {
    return timbreMode == 5 || timbreMode == 6;
}
inline HistoricalAyModel historicalAyModel(int timbreMode) noexcept {
    return timbreMode == 6 ? HistoricalAyModel::ym2149 : HistoricalAyModel::ay38910;
}

struct HistoricalAyFrame {
    float left = 0.0f;
    float right = 0.0f;
};

struct HistoricalAySettings {
    int texture = 0; // 0 tone, 1 tone+noise, 2 noise.
    int noisePeriod = 16; // Hardware divider 1..31.
    int envelope = 0; // 0 fixed; 1 decay; 2 rise/hold; 3 triangle; 4 repeat decay.
    float envelopeRamp = 0.5f; // Approximate duration of 16 envelope steps.
};

// Stateful three-voice historical core. Calls must follow transport order;
// moving backwards deliberately resets the chip to make loops reproducible.
class HistoricalAyEngine {
public:
    bool prepare(HistoricalAyModel model, double sampleRate = 48000.0,
                 double clockRate = 2000000.0) noexcept;
    void reset() noexcept;
    HistoricalAyFrame process(const Sequence& sequence, double seconds,
                              HistoricalAySettings settings = {}) noexcept;
    HistoricalAyFrame processStem(const SongPlayback& playback, double seconds,
                                  int stem, HistoricalAySettings settings = {}) noexcept;

    HistoricalAyModel model() const noexcept { return selectedModel; }
    double sampleRate() const noexcept { return configuredSampleRate; }
    double clockRate() const noexcept { return configuredClockRate; }

private:
    struct VoiceState {
        int period = -1;
        int level = -1;
        int mixerKey = -1;
        double attack = -1.0;
        bool enabled = false;
    };
    void silenceVoices() noexcept;

    ayumi chip{};
    std::array<VoiceState, 3> voices{};
    HistoricalAyModel selectedModel = HistoricalAyModel::ay38910;
    double configuredSampleRate = 0.0;
    double configuredClockRate = 0.0;
    double previousSeconds = -1.0;
    int appliedNoisePeriod = -1;
    int appliedEnvelopePeriod = -1;
    int appliedEnvelopePreset = -1;
    bool configured = false;
};

}
