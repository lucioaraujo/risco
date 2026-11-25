// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Lúcio de Araújo
#pragma once

#include <juce_core/juce_core.h>
#include "core/AlbumManifest.hpp"

#include <functional>
#include <string>

namespace risco::app {

struct AlbumRenderQueueResult {
    std::string message;
    bool cancelled = false;
};

AlbumRenderQueueResult runAlbumRenderQueue(
    AlbumManifest& manifest, const juce::File& directory,
    const std::function<bool()>& shouldCancel = {},
    const std::function<void(double)>& progress = {},
    const std::function<void(const std::string&)>& status = {});

} // namespace risco::app
