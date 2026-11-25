// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Lúcio de Araújo
#include "Song.hpp"
#include "BytebeatExpression.hpp"
#include "FormulaSeed.hpp"
#include "Sound.hpp"
#include "Utf8.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace risco {

bool validInscription(const Inscription& source) noexcept {
    if (source.count < 0 || source.count > int(source.strokes.size()) ||
        source.ascii.size() > Cell::maxTextBytes ||
        !validUtf8(source.ascii) ||
        source.ascii.find_first_of("\r\n\0", 0, 3) != std::string::npos) return false;
    if (source.count == 0) return !source.ascii.empty();
    for (int s = 0; s < source.count; ++s) {
        const auto& stroke = source.strokes[static_cast<size_t>(s)];
        if (stroke.count < 2 || stroke.count > int(stroke.points.size())) return false;
        for (int p = 0; p < stroke.count; ++p) {
            const auto& point = stroke.points[static_cast<size_t>(p)];
            if (!std::isfinite(point.x) || !std::isfinite(point.y) ||
                point.x < 0.0f || point.x > 1.0f || point.y < 0.0f || point.y > 1.0f)
                return false;
        }
    }
    return true;
}

const Cell* findCell(const Song& song, std::uint32_t id) noexcept {
    for (int index = 0; index < std::clamp(song.cellCount, 0, int(song.bank.size())); ++index)
        if (song.bank[static_cast<size_t>(index)].id == id) return &song.bank[static_cast<size_t>(index)];
    return nullptr;
}

CellAudioProfile describeCellAudio(const Cell& cell, double bpm, int beatsPerBar) noexcept {
    CellAudioProfile profile;
    bpm = std::clamp(std::isfinite(bpm) ? bpm : 120.0, 40.0, 240.0);
    beatsPerBar = std::clamp(beatsPerBar, 2, 7);
    Sequence sequence;
    if (cell.formulaEventPhrase) sequence = cell.formulaEvents;
    else if (cell.generatedPhrase)
        sequence = phraseFromSeed(cell.phraseSeed, cell.phraseVariation, cell.phraseVersion,
                                  bpm, cell.phraseSectionMask, beatsPerBar, false, cell.phraseMeters);
    else if (!cell.bytebeatFormula) sequence = interpretInput(composeInput(cell.inscription), cell.inputInterpretation,
        cell.inputInterpretationStrength,cell.inputGestureCurve,cell.inputGestureRangeSemitones);
    if (cell.generatedPhrase) {
        const auto meters = cell.phraseVersion >= 6 ? phraseMeterWeights(cell.phraseMeters, beatsPerBar)
                                                    : std::array<int,4>{{1,1,1,1}};
        sequence = stretchSections(sequence, meters, cell.phraseSectionBars);
        sequence = editSections(sequence, cell.sectionEdits, cell.phraseVersion == 4 ? 2 : 4,
            cell.phraseVersion >= 6 ? phraseSectionWeights(cell.phraseMeters, cell.phraseSectionBars, beatsPerBar)
                                    : cell.phraseSectionBars);
    }

    if (sequence.end > 0.0 && std::isfinite(sequence.end)) {
        const int count = std::clamp(sequence.count, 0, int(sequence.notes.size()));
        profile.hasEvents = true;
        profile.eventsPerBar = float(count / std::max(0.01,
            sequence.end * bpm / (60.0 * beatsPerBar)));
        double midiSum = 0.0;
        int pitched = 0;
        for (int i = 0; i < count; ++i) {
            const auto& note = sequence.notes[static_cast<size_t>(i)];
            if (!std::isfinite(note.frequency) || note.frequency <= 0.0) continue;
            midiSum += 69.0 + 12.0 * std::log2(note.frequency / 440.0);
            ++pitched;
        }
        if (pitched > 0) profile.meanMidi = float(midiSum / pitched);
        int activeSamples = 0;
        constexpr int profileSamples = 64;
        for (int sample = 0; sample < profileSamples; ++sample) {
            const double time = sequence.end * (sample + 0.5) / profileSamples;
            for (int i = 0; i < count; ++i) {
                const auto& note = sequence.notes[static_cast<size_t>(i)];
                if (note.duration > 0.0 && time >= note.start && time < note.start + note.duration) {
                    ++activeSamples;
                    break;
                }
            }
        }
        profile.activeFraction = float(activeSamples) / profileSamples;
        profile.hasActivity = true;
        profile.restLike = count == 0 || profile.activeFraction < 0.08f;
    }

    if (cell.waveformCount > 0) {
        const int count = std::clamp(cell.waveformCount, 0, Cell::waveformSize);
        double squareSum = 0.0;
        for (int i = 0; i < count; ++i) {
            const double value = double(cell.waveform[static_cast<size_t>(i)]) / 32767.0;
            squareSum += value * value;
        }
        profile.rms = float(std::sqrt(squareSum / std::max(1, count)));
        profile.hasLevel = true;
        if (!profile.hasActivity) {
            const auto threshold = std::max(256, int(std::lround(profile.rms * 32767.0f * 0.15f)));
            int activeSamples = 0;
            for (int i = 0; i < count; ++i)
                if (std::abs(int(cell.waveform[static_cast<size_t>(i)])) > threshold) ++activeSamples;
            profile.activeFraction = float(activeSamples) / std::max(1, count);
            profile.hasActivity = true;
            profile.restLike = profile.activeFraction < 0.08f;
        }
    } else if (sequence.end > 0.0) {
        double squareSum = 0.0;
        constexpr int profileSamples = 64;
        for (int i = 0; i < profileSamples; ++i) {
            const float value = sampleAt(sequence, sequence.end * (i + 0.5) / profileSamples,
                                         WaveMode::quantizedSine);
            squareSum += double(value) * value;
        }
        profile.rms = float(std::sqrt(squareSum / profileSamples));
        profile.hasLevel = true;
    }
    return profile;
}

