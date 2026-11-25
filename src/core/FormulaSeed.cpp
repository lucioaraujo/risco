// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Lúcio de Araújo
#include "FormulaSeed.hpp"
#include "BytebeatExpression.hpp"
#include "Harmony.hpp"
#include "LogisticControl.hpp"
#include "MathControl.hpp"
#include <algorithm>
#include <array>
#include <cmath>

namespace risco {
namespace {
std::uint64_t next(std::uint64_t& state) noexcept {
    state += 0x9e3779b97f4a7c15ULL;
    auto value = state;
    value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31);
}
}

std::string formulaFromSeedV1(std::uint64_t seed, unsigned variation) {
    const unsigned family = variation % 4;
    std::uint64_t state = seed ^ (0xd1b54a32d192ed03ULL * (family + 1u));
    const auto multiplier = std::to_string(3u + 2u * unsigned(next(state) % 7u));
    const auto shortShift = std::to_string(3u + unsigned(next(state) % 6u));
    const auto longShift = std::to_string(9u + unsigned(next(state) % 9u));
    const auto mask = std::to_string(15u + 16u * unsigned(next(state) % 8u));
    switch (family) {
        case 0: return "((t*" + multiplier + ") & (t>>" + shortShift + ")) | ((t>>" + longShift + ") ^ " + mask + ")";
        case 1: return "((t + (t>>" + shortShift + ")) * " + multiplier + ") ^ ((t>>" + longShift + ") & " + mask + ")";
        case 2: return "((t*" + multiplier + ") ^ (t>>" + shortShift + ")) & ((t>>" + longShift + ") | " + mask + ")";
        default: return "((t>>" + shortShift + ") | (t*" + multiplier + ")) ^ ((t>>" + longShift + ") & " + mask + ")";
    }
}

std::string formulaFromSeedV2(std::uint64_t seed, unsigned stage) {
    std::uint64_t state = seed ^ 0x6a09e667f3bcc909ULL;
    const auto odd = std::to_string(3u + 2u * unsigned(next(state) % 7u));
    const auto otherOdd = std::to_string(5u + 2u * unsigned(next(state) % 6u));
    const auto shortShift = std::to_string(4u + unsigned(next(state) % 4u));
    const auto middleShift = std::to_string(8u + unsigned(next(state) % 4u));
    const auto longShift = std::to_string(12u + unsigned(next(state) % 5u));
    const auto accentMask = std::to_string(15u + 16u * unsigned(next(state) % 4u));
    const auto carrier = "(t*" + odd + ")";
    const auto rhythm = "(t>>" + shortShift + "|t>>" + middleShift + ")";
    const auto accent = "((t>>" + longShift + ")&" + accentMask + ")";
    const auto voice = "(" + carrier + "&" + rhythm + ")";
    const auto modulated = "((t*(" + odd + "+((t>>" + longShift + ")&3)))&" + rhythm + ")";
    switch (stage % 4u) {
        case 0: return "(t*" + odd + ")&(t>>" + shortShift + ")";
        case 1: return voice + "|" + accent;
        case 2: return modulated + "|" + accent;
        default: return "(" + modulated + "^((t*" + otherOdd + ")>>" + middleShift + "))|" + accent;
    }
}

