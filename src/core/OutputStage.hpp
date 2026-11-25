// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Lúcio de Araújo
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include "LoudnessMeter.hpp"

namespace risco {

// Four-phase FIR detector. Adapted for Risco from the author's Aquorbium
// TruePeakDetector, commit ab1da6b1b820cf06462fb26933e892a2db69f062,
// AGPL-3.0-or-later; origin and hashes are recorded in TAREFAS.md.
class TruePeakDetector final {
public:
    void prepare() noexcept;
    void reset() noexcept;
    float process(float input) noexcept;
    static constexpr std::size_t latency = 8;
private:
    std::array<std::array<float, 16>, 4> coefficients{};
    std::array<float, 16> history{};
    std::size_t index = 0;
};

class OutputStage final {
public:
    struct Stereo { float left = 0.0f, right = 0.0f; };
    struct Meter {
        float samplePeak = 0.0f;
        float truePeak = 0.0f;
        float samplePeakHold = 0.0f;
        float truePeakHold = 0.0f;
        float reductionDb = 0.0f;
        float rms = 0.0f; // Stereo energy average, exponential window ~400 ms; not LUFS.
        float monoRms = 0.0f;
        float correlation = 0.0f; // L/R correlation over the same exponential window.
        LoudnessMeter::Readings loudness;
        std::uint64_t invalidSamples = 0;
        bool clipLatched = false;
    };
    void prepare(double sampleRate) noexcept;
    void reset() noexcept;
    void resetReadouts() noexcept;
    Stereo process(float left, float right, float master) noexcept;
    Meter meter() const noexcept;
    // Read and clear peak maxima once per audio callback; call on the audio thread.
    Meter takeMeterWindow() noexcept;
    std::uint32_t latencySamples() const noexcept { return lookahead; }
    // Internal target leaves margin for different reconstruction filters.
    static constexpr float ceiling = 0.80f;
    static constexpr float calibrationGain = 4.0f;
private:
    static constexpr std::size_t capacity = 2048;
    std::array<float, capacity> delayLeft{}, delayRight{}, requiredGain{};
    std::array<std::uint64_t, capacity> requiredAt{};
    std::size_t writeIndex = 0, queueHead = 0, queueSize = 0;
    std::uint64_t frame = 0;
    std::uint32_t lookahead = 240;
    float dcPole = 0.9987f, smooth = 0.0004f;
    double meanSquare = 0, rmsSmoothing = 0.00005;
    double meanSquareLeft = 0, meanSquareRight = 0, meanCross = 0, meanSquareMono = 0;
    float attack = 0.05f, release = 0.00014f;
    float peakHold = 0.0f, truePeakHold = 0.0f, holdDecay = 0.9999f;
    std::uint32_t holdSamples = 0, sampleHoldRemaining = 0, truePeakHoldRemaining = 0;
    float previousLeft = 0.0f, previousRight = 0.0f;
    float dcLeft = 0.0f, dcRight = 0.0f;
    float currentMaster = 0.0f, limiterGain = 1.0f;
    TruePeakDetector detectorLeft, detectorRight, meterLeft, meterRight;
    Meter telemetry{};
    LoudnessMeter loudness;
};
}