std::uint32_t storeCell(Song& song, const Inscription& source, std::string name) {
    if (!validInscription(source) || name.size() > 128 ||
        name.find_first_of("\r\n\0", 0, 3) != std::string::npos ||
        song.cellCount < 0 || song.cellCount >= int(song.bank.size()) ||
        song.nextCellId == 0 || song.nextCellId == std::numeric_limits<std::uint32_t>::max()) return 0;
    const auto id = song.nextCellId++;
    auto& cell = song.bank[static_cast<size_t>(song.cellCount++)];
    cell = {};
    cell.id = id;
    cell.name = std::move(name);
    cell.inscription = source;
    return id;
}

std::uint32_t storeGeneratedCell(Song& song, std::uint64_t seed,
                                 std::uint32_t variation, std::string name,
                                 const std::array<std::int16_t, Cell::waveformSize>& waveform,
                                 std::uint32_t phraseVersion, std::uint8_t phraseSectionMask) {
    if (name.size() > 128 || name.find_first_of("\r\n\0", 0, 3) != std::string::npos ||
        (phraseVersion < 1 || phraseVersion > 9) || phraseSectionMask == 0 || phraseSectionMask > 0x0f ||
        song.cellCount < 0 || song.cellCount >= int(song.bank.size()) ||
        song.nextCellId == 0 || song.nextCellId == std::numeric_limits<std::uint32_t>::max()) return 0;
    const auto id = song.nextCellId++;
    auto& cell = song.bank[static_cast<size_t>(song.cellCount++)];
    Inscription marker;
    marker.ascii = "GEN";
    cell = {};
    cell.id = id;
    cell.name = std::move(name);
    cell.inscription = marker;
    cell.generatedPhrase = true;
    cell.phraseSeed = seed;
    cell.phraseVariation = variation;
    cell.phraseVersion = phraseVersion;
    cell.phraseSectionMask = phraseSectionMask;
    cell.waveform = waveform;
    cell.waveformCount = Cell::waveformSize;
    return id;
}

std::uint32_t storeFormulaCell(Song& song, std::string expression, std::string name,
                               const std::array<std::int16_t, Cell::waveformSize>& waveform,
                               FormulaOrigin origin) {
    if (expression.empty() || expression.size() > 128 ||
        expression.find_first_of("\r\n\0", 0, 3) != std::string::npos ||
        name.size() > 128 || name.find_first_of("\r\n\0", 0, 3) != std::string::npos ||
        origin.author.size() > 128 || origin.source.size() > 256 ||
        origin.sourceId.size() > 64 || origin.license.size() > 64 ||
        origin.author.find_first_of("\r\n\0", 0, 3) != std::string::npos ||
        origin.source.find_first_of("\r\n\0", 0, 3) != std::string::npos ||
        origin.sourceId.find_first_of("\r\n\0", 0, 3) != std::string::npos ||
        origin.license.find_first_of("\r\n\0", 0, 3) != std::string::npos ||
        (origin.parentA != 0 && findCell(song, origin.parentA) == nullptr) ||
        (origin.parentB != 0 && findCell(song, origin.parentB) == nullptr) ||
        song.cellCount < 0 || song.cellCount >= int(song.bank.size()) ||
        song.nextCellId == 0 || song.nextCellId == std::numeric_limits<std::uint32_t>::max()) return 0;
    BytebeatProgram program;
    std::string error;
    if (!compileBytebeat(expression, program, error)) return 0;
    const auto id = song.nextCellId++;
    auto& cell = song.bank[static_cast<size_t>(song.cellCount++)];
    cell = {};
    cell.id = id;
    cell.name = std::move(name);
    cell.inscription.ascii = "FML";
    cell.bytebeatFormula = true;
    cell.formulaExpression = std::move(expression);
    cell.formulaOrigin = std::move(origin);
    cell.waveform = waveform;
    cell.waveformCount = Cell::waveformSize;
    return id;
}