std::string formulaFromSeedV3(std::uint64_t seed, unsigned stage) {
    std::uint64_t state = seed ^ 0xa0761d6478bd642fULL;
    const unsigned family = static_cast<unsigned>(next(state) % 8u);
    const auto odd = std::to_string(3u + 2u * unsigned(next(state) % 7u));
    const auto otherOdd = std::to_string(5u + 2u * unsigned(next(state) % 6u));
    const auto shiftA = std::to_string(3u + unsigned(next(state) % 5u));
    const auto shiftB = std::to_string(7u + unsigned(next(state) % 7u));
    const auto shiftC = std::to_string(12u + unsigned(next(state) % 7u));
    const auto shiftD = std::to_string(5u + unsigned(next(state) % 8u));
    const auto maskA = std::to_string(31u + 32u * unsigned(next(state) % 8u));
    const auto maskB = std::to_string(15u + 16u * unsigned(next(state) % 8u));
    const auto modulus = std::to_string(31u + 32u * unsigned(next(state) % 8u));
    constexpr const char* rhythmOps[] = {"|", "^", "&", "+"};
    constexpr const char* layerOps[] = {"|", "^", "&", "+", "-"};
    const auto rhythmOp = rhythmOps[family % 4u];
    const std::string carrier = [&] {
        switch (family) {
            case 0: return "(t*" + odd + ")";
            case 1: return "((t+(t>>" + shiftA + "))*" + odd + ")";
            case 2: return "((t^(t>>" + shiftA + "))*" + odd + ")";
            case 3: return "((t|(t>>" + shiftA + "))*" + odd + ")";
            case 4: return "((t*" + odd + ")+(t>>" + shiftA + "))";
            case 5: return "((t*" + odd + ")^(t>>" + shiftA + "))";
            case 6: return "((t*" + odd + ")&(t>>" + shiftA + "))";
            default: return "((t*" + odd + ")|(t>>" + shiftA + "))";
        }
    }();
    const std::string rhythm = "((t>>" + shiftA + ")" + rhythmOp + "(t>>" + shiftB + "))";
    const std::string accent = "((t>>" + shiftC + ")&" + maskA + ")";
    const std::string pulse = "((t*" + otherOdd + ")%" + modulus + ")";
    const std::string slow = "((t*" + otherOdd + ")>>" + shiftC + ")";
    const std::string lowMask = "((t>>" + std::to_string(16u + unsigned(next(state) % 9u)) +
                                 ")&" + maskB + ")";

    std::string expression;
    switch (family) {
        case 0: expression = carrier + "&(t>>" + shiftA + ")"; break;
        case 1: expression = carrier + "^" + rhythm; break;
        case 2: expression = carrier + "|" + rhythm; break;
        case 3: expression = carrier + "+" + rhythm; break;
        case 4: expression = "(" + carrier + "%" + modulus + ")^" + rhythm; break;
        case 5: expression = carrier + "&" + rhythm; break;
        case 6: expression = carrier + "^" + accent; break;
        default: expression = carrier + "-" + rhythm; break;
    }

    const unsigned layers = std::min(stage, 5u);
    if (layers >= 1u)
        expression = "(" + expression + layerOps[(family + 1u) % 5u] + accent + ")";
    if (layers >= 2u)
        expression = "(" + expression + layerOps[(family + 2u) % 5u] + pulse + ")";
    if (layers >= 3u) {
        const std::string secondRhythm = "((t>>" + shiftB + ")" +
            rhythmOps[(family + 1u) % 4u] + "(t>>" + shiftD + "))";
        expression = "(" + expression + layerOps[(family + 3u) % 5u] + secondRhythm + ")";
    }
    if (layers >= 4u)
        expression = "(" + expression + layerOps[(family + 4u) % 5u] + slow + ")";
    if (layers >= 5u)
        expression = "(" + expression + layerOps[family % 5u] + lowMask + ")";
    return expression;
}

std::string recombineFormulaV1(const std::string& first, const std::string& second,
                               std::uint64_t seed) {
    if (first.empty() || second.empty() || first.size() + second.size() > 118)
        return {};
    const char operation = "|^&"[seed % 3u];
    return "(" + first + ")" + operation + "(" + second + ")";
}

std::string recombinationOperatorV2(std::uint64_t seed) {
    // Each operator gives the same two parents a distinct, reproducible
    // relation. Unsigned wrap, modulo-by-zero, and bounded shift behavior are
    // defined by the Bytebeat evaluator, so all nine choices are deterministic.
    constexpr const char* operations[] = {
        "+", "-", "*", "%", "&", "|", "^", "<<", ">>"};
    return operations[seed % (sizeof(operations) / sizeof(operations[0]))];
}

std::string recombineFormulaV2(const std::string& first, const std::string& second,
                               std::uint64_t seed) {
    if (first.empty() || second.empty() || first.size() + second.size() > 118)
        return {};
    const auto operation = recombinationOperatorV2(seed);
    const auto shift = operation == "<<" || operation == ">>";
    auto expression = "(" + first + ")" + operation +
        (shift ? "((" + second + ")&31)" : "(" + second + ")");
    if (expression.size() > 128) return {};
    BytebeatProgram program;
    std::string error;
    if (!compileBytebeat(expression, program, error)) return {};
    return expression;
}

FormulaGlitch glitchFormulaV1(const std::string& source, std::uint64_t seed) {
    std::uint64_t state = seed ^ 0xe7037ed1a0b428dbULL;
    const auto kind = static_cast<FormulaGlitchKind>(next(state) % 3u);
    return glitchFormulaV1(source, seed, kind, 50);
}

