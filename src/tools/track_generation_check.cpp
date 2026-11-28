// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Lúcio de Araújo
#include "core/SongIO.hpp"
#include "core/SongPlayback.hpp"
#include "core/SongRender.hpp"
#include "core/TrackGeneration.hpp"

#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <utility>

namespace {
bool save(const risco::Song& song, std::string& text) {
    std::ostringstream output;
    if (!risco::saveSong(output, song)) return false;
    text = output.str();
    return true;
}

bool trackIsCompleteAndReproducible() {
    auto firstStorage = std::make_unique<risco::Song>();
    auto repeatedStorage = std::make_unique<risco::Song>();
    auto differentStorage = std::make_unique<risco::Song>();
    auto& first = *firstStorage;
    auto& repeated = *repeatedStorage;
    auto& different = *differentStorage;
    constexpr std::uint64_t seed = 0x72d4010abc309e55ULL;
    if (!risco::generateSeedTrack(seed, first) ||
        !risco::generateSeedTrack(seed, repeated) ||
        !risco::generateSeedTrack(seed + 1, different)) {
        std::cerr << "could not generate three continuous tracks\n";
        return false;
    }
    std::string firstText, repeatedText, differentText;
    if (!save(first, firstText) || !save(repeated, repeatedText) ||
        !save(different, differentText) || firstText != repeatedText ||
        firstText == differentText || firstText.rfind("RISCO_SONG_V76\n", 0) != 0 ||
        first.cellCount != 80 || first.placementCount != 80 || first.bpm < 88 || first.bpm > 132) {
        std::cerr << "continuous track format, counts or seed repeatability differed\n";
        return false;
    }

    std::array<int, risco::Song::stemCount> byStem{};
    for (int index = 0; index < first.placementCount; ++index) {
        const auto& placement = first.grid[static_cast<size_t>(index)];
        const auto* cell = risco::findCell(first, placement.cellId);
        if (!cell || !cell->formulaEventPhrase || !cell->bytebeatFormula ||
            cell->formulaOrigin.sourceId != "risco.track.seed.v2" ||
            cell->formulaEvents.count < 1 || cell->waveformCount != risco::Cell::waveformSize ||
            placement.span != risco::CellSpan::fourBars || placement.stem < 0 ||
            placement.stem >= risco::Song::stemCount) return false;
        for (int note = 0; note < cell->formulaEvents.count; ++note) {
            const float gain = cell->formulaEvents.notes[static_cast<size_t>(note)].gain;
            if (cell->role == risco::CellRole::texture ? (gain <= 0.0f || gain > 0.28f) : gain < 0.05f) {
                std::cerr << "unexpected gain in " << cell->name << '\n';
                return false;
            }
        }
        ++byStem[static_cast<size_t>(placement.stem)];
    }
    if (byStem != std::array<int, risco::Song::stemCount>{{16, 16, 16, 16, 16}}) return false;

    auto loadedStorage = std::make_unique<risco::Song>();
    auto& loaded = *loadedStorage;
    std::istringstream input(firstText);
    if (!risco::loadSong(input, loaded) || loaded.cellCount != 80 ||
        loaded.compositionSeed != seed || loaded.compositionSeedAlgorithm != risco::compositionSeedAlgorithmV1) {
        std::cerr << "continuous track failed to reopen with its recipe\n";
        return false;
    }
    risco::SeedTrackOptions unsupportedRecipe;
    if (risco::seedTrackOptionsFromSong(loaded, unsupportedRecipe)) {
        std::cerr << "continuous track was incorrectly offered part editing\n";
        return false;
    }
    const auto playback = risco::prepareSongPlayback(loaded);
    const double expectedDuration = 64.0 * loaded.beatsPerBar * 60.0 / loaded.bpm;
    if (playback.count != 80 || std::abs(playback.end - expectedDuration) > 1.0e-8)
        return false;

    auto unchangedStorage = std::make_unique<risco::Song>();
    auto& unchanged = *unchangedStorage;
    unchanged.bpm = 111.0;
    std::string before, after;
    if (!save(unchanged, before) || risco::generateSeedTrack(0, unchanged) ||
        !save(unchanged, after) || before != after) return false;
    return true;
}

bool shapedFormsReturnAndDiffer() {
    constexpr std::uint64_t seed = 0x72d4010abc309e55ULL;
    auto arcStorage = std::make_unique<risco::Song>();
    auto repeatedStorage = std::make_unique<risco::Song>();
    auto collageStorage = std::make_unique<risco::Song>();
    auto& arc = *arcStorage;
    auto& repeated = *repeatedStorage;
    auto& collage = *collageStorage;
    const risco::SeedTrackOptions arcOptions{0.0, 64, risco::SeedTrackForm::arc, 1};
    const risco::SeedTrackOptions collageOptions{0.0, 64, risco::SeedTrackForm::collage, 2};
    if (!risco::generateSeedTrack(seed, arcOptions, arc) ||
        !risco::generateSeedTrack(seed, arcOptions, repeated) ||
        !risco::generateSeedTrack(seed, collageOptions, collage)) return false;
    std::string arcText, repeatText, collageText;
    if (!save(arc, arcText) || !save(repeated, repeatText) || !save(collage, collageText) ||
        arcText != repeatText || arcText == collageText || arc.placementCount != 80 ||
        collage.placementCount != 80 || arc.bank[0].formulaOrigin.sourceId != "risco.track.seed.v3")
        return false;
    const auto& opening = arc.bank[0].formulaEvents;
    const auto& reprise = arc.bank[60].formulaEvents;
    if (opening.count == 0 || opening.count != reprise.count) return false;
    for (int i = 0; i < opening.count; ++i) {
        const auto& a = opening.notes[static_cast<size_t>(i)];
        const auto& b = reprise.notes[static_cast<size_t>(i)];
        if (a.start != b.start || a.frequency != b.frequency) return false;
    }
    const auto& arcMiddle = arc.bank[40].formulaEvents;
    const auto& collageMiddle = collage.bank[40].formulaEvents;
    if (arcMiddle.count == 0 || collageMiddle.count == 0 ||
        (arcMiddle.count == collageMiddle.count &&
         arcMiddle.notes[0].frequency == collageMiddle.notes[0].frequency &&
         arcMiddle.notes[0].duration == collageMiddle.notes[0].duration)) return false;
    for (const auto* song : {&arc, &collage}) {
        const auto playback = risco::prepareSongPlayback(*song);
        if (playback.count != 80 || std::abs(playback.end - 64.0 * 240.0 / song->bpm) > 1.0e-8)
            return false;
    }
    return true;
}

bool partsAndSourceCues() {
    if (risco::seedTrackTextCue("Um movimento.") != risco::SeedTrackCue::none ||
        risco::seedTrackTextCue("Primeiro, depois") != risco::SeedTrackCue::breath)
        return false;
    risco::Inscription drawing;
    drawing.count = 1;
    drawing.strokes[0].count = 2;
    drawing.strokes[0].points[0].y = 0.8f;
    drawing.strokes[0].points[1].y = 0.2f;
    if (risco::seedTrackDrawingCue(drawing) != risco::SeedTrackCue::rise) return false;
    std::swap(drawing.strokes[0].points[0], drawing.strokes[0].points[1]);
    if (risco::seedTrackDrawingCue(drawing) != risco::SeedTrackCue::fall) return false;

    auto song = std::make_unique<risco::Song>();
    auto loaded = std::make_unique<risco::Song>();
    constexpr std::uint64_t seed = 0x72d4010abc309e55ULL;
    risco::SeedTrackOptions options{110.0, 32, risco::SeedTrackForm::arc, 1};
    options.generatorVersion = 1; // Keep this fixture focused on the legacy, literal return.
    options.arrangeByParts = true;
    options.cue = risco::SeedTrackCue::breath;
    if (!risco::generateSeedTrack(seed, options, *song)) return false;
    std::string encoded, roundTrip;
    if (!save(*song, encoded)) return false;
    std::istringstream input(encoded);
    if (!risco::loadSong(input, *loaded) || !save(*loaded, roundTrip) || encoded != roundTrip)
        return false;
    constexpr unsigned all = (1u << risco::Song::stemCount) - 1u;
    std::array<unsigned, 8> masks{};
    for (int i = 0; i < loaded->placementCount; ++i) {
        const auto& placement = loaded->grid[static_cast<std::size_t>(i)];
        const auto block = placement.startBeat / 16;
        if (block < 0 || block >= int(masks.size()) || placement.stem < 0 ||
            placement.stem >= risco::Song::stemCount) return false;
        masks[static_cast<std::size_t>(block)] |= 1u << placement.stem;
        if (block != 5) continue;
        const auto* cell = risco::findCell(*loaded, placement.cellId);
        if (cell == nullptr) return false;
        const auto& events = cell->formulaEvents;
        for (int j = 0; j < events.count; ++j) {
            const auto& note = events.notes[static_cast<std::size_t>(j)];
            if (note.start + note.duration > events.end * 0.75 + 1.0e-8) return false;
        }
    }
    if (masks[0] == 0 || masks[0] == all || masks[2] == masks[0] ||
        masks[5] == 0 || (masks[5] & (masks[5] - 1)) != 0 ||
        masks[6] != all || masks[7] == 0 || masks[7] == all) return false;
    const auto playback = risco::prepareSongPlayback(*loaded);
    return std::abs(playback.end - 32.0 * 240.0 / options.bpm) < 1.0e-8;
}

bool sharedMotifSurvivesProposalChanges() {
    const std::string text = "O rio vira, passa, volta";
    risco::SeedTrackOptions options{110.0, 32, risco::SeedTrackForm::arc, 1};
    options.arrangeByParts = true;
    options.material = risco::SeedTrackMaterial::text;
    options.materialSeed = 8723;
    options.cue = risco::seedTrackTextCue(text);
    options.motif = risco::seedTrackMotif(risco::composeText(text));
    options.motifStem = 0;
    if (options.motif.count < 2) return false;

    const auto preservesContour = [&options](const risco::Song& song, int startBeat, int stem) {
        for (int i = 0; i < song.placementCount; ++i) {
            const auto& placement = song.grid[static_cast<std::size_t>(i)];
            if (placement.startBeat != startBeat || placement.stem != stem) continue;
            const auto* cell = risco::findCell(song, placement.cellId);
            if (!cell || cell->formulaEvents.count != options.motif.count) return false;
            const auto& notes = cell->formulaEvents.notes;
            for (int j = 0; j < options.motif.count; ++j) {
                const auto n = static_cast<std::size_t>(j);
                const auto interval = std::lround(12.0 * std::log2(notes[n].frequency / notes[0].frequency));
                const auto tick = std::lround(notes[n].start * song.bpm / 30.0);
                if (interval != options.motif.notes[n].semitones || tick != options.motif.notes[n].tick)
                    return false;
            }
            return true;
        }
        return false;
    };

    auto song = std::make_unique<risco::Song>();
    auto reopened = std::make_unique<risco::Song>();
    std::string first;
    for (const std::uint64_t seed : {113u, 758u}) {
        options.motifReturnStem = risco::alternateMotifStem(seed, options.motifStem);
        if (options.motifReturnStem != 2 && options.motifReturnStem != 4) return false;
        std::string encoded;
        if (!risco::generateSeedTrack(seed, options, *song) || !save(*song, encoded)) return false;
        std::istringstream input(encoded);
        if (!risco::loadSong(input, *reopened) || !preservesContour(*reopened, 0, options.motifStem) ||
            !preservesContour(*reopened, 96, options.motifReturnStem) ||
            reopened->compositionSeed != seed) return false;
        if (first.empty()) first = encoded;
        else if (first == encoded) return false;
    }
    return true;
}

bool motifReturnDevelopsBySurpriseAndPersists() {
    risco::SeedTrackOptions options{110.0, 32, risco::SeedTrackForm::arc, 1};
    options.generatorVersion = 2;
    options.arrangeByParts = true;
    options.material = risco::SeedTrackMaterial::text;
    options.materialSeed = 83271;
    options.motif = {{{{0, 0}, {2, 3}, {5, -2}, {}, {}}}, 3};
    options.motifStem = 0;
    constexpr std::uint64_t seed = 77123;
    options.motifReturnStem = risco::alternateMotifStem(seed, options.motifStem);
    const auto readMotif = [&options](const risco::Song& song, int startBeat, int stem,
                                      std::array<int, 5>& intervals, std::array<int, 5>& ticks) {
        for (int i = 0; i < song.placementCount; ++i) {
            const auto& placement = song.grid[static_cast<std::size_t>(i)];
            if (placement.startBeat != startBeat || placement.stem != stem) continue;
            const auto* cell = risco::findCell(song, placement.cellId);
            if (!cell || cell->formulaEvents.count != options.motif.count) return false;
            const auto& notes = cell->formulaEvents.notes;
            for (int note = 0; note < cell->formulaEvents.count; ++note) {
                const auto n = static_cast<std::size_t>(note);
                intervals[n] = static_cast<int>(std::lround(
                    12.0 * std::log2(notes[n].frequency / notes[0].frequency)));
                ticks[n] = static_cast<int>(std::lround(notes[n].start * song.bpm / 30.0));
            }
            return true;
        }
        return false;
    };
    auto song = std::make_unique<risco::Song>();
    for (int surprise = 1; surprise <= 2; ++surprise) {
        options.surprise = surprise;
        if (!risco::generateSeedTrack(seed, options, *song) ||
            song->bank[0].formulaOrigin.sourceId != "risco.track.seed.v8" ||
            !risco::isSeedTrackOrigin(song->bank[0].formulaOrigin)) return false;
        std::array<int, 5> openingIntervals{}, openingTicks{}, returnIntervals{}, returnTicks{};
        if (!readMotif(*song, 0, options.motifStem, openingIntervals, openingTicks) ||
            !readMotif(*song, 96, options.motifReturnStem, returnIntervals, returnTicks)) return false;
        if (openingIntervals[1] != 3 || openingIntervals[2] != -2 ||
            openingTicks[1] != 2 || openingTicks[2] != 5) return false;
        if (surprise == 1) {
            if (returnIntervals[1] != -3 || returnIntervals[2] != 2 ||
                returnTicks[1] != 2 || returnTicks[2] != 5) return false;
        } else if (returnIntervals[1] != -5 || returnIntervals[2] != -2 ||
                   returnTicks[1] != 3 || returnTicks[2] != 5) return false;

        std::string encoded;
        if (!save(*song, encoded)) return false;
        auto loaded = std::make_unique<risco::Song>();
        std::istringstream input(encoded);
        risco::SeedTrackOptions restored;
        auto repeated = std::make_unique<risco::Song>();
        std::string reencoded;
        if (!risco::loadSong(input, *loaded) ||
            !risco::seedTrackOptionsFromSong(*loaded, restored) || restored.generatorVersion != 2 ||
            !risco::generateSeedTrack(loaded->compositionSeed, restored, *repeated) ||
            !save(*repeated, reencoded) || encoded != reencoded) return false;
    }
    options.surprise = 2;
    options.motifReturn = risco::SeedTrackMotifReturn::literal;
    if (!risco::generateSeedTrack(seed, options, *song) ||
        song->bank[0].formulaOrigin.sourceId != "risco.track.seed.v9" ||
        !risco::isSeedTrackOrigin(song->bank[0].formulaOrigin)) return false;
    std::array<int, 5> intervals{}, ticks{};
    if (!readMotif(*song, 96, options.motifReturnStem, intervals, ticks) ||
        intervals[1] != 3 || intervals[2] != -2 || ticks[1] != 2 || ticks[2] != 5) return false;
    std::string encoded;
    if (!save(*song, encoded)) return false;
    auto loaded = std::make_unique<risco::Song>();
    std::istringstream input(encoded);
    risco::SeedTrackOptions restored;
    return risco::loadSong(input, *loaded) &&
           risco::seedTrackOptionsFromSong(*loaded, restored) &&
           restored.motifReturn == risco::SeedTrackMotifReturn::literal &&
           restored.generatorVersion == 2;
}

bool editedPartsChangeTheGridAndPlayback() {
    constexpr std::uint64_t seed = 80826;
    risco::SeedTrackOptions options{110.0, 32, risco::SeedTrackForm::arc, 1};
    options.arrangeByParts = true;
    options.partEdits.front() = {3, 1u << 3, risco::SeedTrackPartDynamics::crescendo, true};
    const auto parts = risco::seedTrackParts(seed, options);
    if (parts[0].endBlock != 3 || parts[1].firstBlock != 3 || parts.back().endBlock != 9)
        return false;

    auto song = std::make_unique<risco::Song>();
    auto loaded = std::make_unique<risco::Song>();
    std::string encoded;
    if (!risco::generateSeedTrack(seed, options, *song) || !save(*song, encoded)) return false;
    std::istringstream input(encoded);
    if (!risco::loadSong(input, *loaded)) return false;
    risco::SeedTrackOptions restoredOptions;
    auto regenerated = std::make_unique<risco::Song>();
    std::string regeneratedText;
    if (!risco::seedTrackOptionsFromSong(*loaded, restoredOptions) ||
        !risco::generateSeedTrack(loaded->compositionSeed, restoredOptions, *regenerated) ||
        !save(*regenerated, regeneratedText) || regeneratedText != encoded) {
        std::cerr << "saved part recipe could not be restored exactly\n";
        return false;
    }
    auto manuallyMoved = std::make_unique<risco::Song>(*loaded);
    manuallyMoved->grid[0].startBeat += 4;
    risco::SeedTrackOptions movedOptions;
    if (risco::seedTrackOptionsFromSong(*manuallyMoved, movedOptions)) {
        std::cerr << "manually moved track was incorrectly offered recipe editing\n";
        return false;
    }
    std::array<const risco::Cell*, 3> opening{};
    for (int i = 0; i < loaded->placementCount; ++i) {
        const auto& placement = loaded->grid[static_cast<std::size_t>(i)];
        if (placement.startBeat >= 48) continue;
        if (placement.stem != 3 || placement.startBeat % 16 != 0) return false;
        auto& cell = opening[static_cast<std::size_t>(placement.startBeat / 16)];
        if (cell) return false;
        cell = risco::findCell(*loaded, placement.cellId);
        if (!cell || cell->formulaOrigin.sourceId != "risco.track.seed.v6") return false;
    }
    if (!opening[0] || !opening[1] || !opening[2]) return false;
    const auto& first = opening.front()->formulaEvents;
    const auto& last = opening.back()->formulaEvents;
    if (first.count == 0 || last.count != first.count) return false;
    for (int i = 0; i < first.count; ++i) {
        const auto n = static_cast<std::size_t>(i);
        if (first.notes[n].frequency != last.notes[n].frequency ||
            first.notes[n].start != last.notes[n].start || first.notes[n].gain >= last.notes[n].gain)
            return false;
    }
    const auto playback = risco::prepareSongPlayback(*loaded);
    if (std::abs(playback.end - 36.0 * 240.0 / options.bpm) > 1.0e-8) return false;
    options.partEdits.front().blocks = 21;
    std::string afterInvalid;
    return !risco::generateSeedTrack(seed, options, *song) && save(*song, afterInvalid) && encoded == afterInvalid;
}

bool archDynamicsSurvivesGenerationAndSave() {
    constexpr std::uint64_t seed = 0x8877665544332211ULL;
    risco::SeedTrackOptions automatic{110.0, 64, risco::SeedTrackForm::arc, 1};
    automatic.arrangeByParts = true;
    auto shaped = automatic;
    shaped.partEdits[1].dynamics = risco::SeedTrackPartDynamics::arch;
    auto plainSong = std::make_unique<risco::Song>();
    auto archSong = std::make_unique<risco::Song>();
    if (!risco::generateSeedTrack(seed, automatic, *plainSong) ||
        !risco::generateSeedTrack(seed, shaped, *archSong) ||
        archSong->bank[0].formulaOrigin.sourceId != "risco.track.seed.v7") return false;

    const auto ratio = [](const risco::Song& expressive, const risco::Song& plain, int block) {
        for (int i = 0; i < expressive.placementCount; ++i) {
            const auto& aPlacement = expressive.grid[static_cast<std::size_t>(i)];
            if (aPlacement.startBeat != block * 16 || aPlacement.stem != 2) continue;
            const auto& bPlacement = plain.grid[static_cast<std::size_t>(i)];
            const auto* a = risco::findCell(expressive, aPlacement.cellId);
            const auto* b = risco::findCell(plain, bPlacement.cellId);
            if (!a || !b || a->formulaEvents.count == 0 || b->formulaEvents.count == 0) return -1.0f;
            return a->formulaEvents.notes[0].gain / b->formulaEvents.notes[0].gain;
        }
        return -1.0f;
    };
    const auto first = ratio(*archSong, *plainSong, 4);
    const auto middle = ratio(*archSong, *plainSong, 6);
    const auto last = ratio(*archSong, *plainSong, 7);
    if (first < 0.0f || middle < 0.0f || last < 0.0f || middle <= first + 0.10f || middle <= last + 0.03f)
        return false;

    std::string saved;
    if (!save(*archSong, saved)) return false;
    auto loaded = std::make_unique<risco::Song>();
    std::istringstream input(saved);
    risco::SeedTrackOptions restored;
    auto regenerated = std::make_unique<risco::Song>();
    std::string repeated;
    return risco::loadSong(input, *loaded) &&
           risco::seedTrackOptionsFromSong(*loaded, restored) &&
           restored.partEdits[1].dynamics == risco::SeedTrackPartDynamics::arch &&
           risco::generateSeedTrack(loaded->compositionSeed, restored, *regenerated) &&
           save(*regenerated, repeated) && repeated == saved;
}

bool consecutiveGeneratedTracksRemainPlayableAfterSave() {
    risco::SeedTrackOptions options{108.0, 32, risco::SeedTrackForm::arc, 1};
    options.generatorVersion = 2;
    for (const std::uint64_t seed : {0x19a2b3ULL, 0x19a2b4ULL}) {
        auto generated = std::make_unique<risco::Song>();
        if (!risco::generateSeedTrack(seed, options, *generated) ||
            !risco::isSeedTrackOrigin(generated->bank[0].formulaOrigin)) return false;
        std::string encoded;
        if (!save(*generated, encoded)) return false;
        auto reopened = std::make_unique<risco::Song>();
        std::istringstream input(encoded);
        if (!risco::loadSong(input, *reopened)) return false;
        auto playback = std::make_unique<risco::SongPlayback>(risco::prepareSongPlayback(*reopened));
        if (playback->count == 0 || playback->end <= 0.0) return false;
        auto renderer = std::make_unique<risco::SongRender>(*reopened, *playback);
        double energy = 0.0;
        for (int frame = 0; frame < 24000; ++frame) {
            const auto stereo = renderer->frame(static_cast<std::uint32_t>(frame));
            if (!std::isfinite(stereo.left) || !std::isfinite(stereo.right)) return false;
            energy += double(stereo.left) * stereo.left + double(stereo.right) * stereo.right;
        }
        if (energy < 1.0) return false;
    }
    return !risco::isSeedTrackOrigin({"", "", "other.generator.v8", 0, 0, ""}) &&
           !risco::isSeedTrackOrigin({"", "", "risco.track.seed.v1", 0, 0, ""});
}
} // namespace

