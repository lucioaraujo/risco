// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Lúcio de Araújo
#include "../app/AudioSnapshotRetention.hpp"

#include <cstddef>
#include <iostream>
#include <memory>
#include <vector>

namespace {
struct Snapshot { int serial = 0; };

bool repeatedPlaybackDoesNotHitSessionLimit() {
    std::vector<std::unique_ptr<Snapshot>> snapshots;
    const Snapshot* active = nullptr;
    for (int serial = 0; serial < 512; ++serial) {
        snapshots.push_back(std::make_unique<Snapshot>(Snapshot{serial}));
        active = snapshots.back().get();
        risco::pruneInactiveAudioSnapshots(snapshots, active, true, 32);
        if (snapshots.size() > 32 || snapshots.back().get() != active ||
            active->serial != serial) return false;
    }
    return true;
}

bool callbackReadersProtectSnapshots() {
    std::vector<std::unique_ptr<Snapshot>> snapshots;
    for (int serial = 0; serial < 40; ++serial)
        snapshots.push_back(std::make_unique<Snapshot>(Snapshot{serial}));
    const auto* active = snapshots[17].get();
    risco::pruneInactiveAudioSnapshots(snapshots, active, false, 32);
    if (snapshots.size() != 40 || snapshots[17].get() != active) return false;
    risco::pruneInactiveAudioSnapshots(snapshots, active, true, 32);
    return snapshots.size() == 1 && snapshots.front().get() == active && active->serial == 17;
}
} // namespace

int main() {
    if (!repeatedPlaybackDoesNotHitSessionLimit() || !callbackReadersProtectSnapshots()) {
        std::cerr << "audio snapshot retention check failed\n";
        return 1;
    }
    return 0;
}