FormulaGlitch glitchFormulaV1(const std::string& source, std::uint64_t seed,
                              FormulaGlitchKind kind, unsigned intensity) {
    FormulaGlitch result;
    if (source.empty() || source.size() > 116) return result;
    std::uint64_t state = seed ^ 0xe7037ed1a0b428dbULL;
    intensity = std::clamp(intensity, 1u, 100u);
    switch (kind) {
        case FormulaGlitchKind::mask: {
            result.kind = "mask";
            const auto bits = 1u + (intensity * 9u + 99u) / 100u;
            result.parameterA = (1u << bits) - 1u;
            result.expression = "(" + source + ")&" + std::to_string(result.parameterA);
            break;
        }
        case FormulaGlitchKind::xorPulse: {
            result.kind = "xor-pulse";
            result.parameterA = 3u + static_cast<std::uint32_t>(next(state) % 10u);
            const auto bits = 1u + (intensity * 7u + 99u) / 100u;
            result.parameterB = (1u << bits) - 1u;
            result.expression = "(" + source + ")^((t>>" +
                std::to_string(result.parameterA) + ")&" +
                std::to_string(result.parameterB) + ")";
            break;
        }
        case FormulaGlitchKind::wrap: {
            result.kind = "wrap";
            result.parameterA = 16u + (intensity * 1008u) / 100u;
            result.expression = "(" + source + ")%" + std::to_string(result.parameterA);
            break;
        }
    }

    if (result.expression.size() > 128) return {};
    BytebeatProgram program;
    std::string error;
    if (!compileBytebeat(result.expression, program, error)) return {};
    return result;
}

Sequence phraseFromSeedV1(std::uint64_t seed, unsigned variation, double bpm) noexcept {
    Sequence sequence;
    // Eight-beat phrase, repeated and varied across four bars at 120 BPM.
    // A small pentatonic pitch field and a four-note motif provide identity;
    // bounded mutations and rests let the phrase breathe between repetitions.
    const unsigned harmony = variation % harmonicPalettes.size();
    const auto& palette = harmonicPalettes[harmony];
    std::uint64_t state = seed ^ (0x94d049bb133111ebULL * (variation + 1u));
    const int root = 48 + int(next(state) % 13u);
    std::array<int, 8> motif{};
    for (auto& degree : motif) degree = int(next(state) % 5u);
    bpm = std::clamp(std::isfinite(bpm) ? bpm : 120.0, 40.0, 240.0);
    const double step = 30.0 / bpm; // eighth-note grid
    const double barDuration = 240.0 / bpm;
    for (int bar = 0; bar < 4; ++bar) {
        const int degree = palette.progression[static_cast<size_t>(bar)];
        const int thirdIndex = (degree + 2) % 7;
        const int fifthIndex = (degree + 4) % 7;
        for (const int chordDegree : {degree, thirdIndex, fifthIndex}) {
            const int midi = std::clamp(root + palette.intervals[static_cast<size_t>(chordDegree)] - 12,
                                        36, 60);
            sequence.notes[static_cast<size_t>(sequence.count++)] = Note{
                bar * barDuration, barDuration * 0.9,
                440.0 * std::pow(2.0, (midi - 69) / 12.0), 0.045f,
                chordDegree == degree ? VoiceRole::bass : VoiceRole::harmony};
        }
    }
    for (int slot = 0; slot < 32 && sequence.count < int(sequence.notes.size()); ++slot) {
        const int cycle = slot / 8;
        const int local = slot % 8;
        const auto random = next(state);
        if ((random & 7u) < (cycle == 0 ? 1u : 2u)) continue;
        const int bar = slot / 8;
        const int harmonicRoot = palette.progression[static_cast<size_t>(bar)];
        int degree = (harmonicRoot + motif[static_cast<size_t>(local)]) % 7;
        if (cycle > 0 && ((random >> 4u) & 3u) == 0u)
            degree = (degree + 1 + int((random >> 8u) % 2u)) % 7;
        const int octave = ((random >> 12u) & 7u) == 0u ? 1 : 0;
        const int midi = std::clamp(root + palette.intervals[static_cast<size_t>(degree)] + octave * 12, 48, 84);
        const float gain = 0.12f + float((random >> 16u) & 3u) * 0.025f;
        sequence.notes[static_cast<size_t>(sequence.count++)] = Note{
            slot * step, step * (((random >> 20u) & 3u) == 0u ? 0.72 : 0.9),
            440.0 * std::pow(2.0, (midi - 69) / 12.0), gain,
            VoiceRole::lead, 0.125f + float((random >> 24u) & 3u) * 0.125f};
    }
    sequence.end = 4.0 * barDuration;
    return sequence;
}

