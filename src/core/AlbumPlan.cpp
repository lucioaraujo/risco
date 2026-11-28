// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Lúcio de Araújo
#include "AlbumPlan.hpp"

#include <algorithm>
#include <cmath>

namespace risco {
namespace {
std::uint64_t mix(std::uint64_t value) noexcept {
    value += 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31);
}

SeedTrackOptions trackOptions(const AlbumTrackPlan& track) {
    SeedTrackOptions options{track.bpm, track.durationBars, track.form, track.surprise, track.pulse, track.palette};
    options.arrangeByParts = track.arrangeByParts;
    options.material = track.source.material;
    options.materialSeed = track.source.fingerprint;
    options.cue = track.cue;
    options.motif = track.source.motif;
    options.motifStem = track.motifStem;
    options.generatorVersion = track.generatorVersion;
    return options;
}

bool validSource(const AlbumSource& source) noexcept {
    if (!validSeedTrackMotif(source.motif)) return false;
    if (source.material == SeedTrackMaterial::none)
        return source == AlbumSource{};
    if (source.fingerprint == 0 || source.motif.count == 0) return false;
    if (source.material == SeedTrackMaterial::text)
        return source.cue == SeedTrackCue::none || source.cue == SeedTrackCue::breath;
    return source.material == SeedTrackMaterial::drawing &&
           (source.cue == SeedTrackCue::none || source.cue == SeedTrackCue::rise ||
            source.cue == SeedTrackCue::fall);
}

void planTrackSource(const AlbumPlanRequest& request, int index, AlbumTrackPlan& track) {
    if (request.source.material == SeedTrackMaterial::none) return;
    track.source = request.source;
    track.generatorVersion = request.generatorVersion;
    constexpr int motifStems[] = {0, 2, 4}; // Melody, bass, texture.
    const bool frame = index == 0 || index == request.trackCount - 1;
    track.motifStem = frame && request.trackCount > 3 ? motifStems[0] : motifStems[index % 3];
    if (!track.arrangeByParts) return;
    switch (request.source.cue) {
        case SeedTrackCue::none: break;
        case SeedTrackCue::breath:
            if (frame || track.form == SeedTrackForm::collage) track.cue = SeedTrackCue::breath;
            break;
        case SeedTrackCue::rise:
        case SeedTrackCue::fall:
            track.cue = request.source.cue;
            if (track.profile == AlbumMusicProfile::contrasting && !frame && index % 2 == 1)
                track.cue = track.cue == SeedTrackCue::rise ? SeedTrackCue::fall : SeedTrackCue::rise;
            break;
    }
}

void planTrackMusic(const AlbumPlanRequest& request, int index, AlbumTrackPlan& track) {
    const auto overrideProfile = request.trackProfiles[static_cast<size_t>(index)];
    track.profile = overrideProfile == AlbumMusicProfile::automatic ? request.profile : overrideProfile;
    if (track.profile == AlbumMusicProfile::original) return;
    const bool contrast = track.profile == AlbumMusicProfile::contrasting;
    const int formOffset = static_cast<int>(mix(request.seed ^ 0x464f524d00000002ULL) % 2);
    const int paletteOffset = static_cast<int>(mix(request.seed ^ 0x50414c4554544502ULL) % 3);
    const int pulseOffset = static_cast<int>(mix(request.seed ^ 0x50554c5345000002ULL) % 3);
    const bool rupture = (index + formOffset) % (contrast ? 2 : 3) == 1;
    track.form = rupture ? SeedTrackForm::collage : SeedTrackForm::arc;
    track.surprise = contrast ? (rupture ? 2 : 1) : (rupture ? 1 : 0);
    track.palette = static_cast<SeedTrackPalette>(
        1 + (paletteOffset + (contrast ? index : index % 2)) % 3);
    track.pulse = static_cast<SeedTrackPulse>((pulseOffset + (contrast ? index : index % 2)) % 3);
}
}