std::uint32_t storeFormulaEventCell(Song& song, std::string expression, std::string name,
                                    const std::array<std::int16_t, Cell::waveformSize>& waveform,
                                    const Sequence& events, FormulaScalePreset scale,
                                    FormulaScaleMotion motion, int tonicSemitones,
                                    double bpm, FormulaOrigin origin, int progression, int harmonicBars, double swing, int subdivision,
                                    const std::array<int, 4>& chordRoots, bool syncopation, int pulsesPerBar) {
    if (!std::isfinite(swing) || swing < 0 || swing > 0.5 ||
        progression < 0 || progression > 2 || harmonicBars < 1 || harmonicBars > 2 ||
        events.count < 1 || events.count > int(events.notes.size()) ||
        !std::isfinite(events.end) || events.end <= 0.0 || !std::isfinite(bpm) || bpm < 40.0 || bpm > 240.0 ||
        int(scale) < int(FormulaScalePreset::major) || int(scale) > int(FormulaScalePreset::prometheus) ||
        tonicSemitones < 0 || tonicSemitones > 11 ||
        int(motion) < int(FormulaScaleMotion::follow) || int(motion) > int(FormulaScaleMotion::chordGuide)) return 0;
    for (int i = 0; i < events.count; ++i) {
        const auto& note = events.notes[static_cast<size_t>(i)];
        if (!std::isfinite(note.start) || !std::isfinite(note.duration) ||
            !std::isfinite(note.frequency) || !std::isfinite(note.gain) ||
            note.start < 0.0 || note.duration <= 0.0 || note.start + note.duration > events.end ||
            note.frequency < 1.0 || note.frequency > 20000.0 || note.gain < 0.0f || note.gain > 1.0f ||
            (note.role != VoiceRole::lead && note.role != VoiceRole::bass && note.role != VoiceRole::harmony) ||
            !std::isfinite(note.pulseDuty) || note.pulseDuty < 0.05f || note.pulseDuty > 0.95f) return 0;
    }
    const auto id = storeFormulaCell(song, std::move(expression), std::move(name), waveform, std::move(origin));
    if (id == 0) return 0;
    auto& cell = song.bank[static_cast<size_t>(song.cellCount - 1)];
    cell.formulaEventPhrase = true;
    cell.formulaScale = scale;
    cell.formulaTonic = static_cast<std::uint8_t>(tonicSemitones);
    cell.formulaMotion = motion;
    cell.formulaEvents = events;
    cell.formulaEventBpm = bpm;
    cell.formulaProgression = progression;
    cell.formulaHarmonicBars = harmonicBars;
    cell.formulaSwing = swing;
    cell.formulaSubdivision = subdivision;
    cell.formulaSyncopation = syncopation;
    cell.formulaPulsesPerBar = pulsesPerBar;
    cell.formulaChordRoots = chordRoots;
    return id;
}

