// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Lúcio de Araújo
#pragma once
#include "Composition.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace risco {
// Original partial study, NOT a faithful chip emulator. Three tone voices,
// 12-bit period, reference clock 2 MHz, 4-bit linear amplitude approximation.
// Source specification only: General Instrument AY-3-8910 data manual.
// Additional noise modes use a custom 17-bit sequence and linear blend.
// Optional 16-step note envelopes are inspired studies, not a shared hardware
// envelope/register bus/analog DAC model. Risco note-edge fades remain active.
// A bounded odd-harmonic sum (up to 17th / 3.5 kHz) avoids naive square aliasing.
inline int ayTonePeriod(double frequency) noexcept {
    if (!std::isfinite(frequency) || frequency <= 0) return 4095;
    return int(std::clamp(std::round(2000000.0 / (16.0 * frequency)), 1.0, 4095.0));
}
inline double ayToneFrequency(double frequency) noexcept {
    return 2000000.0 / (16.0 * ayTonePeriod(frequency));
}
int ayNoiseBitAt(std::uint64_t tick) noexcept;
float ayNoiseStudySampleAt(double seconds, int period) noexcept;
inline int ayEnvelopePeriod(float ramp) noexcept {
    const double seconds = std::clamp(std::isfinite(ramp) ? double(ramp) : 0.5, 0.125, 2.0);
    return int(std::clamp(std::round(seconds * 2000000.0 / (256 * 16)), 1.0, 65535.0));
}
inline float ayEnvelopeLevelAt(double local, int mode, float ramp = 0.5f) noexcept {
    if (mode < 1 || mode > 4) return 1;
    if (!std::isfinite(local) || local < 0 || local > 1e9) return 0;
    const double step = std::floor(local * 2000000.0 / (256 * ayEnvelopePeriod(ramp)));
    double level = 15;
    if (mode == 1) level = 15 - std::min(step, 15.0);
    if (mode == 2) level = std::min(step, 15.0);
    if (mode == 3) { const double phase = std::fmod(step, 32.0); level = phase < 16 ? phase : 31 - phase; }
    if (mode == 4) level = 15 - std::fmod(step, 16.0);
    return float(level / 15);
}
inline float ayToneStudySampleAt(const Sequence& sequence, double seconds, float noiseMix = 0, int noisePeriod = 16,
                                int envelopeMode = 0, float envelopeRamp = 0.5f) noexcept {
    if (!std::isfinite(seconds) || seconds < 0 || seconds >= sequence.end) return 0;
    std::array<const Note*, 3> voices{};
    int count = 0;
    for (int n = 0; n < std::clamp(sequence.count, 0, int(sequence.notes.size())); ++n) {
        const auto& note = sequence.notes[static_cast<size_t>(n)];
        const double local = seconds - note.start;
        if (!std::isfinite(note.frequency) || note.frequency <= 0 || !std::isfinite(note.gain) ||
            local < 0 || local >= note.duration || note.duration <= 0 || note.gain <= 0) continue;
        int slot = count < 3 ? count++ : 2;
        if (voices[static_cast<size_t>(slot)] && note.start <= voices[static_cast<size_t>(slot)]->start) continue;
        voices[static_cast<size_t>(slot)] = &note;
        while (slot > 0 && voices[static_cast<size_t>(slot)]->start > voices[static_cast<size_t>(slot - 1)]->start) {
            std::swap(voices[static_cast<size_t>(slot)], voices[static_cast<size_t>(slot - 1)]); --slot;
        }
    }
    double sum = 0;
    const float noise = noiseMix > 0 ? ayNoiseStudySampleAt(seconds, noisePeriod) : 0;
    for (int v = 0; v < count; ++v) {
        const auto& note = *voices[static_cast<size_t>(v)];
        const double frequency = ayToneFrequency(note.frequency);
        const double phase = std::fmod((seconds - note.start) * frequency, 1.0) * 6.283185307179586;
        double square = 0;
        for (int harmonic = 1; noiseMix < 1 && harmonic <= 17 && harmonic * frequency <= 3500; harmonic += 2)
            square += std::sin(phase * harmonic) / harmonic;
        const double envelope = std::clamp(std::min((seconds - note.start) / 0.01,
            (note.start + note.duration - seconds) / 0.025), 0.0, 1.0) * ayEnvelopeLevelAt(seconds - note.start, envelopeMode, envelopeRamp);
        const double amplitude = std::clamp(std::round(note.gain * 15 / 0.24), 0.0, 15.0) * 0.24 / 15;
        const double tone = square * (4.0 / 3.141592653589793);
        sum += (noiseMix > 0 ? tone * (1 - noiseMix) + noise * noiseMix : tone) * amplitude * envelope;
    }
    return float(std::clamp(sum, -1.0, 1.0));
}
}
