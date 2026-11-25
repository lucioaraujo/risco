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
    return {track.bpm, track.durationBars, track.form, track.surprise, track.pulse, track.palette};
}

void planTrackMusic(const AlbumPlanRequest& request, int index, AlbumTrackPlan& track) {
    if (request.profile == AlbumMusicProfile::original) return;
    const bool contrast = request.profile == AlbumMusicProfile::contrasting;
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
        request.profile > AlbumMusicProfile::contrasting) return false;

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
        planTrackMusic(request, index, track);
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
        !(std::abs(song.bpm - track.bpm) <= 1.0e-6) ||
        song.placementCount != track.durationBars / 4 * Song::stemCount) return false;
    if (track.form == SeedTrackForm::original) return true;
    const auto expected = seedTrackOrigin(trackOptions(track));
    const int cellCount = std::clamp(song.cellCount, 0, int(song.bank.size()));
    return std::any_of(song.bank.begin(), song.bank.begin() + cellCount, [&expected](const Cell& cell) {
        return cell.formulaEventPhrase && cell.formulaOrigin.sourceId == expected.sourceId &&
               cell.formulaOrigin.source == expected.source;
    });
}

} // namespace risco
