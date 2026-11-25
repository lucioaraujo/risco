// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Lúcio de Araújo
#include "core/HistoricalAyEngine.hpp"
#include "core/SongPlayback.hpp"
#include "core/SongRender.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>

namespace {
struct Result { std::uint64_t hash = 14695981039346656037ULL; double peak = 0.0; };

Result render(risco::HistoricalAyModel model, const risco::Sequence& sequence) {
    risco::HistoricalAyEngine engine;
    if (!engine.prepare(model)) return {};
    Result result;
    for (int frame = 0; frame < 48000; ++frame) {
        const auto value = engine.process(sequence, double(frame) / 48000.0);
        for (float sample : {value.left, value.right}) {
            result.peak = std::max(result.peak, std::abs(double(sample)));
            const auto pcm = std::int16_t(std::lround(std::clamp(sample, -1.0f, 1.0f) * 32767.0f));
            const auto word = std::uint16_t(pcm);
            for (int shift : {0, 8}) { result.hash ^= (word >> shift) & 0xffu; result.hash *= 1099511628211ULL; }
        }
    }
    return result;
}

Result render(risco::HistoricalAyModel model, const risco::Sequence& sequence,
              risco::HistoricalAySettings settings) {
    risco::HistoricalAyEngine engine;
    if (!engine.prepare(model)) return {};
    Result result;
    for (int frame = 0; frame < 48000; ++frame) {
        const auto value = engine.process(sequence, double(frame) / 48000.0, settings);
        for (float sample : {value.left, value.right}) {
            result.peak = std::max(result.peak, std::abs(double(sample)));
            const auto word = std::uint16_t(std::int16_t(std::lround(
                std::clamp(sample, -1.0f, 1.0f) * 32767.0f)));
            for (int shift : {0, 8}) { result.hash ^= (word >> shift) & 0xffu; result.hash *= 1099511628211ULL; }
        }
    }
    return result;
}
}

int main() {
    risco::Sequence sequence;
    sequence.count = 4; sequence.end = 1.0;
    sequence.notes[0] = {0.0, 0.80, 220.0, 0.20f, risco::VoiceRole::bass, 0.25f};
    sequence.notes[1] = {0.0, 0.80, 329.63, 0.17f, risco::VoiceRole::harmony, 0.25f};
    sequence.notes[2] = {0.0, 0.80, 440.0, 0.15f, risco::VoiceRole::lead, 0.25f};
    sequence.notes[3] = {0.4, 0.35, 659.25, 0.18f, risco::VoiceRole::lead, 0.25f};
    const auto ay1 = render(risco::HistoricalAyModel::ay38910, sequence);
    const auto ay2 = render(risco::HistoricalAyModel::ay38910, sequence);
    const auto ym = render(risco::HistoricalAyModel::ym2149, sequence);
    if (ay1.hash != ay2.hash || ay1.hash == ym.hash || ay1.peak <= 0.01 || ym.peak <= 0.01 ||
        ay1.peak > 1.0 || ym.peak > 1.0) {
        std::cerr << "historical AY engine check failed\n"; return 1;
    }
    const auto noise = render(risco::HistoricalAyModel::ay38910, sequence, {2, 7, 0, 0.5f});
    const auto mixed = render(risco::HistoricalAyModel::ay38910, sequence, {1, 7, 0, 0.5f});
    const auto envelope = render(risco::HistoricalAyModel::ay38910, sequence, {0, 16, 3, 0.125f});
    const auto envelopeAgain = render(risco::HistoricalAyModel::ay38910, sequence, {0, 16, 3, 0.125f});
    if (noise.peak <= 0.01 || mixed.peak <= 0.01 || envelope.peak <= 0.01 ||
        noise.hash == ay1.hash || mixed.hash == ay1.hash || envelope.hash == ay1.hash ||
        envelope.hash != envelopeAgain.hash) {
        std::cerr << "historical noise/mixer/envelope check failed\n"; return 1;
    }
    risco::HistoricalAyEngine invalid;
    if (invalid.prepare(risco::HistoricalAyModel::ay38910, 0.0, 2000000.0)) {
        std::cerr << "invalid configuration accepted\n"; return 1;
    }
    risco::SongPlayback playback;
    playback.end = 1.0; playback.count = 1; playback.timbreMode = 5;
    playback.placements[0].cell = sequence;
    playback.placements[0].duration = playback.placements[0].playLength = 1.0;
    playback.placements[0].stem = 0;
    risco::HistoricalAyEngine direct, stem;
    direct.prepare(risco::HistoricalAyModel::ay38910);
    stem.prepare(risco::HistoricalAyModel::ay38910);
    for (int frame = 0; frame < 48000; ++frame) {
        const double seconds = double(frame) / 48000.0;
        const auto a = direct.process(sequence, seconds);
        const auto b = stem.processStem(playback, seconds, 0);
        if (a.left != b.left || a.right != b.right) {
            std::cerr << "stem adapter differs from direct sequence\n"; return 1;
        }
    }
    risco::Song song;
    song.timbreMode = 5;
    risco::SongRender renderer(song, playback);
    double renderedPeak = 0.0;
    for (std::uint32_t frame = 0; frame < 48000; ++frame) {
        const auto value = renderer.frame(frame);
        renderedPeak = std::max({renderedPeak, std::abs(double(value.left)), std::abs(double(value.right))});
    }
    if (renderedPeak <= 0.001 || renderedPeak > 1.0 ||
        renderer.latencySamples() != 240 || renderer.meter().truePeak <= 0.001f) {
        std::cerr << "SongRender historical route is silent or unbounded\n"; return 1;
    }
    risco::HistoricalAyEngine looped;
    looped.prepare(risco::HistoricalAyModel::ay38910);
    std::uint64_t loopHashes[2] = {14695981039346656037ULL, 14695981039346656037ULL};
    for (int frame = 0; frame < 96000; ++frame) {
        const auto value = looped.process(sequence, std::fmod(double(frame) / 48000.0, 1.0));
        const auto pcm = std::uint16_t(std::int16_t(std::lround(
            std::clamp((value.left + value.right) * 0.5f, -1.0f, 1.0f) * 32767.0f)));
        auto& hash = loopHashes[frame / 48000];
        for (int shift : {0, 8}) { hash ^= (pcm >> shift) & 0xffu; hash *= 1099511628211ULL; }
    }
    if (loopHashes[0] != loopHashes[1]) {
        std::cerr << "loop reset is not deterministic\n"; return 1;
    }
    std::cout << "Historical AY engine: PASS (three voices, deterministic AY/YM distinction)\n";
}
