// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Lúcio de Araújo

// Local adaptation from Aquorbium commit 1064d55de38093595a35520fce0b0b59eb769463.
// Original author/licence retained; source hashes and adaptations in TAREFAS.md.

#include "core/LoudnessMeter.hpp"
#include "core/OutputStage.hpp"
#include "core/SongRender.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>

namespace
{
constexpr double pi = 3.14159265358979323846;

float measureSine(double sampleRate, double rmsDb, bool stereo,
                  double seconds = 5.0)
{
    risco::LoudnessMeter meter;
    meter.prepare(sampleRate);
    const auto amplitude = std::pow(10.0, rmsDb / 20.0) * std::sqrt(2.0);
    const auto increment = 2.0 * pi * 1000.0 / sampleRate;
    double phase = 0.0;
    const auto samples = static_cast<std::size_t>(sampleRate * seconds);
    for (std::size_t index = 0; index < samples; ++index)
    {
        const auto value = static_cast<float>(amplitude * std::sin(phase));
        meter.push(value, stereo ? value : 0.0f);
        phase += increment;
    }
    return meter.readings().integratedLufs;
}

bool near(float value, float expected, float tolerance)
{
    return std::abs(value - expected) <= tolerance;
}

bool ebuReferenceLevel()
{
    // EBU Tech 3341 reference: identical stereo channels at -26 dBFS RMS
    // produce -23 LUFS after K-weighting and channel summation.
    return near(measureSine(48000.0, -26.0, true), -23.0f, 0.12f)
        && near(measureSine(48000.0, -23.0, false), -23.0f, 0.12f);
}

bool rateAndChannelRelations()
{
    for (const auto rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
        if (!near(measureSine(rate, -26.0, true), -23.0f, 0.16f))
            return false;
    const auto mono = measureSine(48000.0, -23.0, false);
    const auto stereo = measureSine(48000.0, -23.0, true);
    const auto louder = measureSine(48000.0, -17.0, false);
    return near(stereo - mono, 3.0103f, 0.10f)
        && near(louder - mono, 6.0f, 0.10f);
}

bool windowsAndGating()
{
    risco::LoudnessMeter meter;
    meter.prepare(48000.0);
    if (meter.readings().momentaryReady) return false;
    const auto amplitude = std::pow(10.0, -26.0 / 20.0) * std::sqrt(2.0);
    const auto increment = 2.0 * pi * 1000.0 / 48000.0;
    double phase = 0.0;
    for (std::size_t index = 0; index < 48000u * 5u; ++index)
    {
        const auto value = static_cast<float>(amplitude * std::sin(phase));
        meter.push(value, value);
        phase += increment;
    }
    const auto active = meter.readings();
    if (!active.momentaryReady || !active.shortTermReady
        || !near(active.integratedLufs, -23.0f, 0.15f))
        return false;
    for (std::size_t index = 0; index < 48000u * 10u; ++index)
        meter.push(0.0f, 0.0f);
    const auto silentTail = meter.readings();
    return silentTail.momentaryLufs <= risco::LoudnessMeter::silenceLufs
        && near(silentTail.integratedLufs, -23.0f, 0.35f)
        && silentTail.integratedBlocks > active.integratedBlocks;
}

bool resetAndNonFiniteSafety()
{
    risco::LoudnessMeter meter;
    meter.prepare(48000.0);
    for (std::size_t index = 0; index < 48000u; ++index)
        meter.push(index == 0 ? NAN : 0.0f,
                   index == 1 ? INFINITY : 0.0f);
    auto readings = meter.readings();
    if (!std::isfinite(readings.integratedLufs)
        || readings.integratedLufs != risco::LoudnessMeter::silenceLufs)
        return false;
    meter.reset();
    readings = meter.readings();
    return readings.integratedBlocks == 0u && !readings.momentaryReady
        && readings.integratedLufs == risco::LoudnessMeter::silenceLufs;
}

bool stereoOutputDiagnostics()
{
    risco::OutputStage inPhase, antiPhase;
    inPhase.prepare(48000.0);
    antiPhase.prepare(48000.0);
    constexpr int frames = 48000 * 2;
    for (int i = 0; i < frames; ++i) {
        const float value = float(0.12 * std::sin(2.0 * pi * 997.0 * i / 48000.0));
        inPhase.process(value, value, 0.25f);
        antiPhase.process(value, -value, 0.25f);
    }
    const auto same = inPhase.meter();
    const auto opposite = antiPhase.meter();
    if (same.correlation < 0.99f || opposite.correlation > -0.99f ||
        same.truePeakHold < same.truePeak || same.monoRms < 0.001f ||
        opposite.monoRms > 0.0001f || same.clipLatched || opposite.clipLatched)
        return false;
    inPhase.resetReadouts();
    const auto reset = inPhase.meter();
    return reset.truePeakHold == 0.0f && reset.correlation == 0.0f &&
           reset.monoRms == 0.0f && !reset.clipLatched && reset.loudness.integratedBlocks == 0;
}

bool limiterTruePeakCeiling()
{
    for (const double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        for (const double phase : { 0.0, pi * 0.37 })
        {
            risco::OutputStage stage;
            stage.prepare(rate);
            risco::TruePeakDetector externalLeft, externalRight;
            externalLeft.prepare();
            externalRight.prepare();
            const double frequency = rate * 0.43;
            double measured = 0.0;
            const auto frames = static_cast<std::size_t>(rate * 0.08);
            for (std::size_t i = 0; i < frames; ++i)
            {
                const auto input = float(0.95 * std::sin(2.0 * pi * frequency * i / rate + phase));
                const auto output = stage.process(input, -input, 1.0f);
                measured = std::max(measured, double(externalLeft.process(output.left)));
                measured = std::max(measured, double(externalRight.process(output.right)));
            }
            if (!std::isfinite(measured) || measured > risco::OutputStage::ceiling + 0.015 ||
                stage.meter().clipLatched || stage.meter().reductionDb < 0.1f) {
                std::cerr << "limiter fixture rate=" << rate << " phase=" << phase
                          << " measured=" << measured << " reduction=" << stage.meter().reductionDb
                          << " clip=" << stage.meter().clipLatched << '\n';
                return false;
            }
        }
    }
    return true;
}

bool outputRoutingAndRenderPolicy()
{
    risco::OutputStage a, b;
    a.prepare(48000); b.prepare(48000);
    risco::LoudnessMeter reference;
    reference.prepare(48000);
    for (int i = 0; i < 192000; ++i) {
        const float input = float(0.02 * std::sin(2*pi*1000*i/48000));
        if (i == 96000) b.resetReadouts();
        const auto left = a.process(input,input,0.25f), right = b.process(input,input,0.25f);
        if (left.left != right.left || left.right != right.right) return false;
        reference.push(left.left,left.right);
    }
    const auto actual = a.meter().loudness, expected = reference.readings();
    return near(actual.integratedLufs,expected.integratedLufs,0.001f)
        && near(actual.momentaryLufs,expected.momentaryLufs,0.001f)
        && risco::SongRender::validDuration(600)
        && risco::SongRender::validDuration(128)
        && !risco::SongRender::validDuration(600.001)
        && !risco::SongRender::validDuration(NAN)
        && !risco::SongRender::validDuration(0);
}
}

int main()
{
    if (!outputRoutingAndRenderPolicy())
    { std::cerr << "output loudness routing/render duration failed\n"; return EXIT_FAILURE; }
    if (!ebuReferenceLevel())
    { std::cerr << "EBU loudness reference failed\n"; return EXIT_FAILURE; }
    if (!rateAndChannelRelations())
    { std::cerr << "loudness rate/channel relation failed\n"; return EXIT_FAILURE; }
    if (!windowsAndGating())
    { std::cerr << "loudness windows/gating failed\n"; return EXIT_FAILURE; }
    if (!resetAndNonFiniteSafety())
    { std::cerr << "loudness reset/safety failed\n"; return EXIT_FAILURE; }
    if (!stereoOutputDiagnostics())
    { std::cerr << "stereo correlation/mono/peak-hold diagnostics failed\n"; return EXIT_FAILURE; }
    if (!limiterTruePeakCeiling())
    { std::cerr << "true-peak limiter ceiling failed at one or more sample rates/phases\n"; return EXIT_FAILURE; }
    std::cout << "Risco BS.1770 loudness meter passed\n";
    return EXIT_SUCCESS;
}