bool makeAlbumPlan(const AlbumPlanRequest& request, AlbumPlan& destination) {
    if (request.seed == 0 || request.trackCount < 2 || request.trackCount > AlbumPlan::maxTracks ||
        !std::isfinite(request.centerBpm) || request.centerBpm < 40.0 || request.centerBpm > 240.0 ||
        !std::isfinite(request.tempoSpread) || request.tempoSpread < 0.0 || request.tempoSpread > 40.0 ||
        static_cast<unsigned>(request.contour) > static_cast<unsigned>(AlbumTempoContour::arc) ||
        request.profile > AlbumMusicProfile::contrasting || !validSource(request.source)) return false;
    if (request.generatorVersion < 1 || request.generatorVersion > 2 ||
        (request.source.material == SeedTrackMaterial::none && request.generatorVersion != 1)) return false;
    for (int index = 0; index < AlbumPlan::maxTracks; ++index) {
        const auto profile = request.trackProfiles[static_cast<size_t>(index)];
        if (profile != AlbumMusicProfile::automatic && profile > AlbumMusicProfile::contrasting)
            return false;
        if (index >= request.trackCount && profile != AlbumMusicProfile::automatic)
            return false;
    }

    AlbumPlan candidate;
    candidate.request = request;
    candidate.count = request.trackCount;
    for (int index = 0; index < candidate.count; ++index) {
        const auto trackIndex = static_cast<std::uint64_t>(index + 1);
        auto trackSeed = mix(request.seed ^ mix(0x414c42554d000000ULL + trackIndex));
        if (trackSeed == 0) trackSeed = 1;
        const double position = candidate.count == 1 ? 0.0 :
            double(index) / double(candidate.count - 1);
        double offset = 0.0;
        switch (request.contour) {
            case AlbumTempoContour::balanced: {
                const double unit = double(mix(trackSeed ^ 0x42414c414e434544ULL) >> 11) /
                                    double(std::uint64_t(1) << 53);
                offset = (unit * 2.0 - 1.0) * request.tempoSpread * 0.25;
                break;
            }
            case AlbumTempoContour::contrasting:
                offset = (index % 2 == 0 ? -0.5 : 0.5) * request.tempoSpread;
                break;
            case AlbumTempoContour::rising:
                offset = (position - 0.5) * request.tempoSpread;
                break;
            case AlbumTempoContour::arc:
                offset = -0.5 * std::cos(6.2831853071795864769 * position) * request.tempoSpread;
                break;
        }
        auto& track = candidate.tracks[static_cast<size_t>(index)];
        track.seed = trackSeed;
        track.bpm = std::clamp(request.centerBpm + offset, 40.0, 240.0);
        const auto lengthChoice = mix(trackSeed ^ 0x4455524154494f4eULL) % 3u;
        track.durationBars = 48 + static_cast<int>(lengthChoice) * 16;
        track.arrangeByParts = request.arrangeByParts;
        planTrackMusic(request, index, track);
        planTrackSource(request, index, track);
    }
    destination = candidate;
    return true;
}

bool generateAlbumTrack(const AlbumPlan& plan, int trackIndex, Song& destination) {
    if (plan.count < 2 || plan.count > AlbumPlan::maxTracks ||
        trackIndex < 0 || trackIndex >= plan.count) return false;
    const auto& track = plan.tracks[static_cast<size_t>(trackIndex)];
    return generateSeedTrack(track.seed, trackOptions(track), destination);
}

bool albumTrackMatchesProject(const AlbumTrackPlan& track, const Song& song) {
    if (!song.compositionSeedRecipeEnabled || song.compositionSeed != track.seed ||
        !(std::abs(song.bpm - track.bpm) <= 1.0e-6)) return false;
    const auto options = trackOptions(track);
    const auto expected = seedTrackOrigin(options);
    const int cellCount = std::clamp(song.cellCount, 0, int(song.bank.size()));
    if (track.arrangeByParts || track.source.material != SeedTrackMaterial::none) {
        if (song.beatsPerBar != 4 || song.placementCount <= 0 ||
            song.placementCount > int(song.grid.size())) return false;
        double end = 0.0;
        for (int i = 0; i < song.placementCount; ++i) {
            const auto& placement = song.grid[static_cast<std::size_t>(i)];
            end = std::max(end, placementStartSeconds(song, placement) +
                                placementLengthSeconds(song, placement));
        }
        if (std::abs(end - track.durationBars * 240.0 / track.bpm) > 0.02) return false;
        return std::any_of(song.bank.begin(), song.bank.begin() + cellCount,
                           [&expected](const Cell& cell) {
            return cell.formulaEventPhrase && cell.formulaOrigin.sourceId == expected.sourceId &&
                   cell.formulaOrigin.source == expected.source;
        });
    }
    const bool legacyThreeStemTrack = song.placementCount == track.durationBars / 4 * 3;
    if (!legacyThreeStemTrack && song.placementCount != track.durationBars / 4 * Song::stemCount)
        return false;
    if (track.form == SeedTrackForm::original)
        return std::none_of(song.bank.begin(), song.bank.begin() + cellCount, [](const Cell& cell) {
            return cell.formulaEventPhrase &&
                   (cell.formulaOrigin.sourceId == "risco.track.seed.v4" ||
                    cell.formulaOrigin.sourceId == "risco.track.seed.v5" ||
                    cell.formulaOrigin.sourceId == "risco.track.seed.v6");
        });
    const auto legacy = legacySeedTrackOrigin(options);
    return std::any_of(song.bank.begin(), song.bank.begin() + cellCount,
                       [&expected, &legacy, legacyThreeStemTrack](const Cell& cell) {
        const auto& origin = cell.formulaOrigin;
        return cell.formulaEventPhrase &&
               ((origin.sourceId == expected.sourceId && origin.source == expected.source) ||
                (origin.sourceId == legacy.sourceId &&
                 (origin.source == legacy.source ||
                  (legacyThreeStemTrack && origin.source == expected.source))));
    });
}

} // namespace risco
