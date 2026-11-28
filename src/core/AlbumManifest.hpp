// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Lúcio de Araújo
#pragma once

#include "AlbumPlan.hpp"

#include <array>
#include <iosfwd>
#include <string>

namespace risco {

enum class AlbumTrackState : std::uint8_t {
    planned = 0,
    projectReady = 1,
    rendered = 2
};

struct AlbumTrackRecord {
    AlbumTrackPlan plan;
    AlbumTrackState state = AlbumTrackState::planned;
    std::string projectFile;
    std::string renderFile;
    double plannedSeconds = 0.0;
    double renderedSeconds = 0.0;
    bool hasMeasurements = false;
    double integratedLufs = 0.0;
    double truePeakDbtp = 0.0;
};

struct AlbumManifest {
    static constexpr unsigned schemaVersion = 6;
    std::string title;
    AlbumPlan plan;
    std::array<AlbumTrackRecord, AlbumPlan::maxTracks> tracks{};
    int count = 0;
};

bool createAlbumManifest(const AlbumPlan& plan, std::string title,
                         AlbumManifest& destination);
bool recordAlbumProject(AlbumManifest& manifest, int trackIndex,
                        std::string projectFile);
bool invalidateAlbumRender(AlbumManifest& manifest, int trackIndex);
bool recordAlbumRender(AlbumManifest& manifest, int trackIndex,
                       std::string renderFile, double actualSeconds,
                       double integratedLufs, double truePeakDbtp);
bool saveAlbumManifest(std::ostream& out, const AlbumManifest& manifest);
bool loadAlbumManifest(std::istream& in, AlbumManifest& manifest);

} // namespace risco