bool updateCell(Song& song, std::uint32_t id, const Inscription& source) {
    if (id == 0 || !validInscription(source)) return false;
    for (int index = 0; index < std::clamp(song.cellCount, 0, int(song.bank.size())); ++index) {
        auto& cell = song.bank[static_cast<size_t>(index)];
        if (cell.id != id) continue;
        if (cell.revision == std::numeric_limits<std::uint32_t>::max()) return false;
        cell.inscription = source;
        cell.generatedPhrase = false;
        cell.bytebeatFormula = false;
        cell.formulaEventPhrase = false;
        cell.formulaExpression.clear();
        cell.formulaScale = FormulaScalePreset::major;
        cell.formulaTonic = 0;
        cell.formulaMotion = FormulaScaleMotion::follow;
        cell.formulaEvents = {};
        cell.formulaEventBpm = 120.0;
        cell.formulaProgression = 0;
        cell.formulaHarmonicBars = 1;
        cell.formulaSwing = 0.0;
        cell.formulaSubdivision = 0;
        cell.formulaSyncopation = false;
        cell.formulaPulsesPerBar = 0;
        cell.formulaChordRoots = {{-1, -1, -1, -1}};
        cell.sectionEdits = {};
        cell.inputInterpretation = 0;
        cell.inputInterpretationStrength = 1.0f;
        cell.inputGestureCurve = {};
        cell.inputGestureRangeSemitones = 12;
        cell.formulaOrigin = {};
        cell.phraseSeed = 0;
        cell.phraseVariation = 0;
        cell.phraseVersion = 1;
        cell.phraseMeters = {};
        cell.phraseSectionBars = {{1,1,1,1}};
        ++cell.revision;
        return true;
    }
    return false;
}

double placementStartSeconds(const Song& song, const Placement& placement) noexcept {
    const double bpm = std::clamp(std::isfinite(song.bpm) ? song.bpm : 120.0, 40.0, 240.0);
    return std::clamp(placement.startBeat, 0, 4096) * 60.0 / bpm;
}

double placementLengthSeconds(const Song& song, const Placement& placement) noexcept {
    const double bpm = std::clamp(std::isfinite(song.bpm) ? song.bpm : 120.0, 40.0, 240.0);
    const int beats = std::clamp(song.beatsPerBar, 2, 7);
    int span = placement.span == CellSpan::beat ? 1 :
                     placement.span == CellSpan::bar ? beats :
                     placement.span == CellSpan::twoBars ? beats * 2 : beats * 4;
    const auto* cell = findCell(song, placement.cellId);
    if (cell && cell->generatedPhrase && cell->phraseVersion >= 6 && placement.span == CellSpan::fourBars) {
        span = 0;
        for (int weight : phraseSectionWeights(cell->phraseMeters, cell->phraseSectionBars, beats)) span += weight;
    }
    const int length = placement.lengthBeats > 0 ? std::clamp(placement.lengthBeats, 1, 4096)
                                                  : std::clamp(placement.repeats, 1, 8) * span;
    return length * 60.0 / bpm;
}

bool placeCell(Song& song, Placement placement) noexcept {
    if (placement.stem == -1) placement.stem = song.placementCount % Song::stemCount;
    if (placement.cellId == 0 || !findCell(song, placement.cellId) ||
        placement.startBeat < 0 || placement.startBeat > 4096 ||
        (placement.span != CellSpan::beat && placement.span != CellSpan::bar &&
         placement.span != CellSpan::fourBars && placement.span != CellSpan::twoBars) ||
        placement.repeats < 1 || placement.repeats > 8 ||
        placement.sourceStartBeat < 0 || placement.sourceStartBeat > 4096 ||
        placement.lengthBeats < 0 || placement.lengthBeats > 4096 ||
        (placement.lengthBeats > 0 && placement.startBeat + placement.lengthBeats > 4096) ||
        placement.stem < 0 || placement.stem >= Song::stemCount ||
        song.placementCount < 0 || song.placementCount >= int(song.grid.size())) return false;
    song.grid[static_cast<size_t>(song.placementCount++)] = placement;
    return true;
}

