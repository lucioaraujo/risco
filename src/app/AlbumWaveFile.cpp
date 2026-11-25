// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Lúcio de Araújo
#include "AlbumWaveFile.hpp"

#include "core/LoudnessMeter.hpp"
#include "core/TpdfDither.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>

namespace risco::app {
namespace {
juce::int64 expectedBytes(double seconds) {
    if (!std::isfinite(seconds) || seconds <= 0.0 ||
        seconds > SongRender::maximumSongSeconds + 30.0) return -1;
    return static_cast<juce::int64>(std::llround(seconds * SongRender::rate)) * 6 + 44;
}
}

bool validAlbumWav24(const juce::File& file, double durationSeconds) {
    if (!file.existsAsFile() || file.getSize() != expectedBytes(durationSeconds)) return false;
    auto input = file.createInputStream();
    if (!input || input->getTotalLength() < 44) return false;
    std::array<unsigned char, 44> header{};
    if (input->read(header.data(), static_cast<int>(header.size())) != static_cast<int>(header.size())) return false;
    const auto u32 = [&header](int offset) -> juce::int64 {
        return juce::int64(header[static_cast<size_t>(offset)]) |
               (juce::int64(header[static_cast<size_t>(offset + 1)]) << 8) |
               (juce::int64(header[static_cast<size_t>(offset + 2)]) << 16) |
               (juce::int64(header[static_cast<size_t>(offset + 3)]) << 24);
    };
    return std::string(reinterpret_cast<const char*>(header.data()), 4) == "RIFF" &&
           u32(4) == file.getSize() - 8 &&
           std::string(reinterpret_cast<const char*>(header.data() + 8), 4) == "WAVE" &&
           std::string(reinterpret_cast<const char*>(header.data() + 12), 4) == "fmt " &&
           u32(16) == 16 && header[20] == 1 && header[21] == 0 &&
           header[22] == 2 && header[23] == 0 && u32(24) == SongRender::rate &&
           u32(28) == SongRender::rate * 6 && header[32] == 6 && header[33] == 0 &&
           header[34] == 24 && header[35] == 0 &&
           std::string(reinterpret_cast<const char*>(header.data() + 36), 4) == "data" &&
           u32(40) == file.getSize() - 44;
}

bool renderAlbumWav24(const Song& song, const SongPlayback& playback,
                      const juce::File& target, int trackIndex,
                      const std::function<bool()>& shouldCancel,
                      const std::function<void(double)>& progress,
                      AlbumWaveMetrics& metrics, std::string& error) {
    if (trackIndex < 0 || playback.count == 0 || !SongRender::validDuration(playback.end)) {
        error = "empty playback or invalid track index/duration";
        return false;
    }
    const auto seconds = playback.end + SongRender::tailSecondsFor(song);
    const auto frames = static_cast<std::uint32_t>(std::ceil(seconds * SongRender::rate));
    if (frames == 0 || juce::int64(frames) * 6 + 44 > std::numeric_limits<int>::max()) {
        error = "WAV duration exceeds the RIFF size limit";
        return false;
    }

    juce::TemporaryFile temporary(target);
    auto stream = temporary.getFile().createOutputStream();
    if (!stream || !stream->openedOk()) { error = "cannot create temporary WAV"; return false; }
    stream->write("RIFF", 4); stream->writeInt(static_cast<int>(36 + frames * 6));
    stream->write("WAVEfmt ", 8); stream->writeInt(16);
    stream->writeShort(1); stream->writeShort(2);
    stream->writeInt(static_cast<int>(SongRender::rate));
    stream->writeInt(static_cast<int>(SongRender::rate * 6));
    stream->writeShort(6); stream->writeShort(24);
    stream->write("data", 4); stream->writeInt(static_cast<int>(frames * 6));

    SongRender renderer(song, playback);
    TpdfDither leftDither(24, 0x52495343u + static_cast<std::uint32_t>(trackIndex * 2));
    TpdfDither rightDither(24, 0x4f415544u + static_cast<std::uint32_t>(trackIndex * 2));
    LoudnessMeter loudness;
    loudness.prepare(SongRender::rate);
    const auto latency = renderer.latencySamples();
    const auto totalFrames = frames + latency;
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + 120000.0;
    for (std::uint32_t frame = 0; frame < totalFrames; ++frame) {
        if (frame % 256 == 0) {
            if (shouldCancel && shouldCancel()) { error = "cancelled"; return false; }
            if (juce::Time::getMillisecondCounterHiRes() > deadline) {
                error = "per-track processing deadline exceeded";
                return false;
            }
            if (progress) progress(double(frame) / totalFrames);
        }
        const auto audio = renderer.frame(frame);
        if (frame >= latency) {
            loudness.push(audio.left, audio.right);
            const float samples[] = {audio.left, audio.right};
            TpdfDither* dithers[] = {&leftDither, &rightDither};
            for (int channel = 0; channel < 2; ++channel) {
                const auto packed = static_cast<std::uint32_t>(dithers[channel]->quantize(samples[channel]));
                const unsigned char encoded[] = {static_cast<unsigned char>(packed),
                    static_cast<unsigned char>(packed >> 8), static_cast<unsigned char>(packed >> 16)};
                stream->write(encoded, 3);
            }
        }
        if (stream->getStatus().failed()) { error = "failed to write temporary WAV"; return false; }
    }
    stream->flush();
    if (stream->getStatus().failed()) { error = "failed to finish temporary WAV"; return false; }
    stream.reset();
    if (shouldCancel && shouldCancel()) { error = "cancelled"; return false; }
    if (!temporary.overwriteTargetFileWithTemporary()) { error = "failed to publish WAV atomically"; return false; }

    const auto readings = loudness.readings();
    const auto meter = renderer.meter();
    metrics.durationSeconds = double(frames) / SongRender::rate;
    metrics.integratedLufs = readings.integratedBlocks == 0 || !std::isfinite(readings.integratedLufs)
        ? -70.0 : std::clamp(double(readings.integratedLufs), -70.0, 0.0);
    metrics.truePeakDbtp = std::isfinite(meter.truePeak) && meter.truePeak > 1.0e-6f
        ? 20.0 * std::log10(double(meter.truePeak)) : -120.0;
    error.clear();
    return true;
}

} // namespace risco::app
