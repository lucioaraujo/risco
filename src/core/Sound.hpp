// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Lúcio de Araújo
#pragma once

#include "Composition.hpp"
#include "BytebeatExpression.hpp"
#include "EightBitFunction.hpp"
#include "AyToneStudy.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace risco {

enum class WaveMode { quantizedSine, phaseFunction, chiptuneTrio, ayToneStudy, ayToneNoiseStudy, ayNoiseStudy };
inline WaveMode noteWaveMode(int timbreMode) noexcept {
    return timbreMode == 4 ? WaveMode::ayNoiseStudy : timbreMode == 3 ? WaveMode::ayToneNoiseStudy :
        timbreMode == 2 ? WaveMode::ayToneStudy : timbreMode == 1 ? WaveMode::chiptuneTrio : WaveMode::quantizedSine;
}

inline float shapeSample(float input, int mode, float drive) noexcept {
    const float x = std::clamp(input * std::clamp(drive, 0.1f, 8.0f), -8.0f, 8.0f);
    float y = input;
    if (mode == 1) y = std::tanh(x) / std::tanh(std::clamp(drive, 0.1f, 8.0f));
    else if (mode == 2) y = x - (x * x * x) / 3.0f;
    else if (mode == 3) y = 2.0f / 3.14159265f * std::asin(std::sin(x * 1.5707963f));
    return std::clamp(y, -1.0f, 1.0f);
}

constexpr std::uint32_t directBytebeatRate = 8000;

// Global sample counter in an 8 kHz domain, independent of note starts.
inline std::uint32_t directBytebeatTime(std::uint64_t deviceSample,
                                        double deviceRate) noexcept {
    if (!std::isfinite(deviceRate) || deviceRate <= 0.0) return 0;
    return static_cast<std::uint32_t>(
        static_cast<std::uint64_t>(double(deviceSample) * directBytebeatRate / deviceRate));
}

inline float directBytebeatSample(const BytebeatProgram* program,
                                  std::uint32_t time) noexcept {
    if (program != nullptr && program->valid && program->floating)
        return float(evaluateFloatbeat(*program, double(time) / directBytebeatRate));
    const auto value = program != nullptr && program->valid
        ? static_cast<std::uint8_t>(evaluateBytebeat(*program, time))
        : eightBitFunction(time);
    return float(value) / 127.5f - 1.0f;
}

// Three bounded roles: one pulse lead, one arpeggiated triangle harmony and
// a short noise accent on lead attacks. This is an original chip-inspired
// voice budget, not an emulation of any historical sound chip.
inline float chipTrioSampleAt(const Sequence& sequence, double seconds,
                             int bitDepth) noexcept {
    const Note* lead = nullptr;
    std::array<const Note*, 3> chord{};
    int chordCount = 0;
    const int count = std::clamp(sequence.count, 0, int(sequence.notes.size()));
    for (int index = 0; index < count; ++index) {
        const auto& note = sequence.notes[static_cast<size_t>(index)];
        const double local = seconds - note.start;
        if (local < 0.0 || local >= note.duration || note.duration <= 0.0 ||
            !std::isfinite(note.frequency) || note.frequency <= 0.0) continue;
        if (note.role == VoiceRole::lead) {
            if (lead == nullptr || note.start > lead->start) lead = &note;
        } else if (chordCount < int(chord.size())) {
            chord[static_cast<size_t>(chordCount++)] = &note;
        }
    }
    auto envelope = [&](const Note& note) {
        const double local = seconds - note.start;
        return std::clamp(std::min({local / 0.012,
                                    (note.duration - local) / 0.03, 1.0}), 0.0, 1.0);
    };
    double sum = 0.0;
    if (lead != nullptr) {
        const double local = seconds - lead->start;
        const double phase = std::fmod(local * lead->frequency, 1.0);
        const double duty = std::clamp(double(lead->pulseDuty) +
            0.06 * std::sin(6.2831853071795864769 * 2.0 * local), 0.125, 0.75);
        sum += (phase < duty ? 1.0 : -1.0) * lead->gain * envelope(*lead);
        if (local < 0.04) {
            std::uint32_t noise = static_cast<std::uint32_t>(seconds * 8000.0)
                ^ static_cast<std::uint32_t>(lead->frequency * 31.0);
            noise ^= noise << 13; noise ^= noise >> 17; noise ^= noise << 5;
            sum += (noise & 1u ? 1.0 : -1.0) * 0.045 *
                   std::max(0.0, 1.0 - local / 0.04);
        }
    }
    if (chordCount > 0) {
        const int chosen = static_cast<int>(seconds * 12.0) % chordCount;
        const auto& note = *chord[static_cast<size_t>(chosen)];
        const double phase = std::fmod((seconds - note.start) * note.frequency, 1.0);
        const double triangle = 1.0 - 4.0 * std::abs(phase - 0.5);
        sum += triangle * (note.gain * 2.0) * envelope(note);
    }
    const double scale = bitDepth == 16 ? 32767.0 : 127.0;
    return float(std::round(std::clamp(sum, -1.0, 1.0) * scale) / scale);
}