static Sequence phraseFromSeedForm(std::uint64_t seed, unsigned variation, double bpm,
                                   bool alternateForm, bool variedDynamics = false) noexcept {
    Sequence sequence;
    bpm = std::clamp(std::isfinite(bpm) ? bpm : 120.0, 40.0, 240.0);
    const double eighth = 30.0 / bpm;
    const double barLength = 8.0 * eighth;
    // The identity is fixed by the seed. Variation changes development only.
    std::uint64_t identity = seed ^ 0x3c79ac492ba7b653ULL;
    std::uint64_t development = seed ^ (0x1c69b3f74ac4ae35ULL * (std::uint64_t(variation) + 1));
    const auto& palette = harmonicPalettes[static_cast<size_t>(next(identity) % harmonicPalettes.size())];
    const int root = 48 + int(next(identity) % 13u);
    std::array<int, 8> contour{};
    std::array<bool, 8> present{};
    for (int slot = 0; slot < 8; ++slot) {
        contour[static_cast<size_t>(slot)] = int(next(identity) % 5u);
        present[static_cast<size_t>(slot)] = (slot % 2 == 0) || (next(identity) % 5u != 0);
    }
    int previousUpper[2] = {-1, -1};
    const std::array<std::array<float, 4>, 3> dynamicArcs{{
        {{0.66f, 0.82f, 1.00f, 0.70f}}, // build to a clear apex, then resolve
        {{0.66f, 1.00f, 0.82f, 0.68f}}, // early answer, then release
        {{0.62f, 0.74f, 0.92f, 0.82f}}  // longer rise, softer cadence
    }};
    const auto& energy = variedDynamics
        ? dynamicArcs[variation % dynamicArcs.size()]
        : std::array<float, 4>{{0.70f, alternateForm ? 1.0f : 0.82f,
                                alternateForm ? 0.82f : 1.0f, 0.68f}};
    for (int bar = 0; bar < 4; ++bar) {
        // V2: A – A' – contrasting peak – return. V3: A – B – A – C.
        // Both forms retain the original motif and resolve their last bar home.
        const int progressionIndex = alternateForm
            ? ((bar == 0 || bar == 2 || bar == 3) ? 0 : 1)
            : (bar == 3 ? 0 : bar);
        const int chordRoot = palette.progression[static_cast<size_t>(progressionIndex)];
        const int bassDegree = chordRoot % 7;
        const int bassMidi = std::clamp(root - 12 + palette.intervals[static_cast<size_t>(bassDegree)], 36, 60);
        sequence.notes[static_cast<size_t>(sequence.count++)] = Note{
            bar * barLength, barLength * 0.88,
            440.0 * std::pow(2.0, (bassMidi - 69) / 12.0), 0.055f * energy[bar], VoiceRole::bass};
        for (int voice = 0; voice < 2; ++voice) {
            const int degree = chordRoot + 2 * (voice + 1);
            const int base = root + palette.intervals[static_cast<size_t>(degree % 7)]
                + 12 * (degree / 7);
            int chosen = std::clamp(base, 55, 72);
            int bestDistance = 1000;
            for (int octave = -2; octave <= 2; ++octave) {
                const int candidate = base + 12 * octave;
                if (candidate < 55 || candidate > 72) continue;
                const int target = previousUpper[voice] < 0 ? (voice == 0 ? 60 : 67) : previousUpper[voice];
                const int distance = std::abs(candidate - target);
                if (distance < bestDistance) { bestDistance = distance; chosen = candidate; }
            }
            previousUpper[voice] = chosen;
            sequence.notes[static_cast<size_t>(sequence.count++)] = Note{
                bar * barLength, barLength * 0.86,
                440.0 * std::pow(2.0, (chosen - 69) / 12.0), 0.030f * energy[bar], VoiceRole::harmony};
        }
        for (int slot = 0; slot < 8; ++slot) {
            if (!present[static_cast<size_t>(slot)]) continue;
            // Both forms return to the same motif at bar 2 or 3 respectively.
            int degree = contour[static_cast<size_t>(slot)];
            if (alternateForm && bar == 1) {
                degree = 4 - contour[static_cast<size_t>(7 - slot)];
                if (slot != 0 && slot != 4 && (next(development) & 3u) == 0u)
                    degree = (degree + 1) % 5;
            } else if (alternateForm && bar == 3) {
                if (slot == 6) degree = 1;
                if (slot == 7) degree = 0;
            } else if (!alternateForm && bar == 1) {
                if (slot == 2 || slot == 6)
                    degree = (degree + 1 + int(variation % 3u)) % 5;
                else if (slot != 0 && slot != 4 && (next(development) & 3u) == 0u)
                    degree = (degree + 1) % 5;
            }
            if (!alternateForm && bar == 2) {
                degree = 4 - degree;
                if (slot == 2 || slot == 6)
                    degree = (degree + int(variation % 3u)) % 5;
            }
            const int scaleDegree = (chordRoot + degree) % 7;
            const int octave = !alternateForm && bar == 2 && (slot == 0 || slot == 4) ? 12 : 0;
            const int midi = std::clamp(root + palette.intervals[static_cast<size_t>(scaleDegree)] + octave, 48, 84);
            const double start = (bar * 8 + slot) * eighth;
            const float accent = (slot == 0 || slot == 4) ? 1.12f : 0.90f;
            sequence.notes[static_cast<size_t>(sequence.count++)] = Note{
                start, eighth * (bar == 2 ? 0.82 : 0.74),
                440.0 * std::pow(2.0, (midi - 69) / 12.0),
                0.13f * energy[bar] * accent, VoiceRole::lead,
                (!alternateForm && bar == 2) || (alternateForm && bar == 3) ? 0.25f : 0.375f};
        }
    }
    sequence.end = 4.0 * barLength;
    return sequence;
}

