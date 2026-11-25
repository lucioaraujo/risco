// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Lúcio de Araújo
#include "core/Composition.hpp"
#include "core/Sound.hpp"
#include "core/SongIO.hpp"
#include "core/SongPlayback.hpp"
#include "core/SongRender.hpp"
#include "core/TpdfDither.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

namespace {
void write16(std::ofstream& out, std::uint16_t value) {
    out.put(char(value & 0xffu));
    out.put(char((value >> 8) & 0xffu));
}
void write32(std::ofstream& out, std::uint32_t value) {
    write16(out, std::uint16_t(value & 0xffffu));
    write16(out, std::uint16_t(value >> 16));
}
}

int main(int argc, char** argv) {
    const auto renderMode = argc > 1 ? std::string(argv[1]) : std::string{};
    const int selectedStem = renderMode == "stem1" ? 0 : renderMode == "stem2" ? 1 :
                             renderMode == "stem3" ? 2 : -1;
    const bool stereo = renderMode == "stereo" || selectedStem >= 0;
    const bool pcm24 = stereo && argc == 5 && std::string(argv[4]) == "pcm24";
    const bool preMaster = stereo && argc == 5 && std::string(argv[4]) == "premaster";
    const bool float32 = stereo && argc == 5 && (std::string(argv[4]) == "float32" || preMaster);
    if ((argc != 3 && argc != 4 && !pcm24 && !float32) ||
        (renderMode != "sine" && renderMode != "function" &&
         renderMode != "chip" && renderMode != "direct" && !stereo) ||
        (stereo && argc != 4 && !pcm24 && !float32)) {
        std::cerr << "usage: risco_offline_render sine|function|chip output.wav [song.risco]\n"
                     "       risco_offline_render direct output.wav [expression]\n"
                     "       risco_offline_render stereo|stem1|stem2|stem3 output.wav song.risco [pcm24|float32|premaster]\n";
        return 2;
    }
    risco::BytebeatProgram directProgram;
    if (renderMode == "direct" && argc == 4) {
        std::string error;
        if (!risco::compileBytebeat(argv[3], directProgram, error)) {
            std::cerr << "invalid formula: " << error << '\n';
            return 2;
        }
    }
    risco::Inscription inscription;
    inscription.count = 3;
    inscription.ascii = "A.";
    inscription.strokes[0].points[0] = {0.2f, 0.9f};
    inscription.strokes[0].points[1] = {0.5f, 0.1f};
    inscription.strokes[0].count = 2;
    inscription.strokes[1].points[0] = {0.5f, 0.1f};
    inscription.strokes[1].points[1] = {0.8f, 0.9f};
    inscription.strokes[1].count = 2;
    inscription.strokes[2].points[0] = {0.32f, 0.55f};
    inscription.strokes[2].points[1] = {0.68f, 0.55f};
    inscription.strokes[2].count = 2;
    const auto sequence = risco::compose(inscription);
    risco::SongPlayback songPlayback;
    auto song = std::make_unique<risco::Song>();
    if (argc >= 4 && renderMode != "direct") {
        std::ifstream project(argv[3]);
        if (!project || !risco::loadSong(project, *song)) {
            std::cerr << "cannot read Risco song\n";
            return 1;
        }
        songPlayback = risco::prepareSongPlayback(*song);
        if (songPlayback.count == 0) {
            std::cerr << "song grid is empty\n";
            return 1;
        }
    }
    constexpr std::uint32_t rate = 48000;
    const double end = renderMode == "direct" ? 8.0 :
                       argc >= 4 ? songPlayback.end : sequence.end;
    if (!risco::SongRender::validDuration(end)) {
        std::cerr << "render accepts grids up to 10 minutes\n";
        return 1;
    }
    const auto samples = std::uint32_t((end + (renderMode == "direct" ? 0.0 : stereo ? risco::SongRender::tailSecondsFor(*song) : 0.1)) * rate);
    const bool historicalSong = !stereo && argc == 4 &&
                                risco::isHistoricalAyTimbre(songPlayback.timbreMode);
    risco::SongRender renderer(*song, songPlayback, selectedStem, preMaster || historicalSong);
    const std::uint32_t latency = stereo ? renderer.latencySamples() : 0;
    const std::uint32_t channels = stereo ? 2 : 1;
    const std::uint32_t bytesPerSample = float32 ? 4 : pcm24 ? 3 : 2;
    const std::filesystem::path target(argv[2]);
    const auto parent = target.has_parent_path() ? target.parent_path() : std::filesystem::current_path();
    std::error_code fileError;
    const auto disk = std::filesystem::space(parent, fileError);
    if (fileError || disk.available < std::uint64_t(samples) * channels * bytesPerSample + 5000001024ULL) {
        std::cerr << "insufficient free space: preserve 5 GB beyond the WAV size\n"; return 1;
    }
    struct TemporaryOutput {
        std::filesystem::path file;
        ~TemporaryOutput() { std::error_code error; std::filesystem::remove(file,error); }
    } temporary{target.string() + ".partial-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())};
    std::ofstream out(temporary.file, std::ios::binary);
    if (!out) { std::cerr << "cannot open output\n"; return 1; }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(120);
    risco::LoudnessMeter writtenLoudness;
    writtenLoudness.prepare(rate);
    risco::TpdfDither leftDither(pcm24 ? 24 : 16, 0x52495343u);
    risco::TpdfDither rightDither(pcm24 ? 24 : 16, 0x4f415544u);
    out.write("RIFF", 4); write32(out, 36u + (float32 ? 12u : 0u) + samples * bytesPerSample * channels);
    out.write("WAVEfmt ", 8); write32(out, 16);
    write16(out, float32 ? 3 : 1); write16(out, std::uint16_t(channels)); write32(out, rate);
    write32(out, rate * bytesPerSample * channels);
    write16(out, std::uint16_t(bytesPerSample * channels)); write16(out, float32 ? 32 : pcm24 ? 24 : 16);
    if (float32) { out.write("fact", 4); write32(out, 4); write32(out, samples); }
    out.write("data", 4); write32(out, samples * bytesPerSample * channels);
    const auto mode = renderMode == "function" ? risco::WaveMode::phaseFunction :
                      argc == 4 && songPlayback.timbreMode == 2 ? risco::WaveMode::ayToneStudy :
                      renderMode == "chip" || (argc == 4 && (renderMode == "sine" || stereo) &&
                                               songPlayback.timbreMode == 1)
                          ? risco::WaveMode::chiptuneTrio : risco::WaveMode::quantizedSine;
    for (std::uint32_t i = 0; i < samples + latency; ++i) {
        if (i % 4096 == 0 && std::chrono::steady_clock::now() > deadline) { std::cerr << "render processing deadline exceeded\n"; return 1; }
        if (stereo) {
            const auto frame = renderer.frame(i);
            if (i >= latency) {
                writtenLoudness.push(frame.left, frame.right);
                const float channelSamples[] = {frame.left, frame.right};
                risco::TpdfDither* channelDithers[] = {&leftDither, &rightDither};
                for (int channel = 0; channel < 2; ++channel) {
                    const float sample = channelSamples[channel];
                    if (float32) {
                        const float value = std::isfinite(sample) ? sample : 0.0f;
                        std::uint32_t bits = 0;
                        static_assert(sizeof(value) == sizeof(bits));
                        std::memcpy(&bits, &value, sizeof(bits));
                        write32(out, bits);
                    } else if (pcm24) {
                        const auto value = static_cast<std::uint32_t>(channelDithers[channel]->quantize(sample));
                        out.put(static_cast<char>(value & 255u));
                        out.put(static_cast<char>((value >> 8) & 255u));
                        out.put(static_cast<char>((value >> 16) & 255u));
                    } else write16(out, std::uint16_t(channelDithers[channel]->quantize(sample)));
                }
            }
            continue;
        }
        const float sample = historicalSong
            ? [&] { const auto frame = renderer.frame(i); return (frame.left + frame.right) * 0.5f; }()
            : renderMode == "direct"
            ? risco::directBytebeatSample(argc == 4 ? &directProgram : nullptr,
                                         risco::directBytebeatTime(i, rate)) * 0.20f
            : argc == 4 ? risco::songSampleAt(songPlayback, double(i) / rate, mode)
                        : risco::sampleAt(sequence, double(i) / rate, mode);
        write16(out, std::uint16_t(leftDither.quantize(sample)));
    }
    if (!out) { std::cerr << "write failed\n"; return 1; }
    out.close();
    if (!out) { std::cerr << "flush failed\n"; return 1; }
    std::filesystem::rename(temporary.file,target,fileError);
    if (fileError) { std::cerr << "cannot publish rendered WAV: " << fileError.message() << '\n'; return 1; }
    if (stereo) {
        const auto readings = writtenLoudness.readings();
        if (readings.integratedBlocks) std::cout << "integrated " << readings.integratedLufs << " LUFS (rendered frames, before PCM quantization)\n";
        else std::cout << "integrated unavailable: less than 400 ms of rendered audio\n";
    }
    std::cout << "wrote " << samples << " frames, " << channels << " channel(s)\n";
    if (selectedStem >= 0)
        std::cout << "isolated stem " << selectedStem + 1
                  << "; master processing applied independently; aligned to full song\n";
}