PlacementSuggestion suggestPlacement(const Song& song, std::uint32_t cellId,
                                     CellSpan span, int repeats, int fromBeat,
                                     bool balanceSound) noexcept {
    PlacementSuggestion best;
    const auto* sourceCell = findCell(song, cellId);
    if (sourceCell == nullptr || song.placementCount < 0 ||
        song.placementCount >= int(song.grid.size()) || repeats < 1 || repeats > 8 ||
        fromBeat < 0 || fromBeat > 4096 ||
        (span != CellSpan::beat && span != CellSpan::bar && span != CellSpan::fourBars &&
         span != CellSpan::twoBars)) return best;
    const int beats = std::clamp(song.beatsPerBar, 2, 7);
    const auto sourceProfile = describeCellAudio(*sourceCell, song.bpm, beats);
    std::array<CellAudioProfile, 64> profiles{};
    for (int i = 0; i < song.cellCount; ++i)
        profiles[static_cast<size_t>(i)] = describeCellAudio(song.bank[static_cast<size_t>(i)], song.bpm, beats);
    const int length = int(std::lround(placementLengthSeconds(song, {cellId, 0, span, repeats}) * song.bpm / 60.0));
    const int anchor = ((fromBeat + beats - 1) / beats) * beats;
    int bestScore = std::numeric_limits<int>::max();
    for (int bar = 0; bar <= 16; ++bar) {
        const int start = anchor + bar * beats;
        if (start + length > 4096) break;
        for (int stem = 0; stem < Song::stemCount; ++stem) {
            bool blocked = false;
            bool motif = false;
            bool duplicateOverlap = false;
            bool roleOnStem = false;
            bool otherRoleOnStem = false;
            bool sameRoleConcurrent = false;
            const auto intendedStemRole = song.stemRole[static_cast<size_t>(stem)];
            const bool stemRoleMatch = sourceCell->role != CellRole::free &&
                                       intendedStemRole == sourceCell->role;
            const bool stemRoleConflict = sourceCell->role != CellRole::free &&
                                          intendedStemRole != CellRole::free && !stemRoleMatch;
            std::array<bool, Song::stemCount> concurrent{};
            int soundOverlapPenalty = 0;
            for (int i = 0; i < song.placementCount; ++i) {
                const auto& placed = song.grid[static_cast<size_t>(i)];
                const auto* placedCell = findCell(song, placed.cellId);
                const int placedLength = int(std::lround(placementLengthSeconds(song, placed) * song.bpm / 60.0));
                const int end = placed.startBeat + placedLength;
                if (sourceCell->role != CellRole::free && placedCell != nullptr &&
                    placedCell->role != CellRole::free && placed.stem == stem &&
                    end <= start && start - end <= beats * 16) {
                    if (placedCell->role == sourceCell->role) roleOnStem = true;
                    else otherRoleOnStem = true;
                }
                if (placed.stem == stem && placed.cellId == cellId && end <= start &&
                    start - end <= beats * 4) motif = true;
                if (placed.startBeat >= start + length || end <= start) continue;
                if (placed.cellId == cellId) duplicateOverlap = true;
                if (sourceCell->role != CellRole::free && placedCell != nullptr &&
                    placedCell->role == sourceCell->role && placed.stem != stem)
                    sameRoleConcurrent = true;
                if (balanceSound && placedCell != nullptr && placed.stem != stem && sourceProfile.hasActivity &&
                    sourceProfile.hasLevel && !sourceProfile.restLike) {
                    const auto profileIndex = static_cast<size_t>(placedCell - song.bank.data());
                    const auto& otherProfile = profiles[profileIndex];
                    if (otherProfile.hasActivity && otherProfile.hasLevel && !otherProfile.restLike) {
                        const float sourceDensity = sourceProfile.hasEvents
                            ? std::clamp(sourceProfile.eventsPerBar / 8.0f, 0.0f, 1.0f) : 0.5f;
                        const float otherDensity = otherProfile.hasEvents
                            ? std::clamp(otherProfile.eventsPerBar / 8.0f, 0.0f, 1.0f) : 0.5f;
                        const float overlap = sourceDensity * otherDensity *
                            sourceProfile.activeFraction * otherProfile.activeFraction *
                            std::sqrt(std::clamp(sourceProfile.rms, 0.0f, 1.0f) *
                                      std::clamp(otherProfile.rms, 0.0f, 1.0f));
                        soundOverlapPenalty += int(std::lround(24.0f * overlap));
                    }
                }
                if (placed.stem == stem) blocked = true;
                else if (placed.stem >= 0 && placed.stem < Song::stemCount)
                    concurrent[static_cast<size_t>(placed.stem)] = true;
            }
            if (blocked) continue;
            const int active = int(concurrent[0]) + int(concurrent[1]) + int(concurrent[2]);
            const int score = bar * 10 + active * 12 + (duplicateOverlap ? 80 : 0) +
                              (sameRoleConcurrent ? 80 : 0) + (otherRoleOnStem ? 8 : 0) +
                              soundOverlapPenalty + (stemRoleConflict ? 18 : 0) + stem * 2 -
                              (motif ? 8 : 0) - (roleOnStem ? 20 : 0) - (stemRoleMatch ? 24 : 0);
            if (score < bestScore) {
                bestScore = score;
                best = {true, start, stem, active, motif, roleOnStem, sameRoleConcurrent};
                best.stemRoleMatch = stemRoleMatch;
                best.profile = sourceProfile;
            }
        }
    }
    return best;
}

}
