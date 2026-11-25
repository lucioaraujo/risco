// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Lúcio de Araújo
#include "FormulaComposition.hpp"
#include "Sound.hpp"
#include <algorithm>
#include <array>
#include <cmath>

namespace risco {
namespace {
bool isSecondaryGroupStart(int beatsPerBar, int beatInBar, int variant) noexcept {
    switch (beatsPerBar) {
        case 3: return beatInBar == 2;             // 2 + 1
        case 4: return beatInBar == 2;             // 2 + 2
        case 5: return beatInBar == (variant == 0 ? 3 : 2); // 3+2 / 2+3
        case 6: return variant == 0 ? beatInBar == 3
                                    : beatInBar == 2 || beatInBar == 4; // 3+3 / 2+2+2
        case 7:
            return variant == 0 ? beatInBar == 2 || beatInBar == 4
                 : variant == 1 ? beatInBar == 3 || beatInBar == 5
                                : beatInBar == 2 || beatInBar == 5;
        default: return false;
    }
}

float metricAccent(int step, int beatsPerBar, int variant, int stepsPerBeat, bool compound) noexcept {
    if (compound) {
        const int within = step % (3 * stepsPerBeat);
        return within == 0 ? 1.16f : within * 2 == 3 * stepsPerBeat ? 1.09f : 1.0f;
    }
    if (step % stepsPerBeat != 0) return 1.0f;
    const int beat = (step / stepsPerBeat) % beatsPerBar;
    if (beat == 0) return 1.16f;
    if (isSecondaryGroupStart(beatsPerBar, beat, variant)) return 1.09f;
    return 1.03f;
}
}

const char* formulaMetricGrouping(int beatsPerBar, int variant) noexcept {
    switch (std::clamp(beatsPerBar, 2, 7)) {
        case 2: return "2";
        case 3: return "2+1";
        case 4: return "2+2";
        case 5: return variant == 0 ? "3+2" : "2+3";
        case 6: return variant == 0 ? "3+3" : "2+2+2";
        case 7: return variant == 0 ? "2+2+3" : variant == 1 ? "3+2+2" : "2+3+2";
        default: return "";
    }
}

int formulaMetricGroupingCount(int beatsPerBar) noexcept {
    return beatsPerBar == 5 || beatsPerBar == 6 ? 2 : beatsPerBar == 7 ? 3 : 1;
}

FormulaComposition composeFormulaEvents(const BytebeatProgram& formula,
                                        double requestedBpm,
                                        int requestedBeatsPerBar,
                                        FormulaScalePreset scalePreset,
                                        FormulaScaleMotion motion,
                                        int requestedTonicSemitones,
                                        int requestedGrouping, int requestedProgression,
                                        int requestedBarsPerChord, double requestedSwing, int subdivision, bool compoundSixEight,
                                        const std::array<int, 4>& chordRoots, bool syncopation, int requestedPulses) noexcept {
    FormulaComposition result;
    if (!formula.valid) return result;

    const double bpm = std::clamp(std::isfinite(requestedBpm) ? requestedBpm : 120.0,
                                  40.0, 240.0);
    const int beats = compoundSixEight ? 3 : std::clamp(requestedBeatsPerBar, 2, 7);
    const auto groupingByte = static_cast<std::uint8_t>(evaluateBytebeat(formula, 0));
    const int automaticGrouping = beats == 5 ? int(groupingByte & 1u)
        : beats == 6 ? int(groupingByte & 1u)
        : beats == 7 ? int(groupingByte % 3u) : 0;
    result.groupingVariant = requestedGrouping >= 0 &&
        requestedGrouping < formulaMetricGroupingCount(beats)
        ? requestedGrouping : automaticGrouping;
    const bool explicitSubdivision = subdivision == 1 || subdivision == 2 || subdivision == 3 || subdivision == 4 || subdivision == 6;
    const int stepsPerBeat = explicitSubdivision ? subdivision : 4;
    const int pulses = requestedPulses == 2 || requestedPulses == 3 || requestedPulses == 5 || requestedPulses == 7 ? requestedPulses : 0;
    const bool explicitRhythm = explicitSubdivision || pulses > 0;
    const int stepsPerBar = pulses > 0 ? pulses : beats * stepsPerBeat;
    constexpr int maximumSteps = int(Sequence{}.notes.size());
    result.bars = std::clamp(maximumSteps / stepsPerBar, 1, 4);
    result.steps = result.bars * stepsPerBar;
    const double stepSeconds = 60.0 / bpm * beats / stepsPerBar;
    const double swing = std::clamp(std::isfinite(requestedSwing) ? requestedSwing : 0.0, 0.0, 0.5);
    result.sequence.end = result.bars * beats * 60.0 / bpm;

    auto scale = makeFormulaScale(scalePreset);
    const int tonicSemitones = ((requestedTonicSemitones % 12) + 12) % 12;
    scale.tonicCents = 100.0 * tonicSemitones;
    const bool accompanied = motion == FormulaScaleMotion::accompany || motion == FormulaScaleMotion::chordGuide;
    const int scaleCount = std::max(1, int(scale.degreeCount));
    const int progression = std::clamp(requestedProgression, 0, 2);
    const int harmonicBars = std::clamp(requestedBarsPerChord, 1, 2);
    const auto chordRoot = [&](int bar) {
        const int override = chordRoots[static_cast<size_t>(std::clamp(bar, 0, 3))];
        if (override >= 0 && override <= 11) return override % scaleCount;
        if (progression == 2) return 0;
        if (progression == 1) {
            const int pattern[] = {0, 5 * scaleCount / 7, scaleCount / 2, 2 * scaleCount / 3};
            return pattern[(bar / harmonicBars) % 4];
        }
        if (harmonicBars == 2) {
            const int pattern[] = {0, scaleCount / 2, 2 * scaleCount / 3, 0};
            return pattern[(bar / harmonicBars) % 4];
        }
        return bar == 0 || bar == result.bars - 1 ? 0 :
               bar == result.bars - 2 ? 2 * scaleCount / 3 : scaleCount / 2;
    };
    int currentBar = -1;
    double previousPitch = -1.0;
    std::array<bool, 4> activeBars{};
    for (int step = 0; step < result.steps; ++step) {
        if (accompanied && !explicitRhythm && step % 2 != 0) continue;
        // Distribute the melody budget evenly, reserving three voices per bar.
        const int melodyBudget = maximumSteps - 3 * result.bars;
        if (accompanied && explicitRhythm && result.steps > melodyBudget && step > 0 &&
            step * melodyBudget / result.steps == (step - 1) * melodyBudget / result.steps) continue;
        const auto time = static_cast<std::uint32_t>(
            static_cast<std::uint64_t>(double(step) * stepSeconds * directBytebeatRate));
        const int bar = step / stepsPerBar;
        if (bar != currentBar) {
            if (currentBar >= 0) {
                const double barTicks = double(beats) * 60.0 / bpm * directBytebeatRate;
                const auto gestureTime = static_cast<std::uint32_t>(
                    static_cast<std::uint64_t>(double(bar) * barTicks));
                scale = evolveFormulaScale(scale, motion,
                    static_cast<std::uint8_t>(evaluateBytebeat(formula, gestureTime)));
                ++result.machineSections;
            }
            currentBar = bar;
        }
        if (accompanied && chordRoots[static_cast<size_t>(bar)] == -2) { ++result.rests; continue; }
        const auto value = static_cast<std::uint8_t>(evaluateBytebeat(formula, time));
        if (value == 0) {
            ++result.rests;
            continue;
        }

        const int degree = int(value & 0x0fu);
        double pitchCents = formulaScalePitchCents(scale, degree);
        if (motion == FormulaScaleMotion::connect || motion == FormulaScaleMotion::cadence ||
            accompanied) {
            const double target = previousPitch >= 0.0 ? previousPitch : 6000.0;
            double best = pitchCents, distance = 1.0e30;
            for (int octave = -6; octave <= 6; ++octave) {
                const double candidate = pitchCents + octave * 1200.0;
                if (candidate < 4800.0 || candidate > 8400.0) continue;
                const double delta = std::abs(candidate - target);
                if (delta < distance) { distance = delta; best = candidate; }
            }
            pitchCents = best;
            previousPitch = best;
        }
        const float metric = pulses > 0 ? (step % pulses == 0 ? 1.16f : 1.0f) : metricAccent(step, beats, result.groupingVariant, stepsPerBeat, compoundSixEight);
        if (motion == FormulaScaleMotion::chordGuide && metric >= 1.09f) {
            double best = pitchCents, distance = 1.0e30;
            for (int voice = 0; voice < 3; ++voice) {
                const int chordDegree = (chordRoot(bar) + 2 * voice) % scaleCount;
                for (int octave = 0; octave <= 3; ++octave) {
                    const double candidate = 4800.0 + scale.tonicCents + octave * 1200.0 +
                        scale.degreesCents[static_cast<std::size_t>(chordDegree)];
                    if (candidate > 8400.0) continue;
                    const double delta = std::abs(candidate - pitchCents);
                    if (delta < distance) { best = candidate; distance = delta; }
                }
            }
            pitchCents = previousPitch = best;
        }
        const int gateSteps = 1 + int((value >> 4u) & 0x03u);
        const int accent = int(value >> 6u);
        const int stride = accompanied && !explicitRhythm ? 2 : 1;
        // Triplet subdivisions remain evenly spaced; swing is a binary-pair gesture.
        const double binarySwing = pulses > 0 || stepsPerBeat == 3 || stepsPerBeat == 6 ? 0.0 : swing;
        // Anticipate metric attacks by half a grid position. Bar anchors stay put,
        // so neither the previous bar nor a deliberately silent bar is invaded.
        const bool anticipate = pulses == 0 && syncopation && step % stepsPerBar != 0 &&
            (compoundSixEight ? metric >= 1.09f : step % stepsPerBeat == 0);
        const double start = double(step) * stepSeconds +
            ((step / stride) % 2 != 0 ? binarySwing * stride * stepSeconds : 0.0) -
            (anticipate ? stepSeconds * 0.5 : 0.0);
        double duration = std::min(double(gateSteps) * stepSeconds, result.sequence.end - start);
        if (accompanied)
            for (int next = bar + 1; next < result.bars; ++next)
                if (chordRoots[static_cast<size_t>(next)] == -2) {
                    duration = std::min(duration, next * beats * 60.0 / bpm - start); break;
                }
        const float gain = std::clamp((0.10f + 0.035f * float(accent)) * metric,
                                      0.08f, 0.24f);
        result.sequence.notes[static_cast<std::size_t>(result.sequence.count++)] = {
            start, duration, 440.0 * std::pow(2.0, (pitchCents - 6900.0) / 1200.0), gain,
            VoiceRole::lead, accent < 2 ? 0.25f : 0.5f};
        activeBars[static_cast<std::size_t>(bar)] = true;
    }
    if (accompanied) {
        const double barSeconds = beats * 60.0 / bpm;
        const int count = std::max(1, int(scale.degreeCount));
        std::array<double, 2> previousUpper{{6000.0, 6700.0}};
        for (int bar = 0; bar < result.bars; ++bar) {
            if (!activeBars[static_cast<std::size_t>(bar)]) continue;
            if (bar % harmonicBars != 0 && bar > 0 && activeBars[static_cast<std::size_t>(bar - 1)] && chordRoot(bar) == chordRoot(bar - 1)) continue;
            int heldBars = 1;
            while (heldBars < harmonicBars - bar % harmonicBars && bar + heldBars < result.bars &&
                   activeBars[static_cast<std::size_t>(bar + heldBars)] && chordRoot(bar + heldBars) == chordRoot(bar)) ++heldBars;
            // Seven-degree fields: I–IV–V–I (short phrases use I–V–I/I–I).
            // Other collections use analogous degree positions, not tonal claims.
            const int root = chordRoot(bar);
            for (int voice = 0; voice < 3 && result.sequence.count < int(result.sequence.notes.size()); ++voice) {
                const int degree = root + voice * 2;
                double cents = 4800.0 + scale.tonicCents +
                    scale.degreesCents[static_cast<std::size_t>(degree % count)] +
                    1200.0 * (degree / count);
                if (voice == 0) cents -= 1200.0;
                else {
                    double best = cents, distance = 1.0e30;
                    for (int octave = -3; octave <= 3; ++octave) {
                        const double candidate = cents + octave * 1200.0;
                        if (candidate < 5500.0 || candidate > 7900.0) continue;
                        const double delta = std::abs(candidate - previousUpper[static_cast<std::size_t>(voice - 1)]);
                        if (delta < distance) { best = candidate; distance = delta; }
                    }
                    cents = best;
                    previousUpper[static_cast<std::size_t>(voice - 1)] = best;
                }
                result.sequence.notes[static_cast<std::size_t>(result.sequence.count++)] = {
                    bar * barSeconds, barSeconds * heldBars * 0.85,
                    440.0 * std::pow(2.0, (cents - 6900.0) / 1200.0),
                    voice == 0 ? 0.045f : 0.025f, voice == 0 ? VoiceRole::bass : VoiceRole::harmony};
            }
        }
        std::sort(result.sequence.notes.begin(), result.sequence.notes.begin() + result.sequence.count,
            [](const Note& a, const Note& b) {
                return a.start != b.start ? a.start < b.start : a.frequency < b.frequency;
            });
    }
    if (motion == FormulaScaleMotion::cadence && result.sequence.count > 0) {
        auto& last = result.sequence.notes[static_cast<std::size_t>(result.sequence.count - 1)];
        const double target = 6900.0 + 1200.0 * std::log2(last.frequency / 440.0);
        double best = 4800.0 + scale.tonicCents;
        for (double tonic = best; tonic <= 8400.0; tonic += 1200.0)
            if (std::abs(tonic - target) < std::abs(best - target)) best = tonic;
        last.frequency = 440.0 * std::pow(2.0, (best - 6900.0) / 1200.0);
    }
    return result;
}

}
