// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Lúcio de Araújo
#include "core/AlbumPlan.hpp"
#include "core/AlbumManifest.hpp"
#include "core/SongIO.hpp"
#include "core/SongPlayback.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <set>
#include <sstream>

namespace {
bool serialize(const risco::Song& song, std::string& text) {
    std::ostringstream output;
    if (!risco::saveSong(output, song)) return false;
    text = output.str();
    return true;
}

bool contourAndTrackGeneration() {
    for (const auto contour : {risco::AlbumTempoContour::balanced,
                               risco::AlbumTempoContour::contrasting,
                               risco::AlbumTempoContour::rising,
                               risco::AlbumTempoContour::arc}) {
        risco::AlbumPlanRequest request;
        request.seed = 0xc105e2026ULL;
        request.trackCount = 8;
        request.centerBpm = 108.0;
        request.tempoSpread = 24.0;
        request.contour = contour;
        risco::AlbumPlan first, second;
        if (!risco::makeAlbumPlan(request, first) ||
            !risco::makeAlbumPlan(request, second) || first.count != 8 ||
            first.tracks != second.tracks) return false;
        std::set<std::uint64_t> seeds;
        for (int index = 0; index < first.count; ++index) {
            const auto& track = first.tracks[static_cast<size_t>(index)];
            if (track.seed == 0 || !seeds.insert(track.seed).second ||
                track.bpm < 40.0 || track.bpm > 240.0 ||
                (track.durationBars != 48 && track.durationBars != 64 && track.durationBars != 80)) return false;
        }
        if (contour == risco::AlbumTempoContour::rising &&
            !(first.tracks.front().bpm < first.tracks[static_cast<size_t>(first.count - 1)].bpm)) return false;
        if (contour == risco::AlbumTempoContour::arc &&
            !(first.tracks[3].bpm > first.tracks.front().bpm &&
              std::abs(first.tracks.front().bpm - first.tracks[static_cast<size_t>(first.count - 1)].bpm) < 1.0e-9)) { std::cerr << "arc contour\n"; return false; }
        if (contour == risco::AlbumTempoContour::contrasting &&
            !(first.tracks[0].bpm < request.centerBpm && first.tracks[1].bpm > request.centerBpm)) return false;

        risco::Song generated;
        if (!risco::generateAlbumTrack(first, 2, generated) ||
            generated.bpm != first.tracks[2].bpm ||
            generated.placementCount != first.tracks[2].durationBars / 4 * risco::Song::stemCount ||
            generated.cellCount != generated.placementCount) return false;
    }
    return true;
}

bool invalidRequestsAreTransactional() {
    risco::AlbumPlanRequest request;
    risco::AlbumPlan plan;
    request.seed = 13;
    if (!risco::makeAlbumPlan(request, plan)) return false;
    const auto original = plan;
    request.trackCount = risco::AlbumPlan::maxTracks + 1;
    if (risco::makeAlbumPlan(request, plan) || plan.tracks != original.tracks ||
        plan.count != original.count) { std::cerr << "invalid request transaction\n"; return false; }
    risco::Song song;
    song.bpm = 99.0;
    if (risco::generateAlbumTrack(original, original.count, song) || song.bpm != 99.0) return false;
    return true;
}

bool musicallyDistinctTracksAreReproducible() {
    risco::AlbumPlanRequest request;
    request.seed = 0x4a1b20261125ULL;
    request.trackCount = 6;
    risco::AlbumPlan original, related, contrasting, repeated;
    if (!risco::makeAlbumPlan(request, original)) return false;
    request.profile = risco::AlbumMusicProfile::related;
    if (!risco::makeAlbumPlan(request, related)) return false;
    request.profile = risco::AlbumMusicProfile::contrasting;
    if (!risco::makeAlbumPlan(request, contrasting) ||
        !risco::makeAlbumPlan(request, repeated) || contrasting.tracks != repeated.tracks)
        return false;
    std::set<int> timbres;
    for (int index = 0; index < contrasting.count; ++index) {
        const auto i = static_cast<size_t>(index);
        const auto& track = contrasting.tracks[i];
        if (track.seed != original.tracks[i].seed || track.bpm != original.tracks[i].bpm ||
            track.durationBars != original.tracks[i].durationBars ||
            track.form == risco::SeedTrackForm::original ||
            related.tracks[i].palette != related.tracks[i % 2].palette) return false;
        if (i > 0 && (track.form == contrasting.tracks[i - 1].form ||
            track.palette == contrasting.tracks[i - 1].palette ||
            track.pulse == contrasting.tracks[i - 1].pulse)) return false;

        auto generatedStorage = std::make_unique<risco::Song>();
        auto alternateStorage = std::make_unique<risco::Song>();
        auto legacyStorage = std::make_unique<risco::Song>();
        auto& generated = *generatedStorage;
        auto& alternate = *alternateStorage;
        auto& legacy = *legacyStorage;
        if (!risco::generateAlbumTrack(contrasting, index, generated) ||
            !risco::generateAlbumTrack(repeated, index, alternate) ||
            !risco::generateAlbumTrack(original, index, legacy) ||
            !risco::albumTrackMatchesProject(track, generated) ||
            risco::albumTrackMatchesProject(track, legacy)) return false;
        std::string first, second;
        if (!serialize(generated, first) || !serialize(alternate, second) || first != second)
            return false;
        const auto playback = risco::prepareSongPlayback(generated);
        if (std::abs(playback.end - track.durationBars * 240.0 / track.bpm) > 1.0e-8)
            return false;
        timbres.insert(generated.timbreMode);

        if (track.pulse != risco::SeedTrackPulse::straight) {
            const risco::SeedTrackOptions reference{track.bpm, track.durationBars,
                track.form, track.surprise, risco::SeedTrackPulse::straight, track.palette};
            if (!risco::generateSeedTrack(track.seed, reference, alternate)) return false;
            const auto& notes = generated.bank[0].formulaEvents;
            const auto& baseline = alternate.bank[0].formulaEvents;
            if (notes.count != baseline.count) return false;
            bool changed = false;
            for (int n = 0; n < notes.count; ++n) {
                const auto j = static_cast<size_t>(n);
                changed = changed || notes.notes[j].start != baseline.notes[j].start ||
                    notes.notes[j].duration != baseline.notes[j].duration;
            }
            if (!changed) return false;
        }
    }
    return timbres.size() == 3;
}

bool perTrackProfilesAreAppliedDeterministically() {
    risco::AlbumPlanRequest request;
    request.seed = 0x1c2d3e4fULL;
    request.trackCount = 5;
    request.profile = risco::AlbumMusicProfile::contrasting;
    request.trackProfiles[1] = risco::AlbumMusicProfile::original;
    request.trackProfiles[2] = risco::AlbumMusicProfile::related;
    request.trackProfiles[3] = risco::AlbumMusicProfile::contrasting;

    risco::AlbumPlan first, second;
    if (!risco::makeAlbumPlan(request, first) ||
        !risco::makeAlbumPlan(request, second) || first.tracks != second.tracks ||
        first.tracks[0].profile != risco::AlbumMusicProfile::contrasting ||
        first.tracks[1].profile != risco::AlbumMusicProfile::original ||
        first.tracks[2].profile != risco::AlbumMusicProfile::related ||
        first.tracks[3].profile != risco::AlbumMusicProfile::contrasting ||
        first.tracks[4].profile != risco::AlbumMusicProfile::contrasting ||
        first.tracks[1].form != risco::SeedTrackForm::original ||
        first.tracks[2].form == risco::SeedTrackForm::original) return false;

    auto invalid = request;
    invalid.trackProfiles[5] = risco::AlbumMusicProfile::related;
    const auto preserved = first;
    if (risco::makeAlbumPlan(invalid, first) || first.tracks != preserved.tracks ||
        first.count != preserved.count) return false;
    return true;
}

bool sharedDrawingMotifChangesVoice() {
    risco::Inscription drawing;
    drawing.count = 1;
    auto& stroke = drawing.strokes.front();
    stroke.count = 5;
    stroke.points[0] = {0.0f, 0.8f};
    stroke.points[1] = {0.2f, 0.6f};
    stroke.points[2] = {0.4f, 0.4f};
    stroke.points[3] = {0.6f, 0.5f};
    stroke.points[4] = {1.0f, 0.1f};
    risco::AlbumPlanRequest request;
    request.seed = 0x122b1;
    request.trackCount = 4;
    request.profile = risco::AlbumMusicProfile::contrasting;
    request.arrangeByParts = true;
    request.source = {risco::SeedTrackMaterial::drawing, 733, risco::seedTrackDrawingCue(drawing),
                      risco::seedTrackMotif(risco::compose(drawing))};
    risco::AlbumPlan plan;
    if (request.source.cue != risco::SeedTrackCue::rise || request.source.motif.count < 2 ||
        !risco::makeAlbumPlan(request, plan)) return false;
    const int stems[] = {0, 2, 4, 0};
    for (int index = 0; index < plan.count; ++index) {
        const auto& track = plan.tracks[static_cast<std::size_t>(index)];
        auto song = std::make_unique<risco::Song>();
        if (track.motifStem != stems[index] ||
            track.cue != (index == 1 ? risco::SeedTrackCue::fall : risco::SeedTrackCue::rise) ||
            !risco::generateAlbumTrack(plan, index, *song) ||
            !risco::albumTrackMatchesProject(track, *song)) return false;
        const risco::Cell* opening = nullptr;
        for (int i = 0; i < song->placementCount; ++i) {
            const auto& placement = song->grid[static_cast<std::size_t>(i)];
            if (placement.startBeat == 0 && placement.stem == track.motifStem) {
                opening = risco::findCell(*song, placement.cellId);
                break;
            }
        }
        if (!opening || opening->formulaEvents.count != request.source.motif.count) return false;
        const auto& notes = opening->formulaEvents.notes;
        for (int i = 0; i < request.source.motif.count; ++i) {
            const auto n = static_cast<std::size_t>(i);
            const auto movement = std::lround(12.0 * std::log2(notes[n].frequency / notes[0].frequency));
            if (movement != request.source.motif.notes[n].semitones) return false;
        }
    }
    return true;
}

bool sourceMotifUsesDevelopedGeneratorInNewAlbums() {
    risco::AlbumPlanRequest request;
    request.seed = 0x1190abULL;
    request.trackCount = 5;
    request.generatorVersion = 2;
    request.profile = risco::AlbumMusicProfile::contrasting;
    const std::string text = "Uma linha abre, outra retorna";
    request.source = {risco::SeedTrackMaterial::text, 117,
                      risco::seedTrackTextCue(text),
                      risco::seedTrackMotif(risco::composeText(text))};
    risco::AlbumPlan plan, repeated;
    if (!risco::makeAlbumPlan(request, plan) || !risco::makeAlbumPlan(request, repeated) ||
        plan.tracks != repeated.tracks) return false;
    for (int index = 0; index < plan.count; ++index) {
        risco::Song generated;
        if (plan.tracks[static_cast<size_t>(index)].generatorVersion != 2 ||
            !risco::generateAlbumTrack(plan, index, generated) ||
            !risco::albumTrackMatchesProject(plan.tracks[static_cast<size_t>(index)], generated))
            return false;
        bool recordsGeneratorV2 = false;
        for (int cell = 0; cell < generated.cellCount; ++cell)
            recordsGeneratorV2 |= generated.bank[static_cast<size_t>(cell)].formulaOrigin.sourceId ==
                                  "risco.track.seed.v8";
        if (!recordsGeneratorV2) return false;
    }
    return true;
}
}

