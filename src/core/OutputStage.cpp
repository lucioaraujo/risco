// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Lúcio de Araújo
#include "OutputStage.hpp"
#include <algorithm>
#include <cmath>

namespace risco {
namespace {
constexpr double pi = 3.14159265358979323846;
double sinc(double x) noexcept { return std::abs(x) < 1.0e-12 ? 1.0 : std::sin(pi * x) / (pi * x); }
}

void TruePeakDetector::prepare() noexcept {
    // 4x polyphase FIR with a Blackman-Harris window, as in the RASGO
    // Aquorbium/Navalha family. No allocation or coefficient work in process().
    std::array<double, 64> prototype{};
    for (std::size_t tap = 0; tap < prototype.size(); ++tap) {
        const double x = double(tap) - 31.5;
        const double phase = 2.0 * pi * double(tap) / 63.0;
        const double window = 0.35875 - 0.48829 * std::cos(phase) +
            0.14128 * std::cos(2.0 * phase) - 0.01168 * std::cos(3.0 * phase);
        prototype[tap] = sinc(x / 4.0) * window;
    }
    for (std::size_t phase = 0; phase < 4; ++phase) {
        double sum = 0.0;
        for (std::size_t tap = 0; tap < 16; ++tap) sum += prototype[phase + tap * 4];
        for (std::size_t tap = 0; tap < 16; ++tap)
            coefficients[phase][tap] = float(prototype[phase + tap * 4] / sum);
    }
    reset();
}
void TruePeakDetector::reset() noexcept { history.fill(0.0f); index = 0; }
float TruePeakDetector::process(float input) noexcept {
    history[index] = std::isfinite(input) ? input : 0.0f;
    index = (index + 1) % history.size();
    float peak = 0.0f;
    for (const auto& phase : coefficients) {
        double value = 0.0;
        std::size_t read = index;
        for (std::size_t tap = 0; tap < 16; ++tap) {
            read = read == 0 ? history.size() - 1 : read - 1;
            value += double(phase[tap]) * history[read];
        }
        peak = std::max(peak, float(std::abs(value)));
    }
    return peak;
}

void OutputStage::prepare(double rate) noexcept {
    const double sampleRate = std::clamp(std::isfinite(rate) ? rate : 48000.0, 8000.0, 192000.0);
    lookahead = std::uint32_t(std::clamp(std::lround(sampleRate * 0.005), 1l, long(capacity - 16)));
    dcPole = float(std::exp(-2.0 * pi * 10.0 / sampleRate));
    smooth = float(-std::expm1(-1.0 / (sampleRate * 0.015)));
    attack = float(-std::expm1(-1.0 / (sampleRate * 0.00035)));
    release = float(-std::expm1(-1.0 / (sampleRate * 0.150)));
    rmsSmoothing = -std::expm1(-1.0 / (sampleRate * 0.4));
    holdSamples = static_cast<std::uint32_t>(sampleRate);
    holdDecay = static_cast<float>(std::exp(-1.0 / (sampleRate * 0.25)));
    loudness.prepare(sampleRate);
    detectorLeft.prepare(); detectorRight.prepare();
    meterLeft.prepare(); meterRight.prepare();
    reset();
}
void OutputStage::reset() noexcept {
    delayLeft.fill(0.0f); delayRight.fill(0.0f);
    writeIndex = queueHead = queueSize = 0; frame = 0;
    previousLeft = previousRight = dcLeft = dcRight = 0.0f;
    currentMaster = 0.0f; limiterGain = 1.0f;
    detectorLeft.reset(); detectorRight.reset(); meterLeft.reset(); meterRight.reset();
    resetReadouts();
}
void OutputStage::resetReadouts() noexcept {
    telemetry = {};
    meanSquare = meanSquareLeft = meanSquareRight = meanCross = meanSquareMono = 0.0;
    peakHold = truePeakHold = 0.0f;
    sampleHoldRemaining = truePeakHoldRemaining = 0;
    meterLeft.reset(); meterRight.reset();
    loudness.reset();
}
OutputStage::Meter OutputStage::meter() const noexcept {
    Meter result = telemetry;
    result.rms = float(std::sqrt(std::max(0.0, meanSquare)));
    result.monoRms = float(std::sqrt(std::max(0.0, meanSquareMono)));
    const double denominator = std::sqrt(std::max(0.0, meanSquareLeft * meanSquareRight));
    result.correlation = denominator > 1.0e-12
        ? float(std::clamp(meanCross / denominator, -1.0, 1.0)) : 0.0f;
    result.samplePeakHold = peakHold;
    result.truePeakHold = truePeakHold;
    result.loudness = loudness.readings();
    result.reductionDb = std::max(0.0f,
        -20.0f * std::log10(std::max(limiterGain, 1.0e-9f)));
    return result;
}
OutputStage::Meter OutputStage::takeMeterWindow() noexcept {
    const auto result = meter();
    telemetry.samplePeak = telemetry.truePeak = 0.0f;
    return result;
}
OutputStage::Stereo OutputStage::process(float left, float right, float master) noexcept {
    if (!std::isfinite(left)) { left = 0.0f; ++telemetry.invalidSamples; }
    if (!std::isfinite(right)) { right = 0.0f; ++telemetry.invalidSamples; }
    left = std::clamp(left, -16.0f, 16.0f);
    right = std::clamp(right, -16.0f, 16.0f);
    const float filteredLeft = left - previousLeft + dcPole * dcLeft;
    const float filteredRight = right - previousRight + dcPole * dcRight;
    previousLeft = left; previousRight = right;
    dcLeft = filteredLeft; dcRight = filteredRight;
    const float target = std::clamp(std::isfinite(master) ? master : 0.0f, 0.0f, 1.0f);
    currentMaster += (target - currentMaster) * smooth;
    const float gain = currentMaster * calibrationGain;
    const float preparedLeft = filteredLeft * gain;
    const float preparedRight = filteredRight * gain;
    delayLeft[writeIndex] = preparedLeft;
    delayRight[writeIndex] = preparedRight;
    const float detected = std::max({std::abs(preparedLeft), std::abs(preparedRight),
        detectorLeft.process(preparedLeft), detectorRight.process(preparedRight)});
    const float required = detected > ceiling ? ceiling / detected : 1.0f;
    const auto position = [this](std::size_t offset) { return (queueHead + offset) % capacity; };
    while (queueSize && requiredGain[position(queueSize - 1)] >= required) --queueSize;
    requiredGain[position(queueSize)] = required;
    requiredAt[position(queueSize)] = frame;
    ++queueSize;
    const std::uint64_t window = lookahead + TruePeakDetector::latency + 4;
    while (queueSize && requiredAt[queueHead] + window <= frame) {
        queueHead = (queueHead + 1) % capacity;
        --queueSize;
    }
    const float requestedGain = queueSize ? requiredGain[queueHead] : 1.0f;
    limiterGain += (requestedGain - limiterGain) *
        (requestedGain < limiterGain ? attack : release);
    const auto read = (writeIndex + capacity - lookahead) % capacity;
    const Stereo result{delayLeft[read] * limiterGain, delayRight[read] * limiterGain};
    writeIndex = (writeIndex + 1) % capacity;
    ++frame;
    const double power = (double(result.left) * result.left + double(result.right) * result.right) * 0.5;
    meanSquare += rmsSmoothing * (power - meanSquare);
    const double leftPower = double(result.left) * result.left;
    const double rightPower = double(result.right) * result.right;
    const double mono = (double(result.left) + result.right) * 0.5;
    meanSquareLeft += rmsSmoothing * (leftPower - meanSquareLeft);
    meanSquareRight += rmsSmoothing * (rightPower - meanSquareRight);
    meanCross += rmsSmoothing * (double(result.left) * result.right - meanCross);
    meanSquareMono += rmsSmoothing * (mono * mono - meanSquareMono);
    loudness.push(result.left, result.right);
    const float samplePeak = std::max(std::abs(result.left), std::abs(result.right));
    const float truePeak = std::max(meterLeft.process(result.left), meterRight.process(result.right));
    telemetry.samplePeak = std::max(telemetry.samplePeak, samplePeak);
    telemetry.truePeak = std::max(telemetry.truePeak, truePeak);
    if (samplePeak >= peakHold) { peakHold = samplePeak; sampleHoldRemaining = holdSamples; }
    else if (sampleHoldRemaining > 0) --sampleHoldRemaining;
    else peakHold *= holdDecay;
    if (truePeak >= truePeakHold) { truePeakHold = truePeak; truePeakHoldRemaining = holdSamples; }
    else if (truePeakHoldRemaining > 0) --truePeakHoldRemaining;
    else truePeakHold *= holdDecay;
    telemetry.clipLatched = telemetry.clipLatched || samplePeak >= 1.0f || truePeak >= 1.0f;
    return result;
}
}