Sequence phraseFromSeedV2(std::uint64_t seed, unsigned variation, double bpm) noexcept {
    return phraseFromSeedForm(seed, variation, bpm, false);
}

Sequence phraseFromSeedV3(std::uint64_t seed, unsigned variation, double bpm) noexcept {
    return phraseFromSeedForm(seed, variation, bpm, true);
}

Sequence phraseFromSeedV4(std::uint64_t seed, unsigned variation, double bpm) noexcept {
    const auto fourBars = phraseFromSeedV3(seed, variation, bpm);
    bpm = std::clamp(std::isfinite(bpm) ? bpm : 120.0, 40.0, 240.0);
    Sequence result;
    result.end = 480.0 / bpm; // Two 4/4 bars: A–B call and response.
    for (int i = 0; i < fourBars.count && result.count < int(result.notes.size()); ++i) {
        const auto& note = fourBars.notes[static_cast<size_t>(i)];
        if (note.start >= result.end) continue;
        auto clipped = note;
        clipped.duration = std::min(clipped.duration, result.end - clipped.start);
        if (clipped.duration > 0.0)
            result.notes[static_cast<size_t>(result.count++)] = clipped;
    }
    return result;
}

Sequence phraseFromSeedV5(std::uint64_t seed, unsigned variation, double bpm) noexcept {
    return phraseFromSeedForm(seed, variation, bpm, true, true);
}

