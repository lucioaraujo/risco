// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Lúcio de Araújo
#pragma once
#include "Song.hpp"
#include <iosfwd>
#include <vector>

namespace risco {
struct NamedSongVersionRecord {
    std::string name;
    std::string serializedSong;
    int parentIndex = -1; // -1 means a root; parents precede their children.
};
inline constexpr std::size_t maxNamedSongVersions = 16;
inline constexpr std::size_t maxNamedSongVersionBytes = 3 * 1024 * 1024;
bool saveSong(std::ostream& out, const Song& song,
              const std::vector<NamedSongVersionRecord>& versions = {}, int activeVersion = -1);
// Loads into a temporary value. On failure, the existing song is unchanged.
bool loadSong(std::istream& in, Song& song,
              std::vector<NamedSongVersionRecord>* versions = nullptr, int* activeVersion = nullptr);
// Store an independent generated-phrase recipe and remember it for comparison.
// No grid mutation; validates before committing; shares the bank's 64-cell limit.
std::uint32_t storePhraseTake(Song& song, const Cell& recipe);
// Copy one inscription/recipe into the bank; no placements or source-project settings.
// Parent IDs are recorded in provenance and detached from the destination namespace.
std::uint32_t importCell(Song& destination, const Cell& source, const std::string& projectDescription);
}
