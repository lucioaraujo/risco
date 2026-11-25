// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Lúcio de Araújo
#pragma once

#include <juce_core/juce_core.h>
#include "core/SongPlayback.hpp"
#include "core/SongRender.hpp"

#include <functional>
#include <string>

namespace risco::app {

struct AlbumWaveMetrics {
    double durationSeconds = 0.0;
    double integratedLufs = -70.0;
    double truePeakDbtp = -120.0;
};

bool renderAlbumWav24(const Song& song, const SongPlayback& playback,
                      const juce::File& target, int trackIndex,
                      const std::function<bool()>& shouldCancel,
                      const std::function<void(double)>& progress,
                      AlbumWaveMetrics& metrics, std::string& error);

bool validAlbumWav24(const juce::File& file, double durationSeconds);

} // namespace risco::app