int main(int argc, char** argv) {
    const std::string form = argc == 3 ? argv[2] : "original";
    if (argc > 3 || (form != "original" && form != "arc" && form != "collage")) {
        std::cerr << "usage: risco_track_generation_check [output-project.risco [original|arc|collage]]\n";
        return 2;
    }
    if (!trackIsCompleteAndReproducible()) {
        std::cerr << "continuous seed track generation/persistence check failed\n";
        return 1;
    }
    if (!shapedFormsReturnAndDiffer()) {
        std::cerr << "theme return and collage check failed\n";
        return 1;
    }
    if (!partsAndSourceCues()) {
        std::cerr << "arrangement parts and source cues check failed\n";
        return 1;
    }
    if (!sharedMotifSurvivesProposalChanges()) {
        std::cerr << "motif opening and return across proposals check failed\n";
        return 1;
    }
    if (!motifReturnDevelopsBySurpriseAndPersists()) {
        std::cerr << "seeded motif development and persistence check failed\n";
        return 1;
    }
    if (!editedPartsChangeTheGridAndPlayback()) {
        std::cerr << "edited track parts check failed\n";
        return 1;
    }
    if (!archDynamicsSurvivesGenerationAndSave()) {
        std::cerr << "arch dynamics generation/persistence check failed\n";
        return 1;
    }
    if (!consecutiveGeneratedTracksRemainPlayableAfterSave()) {
        std::cerr << "consecutive generated track playback check failed\n";
        return 1;
    }
    if (argc >= 2) {
        auto generatedStorage = std::make_unique<risco::Song>();
        auto& generated = *generatedStorage;
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