bool writeAlbumFixture(const std::filesystem::path& directory, bool contrasting) {
    std::error_code error;
    if (std::filesystem::exists(directory, error) || error) return false;
    if (!std::filesystem::create_directories(directory, error) || error) return false;
    risco::AlbumPlanRequest request;
    request.seed = 0x4a1b20261123ULL;
    request.trackCount = contrasting ? 3 : 2;
    request.centerBpm = 112.0;
    request.tempoSpread = 8.0;
    request.contour = risco::AlbumTempoContour::arc;
    if (contrasting) request.profile = risco::AlbumMusicProfile::contrasting;
    risco::AlbumPlan plan;
    risco::AlbumManifest manifest;
    if (!risco::makeAlbumPlan(request, plan) ||
        !risco::createAlbumManifest(plan, "Album render smoke", manifest)) return false;
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
    std::ofstream output(directory / "album.risco-manifest", std::ios::binary | std::ios::trunc);
    return output && risco::saveAlbumManifest(output, manifest) && bool(output);
}

int main(int argc, char** argv) {
    if (argc > 3 || (argc == 3 && std::string(argv[2]) != "contrast")) {
        std::cerr << "usage: risco_album_plan_check [new-fixture-directory [contrast]]\n";
        return 2;
    }
    if (!contourAndTrackGeneration() || !invalidRequestsAreTransactional() ||
        !musicallyDistinctTracksAreReproducible() ||
        !perTrackProfilesAreAppliedDeterministically() || !sharedDrawingMotifChangesVoice() ||
        !sourceMotifUsesDevelopedGeneratorInNewAlbums()) {
        std::cerr << "album plan determinism/contour validation failed\n";
        return 1;
    }
    if (argc >= 2 && !writeAlbumFixture(argv[1], argc == 3)) {
        std::cerr << "could not write album fixture in a new directory\n";
        return 1;
    }
    std::cout << "album plan determinism/contour validation passed\n";
    return 0;
}
