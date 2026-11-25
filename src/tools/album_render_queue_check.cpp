// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Lúcio de Araújo
#include "AlbumRenderQueue.hpp"
#include "AlbumWaveFile.hpp"
#include "core/AlbumManifest.hpp"
#include "core/AlbumPlan.hpp"
#include "core/SongIO.hpp"

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <system_error>

namespace {
namespace fs = std::filesystem;

struct RemoveDirectoryOnExit {
    fs::path path;
    ~RemoveDirectoryOnExit() {
        std::error_code error;
        fs::remove_all(path, error);
    }
};

bool saveManifest(const fs::path& path, const risco::AlbumManifest& manifest) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    return output && risco::saveAlbumManifest(output, manifest) && bool(output);
}

bool readManifest(const fs::path& path, risco::AlbumManifest& manifest) {
    std::ifstream input(path, std::ios::binary);
    return input && risco::loadAlbumManifest(input, manifest);
}

bool makeFixture(const fs::path& directory, risco::AlbumManifest& manifest) {
    risco::AlbumPlanRequest request;
    request.seed = 0x4a1b20261124ULL;
    request.trackCount = 2;
    request.centerBpm = 112.0;
    request.tempoSpread = 8.0;
    request.contour = risco::AlbumTempoContour::arc;
    request.profile = risco::AlbumMusicProfile::contrasting;
    risco::AlbumPlan plan;
    if (!risco::makeAlbumPlan(request, plan) ||
        !risco::createAlbumManifest(plan, "Album render queue integration", manifest))
        return false;

    for (int index = 0; index < plan.count; ++index) {
        risco::Song song;
        if (!risco::generateAlbumTrack(plan, index, song)) return false;
        const auto name = "track-" + std::string(index + 1 < 10 ? "0" : "") +
                          std::to_string(index + 1) + ".risco";
        std::ofstream output(directory / name, std::ios::binary | std::ios::trunc);
        if (!output || !risco::saveSong(output, song)) return false;
        output.close();
        if (!output || !risco::recordAlbumProject(manifest, index, name)) return false;
    }
    return saveManifest(directory / "album.risco-manifest", manifest);
}

bool validateCompletedManifest(const fs::path& directory,
                               const risco::AlbumManifest& manifest) {
    if (manifest.count != 2) return false;
    for (int index = 0; index < manifest.count; ++index) {
        const auto& track = manifest.tracks[static_cast<size_t>(index)];
        const auto wav = directory / ("track-" + std::string(index + 1 < 10 ? "0" : "") +
                                      std::to_string(index + 1) + ".wav");
        if (track.state != risco::AlbumTrackState::rendered || !track.hasMeasurements ||
            track.integratedLufs < -70.0 || track.integratedLufs > 0.0 ||
            track.truePeakDbtp > 0.0 ||
            !risco::app::validAlbumWav24(juce::File(wav.string()), track.renderedSeconds))
            return false;
    }
    return true;
}

