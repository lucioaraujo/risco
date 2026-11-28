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
    contrasting = 2,
    automatic = 255
};

inline constexpr int maxAlbumTracks = 12;

struct AlbumSource {
    SeedTrackMaterial material = SeedTrackMaterial::none;
    std::uint64_t fingerprint = 0;
    SeedTrackCue cue = SeedTrackCue::none;
    SeedTrackMotif motif;
    friend bool operator==(const AlbumSource& a, const AlbumSource& b) noexcept {
        return a.material == b.material && a.fingerprint == b.fingerprint &&
               a.cue == b.cue && a.motif == b.motif;
    }
};

struct AlbumPlanRequest {
    std::uint64_t seed = 0;
    int trackCount = 8;
    double centerBpm = 110.0;
    double tempoSpread = 20.0;
    AlbumTempoContour contour = AlbumTempoContour::arc;
    AlbumMusicProfile profile = AlbumMusicProfile::original;
    std::array<AlbumMusicProfile, maxAlbumTracks> trackProfiles = [] {
        std::array<AlbumMusicProfile, maxAlbumTracks> values{};
        values.fill(AlbumMusicProfile::automatic);
        return values;
    }();
    bool arrangeByParts = false;
    AlbumSource source;
    // V1 preserves existing albums; source-based albums may opt into motif
    // development between returns with V2.
    int generatorVersion = 1;
};

struct AlbumTrackPlan {
    std::uint64_t seed = 0;
    double bpm = 110.0;
    int durationBars = 64;
    SeedTrackForm form = SeedTrackForm::original;
    int surprise = 1;
    SeedTrackPulse pulse = SeedTrackPulse::straight;
    SeedTrackPalette palette = SeedTrackPalette::original;
    AlbumMusicProfile profile = AlbumMusicProfile::original;
    bool arrangeByParts = false;
    AlbumSource source;
    SeedTrackCue cue = SeedTrackCue::none;
    int motifStem = -1;
    int generatorVersion = 1;
    friend bool operator==(const AlbumTrackPlan& left, const AlbumTrackPlan& right) noexcept {
        return left.seed == right.seed && left.bpm == right.bpm &&
               left.durationBars == right.durationBars && left.form == right.form &&
               left.surprise == right.surprise && left.pulse == right.pulse &&
               left.palette == right.palette && left.profile == right.profile &&
               left.arrangeByParts == right.arrangeByParts && left.source == right.source &&
               left.cue == right.cue && left.motifStem == right.motifStem &&
               left.generatorVersion == right.generatorVersion;
    }
    friend bool operator!=(const AlbumTrackPlan& left, const AlbumTrackPlan& right) noexcept {
        return !(left == right);
    }
};

struct AlbumPlan {
    static constexpr int maxTracks = maxAlbumTracks;
    AlbumPlanRequest request;
    std::array<AlbumTrackPlan, maxTracks> tracks{};
    int count = 0;
};

// Stable plan generation; fails without changing destination on invalid input.
bool makeAlbumPlan(const AlbumPlanRequest& request, AlbumPlan& destination);
bool generateAlbumTrack(const AlbumPlan& plan, int trackIndex, Song& destination);
bool albumTrackMatchesProject(const AlbumTrackPlan& track, const Song& song);

} // namespace risco
