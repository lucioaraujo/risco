// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Lúcio de Araújo
#include "AlbumWaveFile.hpp"
#include "core/AlbumManifest.hpp"
#include "core/AlbumPlan.hpp"

#include <cmath>
#include <iostream>
#include <sstream>

namespace {
bool renderAlbumWavesAndCheckTransactions(const juce::File& directory) {
    risco::AlbumPlanRequest request;
    request.seed = 0x4a1b20261123ULL;
    request.trackCount = 2;
    request.centerBpm = 112.0;
    request.tempoSpread = 8.0;
    request.contour = risco::AlbumTempoContour::arc;
    risco::AlbumPlan plan;
    risco::AlbumManifest manifest;
    if (!risco::makeAlbumPlan(request, plan) ||
        !risco::createAlbumManifest(plan, "Wave file check", manifest)) return false;

    risco::Song first;
    if (!risco::generateAlbumTrack(plan, 0, first)) return false;
    const auto firstPlayback = risco::prepareSongPlayback(first);
    const auto protectedTarget = directory.getChildFile("protected.wav");
    if (!protectedTarget.replaceWithText("keep")) return false;
    int cancelChecks = 0;
    risco::app::AlbumWaveMetrics unused;
    std::string error;
    const bool cancelledWrite = risco::app::renderAlbumWav24(first, firstPlayback,
        protectedTarget, 0, [&cancelChecks] { return ++cancelChecks > 1; }, {}, unused, error);
    if (cancelledWrite || protectedTarget.getSize() != 4 ||
        protectedTarget.loadFileAsString() != juce::String("keep"))
        return false;
    protectedTarget.deleteFile();

    for (int index = 0; index < plan.count; ++index) {
        risco::Song song;
        if (!risco::generateAlbumTrack(plan, index, song)) return false;
        const auto playback = risco::prepareSongPlayback(song);
        const auto target = directory.getChildFile("track-" + juce::String(index + 1).paddedLeft('0', 2) + ".wav");
        risco::app::AlbumWaveMetrics metrics;
        double lastProgress = 0.0;
        bool progressMonotonic = true;
        if (!risco::app::renderAlbumWav24(song, playback, target, index, {},
                [&lastProgress, &progressMonotonic](double progress) {
                    if (progress < lastProgress) progressMonotonic = false;
                    lastProgress = progress;
                }, metrics, error) ||
            !risco::app::validAlbumWav24(target, metrics.durationSeconds) ||
            target.getSize() != static_cast<juce::int64>(std::llround(
                metrics.durationSeconds * risco::SongRender::rate)) * 6 + 44 ||
            !std::isfinite(metrics.integratedLufs) || metrics.integratedLufs > 0.0 ||
            !std::isfinite(metrics.truePeakDbtp) || metrics.truePeakDbtp > 0.1 ||
            !progressMonotonic || lastProgress < 0.99) return false;
        const auto leaf = "track-" + std::string(index + 1 < 10 ? "0" : "") +
                          std::to_string(index + 1) + ".risco";
        if (!risco::recordAlbumProject(manifest, index, leaf) ||
            !risco::recordAlbumRender(manifest, index,
                "track-" + std::string(index + 1 < 10 ? "0" : "") +
                    std::to_string(index + 1) + ".wav",
                metrics.durationSeconds, metrics.integratedLufs, metrics.truePeakDbtp)) return false;
    }
    std::ostringstream serialized;
    if (!risco::saveAlbumManifest(serialized, manifest)) return false;
    risco::AlbumManifest restored;
    std::istringstream input(serialized.str());
    if (!risco::loadAlbumManifest(input, restored) || restored.count != plan.count) return false;
    for (int index = 0; index < restored.count; ++index)
        if (restored.tracks[static_cast<size_t>(index)].state != risco::AlbumTrackState::rendered ||
            !risco::app::validAlbumWav24(directory.getChildFile(
                "track-" + juce::String(index + 1).paddedLeft('0', 2) + ".wav"),
                restored.tracks[static_cast<size_t>(index)].renderedSeconds)) return false;
    const auto firstWave = directory.getChildFile("track-01.wav");
    juce::MemoryBlock damagedWave;
    if (!firstWave.loadFileAsData(damagedWave) || damagedWave.getSize() < 44) return false;
    static_cast<unsigned char*>(damagedWave.getData())[0] = 'X';
    if (!firstWave.replaceWithData(damagedWave.getData(), damagedWave.getSize()) ||
        risco::app::validAlbumWav24(firstWave, restored.tracks[0].renderedSeconds)) return false;
    return true;
}
}

int main() {
    const auto suffix = juce::String(juce::Time::getHighResolutionTicks());
    const auto directory = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("risco-album-wave-check-" + suffix);
    if (!directory.createDirectory().wasOk()) return 2;
    const bool passed = renderAlbumWavesAndCheckTransactions(directory);
    directory.deleteRecursively();
    if (!passed) {
        std::cerr << "album WAV writer/checkpoint/cancellation check failed\n";
        return 1;
    }
    std::cout << "album WAV writer/checkpoint/cancellation check passed\n";
    return 0;
}
