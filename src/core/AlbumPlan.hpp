// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Lúcio de Araújo
#pragma once

#include "TrackGeneration.hpp"

#include <array>
#include <cstdint>

namespace risco {

enum class AlbumTempoContour : std::uint8_t {
    balanced = 0,
    contrasting = 1,
    rising = 2,
    arc = 3
};

enum class AlbumMusicProfile : std::uint8_t {
    original = 0,
    related = 1,
    contrasting = 2
};

struct AlbumPlanRequest {
    std::uint64_t seed = 0;
    int trackCount = 8;
    double centerBpm = 110.0;
    double tempoSpread = 20.0;
    AlbumTempoContour contour = AlbumTempoContour::arc;
    AlbumMusicProfile profile = AlbumMusicProfile::original;
};

struct AlbumTrackPlan {
    std::uint64_t seed = 0;
    double bpm = 110.0;
    int durationBars = 64;
    SeedTrackForm form = SeedTrackForm::original;
    int surprise = 1;
    SeedTrackPulse pulse = SeedTrackPulse::straight;
    SeedTrackPalette palette = SeedTrackPalette::original;
    friend bool operator==(const AlbumTrackPlan& left, const AlbumTrackPlan& right) noexcept {
        return left.seed == right.seed && left.bpm == right.bpm &&
               left.durationBars == right.durationBars && left.form == right.form &&
               left.surprise == right.surprise && left.pulse == right.pulse &&
               left.palette == right.palette;
    }
    friend bool operator!=(const AlbumTrackPlan& left, const AlbumTrackPlan& right) noexcept {
        return !(left == right);
    }
};

struct AlbumPlan {
    static constexpr int maxTracks = 12;
    AlbumPlanRequest request;
    std::array<AlbumTrackPlan, maxTracks> tracks{};
    int count = 0;
};

// Stable plan generation; fails without changing destination on invalid input.
bool makeAlbumPlan(const AlbumPlanRequest& request, AlbumPlan& destination);
bool generateAlbumTrack(const AlbumPlan& plan, int trackIndex, Song& destination);
bool albumTrackMatchesProject(const AlbumTrackPlan& track, const Song& song);

} // namespace risco
