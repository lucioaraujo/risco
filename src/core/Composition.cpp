// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Lúcio de Araújo
#include "Composition.hpp"
#include "FormulaSeed.hpp"
#include "Harmony.hpp"
#include "Utf8.hpp"
#include <algorithm>
#include <cmath>

namespace risco {
bool validPhraseMeters(const PhraseMeters& meters) noexcept {
    for (int meter : meters) if (meter != 0 && (meter < 2 || meter > 8)) return false;
    return true;
}
bool validPhraseSectionBars(const PhraseSectionBars& bars) noexcept {
    return std::all_of(bars.begin(), bars.end(), [](int value) { return value >= 1 && value <= 4; });
}
std::array<int, 4> phraseMeterWeights(const PhraseMeters& meters, int beats) noexcept {
    std::array<int, 4> result{};
    for (int i = 0; i < 4; ++i) result[static_cast<size_t>(i)] = meters[static_cast<size_t>(i)] == 8 ? 3 :
        std::clamp(meters[static_cast<size_t>(i)] == 0 ? beats : meters[static_cast<size_t>(i)], 2, 7);
    return result;
}
std::array<int, 4> phraseSectionWeights(const PhraseMeters& meters,
                                        const PhraseSectionBars& bars,
                                        int beats) noexcept {
    auto result = phraseMeterWeights(meters, beats);
    if (!validPhraseSectionBars(bars)) return result;
    for (int i = 0; i < 4; ++i) result[static_cast<size_t>(i)] *= bars[static_cast<size_t>(i)];
    return result;
}
bool validSectionEdits(const SectionEdits& edits) noexcept {
    for (const auto& edit : edits)
        if (!std::isfinite(edit.density) || edit.density < 0 || edit.density > 1 ||
            !std::isfinite(edit.gain) || edit.gain < 0 || edit.gain > 1.5f ||
            edit.octaves < -2 || edit.octaves > 2) return false;
    return true;
}
bool validTemporalGestureCurve(const TemporalGestureCurve& curve) noexcept {
    if (!curve.enabled) return true;
    return std::all_of(curve.values.begin(), curve.values.end(), [](float value) {
               return std::isfinite(value) && value >= 0.0f && value <= 1.0f;
           }) && std::abs(curve.values[0]) <= 1.0e-6f &&
        std::abs(curve.values[2] - 1.0f) <= 1.0e-6f &&
        curve.values[0] <= curve.values[1] && curve.values[1] <= curve.values[2];
}
bool validTemporalGestureRange(int semitones) noexcept {
    return semitones == 6 || semitones == 12 || semitones == 24;
}

Sequence stretchSections(const Sequence& source, const std::array<int, 4>& sourceWeights,
                         const PhraseSectionBars& bars) noexcept {
    if (!validPhraseSectionBars(bars) || !std::isfinite(source.end) || source.end <= 0) return source;
    int total = 0;
    for (int value : sourceWeights) total += std::max(1, value);
    if (total <= 0) return source;
    std::array<double, 5> from{}, to{};
    for (int i = 0; i < 4; ++i) {
        from[static_cast<size_t>(i + 1)] = from[static_cast<size_t>(i)] +
            source.end * std::max(1, sourceWeights[static_cast<size_t>(i)]) / double(total);
        to[static_cast<size_t>(i + 1)] = to[static_cast<size_t>(i)] +
            (from[static_cast<size_t>(i + 1)] - from[static_cast<size_t>(i)]) * bars[static_cast<size_t>(i)];
    }
    Sequence result = source;
    result.end = to[4];
    result.repeatPeriod = source.repeatPeriod > 0 ? result.end : 0.0;
    for (int n = 0; n < result.count; ++n) {
        auto& note = result.notes[static_cast<size_t>(n)];
        int section = 3;
        for (int s = 0; s < 3; ++s) if (note.start < from[static_cast<size_t>(s + 1)]) { section = s; break; }
        const double scale = double(bars[static_cast<size_t>(section)]);
        note.start = to[static_cast<size_t>(section)] +
            (note.start - from[static_cast<size_t>(section)]) * scale;
        note.duration = std::min(note.duration * scale, result.end - note.start);
    }
    return result;
}

Sequence editSections(const Sequence& source, const SectionEdits& edits, int sections, const std::array<int, 4>& weights) noexcept {
    if (!validSectionEdits(edits) || !std::isfinite(source.end) || source.end <= 0) return source;
    sections = std::clamp(sections, 1, 4);
    Sequence result = source;
    result.count = 0;
    std::array<std::array<int, 3>, 4> ordinals{};
    int totalWeight = 0;
    for (int i = 0; i < sections; ++i) totalWeight += std::clamp(weights[static_cast<size_t>(i)], 1, 7);
    for (int i = 0; i < std::clamp(source.count, 0, int(source.notes.size())); ++i) {
        auto note = source.notes[static_cast<size_t>(i)];
        int section = 0, boundary = std::clamp(weights[0], 1, 7);
        while (section < sections - 1 && note.start >= source.end * boundary / totalWeight) {
            ++section; boundary += std::clamp(weights[static_cast<size_t>(section)], 1, 7);
        }
        const auto& edit = edits[static_cast<size_t>(section)];
        const int role = std::clamp(int(note.role), 0, 2);
        const int ordinal = ordinals[static_cast<size_t>(section)][static_cast<size_t>(role)]++;
        // Same rule for each voice; retain the first gesture of a nonempty section.
        if (edit.density == 0 || edit.gain == 0 ||
            (ordinal > 0 && std::floor(ordinal * edit.density) == std::floor((ordinal - 1) * edit.density))) continue;
        note.frequency = std::clamp(std::ldexp(note.frequency, edit.octaves), 1.0, 20000.0);
        note.gain = std::clamp(note.gain * edit.gain, 0.0f, 1.0f);
        result.notes[static_cast<size_t>(result.count++)] = note;
    }
    return result;
}
Sequence interpretInput(const Sequence& source, int mode, float strength,
                        const TemporalGestureCurve& gestureCurve, int rangeSemitones) noexcept {
    strength = std::clamp(std::isfinite(strength) ? strength : 1.0f, 0.0f, 1.0f);
    if (strength == 0) return source;
    if (mode <= 0 || mode > 13 || source.count < 1 || source.count > int(source.notes.size()) ||
        !std::isfinite(source.end) || source.end <= 0) return source;
    if ((mode >= 8 && mode <= 13) && !validTemporalGestureCurve(gestureCurve)) return source;
    if ((mode >= 11 && mode <= 13) && !validTemporalGestureRange(rangeSemitones)) return source;
    Sequence result = source;
    const double step = source.end / source.count;
    const auto warp = [mode](double position) noexcept {
        const double x = std::clamp(position,0.0,1.0);
        if (mode == 5) return 1.0 - (1.0-x)*(1.0-x); // Slow opening, compressed/quick ending.
        if (mode == 6) return x*x;                    // Quick opening, expanded/slow ending.
        if (mode == 7) return x + std::sin(6.283185307179586*x) * 0.08;
        return x;
    };
    const auto authorialWarp = [&gestureCurve](double position) noexcept {
        const double x = std::clamp(position, 0.0, 1.0);
        if (x <= 0.5)
            return double(gestureCurve.values[0]) + (double(gestureCurve.values[1]) - gestureCurve.values[0]) * x * 2.0;
        return double(gestureCurve.values[1]) + (double(gestureCurve.values[2]) - gestureCurve.values[1]) * (x - 0.5) * 2.0;
    };
    const auto mapPosition = [mode, &warp, &authorialWarp](double position) noexcept {
        return mode == 8 ? authorialWarp(position) : warp(position);
    };
    for (int i = 0; i < result.count; ++i) {
        auto& note = result.notes[static_cast<size_t>(i)];
        if (mode == 4) {
            note.start = std::max(0.0, source.end - note.start - note.duration);
        } else if (mode == 9) {
            const auto& original = source.notes[static_cast<size_t>(i)];
            note.gain = float(original.gain * authorialWarp(original.start / source.end));
        } else if (mode == 10) {
            const auto& original = source.notes[static_cast<size_t>(i)];
            note.gain = float(original.gain * (1.0 - authorialWarp(original.start / source.end)));
        } else if (mode >= 11 && mode <= 13) {
            const auto& original = source.notes[static_cast<size_t>(i)];
            const double curve = authorialWarp(original.start / source.end);
            const double progress = mode == 11 ? curve : mode == 12 ? 1.0 - curve : 1.0 - std::abs(2.0 * curve - 1.0);
            note.frequency = std::clamp(original.frequency * std::exp2(progress * rangeSemitones / 12.0),1.0,20000.0);
        } else if (mode >= 5) {
            const auto& original = source.notes[static_cast<size_t>(i)];
            const double start = mapPosition(original.start/source.end) * source.end;
            const double finish = mapPosition((original.start+original.duration)/source.end) * source.end;
            note.start = start;
            note.duration = std::max(1e-9,finish-start);
        } else {
            if (mode == 1 || mode == 3) { note.start = i * step; note.duration = step * 0.75; }
            if (mode == 2 || mode == 3) note.frequency = 220;
            if (mode == 1 || mode == 2) note.gain = 0.16f;
            note.role = VoiceRole::lead;
        }
        if (strength < 1) {
            const auto& original = source.notes[static_cast<size_t>(i)];
            note.start = original.start + strength * (note.start - original.start);
            note.duration = original.duration + strength * (note.duration - original.duration);
            if (original.frequency > 0 && note.frequency > 0 && original.frequency != note.frequency)
                note.frequency = std::exp(std::log(original.frequency) + strength * (std::log(note.frequency) - std::log(original.frequency)));
            note.gain = original.gain + strength * (note.gain - original.gain);
            note.role = original.role;
        }
    }
    if (mode == 4) std::sort(result.notes.begin(), result.notes.begin() + result.count,
        [](const Note& a, const Note& b) { return a.start < b.start; });
    return result;
}
Sequence compose(const Inscription& input) noexcept {
    Sequence result;
    double cursor = 0.0;
    const int total = std::clamp(input.count, 0, int(input.strokes.size()));
    const auto seed = formulaSeedFromDrawing(input);
    const unsigned harmony = paletteForDrawingSeed(seed);
    const auto& palette = harmonicPalettes[harmony];
    const int root = rootForDrawingSeed(seed);
    const auto progressionGroup = [total](int index) noexcept {
        return total <= 1 ? 0 : (index * 3 + (total - 1) / 2) / (total - 1);
    };
    std::array<double, 12> durations{};
    std::array<double, 4> groupDurations{};
    std::array<bool, 4> groupAudible{};
    std::array<double, 4> groupStarts{};
    groupStarts.fill(-1.0);
    for (int i = 0; i < total; ++i) {
        const auto& stroke = input.strokes[static_cast<size_t>(i)];
        const int points = std::clamp(stroke.count, 0, int(stroke.points.size()));
        if (points < 2) continue;
        double length = 0.0;
        for (int p = 1; p < points; ++p) {
            const auto a = stroke.points[static_cast<size_t>(p - 1)];
            const auto b = stroke.points[static_cast<size_t>(p)];
            length += std::hypot(double(b.x - a.x), double(b.y - a.y));
        }
        const double duration = length < 0.35 ? 0.25 : (length < 0.8 ? 0.5 : 0.75);
        durations[static_cast<size_t>(i)] = duration;
        const int bar = progressionGroup(i);
        groupDurations[static_cast<size_t>(bar)] += duration;
        groupAudible[static_cast<size_t>(bar)] =
            groupAudible[static_cast<size_t>(bar)] || !stroke.crossed;
    }
    for (int i = 0; i < total; ++i) {
        const auto& stroke = input.strokes[static_cast<size_t>(i)];
        if (stroke.count < 2) continue;
        const int points = std::clamp(stroke.count, 0, 128);
        double previousDerivative = 0.0, memory = 0.0;
        double derivativeSum = 0.0, curvatureSum = 0.0;
        for (int p = 1; p < points; ++p) {
            const auto a = stroke.points[static_cast<size_t>(p - 1)];
            const auto b = stroke.points[static_cast<size_t>(p)];
            const double segmentLength = std::hypot(double(b.x - a.x), double(b.y - a.y));
            // Read vertical position as a bounded field and vertical movement
            // per distance as its discrete derivative. This is a Risco
            // adaptation of bounded memory/difference relations studied in
            // Batruc, not copied code or a formula attributed to Vygodski.
            const double field = std::clamp(0.5 - double(b.y), -0.5, 0.5);
            const double derivative = std::clamp(
                double(b.y - a.y) / std::max(segmentLength, 0.01), -1.0, 1.0);
            const double curvature = derivative - previousDerivative;
            memory = std::clamp(0.8 * memory + 0.2 * field, -0.5, 0.5);
            derivativeSum += derivative;
            curvatureSum += curvature;
            previousDerivative = derivative;
        }
        const double divisor = std::max(1, points - 1);
        const double calculusOffset = std::clamp(
            2.0 * memory + 0.45 * derivativeSum / divisor
                + 0.15 * curvatureSum / divisor, -3.0, 3.0);
        const double duration = durations[static_cast<size_t>(i)];
        const int bar = progressionGroup(i);
        if (!stroke.crossed && groupStarts[static_cast<size_t>(bar)] < 0.0)
            groupStarts[static_cast<size_t>(bar)] = cursor;
        const int contourNotes = std::clamp((points + 14) / 16, 1, 4);
        if (!stroke.crossed) {
            for (int event = 0; event < contourNotes &&
                 result.count < int(result.notes.size()) - 2; ++event) {
                const double position = contourNotes == 1 ? 0.5 :
                    double(event) / double(contourNotes - 1);
                const int pointIndex = std::clamp(
                    static_cast<int>(std::lround(position * (points - 1))), 0, points - 1);
                const auto& point = stroke.points[static_cast<size_t>(pointIndex)];
                const auto& before = stroke.points[static_cast<size_t>(std::max(0, pointIndex - 1))];
                const auto& after = stroke.points[static_cast<size_t>(std::min(points - 1, pointIndex + 1))];
                const double localSlope = std::clamp(double(after.y - before.y), -1.0, 1.0);
                const int contourDegree = static_cast<int>(std::lround(
                    (1.0 - std::clamp(double(point.y), 0.0, 1.0)) * 7.0));
                const int inflection = static_cast<int>(std::lround(
                    calculusOffset * 0.5 + localSlope * 0.5));
                const int scaleDegree = std::clamp(
                    contourDegree + palette.progression[static_cast<size_t>(bar)]
                        + inflection, 0, 14);
                const int midi = root + (scaleDegree / 7) * 12
                    + palette.intervals[static_cast<size_t>(scaleDegree % 7)];
                const float pulseDuty = static_cast<float>(std::clamp(
                    0.25 + 0.08 * memory + 0.12 * localSlope
                        + 0.10 * curvatureSum / divisor, 0.125, 0.5));
                const double eventDuration = duration / contourNotes;
                result.notes[static_cast<size_t>(result.count++)] = {
                    cursor + event * eventDuration, eventDuration * 0.72,
                    440.0 * std::pow(2.0, (midi - 69) / 12.0),
                    static_cast<float>(std::clamp(0.17 + 0.03 * memory, 0.12, 0.22)),
                    VoiceRole::lead, pulseDuty};
            }
        }
        cursor += duration;
    }
    for (size_t bar = 0; bar < groupStarts.size(); ++bar) {
        if (!groupAudible[bar] || groupStarts[bar] < 0.0 || groupDurations[bar] <= 0.0)
            continue;
        const int degree = palette.progression[bar];
        const double chordDuration = groupDurations[bar] * 0.9;
        for (int chordTone = 0; chordTone < 3; ++chordTone) {
            const int chordDegree = degree + chordTone * 2;
            const int midi = root - 12 + (chordDegree / 7) * 12
                + palette.intervals[static_cast<size_t>(chordDegree % 7)];
            result.notes[static_cast<size_t>(result.count++)] = {
                groupStarts[bar], chordDuration,
                440.0 * std::pow(2.0, (midi - 69) / 12.0),
                chordTone == 0 ? 0.045f : 0.035f,
                chordTone == 0 ? VoiceRole::bass : VoiceRole::harmony};
        }
    }
    if (input.ascii.find('.') != std::string::npos) cursor += 0.5;
    const double replyStart = cursor + 0.4;
    const int sourceCount = std::min(result.count, 2);
    for (int i = 0; i < sourceCount; ++i) {
        const auto source = result.notes[static_cast<size_t>(i)];
        result.notes[static_cast<size_t>(result.count++)] =
            {replyStart + i * 0.5, source.duration, source.frequency * 0.5, 0.13f,
             VoiceRole::lead, source.pulseDuty};
    }
    result.end = replyStart + sourceCount * 0.5 + 0.5;
    return result;
}

Sequence composeText(const std::string& text) noexcept {
    Sequence result;
    double cursor = 0.0;
    int previous = 0;
    std::size_t offset = 0;
    std::uint32_t symbol = 0;
    while (offset < text.size()) {
        if (!nextUtf8(text, offset, symbol)) continue;
        if (symbol >= 0x300u && symbol <= 0x36fu) {
            if (result.count > 0) {
                auto& previousNote = result.notes[static_cast<size_t>(result.count - 1)];
                previousNote.gain = std::min(0.25f, previousNote.gain + 0.02f);
                previousNote.pulseDuty = std::min(0.5f, previousNote.pulseDuty + 0.05f);
            }
            continue;
        }
        if (symbol == ' ' || symbol == '\t' || symbol == 0xa0u) {
            cursor += 0.18;
            previous = 0;
            continue;
        }
        if (symbol == '.' || symbol == ',' || symbol == ';' || symbol == ':' ||
            symbol == '!' || symbol == '?' || symbol == 0x2014u || symbol == 0x2026u) {
            cursor += symbol == '.' || symbol == 0x2026u ? 0.38 : 0.22;
            previous = 0;
            continue;
        }
        const int value = static_cast<int>(symbol);
        const int interval = previous == 0 ? 0 : ((value - previous) % 13 + 13) % 13 - 6;
        const int midi = std::clamp(55 + ((value * 7 + (value >> 2)) % 19) + interval, 48, 84);
        const double duration = 0.22 + double((value >> 1) % 4) * 0.045;
        if (result.count < int(result.notes.size())) {
            result.notes[static_cast<size_t>(result.count++)] = {
                cursor, duration * 0.88,
                440.0 * std::pow(2.0, (midi - 69) / 12.0),
                0.16f + float(value % 4) * 0.012f,
                VoiceRole::lead, 0.125f + float(value % 4) * 0.125f};
        }
        cursor += duration;
        previous = value;
    }
    result.end = std::max(cursor, 0.18);
    return result;
}

Sequence composeInput(const Inscription& inscription) noexcept {
    const int strokeCount = std::clamp(inscription.count, 0, int(inscription.strokes.size()));
    for (int i = 0; i < strokeCount; ++i)
        if (inscription.strokes[static_cast<size_t>(i)].count >= 2)
            return compose(inscription);
    return composeText(inscription.ascii);
}

Sequence arrange(const Sequence& source, CellPlan plan) noexcept {
    Sequence output;
    if (!(source.end > 0.0) || !std::isfinite(source.end)) return output;
    const double bpm = std::clamp(std::isfinite(plan.bpm) ? plan.bpm : 120.0, 40.0, 240.0);
    const int beats = std::clamp(plan.beatsPerBar, 2, 7);
    const int repetitions = std::clamp(plan.repeats, 1, 8);
    const int cellBeats = plan.span == CellSpan::beat ? 1 :
                          plan.span == CellSpan::bar ? beats :
                          plan.span == CellSpan::twoBars ? 2 * beats : 4 * beats;
    const double cellSeconds = 60.0 * cellBeats / bpm;
    const double factor = cellSeconds / source.end;
    const int sourceCount = std::clamp(source.count, 0, int(source.notes.size()));
    for (int index = 0; index < sourceCount && output.count < int(output.notes.size()); ++index) {
        const auto& note = source.notes[static_cast<size_t>(index)];
        if (!std::isfinite(note.start) || !std::isfinite(note.duration) ||
            !std::isfinite(note.frequency) || note.duration <= 0.0) continue;
        output.notes[static_cast<size_t>(output.count++)] = {
            note.start * factor, note.duration * factor, note.frequency, note.gain, note.role,
            note.pulseDuty};
    }
    output.end = cellSeconds * repetitions;
    output.repeatPeriod = repetitions > 1 ? cellSeconds : 0.0;
    return output;
}
}