// Pure, allocation-free audio render. The device/file format is independent of
// the voice's eight-bit amplitude quantization.
inline float sampleAt(const Sequence& sequence, double seconds, WaveMode mode,
                      const BytebeatProgram* customProgram = nullptr,
                      int bitDepth = 8, int ayNoisePeriod = 16, int ayEnvelopeMode = 0, float ayEnvelopeRamp = 0.5f) noexcept {
    if (!std::isfinite(seconds) || seconds < 0.0 || seconds >= sequence.end) return 0.0f;
    if (sequence.repeatPeriod > 0.0 && std::isfinite(sequence.repeatPeriod))
        seconds = std::fmod(seconds, sequence.repeatPeriod);
    if (mode == WaveMode::chiptuneTrio)
        return chipTrioSampleAt(sequence, seconds, bitDepth);
    if (mode == WaveMode::ayToneStudy)
        return ayToneStudySampleAt(sequence, seconds, 0, ayNoisePeriod, ayEnvelopeMode, ayEnvelopeRamp);
    if (mode == WaveMode::ayToneNoiseStudy || mode == WaveMode::ayNoiseStudy)
        return ayToneStudySampleAt(sequence, seconds, mode == WaveMode::ayNoiseStudy ? 1.0f : 0.25f, ayNoisePeriod, ayEnvelopeMode, ayEnvelopeRamp);
    double sum = 0.0;
    const int count = std::clamp(sequence.count, 0, int(sequence.notes.size()));
    for (int index = 0; index < count; ++index) {
        const auto& note = sequence.notes[static_cast<size_t>(index)];
        const double local = seconds - note.start;
        if (local < 0.0 || local >= note.duration || note.duration <= 0.0 ||
            !std::isfinite(note.frequency) || note.frequency <= 0.0) continue;
        const double fade = std::clamp(std::min({local / 0.012,
                                               (note.duration - local) / 0.03,
                                               1.0}), 0.0, 1.0);
        double wave = 0.0;
        if (mode == WaveMode::phaseFunction) {
            const auto phase = static_cast<std::uint32_t>(local * note.frequency * 256.0);
            const auto value = customProgram != nullptr && customProgram->valid
                ? static_cast<std::uint8_t>(evaluateBytebeat(*customProgram, phase))
                : eightBitFunction(phase);
            wave = double(value) / 127.5 - 1.0;
        } else {
            wave = std::sin(6.2831853071795864769 * note.frequency * local);
        }
        sum += wave * note.gain * fade;
    }
    const double bounded = std::clamp(sum, -1.0, 1.0);
    const double scale = bitDepth == 16 ? 32767.0 : 127.0;
    return float(std::round(bounded * scale) / scale);
}

}
