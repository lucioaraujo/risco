// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Lúcio de Araújo
#include "core/SongIO.hpp"
#include "core/SongPlayback.hpp"
#include "core/TrackGeneration.hpp"

#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace {
bool save(const risco::Song& song, std::string& text) {
    std::ostringstream output;
    if (!risco::saveSong(output, song)) return false;
    text = output.str();
    return true;
}

bool trackIsCompleteAndReproducible() {
    risco::Song first, repeated, different;
    constexpr std::uint64_t seed = 0x72d4010abc309e55ULL;
    if (!risco::generateSeedTrack(seed, first) ||
        !risco::generateSeedTrack(seed, repeated) ||
        !risco::generateSeedTrack(seed + 1, different)) return false;
    std::string firstText, repeatedText, differentText;
    if (!save(first, firstText) || !save(repeated, repeatedText) ||
        !save(different, differentText) || firstText != repeatedText ||
        firstText == differentText || firstText.rfind("RISCO_SONG_V76\n", 0) != 0 ||
        first.cellCount != 48 || first.placementCount != 48 || first.bpm < 88 || first.bpm > 132)
        return false;

    std::array<int, risco::Song::stemCount> byStem{};
    for (int index = 0; index < first.placementCount; ++index) {
        const auto& placement = first.grid[static_cast<size_t>(index)];
        const auto* cell = risco::findCell(first, placement.cellId);
        if (!cell || !cell->formulaEventPhrase || !cell->bytebeatFormula ||
            cell->formulaOrigin.sourceId != "risco.track.seed.v1" ||
            cell->formulaEvents.count < 1 || cell->waveformCount != risco::Cell::waveformSize ||
            placement.span != risco::CellSpan::fourBars || placement.stem < 0 ||
            placement.stem >= risco::Song::stemCount) return false;
        for (int note = 0; note < cell->formulaEvents.count; ++note)
            if (cell->formulaEvents.notes[static_cast<size_t>(note)].gain < 0.05f) return false;
        ++byStem[static_cast<size_t>(placement.stem)];
    }
    if (byStem != std::array<int, risco::Song::stemCount>{{16, 16, 16}}) return false;

    risco::Song loaded;
    std::istringstream input(firstText);
    if (!risco::loadSong(input, loaded) || loaded.cellCount != 48 ||
        loaded.compositionSeed != seed || loaded.compositionSeedAlgorithm != risco::compositionSeedAlgorithmV1)
        return false;
    const auto playback = risco::prepareSongPlayback(loaded);
    const double expectedDuration = 64.0 * loaded.beatsPerBar * 60.0 / loaded.bpm;
    if (playback.count != 48 || std::abs(playback.end - expectedDuration) > 1.0e-8)
        return false;

    risco::Song unchanged;
    unchanged.bpm = 111.0;
    std::string before, after;
    if (!save(unchanged, before) || risco::generateSeedTrack(0, unchanged) ||
        !save(unchanged, after) || before != after) return false;
    return true;
}

bool shapedFormsReturnAndDiffer() {
    constexpr std::uint64_t seed = 0x72d4010abc309e55ULL;
    risco::Song arc, repeated, collage;
    const risco::SeedTrackOptions arcOptions{0.0, 64, risco::SeedTrackForm::arc, 1};
    const risco::SeedTrackOptions collageOptions{0.0, 64, risco::SeedTrackForm::collage, 2};
    if (!risco::generateSeedTrack(seed, arcOptions, arc) ||
        !risco::generateSeedTrack(seed, arcOptions, repeated) ||
        !risco::generateSeedTrack(seed, collageOptions, collage)) return false;
    std::string arcText, repeatText, collageText;
    if (!save(arc, arcText) || !save(repeated, repeatText) || !save(collage, collageText) ||
        arcText != repeatText || arcText == collageText || arc.placementCount != 48 ||
        collage.placementCount != 48 || arc.bank[0].formulaOrigin.sourceId != "risco.track.seed.v2")
        return false;
    const auto& opening = arc.bank[0].formulaEvents;
    const auto& reprise = arc.bank[36].formulaEvents;
    if (opening.count == 0 || opening.count != reprise.count) return false;
    for (int i = 0; i < opening.count; ++i) {
        const auto& a = opening.notes[static_cast<size_t>(i)];
        const auto& b = reprise.notes[static_cast<size_t>(i)];
        if (a.start != b.start || a.frequency != b.frequency) return false;
    }
    const auto& arcMiddle = arc.bank[24].formulaEvents;
    const auto& collageMiddle = collage.bank[24].formulaEvents;
    if (arcMiddle.count == 0 || collageMiddle.count == 0 ||
        (arcMiddle.count == collageMiddle.count &&
         arcMiddle.notes[0].frequency == collageMiddle.notes[0].frequency &&
         arcMiddle.notes[0].duration == collageMiddle.notes[0].duration)) return false;
    for (const auto* song : {&arc, &collage}) {
        const auto playback = risco::prepareSongPlayback(*song);
        if (playback.count != 48 || std::abs(playback.end - 64.0 * 240.0 / song->bpm) > 1.0e-8)
            return false;
    }
    return true;
}
} // namespace

int main(int argc, char** argv) {
    const std::string form = argc == 3 ? argv[2] : "original";
    if (argc > 3 || (form != "original" && form != "arc" && form != "collage")) {
        std::cerr << "usage: risco_track_generation_check [output-project.risco [original|arc|collage]]\n";
        return 2;
    }
    if (!trackIsCompleteAndReproducible() || !shapedFormsReturnAndDiffer()) {
        std::cerr << "seed track generation/persistence check failed\n";
        return 1;
    }
    if (argc >= 2) {
        risco::Song generated;
        risco::SeedTrackOptions options;
        if (form == "arc") options.form = risco::SeedTrackForm::arc;
        if (form == "collage") {
            options.form = risco::SeedTrackForm::collage;
            options.surprise = 2;
        }
        if (!risco::generateSeedTrack(0x72d4010abc309e55ULL, options, generated)) return 1;
        const std::filesystem::path target(argv[1]);
        std::error_code error;
        std::filesystem::create_directories(target.parent_path(), error);
        if (error) return 1;
        std::ofstream output(target, std::ios::binary | std::ios::trunc);
        if (!output || !risco::saveSong(output, generated)) return 1;
        std::cout << "wrote " << target.string() << '\n';
    }
    std::cout << "seed track generation/persistence check passed\n";
    return 0;
}
