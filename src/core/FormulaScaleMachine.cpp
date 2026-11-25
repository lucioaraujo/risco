// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Lúcio de Araújo
//
// Original Risco adaptation of the scale-machine concepts in Trioio:
// PROJETOS_AUTORAIS/TRIOIO/src/core/ScaleMachine.{h,cpp}, commit
// 9446b4ad74b9442744cafb5d0cdf03dce2448a45. The source is AGPL-3.0-or-later.
// Source checksums are recorded in CREDITOS_E_LICENCAS.md. This bounded
// adapter uses only scale rotation, alteration, mirror and interpolation;
// it does not depend on Trioio or copy its scale archive/data.
#include "FormulaScaleMachine.hpp"
#include <algorithm>
#include <cmath>
#include <initializer_list>

namespace risco {
namespace {
constexpr double period = 1200.0;

double wrap(double value, double modulus) noexcept {
    return std::fmod(std::fmod(value, modulus) + modulus, modulus);
}

void normalize(FormulaMachineScale& scale) noexcept {
    const auto count = std::clamp<int>(scale.degreeCount, 1, int(scale.degreesCents.size()));
    scale.degreeCount = static_cast<std::uint8_t>(count);
    scale.periodCents = std::clamp(std::isfinite(scale.periodCents)
        ? scale.periodCents : period, 100.0, 2400.0);
    scale.tonicCents = wrap(std::isfinite(scale.tonicCents) ? scale.tonicCents : 0.0, period);
    for (int i = 0; i < count; ++i)
        scale.degreesCents[static_cast<std::size_t>(i)] =
            wrap(scale.degreesCents[static_cast<std::size_t>(i)], scale.periodCents);
    std::sort(scale.degreesCents.begin(), scale.degreesCents.begin() + count);
    scale.degreesCents[0] = 0.0;
    for (int i = 1; i < count; ++i) {
        auto& degree = scale.degreesCents[static_cast<std::size_t>(i)];
        degree = std::max(degree, scale.degreesCents[static_cast<std::size_t>(i - 1)] + 1.0);
        degree = std::min(degree, scale.periodCents - double(count - i));
    }
}

FormulaMachineScale rotate(const FormulaMachineScale& source, int degree) noexcept {
    auto result = source;
    const int count = source.degreeCount;
    if (count < 2) return result;
    const int index = ((degree % count) + count) % count;
    const double base = source.degreesCents[static_cast<std::size_t>(index)];
    for (int i = 0; i < count; ++i)
        result.degreesCents[static_cast<std::size_t>(i)] -= base;
    result.tonicCents += base;
    normalize(result);
    return result;
}

FormulaMachineScale alter(const FormulaMachineScale& source, int degree,
                          double delta) noexcept {
    auto result = source;
    const int count = source.degreeCount;
    if (count < 3) return result;
    const int index = 1 + ((degree - 1) % (count - 1));
    result.degreesCents[static_cast<std::size_t>(index)] = wrap(
        result.degreesCents[static_cast<std::size_t>(index)] + delta, result.periodCents);
    normalize(result);
    return result;
}

FormulaMachineScale mirror(const FormulaMachineScale& source) noexcept {
    auto result = source;
    for (int i = 0; i < result.degreeCount; ++i)
        result.degreesCents[static_cast<std::size_t>(i)] =
            source.periodCents - source.degreesCents[static_cast<std::size_t>(i)];
    normalize(result);
    return result;
}

double nearestSignedDistance(double from, double to) noexcept {
    double distance = wrap(to - from + period / 2.0, period) - period / 2.0;
    return distance;
}

FormulaMachineScale interpolate(const FormulaMachineScale& from,
                                const FormulaMachineScale& to,
                                double amount) noexcept {
    auto result = from;
    amount = std::clamp(amount, 0.0, 1.0);
    const int fromCount = from.degreeCount;
    for (int i = 1; i < fromCount; ++i) {
        const double here = wrap(from.tonicCents + from.degreesCents[static_cast<std::size_t>(i)], period);
        double nearest = 0.0;
        double best = period;
        for (int j = 0; j < to.degreeCount; ++j) {
            const double target = wrap(to.tonicCents + to.degreesCents[static_cast<std::size_t>(j)], period);
            const double distance = nearestSignedDistance(here, target);
            if (std::abs(distance) < std::abs(best)) { best = distance; nearest = target; }
        }
        result.degreesCents[static_cast<std::size_t>(i)] += amount * best;
    }
    result.periodCents = from.periodCents + amount * (to.periodCents - from.periodCents);
    normalize(result);
    return result;
}
}

FormulaMachineScale makeFormulaScale(FormulaScalePreset preset) noexcept {
    FormulaMachineScale result;
    const auto presetIndex = static_cast<unsigned>(preset);
    result.family = static_cast<std::uint8_t>(presetIndex < 12u ? 0u
        : presetIndex < 18u ? 1u
        : presetIndex < 20u ? 2u : 3u);
    const auto set = [&result](std::initializer_list<double> degrees) {
        result.degreeCount = static_cast<std::uint8_t>(degrees.size());
        std::copy(degrees.begin(), degrees.end(), result.degreesCents.begin());
    };
    switch (preset) {
        case FormulaScalePreset::naturalMinor: set({0, 200, 300, 500, 700, 800, 1000}); break;
        case FormulaScalePreset::dorian: set({0, 200, 300, 500, 700, 900, 1000}); break;
        case FormulaScalePreset::pentatonic: set({0, 300, 500, 700, 1000}); break;
        case FormulaScalePreset::harmonicMinor: set({0, 200, 300, 500, 700, 800, 1100}); break;
        case FormulaScalePreset::melodicMinor: set({0, 200, 300, 500, 700, 900, 1100}); break;
        case FormulaScalePreset::phrygian: set({0, 100, 300, 500, 700, 800, 1000}); break;
        case FormulaScalePreset::mixolydian: set({0, 200, 400, 500, 700, 900, 1000}); break;
        case FormulaScalePreset::lydian: set({0, 200, 400, 600, 700, 900, 1100}); break;
        case FormulaScalePreset::bluesMinor: set({0, 300, 500, 600, 700, 1000}); break;
        case FormulaScalePreset::wholeTone: set({0, 200, 400, 600, 800, 1000}); break;
        case FormulaScalePreset::chromatic:
            set({0, 100, 200, 300, 400, 500, 600, 700, 800, 900, 1000, 1100}); break;
        case FormulaScalePreset::bebopDominant: set({0, 200, 400, 500, 700, 900, 1000, 1100}); break;
        case FormulaScalePreset::bebopMajor: set({0, 200, 400, 500, 700, 800, 900, 1100}); break;
        case FormulaScalePreset::alteredDominant: set({0, 100, 300, 400, 600, 800, 1000}); break;
        case FormulaScalePreset::diminishedHalfWhole: set({0, 100, 300, 400, 600, 700, 900, 1000}); break;
        case FormulaScalePreset::diminishedWholeHalf: set({0, 200, 300, 500, 600, 800, 900, 1100}); break;
        case FormulaScalePreset::lydianDominant: set({0, 200, 400, 600, 700, 900, 1000}); break;
        case FormulaScalePreset::freygish: set({0, 100, 400, 500, 700, 800, 1000}); break;
        case FormulaScalePreset::misheberakh: set({0, 200, 300, 600, 700, 900, 1000}); break;
        case FormulaScalePreset::harmonicMajor: set({0, 200, 400, 500, 700, 800, 1100}); break;
        case FormulaScalePreset::neapolitanMinor: set({0, 100, 300, 500, 700, 800, 1100}); break;
        case FormulaScalePreset::hungarianMinor: set({0, 200, 300, 600, 700, 800, 1100}); break;
        case FormulaScalePreset::doubleHarmonic: set({0, 100, 400, 500, 700, 800, 1100}); break;
        case FormulaScalePreset::augmented: set({0, 300, 400, 700, 800, 1100}); break;
        case FormulaScalePreset::persian: set({0, 100, 400, 500, 600, 800, 1000}); break;
        case FormulaScalePreset::prometheus: set({0, 200, 400, 600, 900, 1000}); break;
        case FormulaScalePreset::major:
        default: set({0, 200, 400, 500, 700, 900, 1100}); break;
    }
    normalize(result);
    return result;
}

FormulaMachineScale evolveFormulaScale(const FormulaMachineScale& previous,
                                       FormulaScaleMotion motion,
                                       std::uint32_t gesture) noexcept {
    switch (motion) {
        case FormulaScaleMotion::cadence:
        case FormulaScaleMotion::accompany:
        case FormulaScaleMotion::chordGuide:
        case FormulaScaleMotion::connect:
            return previous; // Stable pitch field; composition handles octave placement.
        case FormulaScaleMotion::counter:
            return mirror(previous);
        case FormulaScaleMotion::transfigure: {
            constexpr unsigned familyStart[] = {0u, 12u, 18u, 20u};
            constexpr unsigned familySize[] = {12u, 6u, 2u, 7u};
            const unsigned family = std::min<unsigned>(previous.family, 3u);
            const auto targetKind = static_cast<FormulaScalePreset>(familyStart[family] +
                static_cast<unsigned>(gesture >> 2u) % familySize[family]);
            auto target = makeFormulaScale(targetKind);
            target.tonicCents = previous.tonicCents;
            const double amount = 0.35 + 0.4 * double((gesture >> 4u) & 0x0fu) / 15.0;
            return interpolate(previous, target, amount);
        }
        case FormulaScaleMotion::follow:
        default:
            if ((gesture & 1u) == 0u)
                return rotate(previous, 1 + int((gesture >> 1u) %
                    std::max(1u, unsigned(previous.degreeCount - 1u))));
            else {
                constexpr double deviations[] = {21.506, -21.506, 50.0, -50.0};
                const int degree = 1 + int((gesture >> 1u) %
                    std::max(1u, unsigned(previous.degreeCount - 1u)));
                return alter(previous, degree,
                    deviations[(gesture >> 5u) % (sizeof deviations / sizeof deviations[0])]);
            }
    }
}

double formulaScalePitchCents(const FormulaMachineScale& scale, int degree) noexcept {
    const int count = std::max(1, int(scale.degreeCount));
    const int byteDegree = std::clamp(degree, 0, 15);
    // Include both endpoint tonics so all 16 byte degrees span exactly three
    // scale periods; intermediate values are distributed across that range.
    const int maximumScaleStep = 3 * count;
    const int mappedStep = (byteDegree * maximumScaleStep + 7) / 15;
    const int octave = mappedStep / count;
    const int index = mappedStep % count;
    return 4800.0 + scale.tonicCents + double(octave) * scale.periodCents +
           scale.degreesCents[static_cast<std::size_t>(index)];
}

const char* formulaScaleName(FormulaScalePreset preset) noexcept {
    switch (preset) {
        case FormulaScalePreset::naturalMinor: return "menor natural";
        case FormulaScalePreset::dorian: return "dórico";
        case FormulaScalePreset::pentatonic: return "pentatônica";
        case FormulaScalePreset::harmonicMinor: return "menor harmônica";
        case FormulaScalePreset::melodicMinor: return "menor melódica";
        case FormulaScalePreset::phrygian: return "frígio";
        case FormulaScalePreset::mixolydian: return "mixolídio";
        case FormulaScalePreset::lydian: return "lídio";
        case FormulaScalePreset::bluesMinor: return "blues menor";
        case FormulaScalePreset::wholeTone: return "tons inteiros";
        case FormulaScalePreset::chromatic: return "cromática";
        case FormulaScalePreset::bebopDominant: return "bebop dominante";
        case FormulaScalePreset::bebopMajor: return "bebop maior";
        case FormulaScalePreset::alteredDominant: return "dominante alterada";
        case FormulaScalePreset::diminishedHalfWhole: return "diminuta semitom-tom";
        case FormulaScalePreset::diminishedWholeHalf: return "diminuta tom-semitom";
        case FormulaScalePreset::lydianDominant: return "lídio dominante";
        case FormulaScalePreset::freygish: return "Freygish / Ahava Rabbah";
        case FormulaScalePreset::misheberakh: return "Misheberakh";
        case FormulaScalePreset::harmonicMajor: return "maior harmônica";
        case FormulaScalePreset::neapolitanMinor: return "menor napolitana";
        case FormulaScalePreset::hungarianMinor: return "menor húngara";
        case FormulaScalePreset::doubleHarmonic: return "dupla harmônica";
        case FormulaScalePreset::augmented: return "aumentada";
        case FormulaScalePreset::persian: return "persa";
        case FormulaScalePreset::prometheus: return "Prometheus";
        case FormulaScalePreset::major:
        default: return "maior";
    }
}

const char* formulaScaleMotionName(FormulaScaleMotion motion) noexcept {
    switch (motion) {
        case FormulaScaleMotion::connect: return "encadear";
        case FormulaScaleMotion::cadence: return "cadenciar";
        case FormulaScaleMotion::accompany: return "acompanhar";
        case FormulaScaleMotion::chordGuide: return "guiar acorde";
        case FormulaScaleMotion::counter: return "contrapor";
        case FormulaScaleMotion::transfigure: return "transfigurar";
        case FormulaScaleMotion::follow:
        default: return "seguir";
    }
}

}