static Sequence phraseFromSeedV6(std::uint64_t seed, unsigned variation, double bpm,
                                 unsigned mask, int meter, bool compound, const PhraseMeters& meters,
                                 bool stateful = false, int form = 0) noexcept {
    Sequence result;
    bpm = std::clamp(std::isfinite(bpm) ? bpm : 120.0, 40.0, 240.0);
    const auto weights = phraseMeterWeights(meters, compound ? 3 : meter);
    const double step = 30.0 / bpm;
    double barStart = 0;
    for (int weight : weights) result.end += weight * 60.0 / bpm;
    std::uint64_t identity = seed ^ 0x3c79ac492ba7b653ULL;
    const auto& palette = harmonicPalettes[static_cast<size_t>(next(identity) % harmonicPalettes.size())];
    const int root = 48 + int(next(identity) % 13u);
    std::array<int, 14> motif{};
    for (auto& degree : motif) degree = int(next(identity) % 5);
    LogisticControl control;
    if (stateful) control.reset(seed ^ (std::uint64_t(variation) + 1) * 0x1c69b3f74ac4ae35ULL);
    const auto boundedMap = static_cast<BoundedMathMap>(variation % 5u);
    // Forms share the same seed identity and meter engine. Section identity
    // decides what repeats; transformation decides how a repeat develops.
    constexpr std::array<std::array<int,4>,3> identities{{
        {{0,1,0,3}}, // A-B-A-C
        {{0,0,2,0}}, // A-A'-climax-return
        {{0,1,2,0}}  // A-B-C-A'
    }};
    constexpr std::array<std::array<int,4>,3> progressions{{
        {{0,1,0,0}},
        {{0,1,2,0}},
        {{0,1,2,0}}
    }};
    constexpr std::array<std::array<float,4>,3> energies{{
        {{0.66f,0.82f,1.00f,0.70f}},
        {{0.62f,0.76f,1.00f,0.64f}},
        {{0.62f,0.78f,0.94f,0.70f}}
    }};
    form = std::clamp(form,0,2);
    for (int bar = 0; bar < 4; ++bar) {
        const int beats = weights[static_cast<size_t>(bar)], slots = beats * 2;
        const bool sectionCompound = meters[static_cast<size_t>(bar)] == 8 || (meters[static_cast<size_t>(bar)] == 0 && compound);
        const double start = barStart, barSeconds = slots * step;
        barStart += barSeconds;
        std::array<std::uint32_t, 14> values{};
        // Advance through silent sections too: masking never changes later state.
        if (stateful) for (int slot = 0; slot < slots; ++slot) values[static_cast<size_t>(slot)] = control.next();
        if ((mask & (1u << bar)) == 0) continue;
        std::uint64_t rhythm = seed ^ (std::uint64_t(variation) + 1) * 0x1c69b3f74ac4ae35ULL ^
            (std::uint64_t(identities[static_cast<size_t>(form)][static_cast<size_t>(bar)]) * 0x94d049bb133111ebULL);
        const int progression = progressions[static_cast<size_t>(form)][static_cast<size_t>(bar)];
        const int chord = palette.progression[static_cast<size_t>(progression)];
        const float energy = energies[static_cast<size_t>(form)][static_cast<size_t>(bar)];
        for (int voice = 0; voice < 3; ++voice) {
            const int degree = chord + voice * 2;
            const int midi = std::clamp(root + palette.intervals[static_cast<size_t>(degree % 7)] +
                12 * (degree / 7) - (voice == 0 ? 12 : 0), 36, 84);
            result.notes[static_cast<size_t>(result.count++)] = {start, barSeconds * 0.85,
                440.0 * std::pow(2.0, (midi - 69) / 12.0), (voice == 0 ? 0.055f : 0.03f) * energy,
                voice == 0 ? VoiceRole::bass : VoiceRole::harmony};
        }
        const int variant = int(seed % 3);
        const auto groupStart = [&](int slot) {
            if (sectionCompound) return slot == 0 || slot == 3;
            if (slot % 2 != 0) return false;
            const int beat = slot / 2;
            if (beat == 0) return true;
            if (beats == 5) return beat == (variant % 2 == 0 ? 3 : 2);
            if (beats == 7) return variant == 0 ? beat == 2 || beat == 4 :
                variant == 1 ? beat == 3 || beat == 5 : beat == 2 || beat == 5;
            if (beats == 6) return beat == 3;
            return beats >= 3 && beat == 2;
        };
        int leadCount = 0;
        for (int slot = 0; slot < slots && leadCount < 13; ++slot) {
            const auto random = stateful
                ? std::uint64_t(boundedMathMap(values[static_cast<size_t>(slot)], boundedMap))
                : next(rhythm);
            const bool anchor = groupStart(slot);
            if (!anchor && random % 5 < 2) continue;
            int degree = stateful ? applyMathDestination(static_cast<std::uint32_t>(random), MathDestination::degree)
                                  : motif[static_cast<size_t>(slot)];
            if (form == 0 && bar == 1) degree = (stateful ? 6 : 4) - degree;
            if (form == 1 && bar == 1 && !anchor)
                degree = (degree + 1 + int(variation % 2u)) % 5;
            if (form == 1 && bar == 2) {
                degree = 4 - degree;
                if (anchor) degree = (degree + 2) % 5;
            }
            if (form == 2 && bar == 1) degree = 4 - degree;
            if (form == 2 && bar == 2)
                degree = (degree + 2 + int((slot + variation) % 2u)) % 5;
            if (bar == 3 && slot >= slots - 2) degree = 0;
            const int octave = (form == 1 && bar == 2 && anchor) || (form == 2 && bar == 2 && slot == 0) ? 12 : 0;
            const int midi = std::clamp(root + palette.intervals[static_cast<size_t>((chord + degree) % 7)] + octave, 48, 84);
            result.notes[static_cast<size_t>(result.count++)] = {start + slot * step,
                std::min(step * (anchor ? 1.5 : stateful ? 0.65 + ((random >> 3) & 3u) * 0.12 : 0.72), (slots - slot) * step),
                440.0 * std::pow(2.0, (midi - 69) / 12.0),
                0.13f * energy * (anchor ? 1.12f : 0.9f) * (stateful ? 0.85f + float((random >> 6) & 3u) * 0.1f : 1.0f),
                VoiceRole::lead, stateful ? 0.125f + float((random >> 11) & 3u) * 0.125f : bar == 1 ? 0.25f : 0.375f};
            ++leadCount;
        }
    }
    return result;
}

