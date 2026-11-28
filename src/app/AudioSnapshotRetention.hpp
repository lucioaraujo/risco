// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Lúcio de Araújo
#pragma once

#include <algorithm>
#include <cstddef>
#include <memory>
#include <vector>

namespace risco {

template <typename Snapshot>
void pruneInactiveAudioSnapshots(std::vector<std::unique_ptr<Snapshot>>& snapshots,
                                 const Snapshot* activeSnapshot,
                                 bool audioCallbackIdle,
                                 std::size_t threshold) {
    if (!audioCallbackIdle || snapshots.size() <= threshold) return;
    snapshots.erase(std::remove_if(snapshots.begin(), snapshots.end(),
        [activeSnapshot](const auto& snapshot) {
            return snapshot.get() != activeSnapshot;
        }), snapshots.end());
}

} // namespace risco
