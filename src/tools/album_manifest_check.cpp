// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Lúcio de Araújo
#include "core/AlbumManifest.hpp"

#include <iostream>
#include <sstream>
#include <string>

namespace {
bool serialize(const risco::AlbumManifest& manifest, std::string& text) {
    std::ostringstream output;
    if (!risco::saveAlbumManifest(output, manifest)) return false;
    text = output.str();
    return true;
}

bool checkpointsRoundTripAndResume() {
    risco::AlbumPlanRequest request;
    request.seed = 0x0a1b2c3d4e5f6789ULL;
    request.trackCount = 5;
    request.centerBpm = 104.0;
    request.tempoSpread = 18.0;
    request.contour = risco::AlbumTempoContour::rising;
    risco::AlbumPlan plan;
    risco::AlbumManifest manifest;
    if (!risco::makeAlbumPlan(request, plan) ||
        !risco::createAlbumManifest(plan, "Écho d'été", manifest) ||
        manifest.count != 5 || manifest.tracks[0].state != risco::AlbumTrackState::planned ||
        manifest.tracks[0].plannedSeconds <= 0.0) return false;

    const double actualDuration = manifest.tracks[0].plannedSeconds + 1.75;
    if (!risco::recordAlbumProject(manifest, 0, "01-track.risco") ||
        !risco::recordAlbumProject(manifest, 1, "02-track.risco") ||
        !risco::recordAlbumRender(manifest, 0, "01-mix.wav", actualDuration, -20.25, -1.05))
        return false;
    const std::string beforeInvalid = [&] { std::string value; serialize(manifest, value); return value; }();
    if (risco::recordAlbumProject(manifest, 2, "../escape.risco") ||
        risco::recordAlbumProject(manifest, 2, "01-track.risco") ||
        risco::recordAlbumRender(manifest, 2, "03-mix.wav", 100.0, -20.0, 18.0)) return false;
    std::string afterInvalid;
    if (!serialize(manifest, afterInvalid) || beforeInvalid != afterInvalid) return false;

    std::string encoded;
    if (!serialize(manifest, encoded) || encoded.rfind("RISCO_ALBUM_V1\n", 0) != 0) return false;
    risco::AlbumManifest loaded;
    std::istringstream input(encoded);
    if (!risco::loadAlbumManifest(input, loaded) || loaded.title != manifest.title ||
        loaded.count != manifest.count || loaded.plan.request.seed != request.seed ||
        loaded.plan.request.contour != request.contour ||
        loaded.tracks[0].state != risco::AlbumTrackState::rendered ||
        loaded.tracks[0].projectFile != "01-track.risco" ||
        loaded.tracks[0].renderFile != "01-mix.wav" ||
        loaded.tracks[0].renderedSeconds != actualDuration ||
        loaded.tracks[0].integratedLufs != -20.25 ||
        loaded.tracks[0].truePeakDbtp != -1.05 ||
        loaded.tracks[1].state != risco::AlbumTrackState::projectReady ||
        loaded.tracks[2].state != risco::AlbumTrackState::planned) return false;
    std::string reencoded;
    if (!serialize(loaded, reencoded) || encoded != reencoded) return false;

    auto resumed = loaded;
    if (!risco::invalidateAlbumRender(resumed, 0) ||
        resumed.tracks[0].state != risco::AlbumTrackState::projectReady ||
        resumed.tracks[0].projectFile != "01-track.risco" ||
        !resumed.tracks[0].renderFile.empty() || resumed.tracks[0].renderedSeconds != 0.0 ||
        resumed.tracks[0].hasMeasurements || resumed.tracks[0].integratedLufs != 0.0 ||
        resumed.tracks[0].truePeakDbtp != 0.0 ||
        risco::invalidateAlbumRender(resumed, 0)) return false;
    std::string resetEncoded;
    if (!serialize(resumed, resetEncoded)) return false;
    risco::AlbumManifest resetRestored;
    std::istringstream resetInput(resetEncoded);
    if (!risco::loadAlbumManifest(resetInput, resetRestored) ||
        resetRestored.tracks[0].state != risco::AlbumTrackState::projectReady ||
        resetRestored.tracks[0].projectFile != "01-track.risco") return false;

    auto truncated = encoded.substr(0, encoded.rfind("TRACK "));
    const auto original = reencoded;
    std::istringstream broken(truncated);
    if (risco::loadAlbumManifest(broken, loaded) || !serialize(loaded, reencoded) ||
        reencoded != original) return false;
    std::istringstream unsupported("RISCO_ALBUM_V4\n");
    if (risco::loadAlbumManifest(unsupported, loaded)) return false;

    // The WAV writer reports its meter floor for a silent track.
    if (!risco::recordAlbumRender(loaded, 1, "02-mix.wav", loaded.tracks[1].plannedSeconds,
                                -70.0, -120.0) || !serialize(loaded, reencoded)) return false;
    std::istringstream silentTrack(reencoded);
    if (!risco::loadAlbumManifest(silentTrack, resumed) ||
        resumed.tracks[1].state != risco::AlbumTrackState::rendered ||
        resumed.tracks[1].truePeakDbtp != -120.0) return false;
    return true;
}

bool detailedManifestRoundTrip() {
    risco::AlbumPlanRequest request;
    request.seed = 281;
    request.trackCount = 3;
    request.profile = risco::AlbumMusicProfile::contrasting;
    risco::AlbumPlan plan;
    risco::AlbumManifest manifest, loaded;
    std::string encoded, reencoded;
    if (!risco::makeAlbumPlan(request, plan) ||
        !risco::createAlbumManifest(plan, "Cuts", manifest) ||
        !risco::recordAlbumProject(manifest, 0, "track-01.risco") ||
        !serialize(manifest, encoded) || encoded.rfind("RISCO_ALBUM_V2\n", 0) != 0)
        return false;
    std::istringstream input(encoded);
    if (!risco::loadAlbumManifest(input, loaded) ||
        loaded.plan.request.profile != request.profile || loaded.plan.tracks != plan.tracks ||
        loaded.tracks[0].state != risco::AlbumTrackState::projectReady ||
        !serialize(loaded, reencoded) || encoded != reencoded) return false;

    // Changing only a stored palette must not silently change the seeded piece.
    auto tampered = encoded;
    const auto end = tampered.find('\n', tampered.find("TRACK 0 "));
    if (end == std::string::npos || end < 2) return false;
    tampered[end - 1] = tampered[end - 1] == '1' ? '2' : '1';
    std::istringstream altered(tampered);
    return !risco::loadAlbumManifest(altered, loaded) &&
           serialize(loaded, reencoded) && encoded == reencoded;
}

bool perTrackProfileManifestRoundTrip() {
    risco::AlbumPlanRequest request;
    request.seed = 0x9abc1234ULL;
    request.trackCount = 4;
    request.profile = risco::AlbumMusicProfile::contrasting;
    request.trackProfiles[1] = risco::AlbumMusicProfile::original;
    request.trackProfiles[2] = risco::AlbumMusicProfile::related;
    request.trackProfiles[3] = risco::AlbumMusicProfile::contrasting;

    risco::AlbumPlan plan;
    risco::AlbumManifest manifest, loaded;
    std::string encoded, reencoded;
    if (!risco::makeAlbumPlan(request, plan) ||
        !risco::createAlbumManifest(plan, "Per track", manifest) ||
        !risco::recordAlbumProject(manifest, 0, "track-01.risco") ||
        !serialize(manifest, encoded) || encoded.rfind("RISCO_ALBUM_V3\n", 0) != 0)
        return false;
    std::istringstream input(encoded);
    if (!risco::loadAlbumManifest(input, loaded) ||
        loaded.plan.request.trackProfiles != request.trackProfiles ||
        loaded.plan.tracks != plan.tracks ||
        loaded.tracks[0].state != risco::AlbumTrackState::projectReady ||
        !serialize(loaded, reencoded) || encoded != reencoded) return false;

    // A changed profile header must not detach the generated tracks from the plan.
    const auto headerEnd = encoded.find('\n', encoded.find('\n') + 1);
    if (headerEnd == std::string::npos) return false;
    auto tampered = encoded;
    const auto profileSequence = tampered.rfind(" 2 255 0 1 2\n", headerEnd);
    if (profileSequence == std::string::npos) return false;
    tampered.replace(profileSequence, std::string(" 2 255 0 1 2\n").size(), " 2 255 1 1 2\n");
    std::istringstream altered(tampered);
    return !risco::loadAlbumManifest(altered, loaded) &&
           serialize(loaded, reencoded) && encoded == reencoded;
}

bool arrangedAlbumRoundTrip() {
    risco::AlbumPlanRequest request;
    request.seed = 712;
    request.trackCount = 3;
    request.profile = risco::AlbumMusicProfile::related;
    request.trackProfiles[1] = risco::AlbumMusicProfile::contrasting;
    request.arrangeByParts = true;
    risco::AlbumPlan plan;
    risco::AlbumManifest manifest, loaded;
    std::string encoded, reencoded;
    if (!risco::makeAlbumPlan(request, plan) ||
        !risco::createAlbumManifest(plan, "Parts", manifest) ||
        !risco::recordAlbumProject(manifest, 0, "track-01.risco") ||
        !serialize(manifest, encoded) || encoded.rfind("RISCO_ALBUM_V4\n", 0) != 0)
        return false;
    std::istringstream input(encoded);
    if (!risco::loadAlbumManifest(input, loaded) || !loaded.plan.request.arrangeByParts ||
        loaded.plan.tracks != plan.tracks || !serialize(loaded, reencoded) || encoded != reencoded)
        return false;
    auto wrongVersion = encoded;
    wrongVersion.replace(0, std::string("RISCO_ALBUM_V4").size(), "RISCO_ALBUM_V3");
    std::istringstream altered(wrongVersion);
    return !risco::loadAlbumManifest(altered, loaded);
}

bool sourceAlbumRoundTrip() {
    risco::AlbumPlanRequest request;
    request.seed = 507;
    request.trackCount = 4;
    request.profile = risco::AlbumMusicProfile::contrasting;
    const std::string inscription = "Logo cedo, a cidade respira";
    request.source = {risco::SeedTrackMaterial::text, 811, risco::seedTrackTextCue(inscription),
                      risco::seedTrackMotif(risco::composeText(inscription))};
    for (const bool byParts : {false, true}) {
        request.arrangeByParts = byParts;
        risco::AlbumPlan plan;
        risco::AlbumManifest manifest, loaded;
        std::string encoded, reencoded;
        if (!risco::makeAlbumPlan(request, plan) || !risco::createAlbumManifest(plan, "Source", manifest) ||
            !risco::recordAlbumProject(manifest, 0, "track-01.risco") ||
            !serialize(manifest, encoded) || encoded.rfind("RISCO_ALBUM_V5\n", 0) != 0 ||
            encoded.find(inscription) != std::string::npos) return false;
        std::istringstream input(encoded);
        if (!risco::loadAlbumManifest(input, loaded) ||
            !(loaded.plan.request.source == request.source) ||
            loaded.plan.request.arrangeByParts != byParts || loaded.plan.tracks != plan.tracks ||
            !serialize(loaded, reencoded) || encoded != reencoded) return false;
        auto wrongStem = encoded;
        const auto end = wrongStem.find('\n', wrongStem.find("TRACK 0 "));
        if (end == std::string::npos || wrongStem[end - 1] != '0') return false;
        wrongStem[end - 1] = '2';
        std::istringstream altered(wrongStem);
        if (risco::loadAlbumManifest(altered, loaded)) return false;
    }
    return true;
}

bool developedSourceAlbumRoundTrip() {
    risco::AlbumPlanRequest request;
    request.seed = 0x741122ULL;
    request.trackCount = 5;
    request.profile = risco::AlbumMusicProfile::related;
    const std::string inscription = "A rua muda de ritmo";
    request.source = {risco::SeedTrackMaterial::text, 991,
                      risco::seedTrackTextCue(inscription),
                      risco::seedTrackMotif(risco::composeText(inscription))};
    request.generatorVersion = 2;
    risco::AlbumPlan plan;
    risco::AlbumManifest manifest, loaded;
    std::string encoded, reencoded;
    if (!risco::makeAlbumPlan(request, plan) || !risco::createAlbumManifest(plan, "Returns", manifest) ||
        !serialize(manifest, encoded) || encoded.rfind("RISCO_ALBUM_V6\n", 0) != 0)
        return false;
    std::istringstream input(encoded);
    if (!risco::loadAlbumManifest(input, loaded) || loaded.plan.request.generatorVersion != 2 ||
        loaded.plan.tracks != plan.tracks || !serialize(loaded, reencoded) || encoded != reencoded)
        return false;
    auto changedVersion = encoded;
    changedVersion.replace(0, std::string("RISCO_ALBUM_V6").size(), "RISCO_ALBUM_V5");
    std::istringstream altered(changedVersion);
    return !risco::loadAlbumManifest(altered, loaded) && serialize(loaded, reencoded) &&
           encoded == reencoded;
}
}

int main() {
    if (!checkpointsRoundTripAndResume() || !detailedManifestRoundTrip() ||
        !perTrackProfileManifestRoundTrip() || !arrangedAlbumRoundTrip() ||
        !sourceAlbumRoundTrip() || !developedSourceAlbumRoundTrip()) {
        std::cerr << "album manifest checkpoint/resume check failed\n";
        return 1;
    }
    std::cout << "album manifest checkpoint/resume check passed\n";
    return 0;
}
