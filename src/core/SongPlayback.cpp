// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Lúcio de Araújo
#include "SongPlayback.hpp"
#include "FormulaSeed.hpp"
#include <algorithm>
#include <cmath>

namespace risco {

SongPlayback prepareSongPlayback(const Song& song) {
    SongPlayback playback;
    playback.bitDepth = song.bitDepth == 16 ? 16 : 8;
    playback.timbreMode = std::clamp(song.timbreMode, 0, 6);
    playback.ayNoisePeriod = std::clamp(song.ayNoisePeriod, 1, 31);
    playback.ayEnvelopeMode = std::clamp(song.ayEnvelopeMode, 0, 4);
    playback.ayEnvelopeRamp = std::clamp(std::isfinite(song.ayEnvelopeRamp) ? song.ayEnvelopeRamp : 0.5f, 0.125f, 2.0f);
    playback.historicalAySettings.texture = std::clamp(song.historicalAyTexture, 0, 2);
    playback.historicalAySettings.noisePeriod = std::clamp(song.historicalAyNoisePeriod, 1, 31);
    playback.historicalAySettings.envelope = std::clamp(song.historicalAyEnvelope, 0, 4);
    playback.historicalAySettings.envelopeRamp = std::clamp(std::isfinite(song.historicalAyEnvelopeRamp)
        ? song.historicalAyEnvelopeRamp : 0.5f, 0.125f, 2.0f);
    const double barSeconds = std::clamp(song.beatsPerBar, 2, 7) *
                              60.0 / std::clamp(song.bpm, 40.0, 240.0);
    const int count = std::clamp(song.placementCount, 0, int(song.grid.size()));
    for (int i = 0; i < count; ++i) {
        const auto& placement = song.grid[static_cast<size_t>(i)];
        const auto* cell = findCell(song, placement.cellId);
        if (!cell || placement.repeats < 1 || placement.repeats > 8 ||
            placement.startBeat < 0 || placement.startBeat > 4096 ||
            placement.sourceStartBeat < 0 || placement.sourceStartBeat > 4096 ||
            placement.lengthBeats < 0 || placement.lengthBeats > 4096 ||
            (placement.lengthBeats > 0 && placement.startBeat + placement.lengthBeats > 4096) ||
            placement.stem < 0 || placement.stem >= Song::stemCount ||
            (placement.span != CellSpan::beat && placement.span != CellSpan::bar &&
             placement.span != CellSpan::fourBars && placement.span != CellSpan::twoBars)) continue;
        CellPlan plan;
        plan.bpm = song.bpm;
        plan.beatsPerBar = song.beatsPerBar;
        plan.span = placement.span;
        plan.repeats = 1;
        auto& target = playback.placements[static_cast<size_t>(playback.count)];
        target.cellId = placement.cellId;
        target.gridIndex = i;
        target.stem = placement.stem;
        target.bytebeatFormula = cell->bytebeatFormula && !cell->formulaEventPhrase;
        target.sourceStart = placement.sourceStartBeat * 60.0 / std::clamp(song.bpm, 40.0, 240.0);
        if (cell->bytebeatFormula && cell->formulaEventPhrase) {
            target.cell = cell->formulaEvents;
            const double tempoScale = cell->formulaEventBpm / std::clamp(song.bpm, 40.0, 240.0);
            target.cell.end *= tempoScale;
            for (int n = 0; n < target.cell.count; ++n) {
                auto& note = target.cell.notes[static_cast<size_t>(n)];
                note.start *= tempoScale;
                note.duration *= tempoScale;
            }
            target.duration = target.cell.end;
        } else if (cell->bytebeatFormula) {
            std::string error;
            if (!compileBytebeat(cell->formulaExpression, target.formula, error)) continue;
            target.duration = placementLengthSeconds(song,
                Placement{placement.cellId, placement.startBeat, placement.span, 1});
        } else {
            auto source = cell->generatedPhrase
                ? phraseFromSeed(cell->phraseSeed, cell->phraseVariation, cell->phraseVersion,
                                 song.bpm, cell->phraseSectionMask, song.beatsPerBar, song.compoundSixEight, cell->phraseMeters)
                : interpretInput(composeInput(cell->inscription), cell->inputInterpretation,
                    cell->inputInterpretationStrength,cell->inputGestureCurve,cell->inputGestureRangeSemitones);
            if (cell->generatedPhrase) source = stretchSections(source,
                cell->phraseVersion >= 6 ? phraseMeterWeights(cell->phraseMeters, song.beatsPerBar)
                                         : std::array<int,4>{{1,1,1,1}},
                cell->phraseSectionBars);
            target.cell = cell->generatedPhrase && cell->phraseVersion >= 6 && placement.span == CellSpan::fourBars ? source : arrange(source, plan);
            target.cell = editSections(target.cell, cell->sectionEdits, cell->phraseVersion == 4 ? 2 : 4,
                cell->phraseVersion >= 6 ? phraseSectionWeights(cell->phraseMeters, cell->phraseSectionBars, song.beatsPerBar)
                                         : cell->phraseSectionBars);
            target.duration = target.cell.end;
        }
        target.start = placementStartSeconds(song, placement);
        target.repeats = placement.repeats;
        if (target.duration <= 0.0 || !std::isfinite(target.duration)) continue;
        target.playLength = placement.lengthBeats > 0
            ? placementLengthSeconds(song, placement)
            : target.duration * placement.repeats;
        playback.end = std::max(playback.end, target.start + target.playLength);
        ++playback.count;
    }
    // All destination cells must exist before checking their internal rests.
    // Analytic periodic intersections avoid expanding repeated/cropped cells.
    const auto gapCheck = [&]() {
        if (playback.responseGapChecks >= SongPlayback::gapCheckLimit) {
            playback.responseGapBudgetExceeded = true;
            return false;
        }
        ++playback.responseGapChecks;
        return true;
    };
    const auto noteOccupied = [&](double begin, double end, int stem) {
        if (playback.responseGapBudgetExceeded) return true;
        for (int p = 0; p < playback.count; ++p) {
            if (!gapCheck()) return true;
            const auto& other = playback.placements[static_cast<size_t>(p)];
            if (other.stem != stem || begin >= other.start + other.playLength || end <= other.start) continue;
            if (other.bytebeatFormula) return true; // No inferred silence in arbitrary formulas.
            const double from = std::max(begin, other.start) - other.start + other.sourceStart;
            const double to = std::min(end, other.start + other.playLength) - other.start + other.sourceStart;
            for (int n = 0; n < other.cell.count; ++n) {
                if (!gapCheck()) return true;
                const auto& note = other.cell.notes[static_cast<size_t>(n)];
                if (note.gain <= 0 || note.frequency <= 0 || note.duration <= 0) continue;
                const double cycle = std::floor((from - note.start) / other.duration);
                const double attack = note.start + cycle * other.duration;
                if ((from < attack + note.duration && to > attack) ||
                    (from < attack + other.duration + note.duration && to > attack + other.duration)) return true;
            }
        }
        for (int r = 0; r < playback.responseCount; ++r) {
            const auto& previous = playback.responses[static_cast<size_t>(r)];
            if (previous.stem != stem) continue;
            for (int n = 0; n < previous.cell.count; ++n) {
                if (!gapCheck()) return true;
                const auto& note = previous.cell.notes[static_cast<size_t>(n)];
                const double attack = previous.start + note.start;
                if (note.gain > 0 && begin < attack + note.duration && end > attack) return true;
            }
        }
        return false;
    };
    std::array<int,128> responseOrder{};
    for (int p = 0; p < playback.count; ++p) responseOrder[static_cast<size_t>(p)] = p;
    if (song.responsePolicy == 7) std::sort(responseOrder.begin(), responseOrder.begin() + playback.count, [&](int a, int b) {
        const auto& x = playback.placements[static_cast<size_t>(a)];
        const auto& y = playback.placements[static_cast<size_t>(b)];
        if (x.start != y.start) return x.start < y.start;
        if (x.stem != y.stem) return x.stem < y.stem;
        if (x.cellId != y.cellId) return x.cellId < y.cellId;
        return x.gridIndex < y.gridIndex;
    });
    for (int p = 0; p < playback.count; ++p) {
        auto& target = playback.placements[static_cast<size_t>(responseOrder[static_cast<size_t>(p)])];
        const auto& placement = song.grid[static_cast<size_t>(target.gridIndex)];
        const auto* cell = findCell(song,target.cellId);
        if (!cell) continue;
        if (song.interStemResponse > 0 && song.interStemResponse <= 9 &&
            target.stem == (song.interStemResponse - 1) / 3 && !target.bytebeatFormula &&
            placement.sourceStartBeat == 0 && placement.lengthBeats == 0 &&
            ((cell->generatedPhrase && cell->phraseVersion >= 6) ||
             (target.duration >= 2.0 * barSeconds && target.playLength >= 2.0 * barSeconds)) &&
            playback.responseCount < int(playback.responses.size())) {
            PlaybackResponse response;
            response.stem = (target.stem + 1) % Song::stemCount;
            constexpr double semitones[] = {-5.0, 5.0, -12.0};
            const int interval = (song.interStemResponse - 1) % 3;
            const double transpose = std::pow(2.0, semitones[interval] / 12.0);
            const int sourceNotes = std::clamp(target.cell.count, 0, int(target.cell.notes.size()));
            const bool nativeSections = cell->generatedPhrase && cell->phraseVersion >= 6;
            std::array<double, 5> boundaries{};
            const auto weights = phraseSectionWeights(cell->phraseMeters, cell->phraseSectionBars, song.beatsPerBar);
            int totalWeight = 0;
            for (int weight : weights) totalWeight += weight;
            int accumulated = 0;
            for (int s = 0; s < 4; ++s) {
                accumulated += weights[static_cast<size_t>(s)];
                boundaries[static_cast<size_t>(s + 1)] = target.duration * accumulated / totalWeight;
            }
            const int sourceBars = nativeSections ? 4 : std::clamp(int(std::ceil(target.duration / barSeconds)), 1, 4);
            for (int sourceBar = 0; sourceBar < sourceBars; ++sourceBar) {
                const int sectionIntent = nativeSections ? song.responseSectionIntent[static_cast<size_t>(sourceBar)] : 0;
                if (sectionIntent == 5) { ++playback.responsesSkipped; continue; }
                const double sourceStart = nativeSections ? boundaries[static_cast<size_t>(sourceBar)] : sourceBar * barSeconds;
                const double sourceLength = nativeSections ? boundaries[static_cast<size_t>(sourceBar + 1)] - sourceStart : barSeconds;
                const double answerStart = sourceStart + sourceLength;
                const double answerLength = nativeSections ? (sourceBar < 3 ?
                    boundaries[static_cast<size_t>(sourceBar + 2)] - answerStart : boundaries[1]) : barSeconds;
                const double timeScale = answerLength / sourceLength;
                // Keep the answer inside this placement's audible extent.
                if (answerStart + answerLength > target.playLength + 1.0e-9) break;
                if (song.responsePolicy > 0 && song.responsePolicy < 7) {
                    const double begin = target.start + answerStart;
                    const double end = begin + answerLength;
                    bool occupied = false;
                    for (int g = 0; g < count; ++g) {
                        const auto& other = song.grid[static_cast<size_t>(g)];
                        if (other.stem != response.stem) continue;
                        const double first = placementStartSeconds(song, other);
                        const double last = first + placementLengthSeconds(song, other);
                        if (begin < last && end > first) { occupied = true; break; }
                    }
                    for (int r = 0; r < playback.responseCount; ++r) {
                        const auto& previous = playback.responses[static_cast<size_t>(r)];
                        if (previous.stem == response.stem && begin < previous.start + previous.cell.end && end > previous.start)
                            occupied = true;
                    }
                    if (occupied) { ++playback.responsesSkipped; continue; }
                }
                std::array<int,64> candidates{};
                int candidateCount = 0;
                for (int n = 0; n < sourceNotes; ++n) {
                    const auto& call = target.cell.notes[static_cast<size_t>(n)];
                    if (!std::isfinite(call.start) || !std::isfinite(call.duration) ||
                        !std::isfinite(call.frequency) || !std::isfinite(call.gain) ||
                        call.start < sourceStart || call.start >= sourceStart + sourceLength ||
                        call.duration <= 0.0 || call.frequency <= 0.0f || call.gain <= 0.0f) continue;
                    candidates[static_cast<size_t>(candidateCount++)] = n;
                }
                if (candidateCount == 0) continue;
                std::array<bool,64> motifSelected{};
                if (song.responseMotif == 0) {
                    for (int c = 0; c < candidateCount; ++c) motifSelected[static_cast<size_t>(candidates[static_cast<size_t>(c)])] = true;
                } else if (song.responseMotif == 1) {
                    std::array<bool,4> occupiedGroups{};
                    for (int c = 0; c < candidateCount; ++c) {
                        const int n = candidates[static_cast<size_t>(c)];
                        const auto& call = target.cell.notes[static_cast<size_t>(n)];
                        const int group = std::clamp(int(4*(call.start-sourceStart)/sourceLength),0,3);
                        if (!occupiedGroups[static_cast<size_t>(group)]) {
                            occupiedGroups[static_cast<size_t>(group)] = true;
                            motifSelected[static_cast<size_t>(n)] = true;
                        }
                    }
                } else {
                    int lowest = candidates[0], highest = candidates[0];
                    for (int c = 1; c < candidateCount; ++c) {
                        const int n = candidates[static_cast<size_t>(c)];
                        if (target.cell.notes[static_cast<size_t>(n)].frequency < target.cell.notes[static_cast<size_t>(lowest)].frequency) lowest = n;
                        if (target.cell.notes[static_cast<size_t>(n)].frequency > target.cell.notes[static_cast<size_t>(highest)].frequency) highest = n;
                    }
                    motifSelected[static_cast<size_t>(candidates[0])] = true;
                    motifSelected[static_cast<size_t>(candidates[static_cast<size_t>(candidateCount-1)])] = true;
                    motifSelected[static_cast<size_t>(lowest)] = motifSelected[static_cast<size_t>(highest)] = true;
                }
                std::array<int,64> motifNotes{};
                int motifCount = 0;
                for (int c = 0; c < candidateCount; ++c) {
                    const int n = candidates[static_cast<size_t>(c)];
                    if (motifSelected[static_cast<size_t>(n)]) motifNotes[static_cast<size_t>(motifCount++)] = n;
                }
                std::array<bool,64> selected{};
                const int stride = song.responseDensity == 2 ? 4 : song.responseDensity == 1 ? 2 : 1;
                for (int m = 0; m < motifCount; ++m)
                    if (m % stride == 0 || m == motifCount-1) selected[static_cast<size_t>(motifNotes[static_cast<size_t>(m)])] = true;
                double logPitchSum = 0.0;
                int pitchCount = 0;
                for (int n = 0; n < sourceNotes; ++n) {
                    if (!selected[static_cast<size_t>(n)]) continue;
                    const auto& call = target.cell.notes[static_cast<size_t>(n)];
                    logPitchSum += std::log(double(call.frequency));
                    ++pitchCount;
                }
                // Mirror the call's pitch contour around its geometric mean;
                // the selected interval then moves the answer as a whole.
                const double pitchCenter = std::exp(logPitchSum / pitchCount);
                int answerCount = 0;
                for (int n = 0; n < sourceNotes && response.cell.count < int(response.cell.notes.size()); ++n) {
                    if (!selected[static_cast<size_t>(n)]) continue;
                    const auto& call = target.cell.notes[static_cast<size_t>(n)];
                    if (!std::isfinite(call.start) || !std::isfinite(call.duration) ||
                        !std::isfinite(call.frequency) || !std::isfinite(call.gain) ||
                        call.start < sourceStart || call.start >= sourceStart + sourceLength ||
                        call.duration <= 0.0 || call.frequency <= 0.0f || call.gain <= 0.0f) continue;
                    auto answer = call;
                    answer.start = (call.start - sourceStart) * timeScale;
                    answer.duration = std::min(answer.duration, sourceStart + sourceLength - call.start) * timeScale;
                    constexpr double minimumFrequency = 32.70319566257483; // MIDI 24.
                    constexpr double maximumFrequency = 4186.009044809578; // MIDI 108.
                    double reflected = sectionIntent == 1 ? call.frequency * transpose
                        : (pitchCenter * pitchCenter / call.frequency) * transpose;
                    if (sectionIntent == 2) reflected *= sourceBar % 2 == 0 ? 2.0 : 0.5;
                    else if (sectionIntent == 3) reflected *= std::pow(2.0, 2.0 / 12.0);
                    else if (sectionIntent == 4 && answerCount == motifCount - 1) reflected = pitchCenter * transpose;
                    answer.frequency = float(std::clamp(reflected, minimumFrequency, maximumFrequency));
                    if (song.responsePolicy >= 2) {
                        const auto role = song.stemRole[static_cast<size_t>(response.stem)];
                        const double lower = role == CellRole::bass ? 65.406391 : role == CellRole::melody ? 261.625565 : 130.812783;
                        const double upper = role == CellRole::bass ? 261.625565 : role == CellRole::melody ? 2093.004522 : 1046.502261;
                        while (answer.frequency < lower) answer.frequency *= 2;
                        while (answer.frequency > upper) answer.frequency *= 0.5;
                        answer.role = role == CellRole::bass ? VoiceRole::bass : role == CellRole::harmony ? VoiceRole::harmony : VoiceRole::lead;
                    }
                    answer.gain *= 0.72f;
                    if (sectionIntent == 3) answer.gain *= 1.08f;
                    else if (sectionIntent == 4) answer.gain *= 0.88f;
                    if (!std::isfinite(answer.frequency) || answer.duration <= 0.0) continue;
                    if (song.responsePolicy == 7 && noteOccupied(target.start + answerStart + answer.start,
                        target.start + answerStart + answer.start + answer.duration, response.stem)) {
                        ++playback.responseNotesRejected;
                        continue;
                    }
                    response.cell.notes[static_cast<size_t>(response.cell.count++)] = answer;
                    ++answerCount;
                }
                if (answerCount > 0) {
                    if (song.responseMemory > 0 && playback.responseCount > 0) {
                        const auto& previous = playback.responses[static_cast<size_t>(playback.responseCount - 1)].cell;
                        if (previous.count > 0) {
                            double ratio = 1.0;
                            if (song.responseMemory == 2) {
                                const double previousEnd = previous.notes[static_cast<size_t>(previous.count - 1)].frequency;
                                const double currentStart = response.cell.notes[0].frequency;
                                while (currentStart * ratio < previousEnd / std::sqrt(2.0)) ratio *= 2.0;
                                while (currentStart * ratio > previousEnd * std::sqrt(2.0)) ratio *= 0.5;
                            } else {
                                double previousLog = 0, currentLog = 0;
                                for (int n = 0; n < previous.count; ++n) previousLog += std::log(previous.notes[static_cast<size_t>(n)].frequency);
                                for (int n = 0; n < response.cell.count; ++n) currentLog += std::log(response.cell.notes[static_cast<size_t>(n)].frequency);
                                const double semitonesApart = 12.0 * std::abs(previousLog / previous.count - currentLog / response.cell.count) / std::log(2.0);
                                if (semitonesApart < 3.0) ratio = std::pow(2.0, (playback.responseCount % 2 == 0 ? 5.0 : -5.0) / 12.0);
                            }
                            for (int n = 0; n < response.cell.count; ++n)
                                response.cell.notes[static_cast<size_t>(n)].frequency = float(std::clamp(
                                    response.cell.notes[static_cast<size_t>(n)].frequency * ratio, 32.70319566257483, 4186.009044809578));
                        }
                    }
                    response.cell.end = answerLength;
                    response.start = target.start + answerStart;
                    response.sourceSection = nativeSections ? sourceBar : -1;
                    break; // One answer per placement, from its first active section.
                }
                if (song.responsePolicy == 7) ++playback.responsesSkipped;
            }
            if (response.cell.count > 0)
                playback.responses[static_cast<size_t>(playback.responseCount++)] = response;
        }
    }
    // Resolve context only after all placements exist: grid storage order must
    // not hide a later-listed harmonic reference. This runs off the audio thread.
    struct HarmonicContext {
        std::array<double, 64> chord{};
        int count = 0;
        std::uint32_t id = 0;
    };
    const auto contextAt = [&](double time, int excludedStem) {
        HarmonicContext result;
        int bestScore = -1;
        double bestStart = -1;
        for (int p = 0; p < playback.count; ++p) {
            const auto& reference = playback.placements[static_cast<size_t>(p)];
            if (reference.bytebeatFormula || reference.stem == excludedStem || reference.duration <= 0 ||
                time < reference.start || time >= reference.start + reference.playLength) continue;
            const double local = std::fmod(time - reference.start + reference.sourceStart, reference.duration);
            std::array<double, 64> sounding{};
            int count = 0;
            for (int n = 0; n < std::clamp(reference.cell.count, 0, int(reference.cell.notes.size())); ++n) {
                const auto& note = reference.cell.notes[static_cast<size_t>(n)];
                if ((note.role != VoiceRole::bass && note.role != VoiceRole::harmony) ||
                    !std::isfinite(note.gain) || note.gain <= 0 || local < note.start ||
                    local >= note.start + note.duration || !std::isfinite(note.frequency) || note.frequency <= 0) continue;
                sounding[static_cast<size_t>(count++)] = note.frequency;
            }
            if (count == 0) continue;
            const auto* cell = findCell(song, reference.cellId);
            const int score = (cell && cell->role == CellRole::harmony ? 2 : 0) +
                (song.stemRole[static_cast<size_t>(reference.stem)] == CellRole::harmony ? 1 : 0);
            if (score < bestScore || (score == bestScore && (reference.cellId > result.id ||
                (reference.cellId == result.id && reference.start <= bestStart)))) continue;
            bestScore = score; bestStart = reference.start; result.chord = sounding; result.count = count;
            result.id = reference.cellId;
        }
        return result;
    };
    if (song.responsePolicy >= 3) for (int r = 0; r < playback.responseCount; ++r) {
        auto& response = playback.responses[static_cast<size_t>(r)];
        auto context = song.responsePolicy == 6 ? HarmonicContext{} : contextAt(response.start, response.stem);
        response.harmonicReferenceId = context.id;
        double lastTime = song.responsePolicy == 6 ? -1 : response.start;
        double lastAttack = 0;
        for (int n = 0; n < response.cell.count; ++n)
            lastAttack = std::max(lastAttack, response.cell.notes[static_cast<size_t>(n)].start);
        const auto role = song.stemRole[static_cast<size_t>(response.stem)];
        const double lower = role == CellRole::bass ? 65.406391 : role == CellRole::melody ? 261.625565 : 130.812783;
        const double upper = role == CellRole::bass ? 261.625565 : role == CellRole::melody ? 2093.004522 : 1046.502261;
        for (int n = 0; n < response.cell.count; ++n) {
            auto& note = response.cell.notes[static_cast<size_t>(n)];
            if (song.responsePolicy == 6 && note.start != lastAttack) continue;
            const double time = response.start + note.start;
            if (song.responsePolicy >= 4 && time != lastTime) {
                context = contextAt(time, response.stem); lastTime = time;
                if (response.harmonicReferenceId == 0) response.harmonicReferenceId = context.id;
            }
            if (context.count == 0) continue; // Retain this note's role/register response.
            double best = note.frequency, distance = 1e30;
            for (int c = 0; c < context.count; ++c) for (int octave = -8; octave <= 8; ++octave) {
                const double candidate = std::ldexp(context.chord[static_cast<size_t>(c)], octave);
                if (candidate < lower || candidate > upper) continue;
                const double delta = std::abs(std::log2(candidate / note.frequency));
                if (delta < distance) { distance = delta; best = candidate; }
            }
            // A short chromatic neighbour can create early tension while long
            // voices retain support. Later attacks return to the current chord.
            const double midpoint = response.cell.end * 0.5;
            if (song.responsePolicy == 5 && lastAttack >= midpoint && note.start < midpoint &&
                note.start + note.duration <= midpoint + 1e-9) {
                double neighbour = best, neighbourDistance = 1e30;
                for (int semitone : {1, -1}) {
                    const double candidate = best * std::pow(2.0, semitone / 12.0);
                    if (candidate < lower || candidate > upper) continue;
                    bool member = false;
                    for (int c = 0; c < context.count; ++c) {
                        const double octaves = std::log2(candidate / context.chord[static_cast<size_t>(c)]);
                        if (std::abs(octaves - std::round(octaves)) < 1e-7) { member = true; break; }
                    }
                    const double delta = std::abs(std::log2(candidate / note.frequency));
                    if (!member && delta < neighbourDistance) { neighbour = candidate; neighbourDistance = delta; }
                }
                best = neighbour;
            }
            note.frequency = best;
        }
    }
    return playback;
}

float songStemDirectFormulaSampleAt(const SongPlayback& playback, double seconds, int stem,
                                    double continuousSeconds) noexcept {
    if (!std::isfinite(seconds) || seconds < 0.0 || seconds >= playback.end) return 0.0f;
    if (stem < 0 || stem >= Song::stemCount) return 0.0f;
    double sum = 0.0;
    const int count = std::clamp(playback.count, 0, int(playback.placements.size()));
    for (int i = 0; i < count; ++i) {
        const auto& placement = playback.placements[static_cast<size_t>(i)];
        if (placement.stem != stem) continue;
        const double local = seconds - placement.start;
        if (local < 0.0 || local >= placement.playLength) continue;
        if (!placement.bytebeatFormula) continue;
        const float fade = float(std::clamp(std::min(local / 0.01,
            (placement.playLength - local) / 0.01), 0.0, 1.0));
        sum += directBytebeatSample(&placement.formula,
            static_cast<std::uint32_t>(
                ((continuousSeconds >= 0.0 ? continuousSeconds : seconds) +
                 placement.formulaTimeOffset) * directBytebeatRate))
            * 0.20f * fade;
    }
    return float(sum);
}

float songStemSampleAt(const SongPlayback& playback, double seconds, int stem, WaveMode mode,
                       const BytebeatProgram* customProgram,
                       double continuousSeconds, int ayNoisePeriod, int ayEnvelopeMode, float ayEnvelopeRamp) noexcept {
    if (!std::isfinite(seconds) || seconds < 0.0 || seconds >= playback.end) return 0.0f;
    if (stem < 0 || stem >= Song::stemCount) return 0.0f;
    double sum = songStemDirectFormulaSampleAt(playback, seconds, stem, continuousSeconds);
    const int count = std::clamp(playback.count, 0, int(playback.placements.size()));
    for (int i = 0; i < count; ++i) {
        const auto& placement = playback.placements[static_cast<size_t>(i)];
        if (placement.stem != stem || placement.bytebeatFormula) continue;
        const double local = seconds - placement.start;
        if (local < 0.0 || local >= placement.playLength) continue;
        const double withinCell = std::fmod(local + placement.sourceStart, placement.duration);
        sum += sampleAt(placement.cell, withinCell, mode, customProgram, playback.bitDepth, ayNoisePeriod > 0 ? ayNoisePeriod : playback.ayNoisePeriod,
            ayEnvelopeMode >= 0 ? ayEnvelopeMode : playback.ayEnvelopeMode, ayEnvelopeRamp > 0 ? ayEnvelopeRamp : playback.ayEnvelopeRamp);
    }
    const int responseCount = std::clamp(playback.responseCount, 0, int(playback.responses.size()));
    for (int i = 0; i < responseCount; ++i) {
        const auto& response = playback.responses[static_cast<size_t>(i)];
        const double local = seconds - response.start;
        if (response.stem == stem && local >= 0.0 && local < response.cell.end)
            sum += sampleAt(response.cell, local, mode, customProgram, playback.bitDepth, ayNoisePeriod > 0 ? ayNoisePeriod : playback.ayNoisePeriod,
                ayEnvelopeMode >= 0 ? ayEnvelopeMode : playback.ayEnvelopeMode, ayEnvelopeRamp > 0 ? ayEnvelopeRamp : playback.ayEnvelopeRamp);
    }
    // The caller bounds the mix after combining stems and applying their pans.
    return float(sum);
}

float songSampleAt(const SongPlayback& playback, double seconds, WaveMode mode,
                   const BytebeatProgram* customProgram,
                   double continuousSeconds) noexcept {
    double sum = 0.0;
    for (int stem = 0; stem < Song::stemCount; ++stem)
        sum += songStemSampleAt(playback, seconds, stem, mode, customProgram, continuousSeconds);
    return float(std::clamp(sum, -1.0, 1.0));
}

}