Sequence phraseFromSeed(std::uint64_t seed, unsigned variation,
                        unsigned version, double bpm, unsigned sectionMask, int beatsPerBar, bool compoundSixEight, const PhraseMeters& meters) noexcept {
    if (version >= 6 && version <= 9) return phraseFromSeedV6(seed, variation, bpm, sectionMask,
        beatsPerBar, compoundSixEight, meters, version == 7, version == 8 ? 1 : version == 9 ? 2 : 0);
    auto phrase = version == 5 ? phraseFromSeedV5(seed, variation, bpm) :
                  version == 3 ? phraseFromSeedV3(seed, variation, bpm) :
                  version == 4 ? phraseFromSeedV4(seed, variation, bpm) :
                  version == 2 ? phraseFromSeedV2(seed, variation, bpm) :
                                 phraseFromSeedV1(seed, variation, bpm);
    const double safeBpm = std::clamp(std::isfinite(bpm) ? bpm : 120.0, 40.0, 240.0);
    const double barDuration = 240.0 / safeBpm;
    int kept = 0;
    for (int i = 0; i < phrase.count; ++i) {
        const auto note = phrase.notes[static_cast<size_t>(i)];
        const auto section = std::min(3u, static_cast<unsigned>(note.start / barDuration));
        if ((sectionMask & (1u << section)) != 0)
            phrase.notes[static_cast<size_t>(kept++)] = note;
    }
    phrase.count = kept;
    return phrase;
}

std::uint64_t formulaSeedFromText(const std::string& text) noexcept {
    std::uint64_t hash = 14695981039346656037ULL;
    for (const unsigned char byte : text) {
        hash ^= byte;
        hash *= 1099511628211ULL;
    }
    hash ^= static_cast<std::uint64_t>(text.size());
    return hash * 1099511628211ULL;
}

std::uint64_t formulaSeedFromDrawing(const Inscription& inscription) noexcept {
    std::uint64_t hash = 14695981039346656037ULL;
    auto feed = [&](std::uint32_t value) {
        for (int shift = 0; shift < 32; shift += 8) {
            hash ^= (value >> shift) & 255u;
            hash *= 1099511628211ULL;
        }
    };
    const int strokeCount = std::clamp(inscription.count, 0, int(inscription.strokes.size()));
    feed(static_cast<std::uint32_t>(strokeCount));
    for (int s = 0; s < strokeCount; ++s) {
        const auto& stroke = inscription.strokes[static_cast<std::size_t>(s)];
        const int count = std::clamp(stroke.count, 0, int(stroke.points.size()));
        feed(static_cast<std::uint32_t>(count));
        feed(stroke.crossed ? 1u : 0u);
        for (int i = 0; i < count; ++i) {
            const auto& point = stroke.points[static_cast<std::size_t>(i)];
            const auto x = std::isfinite(point.x) ? std::clamp(point.x, 0.0f, 1.0f) : 0.0f;
            const auto y = std::isfinite(point.y) ? std::clamp(point.y, 0.0f, 1.0f) : 0.0f;
            feed(static_cast<std::uint32_t>(std::lround(x * 1023.0f)));
            feed(static_cast<std::uint32_t>(std::lround(y * 1023.0f)));
        }
    }
    return hash;
}
}