bool queueCancelRejectResumeAndRepair() {
    const auto fail = [](const char* reason) {
        std::cerr << "album render queue check: " << reason << '\n';
        return false;
    };
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto directory = fs::temp_directory_path() /
        ("risco-album-render-queue-" + std::to_string(stamp));
    std::error_code error;
    if (!fs::create_directories(directory, error) || error) return false;
    const RemoveDirectoryOnExit cleanup{directory};
    risco::AlbumManifest initial;
    if (!makeFixture(directory, initial)) return fail("fixture generation");

    const auto manifestPath = directory / "album.risco-manifest";
    auto cancelAfterFirstCheckpoint = [&] {
        risco::AlbumManifest current;
        return readManifest(manifestPath, current) && current.count == 2 &&
               current.tracks[0].state == risco::AlbumTrackState::rendered &&
               current.tracks[1].state == risco::AlbumTrackState::projectReady;
    };
    auto result = risco::app::runAlbumRenderQueue(initial, juce::File(directory.string()),
                                                  cancelAfterFirstCheckpoint);
    risco::AlbumManifest partial;
    if (!result.cancelled || result.message.rfind("Render de álbum interrompido", 0) != 0 ||
        !readManifest(manifestPath, partial) || partial.count != 2 ||
        partial.tracks[0].state != risco::AlbumTrackState::rendered ||
        partial.tracks[1].state != risco::AlbumTrackState::projectReady ||
        partial.tracks[1].hasMeasurements || !partial.tracks[1].renderFile.empty() ||
        !risco::app::validAlbumWav24(juce::File((directory / "track-01.wav").string()),
                                     partial.tracks[0].renderedSeconds) ||
        fs::exists(directory / "track-02.wav")) {
        std::cerr << "cancelled=" << result.cancelled << " message=" << result.message
                  << " manifest=" << fs::exists(manifestPath);
        if (readManifest(manifestPath, partial))
            std::cerr << " count=" << partial.count
                      << " first_state=" << static_cast<int>(partial.tracks[0].state)
                      << " second_state=" << static_cast<int>(partial.tracks[1].state)
                      << " first_wav=" << risco::app::validAlbumWav24(
                             juce::File((directory / "track-01.wav").string()),
                             partial.tracks[0].renderedSeconds)
                      << " second_wav=" << fs::exists(directory / "track-02.wav");
        std::cerr << '\n';
        return fail("cancel and first checkpoint");
    }

    const auto secondProject = directory / "track-02.risco";
    std::ifstream projectInput(secondProject, std::ios::binary);
    const std::string savedProject((std::istreambuf_iterator<char>(projectInput)), {});
    if (savedProject.empty()) return fail("read saved project before rejection test");
    {
        std::ofstream invalid(secondProject, std::ios::binary | std::ios::trunc);
        invalid << "invalid project";
    }
    result = risco::app::runAlbumRenderQueue(partial, juce::File(directory.string()));
    if (result.cancelled || result.message.find("Falha no render da faixa 2:") == std::string::npos ||
        fs::exists(directory / "track-02.wav")) {
        std::cerr << "invalid-project result=" << result.message
                  << " cancelled=" << result.cancelled
                  << " second_wav=" << fs::exists(directory / "track-02.wav") << '\n';
        return fail("reject invalid project");
    }
    {
        std::ofstream restored(secondProject, std::ios::binary | std::ios::trunc);
        restored.write(savedProject.data(), static_cast<std::streamsize>(savedProject.size()));
        if (!restored) return fail("restore valid project fixture");
    }

    risco::AlbumManifest resume;
    if (!readManifest(manifestPath, resume)) return fail("read resume manifest");
    result = risco::app::runAlbumRenderQueue(resume, juce::File(directory.string()));
    risco::AlbumManifest complete;
    if (result.cancelled || result.message.find("WAVs de álbum prontos: 2/2") == std::string::npos ||
        !readManifest(manifestPath, complete) || !validateCompletedManifest(directory, complete))
        return fail("resume and complete album");

    {
        std::fstream corrupt(directory / "track-01.wav", std::ios::binary | std::ios::in | std::ios::out);
        if (!corrupt) return fail("open WAV for corruption test");
        const char invalid = 'X';
        corrupt.write(&invalid, 1);
        if (!corrupt) return fail("corrupt WAV fixture");
    }
    result = risco::app::runAlbumRenderQueue(complete, juce::File(directory.string()));
    risco::AlbumManifest repaired;
    if (result.cancelled || result.message.find("WAVs de álbum prontos: 2/2") == std::string::npos ||
        !readManifest(manifestPath, repaired) || !validateCompletedManifest(directory, repaired))
        return fail("repair invalid WAV checkpoint");
    return true;
}
}

int main() {
    if (!queueCancelRejectResumeAndRepair()) {
        std::cerr << "album render queue cancel/reject/resume/repair check failed\n";
        return 1;
    }
    std::cout << "album render queue cancel/reject/resume/repair check passed\n";
    return 0;
}
