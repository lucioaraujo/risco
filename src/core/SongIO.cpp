// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Lúcio de Araújo
#include "SongIO.hpp"
#include "CompositionSeed.hpp"
#include "BytebeatExpression.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <istream>
#include <limits>
#include <memory>
#include <ostream>
#include <sstream>
#include <string>
#include <utility>

namespace risco {
namespace {
bool line(std::istream& in, std::string& value) {
    value.clear();
    // A CELL record can contain 16 KiB of quoted UTF-8 inscription text.
    for (int count = 0; count < 20480; ++count) {
        const int next = in.get();
        if (next == std::char_traits<char>::eof()) return !value.empty() && in.eof();
        if (next == '\n') return true;
        if (next == '\0') return false;
        value.push_back(static_cast<char>(next));
    }
    return false;
}
bool complete(std::istringstream& parser) {
    parser >> std::ws;
    return parser.eof();
}
bool safeText(const std::string& value, size_t limit) {
    return value.size() <= limit && value.find_first_of("\r\n\0", 0, 3) == std::string::npos;
}
bool validFormulaEvents(const Cell& cell) {
    if (!validPhraseMeters(cell.phraseMeters)) return false;
    if (cell.formulaPulsesPerBar != 0 && cell.formulaPulsesPerBar != 2 && cell.formulaPulsesPerBar != 3 && cell.formulaPulsesPerBar != 5 && cell.formulaPulsesPerBar != 7) return false;
    if (cell.inputInterpretation < 0 || cell.inputInterpretation > 13 ||
        ((cell.inputInterpretation >= 8 && cell.inputInterpretation <= 13) && !cell.inputGestureCurve.enabled) ||
        !validTemporalGestureRange(cell.inputGestureRangeSemitones) ||
        !validTemporalGestureCurve(cell.inputGestureCurve) || !safeText(cell.importProvenance, 2048)) return false;
    if (!std::isfinite(cell.inputInterpretationStrength) || cell.inputInterpretationStrength < 0 || cell.inputInterpretationStrength > 1) return false;
    if (std::any_of(cell.formulaChordRoots.begin(), cell.formulaChordRoots.end(), [](int degree) { return degree < -2 || degree > 11; })) return false;
    if (!validSectionEdits(cell.sectionEdits) ||
        (cell.formulaSubdivision != 0 && cell.formulaSubdivision != 1 && cell.formulaSubdivision != 2 &&
         cell.formulaSubdivision != 3 && cell.formulaSubdivision != 4 && cell.formulaSubdivision != 6)) return false;
    if (!std::isfinite(cell.formulaSwing) || cell.formulaSwing < 0 || cell.formulaSwing > 0.5) return false;
    if (cell.formulaProgression < 0 || cell.formulaProgression > 2 ||
        cell.formulaHarmonicBars < 1 || cell.formulaHarmonicBars > 2) return false;
    const auto& sequence = cell.formulaEvents;
    if (cell.formulaTonic > 11) return false;
    if (!cell.formulaEventPhrase)
        return sequence.count == 0 && sequence.end == 0.0 && sequence.repeatPeriod == 0.0 &&
               cell.formulaEventBpm == 120.0;
    if (sequence.count < 1 || sequence.count > int(sequence.notes.size()) ||
        !std::isfinite(sequence.end) || sequence.end <= 0.0 ||
        !std::isfinite(sequence.repeatPeriod) || sequence.repeatPeriod < 0.0 ||
        !std::isfinite(cell.formulaEventBpm) || cell.formulaEventBpm < 40.0 || cell.formulaEventBpm > 240.0 ||
        int(cell.formulaScale) < int(FormulaScalePreset::major) ||
        int(cell.formulaScale) > int(FormulaScalePreset::prometheus) ||
        int(cell.formulaMotion) < int(FormulaScaleMotion::follow) ||
        int(cell.formulaMotion) > int(FormulaScaleMotion::chordGuide)) return false;
    for (int i = 0; i < sequence.count; ++i) {
        const auto& note = sequence.notes[static_cast<size_t>(i)];
        if (!std::isfinite(note.start) || !std::isfinite(note.duration) ||
            !std::isfinite(note.frequency) || !std::isfinite(note.gain) ||
            !std::isfinite(note.pulseDuty) || note.start < 0.0 || note.duration <= 0.0 ||
            note.start + note.duration > sequence.end || note.frequency < 1.0 ||
            note.frequency > 20000.0 || note.gain < 0.0f || note.gain > 1.0f ||
            note.pulseDuty < 0.05f || note.pulseDuty > 0.95f ||
            (note.role != VoiceRole::lead && note.role != VoiceRole::bass && note.role != VoiceRole::harmony)) return false;
    }
    return true;
}
}

static bool loadSongImpl(std::istream& in, Song& destination,
                         std::vector<NamedSongVersionRecord>* loadedVersions, int* loadedActiveVersion,
                         bool allowVersions);

bool saveSong(std::ostream& out, const Song& song,
              const std::vector<NamedSongVersionRecord>& versions, int activeVersion) {
    if (versions.size() > maxNamedSongVersions || activeVersion < -1 ||
        activeVersion >= int(versions.size())) return false;
    size_t versionBytes = 0;
    std::vector<std::string> versionNames;
    for (size_t versionIndex = 0; versionIndex < versions.size(); ++versionIndex) {
        const auto& version = versions[versionIndex];
        if (!safeText(version.name,128) || version.name.empty() || version.serializedSong.empty() ||
            version.serializedSong.size() > 4 * 1024 * 1024 ||
            version.serializedSong.size() > maxNamedSongVersionBytes - versionBytes ||
            version.parentIndex < -1 || version.parentIndex >= int(versionIndex)) return false;
        for (const auto& name : versionNames)
            if (name == version.name) return false;
        std::istringstream snapshotStream(version.serializedSong);
        auto snapshot = std::make_unique<Song>();
        if (!loadSongImpl(snapshotStream,*snapshot,nullptr,nullptr,false)) return false;
        versionNames.push_back(version.name);
        versionBytes += version.serializedSong.size();
    }
    const bool validSeedRecipe = song.compositionSeedRecipeEnabled
        ? song.compositionSeedAlgorithm == compositionSeedAlgorithmV1 &&
          song.compositionSeedLevel >= 1 && song.compositionSeedLevel <= 3 &&
          (song.compositionSeedLocks & ~compositionSeedKnownLocks) == 0
        : song.compositionSeed == 0 && song.compositionSeedAlgorithm == 0 &&
          song.compositionSeedLocks == 0 && song.compositionSeedLevel == 0;
    if (!validSeedRecipe || !std::isfinite(song.bpm) || song.bpm < 40.0 || song.bpm > 240.0 ||
        song.beatsPerBar < 2 || song.beatsPerBar > 7 ||
        (song.compoundSixEight && song.beatsPerBar != 3) ||
        (song.bitDepth != 8 && song.bitDepth != 16) ||
        song.timbreMode < 0 || song.timbreMode > 6 || song.ayNoisePeriod < 1 || song.ayNoisePeriod > 31 ||
        song.ayEnvelopeMode < 0 || song.ayEnvelopeMode > 4 || !std::isfinite(song.ayEnvelopeRamp) || song.ayEnvelopeRamp < 0.125f || song.ayEnvelopeRamp > 2 ||
        song.historicalAyTexture < 0 || song.historicalAyTexture > 2 ||
        song.historicalAyNoisePeriod < 1 || song.historicalAyNoisePeriod > 31 ||
        song.historicalAyEnvelope < 0 || song.historicalAyEnvelope > 4 ||
        !std::isfinite(song.historicalAyEnvelopeRamp) || song.historicalAyEnvelopeRamp < 0.125f || song.historicalAyEnvelopeRamp > 2 ||
        song.interStemResponse < 0 || song.interStemResponse > 9 ||
        song.responsePolicy < 0 || song.responsePolicy > 7 ||
        song.responseMotif < 0 || song.responseMotif > 2 || song.responseDensity < 0 || song.responseDensity > 2 ||
        song.responseMemory < 0 || song.responseMemory > 2 ||
        std::any_of(song.responseSectionIntent.begin(), song.responseSectionIntent.end(),
            [](int intent) { return intent < 0 || intent > 5; }) ||
        std::any_of(song.stemEffect.begin(), song.stemEffect.end(), [](int effect) { return effect < 0 || effect > 9; }) ||
        std::any_of(song.stemAutomation.begin(), song.stemAutomation.end(), [](const GainAutomation& curve) {
            return std::any_of(curve.levels.begin(), curve.levels.end(), [](float v) { return !std::isfinite(v) || v < 0 || v > 1; });
        }) ||
        std::any_of(song.stemPanAutomation.begin(), song.stemPanAutomation.end(), [](const PanAutomation& curve) {
            return std::any_of(curve.levels.begin(),curve.levels.end(),[](float v) { return !std::isfinite(v) || v < -1 || v > 1; });
        }) ||
        std::any_of(song.stemFilterCutoff.begin(), song.stemFilterCutoff.end(), [](float v) { return !std::isfinite(v) || (v != 0 && (v < 80 || v > 16000)); }) ||
        std::any_of(song.stemBitcrushBits.begin(), song.stemBitcrushBits.end(), [](int v) { return v != 0 && (v < 2 || v > 16); }) ||
        std::any_of(song.stemBitcrushAutomation.begin(), song.stemBitcrushAutomation.end(), [](const ParameterAutomation& curve) {
            return std::any_of(curve.values.begin(), curve.values.end(), [](float v) { return !std::isfinite(v) || (v != 0.0f && (v < 2.0f || v > 16.0f)); });
        }) ||
        std::any_of(song.stemFilterAutomation.begin(), song.stemFilterAutomation.end(), [](const ParameterAutomation& curve) {
            return std::any_of(curve.values.begin(), curve.values.end(), [](float v) { return !std::isfinite(v) || v < 80 || v > 16000; });
        }) ||
        std::any_of(song.stemDelayMixAutomation.begin(), song.stemDelayMixAutomation.end(), [](const ParameterAutomation& curve) {
            return std::any_of(curve.values.begin(), curve.values.end(), [](float v) { return !std::isfinite(v) || v < 0 || v > 0.6f; });
        }) ||
        std::any_of(song.stemDelayFeedbackAutomation.begin(), song.stemDelayFeedbackAutomation.end(), [](const ParameterAutomation& curve) {
            return std::any_of(curve.values.begin(), curve.values.end(), [](float v) { return !std::isfinite(v) || v < 0 || v > 0.6f; });
        }) ||
        std::any_of(song.stemReverbMixAutomation.begin(), song.stemReverbMixAutomation.end(), [](const ParameterAutomation& curve) {
            return std::any_of(curve.values.begin(), curve.values.end(), [](float v) { return !std::isfinite(v) || v < 0 || v > 0.6f; });
        }) ||
        std::any_of(song.stemReverbDecayAutomation.begin(), song.stemReverbDecayAutomation.end(), [](const ParameterAutomation& curve) {
            return std::any_of(curve.values.begin(), curve.values.end(), [](float v) { return !std::isfinite(v) || (v != 0.0f && (v < 0.25f || v > 3.0f)); });
        }) ||
        std::any_of(song.stemReverbMix.begin(), song.stemReverbMix.end(), [](float v) { return !std::isfinite(v) || v < 0 || v > 0.6f; }) ||
        std::any_of(song.stemReverbDecay.begin(), song.stemReverbDecay.end(), [](float v) { return !std::isfinite(v) || (v != 0 && (v < 0.25f || v > 3)); }) ||
        std::any_of(song.stemDelayMix.begin(), song.stemDelayMix.end(), [](float v) { return !std::isfinite(v) || v < 0 || v > 0.6f; }) ||
        std::any_of(song.stemDelayFeedback.begin(), song.stemDelayFeedback.end(), [](float v) { return !std::isfinite(v) || v < 0 || v > 0.6f; }) ||
        std::any_of(song.stemMeter.begin(), song.stemMeter.end(), [](int meter) {
            return meter != 0 && (meter < 2 || meter > 8);
        }) ||
        song.formulaMetricGrouping < -1 || song.formulaMetricGrouping > 2 ||
        (song.formulaMetricGrouping >= 0 &&
         song.formulaMetricGrouping >= (song.beatsPerBar == 7 ? 3 :
             (song.beatsPerBar == 5 || song.beatsPerBar == 6 ? 2 : 1))) ||
        std::any_of(song.stemRole.begin(), song.stemRole.end(), [](CellRole role) {
            return int(role) < int(CellRole::free) || int(role) > int(CellRole::texture);
        }) ||
        song.shapeMode < 0 || song.shapeMode > 3 || !std::isfinite(song.shapeDrive) ||
        song.shapeDrive < 0.1f || song.shapeDrive > 8.0f ||
        !std::isfinite(song.masterGain) || song.masterGain < 0.0f || song.masterGain > 1.0f ||
        song.cellCount < 0 || song.cellCount > int(song.bank.size()) ||
        song.placementCount < 0 || song.placementCount > int(song.grid.size())) return false;
    for (const float pan : song.stemPan)
        if (!std::isfinite(pan) || pan < -1.0f || pan > 1.0f) return false;
    for (const float gain : song.stemGain)
        if (!std::isfinite(gain) || gain < 0.0f || gain > 1.5f) return false;
    for (int i = 0; i < song.cellCount; ++i) {
        const auto& cell = song.bank[static_cast<size_t>(i)];
        if (cell.id == 0 || cell.id == std::numeric_limits<std::uint32_t>::max() ||
            cell.revision == 0 || !safeText(cell.name, 128) ||
            (!cell.generatedPhrase && !validInscription(cell.inscription)) ||
            (cell.generatedPhrase && cell.inscription.ascii != "GEN") ||
            (cell.phraseVersion < 1 || cell.phraseVersion > 9) ||
            (int(cell.role) < int(CellRole::free) || int(cell.role) > int(CellRole::texture)) ||
            (!cell.generatedPhrase && cell.phraseVersion != 1) ||
            (cell.bytebeatFormula && (cell.generatedPhrase || cell.inscription.ascii != "FML" ||
                                      cell.inscription.count != 0 ||
                                      !safeText(cell.formulaExpression, 128) ||
                                      cell.formulaExpression.empty())) ||
            (!cell.bytebeatFormula && cell.formulaEventPhrase) ||
            !validFormulaEvents(cell) || !validPhraseSectionBars(cell.phraseSectionBars) ||
            (!cell.bytebeatFormula && (!cell.formulaExpression.empty() ||
                                       !cell.formulaOrigin.author.empty() ||
                                       !cell.formulaOrigin.source.empty() ||
                                       !cell.formulaOrigin.sourceId.empty() ||
                                       !cell.formulaOrigin.license.empty() ||
                                       cell.formulaOrigin.parentA != 0 ||
                                       cell.formulaOrigin.parentB != 0)) ||
            !safeText(cell.formulaOrigin.author, 128) ||
            !safeText(cell.formulaOrigin.source, 256) ||
            !safeText(cell.formulaOrigin.sourceId, 64) ||
            !safeText(cell.formulaOrigin.license, 64) ||
            (cell.formulaOrigin.parentA != 0 && !findCell(song, cell.formulaOrigin.parentA)) ||
            (cell.formulaOrigin.parentB != 0 && !findCell(song, cell.formulaOrigin.parentB))) return false;
        if (cell.bytebeatFormula) {
            BytebeatProgram program;
            std::string error;
            if (!compileBytebeat(cell.formulaExpression, program, error)) return false;
        }
        if (cell.waveformCount < 0 || cell.waveformCount > Cell::waveformSize) return false;
        for (int j = 0; j < i; ++j)
            if (song.bank[static_cast<size_t>(j)].id == cell.id) return false;
    }
    for (int i = 0; i < song.placementCount; ++i) {
        const auto& placement = song.grid[static_cast<size_t>(i)];
        if (!findCell(song, placement.cellId) || placement.startBeat < 0 ||
            placement.startBeat > 4096 || placement.repeats < 1 || placement.repeats > 8 ||
            placement.sourceStartBeat < 0 || placement.sourceStartBeat > 4096 ||
            placement.lengthBeats < 0 || placement.lengthBeats > 4096 ||
            (placement.lengthBeats > 0 && placement.startBeat + placement.lengthBeats > 4096) ||
            placement.stem < 0 || placement.stem >= Song::stemCount ||
            (placement.span != CellSpan::beat && placement.span != CellSpan::bar &&
             placement.span != CellSpan::fourBars && placement.span != CellSpan::twoBars)) return false;
    }
    const bool hasStrokePressure = std::any_of(song.bank.begin(), song.bank.begin() + song.cellCount,
        [](const Cell& cell) {
            for (int s = 0; s < cell.inscription.count; ++s)
                for (int p = 0; p < cell.inscription.strokes[static_cast<size_t>(s)].count; ++p)
                    if (cell.inscription.strokes[static_cast<size_t>(s)].points[static_cast<size_t>(p)].pressure >= 0.0f)
                        return true;
            return false;
        });
    const bool version77 = hasStrokePressure || std::any_of(versions.begin(), versions.end(),
        [](const NamedSongVersionRecord& version) {
            return version.serializedSong.rfind("RISCO_SONG_V77\n", 0) == 0;
        });
    const bool version76 = version77 || song.compositionSeedRecipeEnabled;
    const bool version75 = version76 || !versions.empty();
    const bool version74 = version75 || std::any_of(song.bank.begin(),song.bank.begin() + song.cellCount,[](const Cell& cell) {
        return cell.inputInterpretation > 12;
    }) || std::any_of(versions.begin(),versions.end(),[](const NamedSongVersionRecord& version) {
        return version.serializedSong.rfind("RISCO_SONG_V74\n",0) == 0;
    });
    const bool version73 = version74 || std::any_of(song.bank.begin(),song.bank.begin() + song.cellCount,[](const Cell& cell) {
        return cell.inputInterpretation > 10 || cell.inputGestureRangeSemitones != 12;
    }) || std::any_of(versions.begin(),versions.end(),[](const NamedSongVersionRecord& version) {
        return version.serializedSong.rfind("RISCO_SONG_V73\n",0) == 0;
    });
    if (song.preferredTakeId != 0 && !findCell(song,song.preferredTakeId)) return false;
    out << (version77 ? "RISCO_SONG_V77\n" : version76 ? "RISCO_SONG_V76\n" : version75 ? "RISCO_SONG_V75\n" : version74 ? "RISCO_SONG_V74\n" : version73 ? "RISCO_SONG_V73\n" : "RISCO_SONG_V71\n")
        << std::setprecision(9)
        << song.bpm << ' ' << song.beatsPerBar << ' ' << song.bitDepth << ' '
        << song.timbreMode << ' ' << song.shapeMode << ' ' << song.shapeDrive << ' '
        << song.stemPan[0] << ' ' << song.stemPan[1] << ' ' << song.stemPan[2] << ' '
        << song.stemGain[0] << ' ' << song.stemGain[1] << ' ' << song.stemGain[2] << ' '
        << song.cellCount << ' ' << song.placementCount << ' ' << song.masterGain << ' '
        << song.interStemResponse << ' ' << song.formulaMetricGrouping << ' '
        << int(song.stemRole[0]) << ' ' << int(song.stemRole[1]) << ' '
        << int(song.stemRole[2]) << '\n';
    out << "SETTINGS " << song.responsePolicy << ' ' << song.stemEffect[0] << ' '
        << song.stemEffect[1] << ' ' << song.stemEffect[2] << ' ' << int(song.compoundSixEight) << '\n';
    if (version76)
        out << "SEED_RECIPE " << song.compositionSeedAlgorithm << ' '
            << song.compositionSeed << ' ' << unsigned(song.compositionSeedLevel) << ' '
            << song.compositionSeedLocks << '\n';
    out << "DELAY";
    for (int s = 0; s < 3; ++s)
        out << ' ' << song.stemDelayMix[static_cast<size_t>(s)] << ' ' << song.stemDelayFeedback[static_cast<size_t>(s)];
    out << '\n';
    out << "FILTER " << song.stemFilterCutoff[0] << ' ' << song.stemFilterCutoff[1] << ' ' << song.stemFilterCutoff[2] << '\n';
    out << "BITCRUSH " << song.stemBitcrushBits[0] << ' ' << song.stemBitcrushBits[1] << ' ' << song.stemBitcrushBits[2] << '\n';
    out << "BITCRUSH_PARAMS";
    for (int s = 0; s < 3; ++s) {
        const auto& curve = song.stemBitcrushAutomation[static_cast<size_t>(s)];
        out << ' ' << int(curve.enabled) << ' ' << curve.values[0] << ' ' << curve.values[1] << ' ' << curve.values[2];
    }
    out << '\n';
    out << "REVERB " << song.stemReverbMix[0] << ' ' << song.stemReverbMix[1] << ' ' << song.stemReverbMix[2]
        << ' ' << song.stemReverbDecay[0] << ' ' << song.stemReverbDecay[1] << ' ' << song.stemReverbDecay[2] << '\n';
    out << "AUTOMATION";
    for (int s = 0; s < 3; ++s) { const auto& curve = song.stemAutomation[static_cast<size_t>(s)];
        out << ' ' << int(curve.enabled) << ' ' << curve.levels[0] << ' ' << curve.levels[1] << ' ' << curve.levels[2];
    }
    out << '\n';
    out << "PAN_AUTOMATION";
    for (int s = 0; s < 3; ++s) { const auto& curve = song.stemPanAutomation[static_cast<size_t>(s)];
        out << ' ' << int(curve.enabled) << ' ' << curve.levels[0] << ' ' << curve.levels[1] << ' ' << curve.levels[2];
    }
    out << '\n';
    out << "FX_AUTOMATION";
    for (int s = 0; s < 3; ++s) { const auto& curve = song.stemFxAutomation[static_cast<size_t>(s)];
        out << ' ' << int(curve.enabled) << ' ' << int(curve.active[0]) << ' ' << int(curve.active[1]) << ' ' << int(curve.active[2]);
    }
    out << '\n';
    out << "FX_PARAMS";
    for (int s = 0; s < 3; ++s) { const auto& curve = song.stemFilterAutomation[static_cast<size_t>(s)];
        out << ' ' << int(curve.enabled) << ' ' << curve.values[0] << ' ' << curve.values[1] << ' ' << curve.values[2];
    }
    out << '\n';
    out << "DELAY_PARAMS";
    for (int s = 0; s < 3; ++s) {
        const auto& mix = song.stemDelayMixAutomation[static_cast<size_t>(s)];
        const auto& feedback = song.stemDelayFeedbackAutomation[static_cast<size_t>(s)];
        out << ' ' << int(mix.enabled) << ' ' << mix.values[0] << ' ' << mix.values[1] << ' ' << mix.values[2]
            << ' ' << int(feedback.enabled) << ' ' << feedback.values[0] << ' ' << feedback.values[1] << ' ' << feedback.values[2];
    }
    out << '\n';
    out << "REVERB_PARAMS";
    for (int s = 0; s < 3; ++s) { const auto& curve = song.stemReverbMixAutomation[static_cast<size_t>(s)];
        out << ' ' << int(curve.enabled) << ' ' << curve.values[0] << ' ' << curve.values[1] << ' ' << curve.values[2];
    }
    out << '\n';
    out << "REVERB_DECAY_PARAMS";
    for (int s = 0; s < 3; ++s) { const auto& curve = song.stemReverbDecayAutomation[static_cast<size_t>(s)];
        out << ' ' << int(curve.enabled) << ' ' << curve.values[0] << ' ' << curve.values[1] << ' ' << curve.values[2];
    }
    out << '\n';
    out << "AY " << song.ayNoisePeriod << ' ' << song.ayEnvelopeMode << ' ' << song.ayEnvelopeRamp << '\n';
    out << "HISTORICAL_AY " << song.historicalAyTexture << ' ' << song.historicalAyNoisePeriod
        << ' ' << song.historicalAyEnvelope << ' ' << song.historicalAyEnvelopeRamp << '\n';
    out << "METRIC";
    for (int s = 0; s < 3; ++s) out << ' ' << song.stemMeter[static_cast<size_t>(s)];
    for (int i = 0; i < song.cellCount; ++i)
        for (int bars : song.bank[static_cast<size_t>(i)].phraseSectionBars) out << ' ' << bars;
    out << '\n';
    out << "DIALOGUE " << song.responseMemory;
    for (int intent : song.responseSectionIntent) out << ' ' << intent;
    out << '\n';
    out << "TAKE " << song.preferredTakeId << '\n';
    out << "RESPONSE " << song.responseMotif << ' ' << song.responseDensity << '\n';
    for (int i = 0; i < song.cellCount; ++i) {
        const auto& cell = song.bank[static_cast<size_t>(i)];
        out << "CELL " << cell.id << ' ' << cell.revision << ' '
            << std::quoted(cell.name) << ' ' << std::quoted(cell.inscription.ascii)
            << ' ' << cell.inscription.count << ' ' << int(cell.generatedPhrase)
            << ' ' << cell.phraseSeed << ' ' << cell.phraseVariation
            << ' ' << cell.waveformCount << ' ' << int(cell.bytebeatFormula)
            << ' ' << std::quoted(cell.formulaExpression)
            << ' ' << std::quoted(cell.formulaOrigin.author)
            << ' ' << std::quoted(cell.formulaOrigin.source)
            << ' ' << std::quoted(cell.formulaOrigin.sourceId)
            << ' ' << cell.formulaOrigin.parentA
            << ' ' << cell.formulaOrigin.parentB
            << ' ' << std::quoted(cell.formulaOrigin.license)
            << ' ' << cell.phraseVersion << ' ' << int(cell.role)
            << ' ' << int(cell.formulaEventPhrase) << ' ' << int(cell.formulaScale)
            << ' ' << int(cell.formulaTonic) << ' ' << int(cell.formulaMotion)
            << ' ' << cell.formulaEvents.count
            << ' ' << cell.formulaEvents.end << ' ' << cell.formulaEvents.repeatPeriod
            << ' ' << cell.formulaEventBpm << ' ' << unsigned(cell.phraseSectionMask)
            << ' ' << cell.formulaProgression << ' ' << cell.formulaHarmonicBars << ' ' << cell.formulaSwing << '\n';
        out << "EDIT " << cell.formulaSubdivision << ' ' << int(cell.favorite) << ' ' << cell.inputInterpretation;
        for (const auto root : cell.formulaChordRoots) out << ' ' << root;
        for (const auto& edit : cell.sectionEdits)
            out << ' ' << edit.density << ' ' << edit.gain << ' ' << edit.octaves;
        out << ' ' << int(cell.formulaSyncopation) << ' ' << cell.formulaPulsesPerBar;
        for (int meter : cell.phraseMeters) out << ' ' << meter;
        out << ' ' << cell.inputInterpretationStrength << ' ' << int(cell.inputGestureCurve.enabled)
            << ' ' << cell.inputGestureCurve.values[0] << ' ' << cell.inputGestureCurve.values[1]
            << ' ' << cell.inputGestureCurve.values[2];
        if (version73) out << ' ' << cell.inputGestureRangeSemitones;
        out << '\n';
        out << "IMPORT " << std::quoted(cell.importProvenance) << '\n';
        for (int p = 0; p < cell.waveformCount; ++p)
            out << "W " << cell.waveform[static_cast<size_t>(p)] << '\n';
        for (int s = 0; s < cell.inscription.count; ++s) {
            const auto& stroke = cell.inscription.strokes[static_cast<size_t>(s)];
            out << "STROKE " << int(stroke.crossed) << ' ' << stroke.count << '\n';
            for (int p = 0; p < stroke.count; ++p) {
                const auto& point = stroke.points[static_cast<size_t>(p)];
                out << "P " << point.x << ' ' << point.y;
                if (version77) out << ' ' << point.pressure;
                out << '\n';
            }
        }
        for (int n = 0; n < cell.formulaEvents.count; ++n) {
            const auto& note = cell.formulaEvents.notes[static_cast<size_t>(n)];
            out << "N " << note.start << ' ' << note.duration << ' ' << note.frequency
                << ' ' << note.gain << ' ' << int(note.role) << ' ' << note.pulseDuty << '\n';
        }
    }
    for (int i = 0; i < song.placementCount; ++i) {
        const auto& p = song.grid[static_cast<size_t>(i)];
        out << "PLACE " << p.cellId << ' ' << p.startBeat << ' '
            << int(p.span) << ' ' << p.repeats << ' '
            << p.sourceStartBeat << ' ' << p.lengthBeats << ' ' << p.stem << '\n';
    }
    if (version73 || !versions.empty()) {
        out << "VERSIONS " << versions.size() << ' ' << activeVersion << '\n';
        for (const auto& version : versions) {
            out << "VERSION " << std::quoted(version.name) << ' ' << version.parentIndex << ' '
                << version.serializedSong.size() << '\n';
            out.write(version.serializedSong.data(),static_cast<std::streamsize>(version.serializedSong.size()));
            out << '\n';
        }
    }
    out << "STEM_EXTRAS 2\n";
    for (int s = 3; s < Song::stemCount; ++s) {
        const auto index = static_cast<size_t>(s);
        const auto& gainAuto = song.stemAutomation[index];
        const auto& panAuto = song.stemPanAutomation[index];
        const auto& fxAuto = song.stemFxAutomation[index];
        const auto& filterAuto = song.stemFilterAutomation[index];
        const auto& crushAuto = song.stemBitcrushAutomation[index];
        const auto& delayMixAuto = song.stemDelayMixAutomation[index];
        const auto& delayFeedbackAuto = song.stemDelayFeedbackAutomation[index];
        const auto& reverbMixAuto = song.stemReverbMixAutomation[index];
        const auto& reverbDecayAuto = song.stemReverbDecayAutomation[index];
        out << "STEM " << s << ' ' << int(song.stemRole[index]) << ' ' << song.stemMeter[index]
            << ' ' << song.stemPan[index] << ' ' << song.stemGain[index] << ' ' << song.stemEffect[index]
            << ' ' << song.stemFilterCutoff[index] << ' ' << song.stemBitcrushBits[index]
            << ' ' << song.stemReverbMix[index] << ' ' << song.stemReverbDecay[index]
            << ' ' << song.stemDelayMix[index] << ' ' << song.stemDelayFeedback[index]
            << ' ' << int(gainAuto.enabled) << ' ' << gainAuto.levels[0] << ' ' << gainAuto.levels[1] << ' ' << gainAuto.levels[2]
            << ' ' << int(panAuto.enabled) << ' ' << panAuto.levels[0] << ' ' << panAuto.levels[1] << ' ' << panAuto.levels[2]
            << ' ' << int(fxAuto.enabled) << ' ' << int(fxAuto.active[0]) << ' ' << int(fxAuto.active[1]) << ' ' << int(fxAuto.active[2])
            << ' ' << int(filterAuto.enabled) << ' ' << filterAuto.values[0] << ' ' << filterAuto.values[1] << ' ' << filterAuto.values[2]
            << ' ' << int(crushAuto.enabled) << ' ' << crushAuto.values[0] << ' ' << crushAuto.values[1] << ' ' << crushAuto.values[2]
            << ' ' << int(delayMixAuto.enabled) << ' ' << delayMixAuto.values[0] << ' ' << delayMixAuto.values[1] << ' ' << delayMixAuto.values[2]
            << ' ' << int(delayFeedbackAuto.enabled) << ' ' << delayFeedbackAuto.values[0] << ' ' << delayFeedbackAuto.values[1] << ' ' << delayFeedbackAuto.values[2]
            << ' ' << int(reverbMixAuto.enabled) << ' ' << reverbMixAuto.values[0] << ' ' << reverbMixAuto.values[1] << ' ' << reverbMixAuto.values[2]
            << ' ' << int(reverbDecayAuto.enabled) << ' ' << reverbDecayAuto.values[0] << ' ' << reverbDecayAuto.values[1] << ' ' << reverbDecayAuto.values[2] << '\n';
    }
    return bool(out);
}

static bool loadSongImpl(std::istream& in, Song& destination,
                         std::vector<NamedSongVersionRecord>* loadedVersions, int* loadedActiveVersion,
                         bool allowVersions) {
    std::string record;
    if (!line(in, record)) return false;
    const bool version77 = record == "RISCO_SONG_V77";
    const bool version76 = record == "RISCO_SONG_V76" || version77;
    const bool version75 = record == "RISCO_SONG_V75" || version76;
    const bool version74 = record == "RISCO_SONG_V74" || version75;
    const bool version73 = record == "RISCO_SONG_V73" || version74;
    const bool version72 = record == "RISCO_SONG_V72" || version73;
    const bool version71 = record == "RISCO_SONG_V71" || version72;
    const bool version70 = record == "RISCO_SONG_V70" || version71;
    const bool version69 = record == "RISCO_SONG_V69" || version70;
    const bool version68 = record == "RISCO_SONG_V68" || version69;
    const bool version67 = record == "RISCO_SONG_V67" || version68;
    const bool version66 = record == "RISCO_SONG_V66" || version67;
    const bool version65 = record == "RISCO_SONG_V65" || version66;
    const bool version64 = record == "RISCO_SONG_V64" || version65;
    const bool version63 = record == "RISCO_SONG_V63" || version64;
    const bool version62 = record == "RISCO_SONG_V62" || version63;
    const bool version61 = record == "RISCO_SONG_V61" || version62;
    const bool version60 = record == "RISCO_SONG_V60" || version61;
    const bool version59 = record == "RISCO_SONG_V59" || version60;
    const bool version58 = record == "RISCO_SONG_V58" || version59;
    const bool version57 = record == "RISCO_SONG_V57" || version58;
    const bool version56 = record == "RISCO_SONG_V56" || version57;
    const bool version55 = record == "RISCO_SONG_V55" || version56;
    const bool version54 = record == "RISCO_SONG_V54" || version55;
    const bool version53 = record == "RISCO_SONG_V53" || version54;
    const bool version52 = record == "RISCO_SONG_V52" || version53;
    const bool version51 = record == "RISCO_SONG_V51" || version52;
    const bool version50 = record == "RISCO_SONG_V50" || version51;
    const bool version49 = record == "RISCO_SONG_V49" || version50;
    const bool version48 = record == "RISCO_SONG_V48" || version49;
    const bool version47 = record == "RISCO_SONG_V47" || version48;
    const bool version46 = record == "RISCO_SONG_V46" || version47;
    const bool version45 = record == "RISCO_SONG_V45" || version46;
    const bool version44 = record == "RISCO_SONG_V44" || version45;
    const bool version43 = record == "RISCO_SONG_V43" || version44;
    const bool version42 = record == "RISCO_SONG_V42" || version43;
    const bool version41 = record == "RISCO_SONG_V41" || version42;
    const bool version40 = record == "RISCO_SONG_V40" || version41;
    const bool version39 = record == "RISCO_SONG_V39" || version40;
    const bool version38 = record == "RISCO_SONG_V38" || version39;
    const bool version37 = record == "RISCO_SONG_V37" || version38;
    const bool version36 = record == "RISCO_SONG_V36" || version37;
    const bool version35 = record == "RISCO_SONG_V35" || version36;
    const bool version34 = record == "RISCO_SONG_V34" || version35;
    const bool version33 = record == "RISCO_SONG_V33" || version34;
    const bool version32 = record == "RISCO_SONG_V32" || version33;
    const bool version31 = record == "RISCO_SONG_V31" || version32;
    const bool version30 = record == "RISCO_SONG_V30" || version31;
    const bool version29 = record == "RISCO_SONG_V29" || version30;
    const bool version28 = record == "RISCO_SONG_V28" || version29;
    const bool version27 = record == "RISCO_SONG_V27" || version28;
    const bool version26 = record == "RISCO_SONG_V26" || version27;
    const bool version25 = record == "RISCO_SONG_V25" || version26;
    const bool version24 = record == "RISCO_SONG_V24" || version25;
    const bool version23 = record == "RISCO_SONG_V23" || version24;
    const bool version22 = record == "RISCO_SONG_V22" || version23;
    const bool version21 = record == "RISCO_SONG_V21" || version22;
    const bool version20 = record == "RISCO_SONG_V20" || version21;
    const bool version19 = record == "RISCO_SONG_V19" || version20;
    const bool version18 = record == "RISCO_SONG_V18" || version19;
    const bool version17 = record == "RISCO_SONG_V17" || version18;
    const bool version16 = record == "RISCO_SONG_V16" || version17;
    const bool version15 = record == "RISCO_SONG_V15" || version16;
    const bool version14 = record == "RISCO_SONG_V14" || version15;
    const bool version13 = record == "RISCO_SONG_V13" || version14;
    const bool version12 = record == "RISCO_SONG_V12" || version13;
    const bool version11 = record == "RISCO_SONG_V11" || version12;
    const bool version10 = record == "RISCO_SONG_V10" || version11;
    const bool version9 = record == "RISCO_SONG_V9" || version10;
    if (record != "RISCO_SONG_V1" && record != "RISCO_SONG_V2" &&
        record != "RISCO_SONG_V3" && record != "RISCO_SONG_V4" &&
        record != "RISCO_SONG_V5" && record != "RISCO_SONG_V6" &&
        record != "RISCO_SONG_V7" && record != "RISCO_SONG_V8" && !version9 && !version18 && !version20 && !version22 && !version72 && !version76) return false;
    constexpr int stemSlots = 3;
    const bool version2 = record == "RISCO_SONG_V2" || record == "RISCO_SONG_V3" ||
                          record == "RISCO_SONG_V4" || record == "RISCO_SONG_V5" ||
                          record == "RISCO_SONG_V6" || record == "RISCO_SONG_V7" || record == "RISCO_SONG_V8" || version9;
    const bool version3 = record == "RISCO_SONG_V3" || record == "RISCO_SONG_V4" ||
                          record == "RISCO_SONG_V5" || record == "RISCO_SONG_V6" ||
                          record == "RISCO_SONG_V7" || record == "RISCO_SONG_V8" || version9;
    const bool version4 = record == "RISCO_SONG_V4" || record == "RISCO_SONG_V5" ||
                          record == "RISCO_SONG_V6" || record == "RISCO_SONG_V7" || record == "RISCO_SONG_V8" || version9;
    const bool version5 = record == "RISCO_SONG_V5" || record == "RISCO_SONG_V6" ||
                          record == "RISCO_SONG_V7" || record == "RISCO_SONG_V8" || version9;
    const bool version6 = record == "RISCO_SONG_V6" || record == "RISCO_SONG_V7" || record == "RISCO_SONG_V8" || version9;
    const bool version7 = record == "RISCO_SONG_V7" || record == "RISCO_SONG_V8" || version9;
    const bool version8 = record == "RISCO_SONG_V8" || version9;
    const int maximumScalePreset = version18
        ? int(FormulaScalePreset::prometheus)
        : version17 ? int(FormulaScalePreset::chromatic)
                    : int(FormulaScalePreset::pentatonic);
    if (!line(in, record)) return false;
    auto nextStorage = std::make_unique<Song>();
    Song& next = *nextStorage;
    int cells = 0, placements = 0;
    std::array<PhraseSectionBars, Song::bankCapacity> loadedSectionBars{};
    for (auto& bars : loadedSectionBars) bars = {{1,1,1,1}};
    std::array<int, Song::stemCount> stemRoleValues{};
    {
        std::istringstream parser(record);
        if (!(parser >> next.bpm >> next.beatsPerBar) ||
            (version4 && !(parser >> next.bitDepth)) ||
            (version5 && !(parser >> next.timbreMode)) ||
            (version8 && !(parser >> next.shapeMode >> next.shapeDrive))) return false;
        for (int stem = 0; version11 && stem < stemSlots; ++stem)
            if (!(parser >> next.stemPan[static_cast<size_t>(stem)])) return false;
        for (int stem = 0; version12 && stem < stemSlots; ++stem)
            if (!(parser >> next.stemGain[static_cast<size_t>(stem)])) return false;
        if (!(parser >> cells >> placements) ||
            (version16 && !(parser >> next.masterGain)) ||
            (version21 && !(parser >> next.interStemResponse)) ||
            (version23 && !(parser >> next.formulaMetricGrouping))) return false;
        for (int stem = 0; version24 && stem < stemSlots; ++stem)
            if (!(parser >> stemRoleValues[static_cast<size_t>(stem)])) return false;
        if (!complete(parser) ||
            !std::isfinite(next.bpm) || next.bpm < 40.0 || next.bpm > 240.0 ||
            (next.bitDepth != 8 && next.bitDepth != 16) ||
            next.timbreMode < 0 || next.timbreMode > (version59 ? 6 : version51 ? 4 : version33 ? 2 : 1) ||
            next.interStemResponse < 0 || next.interStemResponse > 9 ||
            next.formulaMetricGrouping < -1 || next.formulaMetricGrouping > 2 ||
            (next.formulaMetricGrouping >= 0 &&
             next.formulaMetricGrouping >= (next.beatsPerBar == 7 ? 3 :
                 (next.beatsPerBar == 5 || next.beatsPerBar == 6 ? 2 : 1))) ||
            std::any_of(stemRoleValues.begin(), stemRoleValues.end(), [](int role) {
                return role < int(CellRole::free) || role > int(CellRole::texture);
            }) ||
            next.shapeMode < 0 || next.shapeMode > 3 ||
            !std::isfinite(next.shapeDrive) || next.shapeDrive < 0.1f || next.shapeDrive > 8.0f ||
            !std::isfinite(next.masterGain) || next.masterGain < 0.0f || next.masterGain > 1.0f ||
            std::any_of(next.stemPan.begin(), next.stemPan.end(), [](float pan) {
                return !std::isfinite(pan) || pan < -1.0f || pan > 1.0f;
            }) ||
            std::any_of(next.stemGain.begin(), next.stemGain.end(), [](float gain) {
                return !std::isfinite(gain) || gain < 0.0f || gain > 1.5f;
            }) ||
            next.beatsPerBar < 2 || next.beatsPerBar > 7 ||
            cells < 0 || cells > int(next.bank.size()) ||
            placements < 0 || placements > int(next.grid.size())) return false;
        if (version24)
            for (int stem = 0; stem < stemSlots; ++stem)
                next.stemRole[static_cast<size_t>(stem)] =
                    static_cast<CellRole>(stemRoleValues[static_cast<size_t>(stem)]);
    }
    if (version33) {
        if (!line(in, record)) return false;
        std::istringstream parser(record);
        std::string tag;
        int compound = 0;
        if (!(parser >> tag >> next.responsePolicy) || tag != "SETTINGS") return false;
        for (int stem = 0; stem < stemSlots; ++stem)
            if (!(parser >> next.stemEffect[static_cast<size_t>(stem)])) return false;
        if (!(parser >> compound) || !complete(parser) || next.responsePolicy < 0 || next.responsePolicy > (version53 ? 7 : version40 ? 6 : version39 ? 4 : version38 ? 3 : 2) ||
            compound < 0 || compound > 1 || (compound == 1 && next.beatsPerBar != 3) ||
            std::any_of(next.stemEffect.begin(), next.stemEffect.end(), [version42, version44](int effect) { return effect < 0 || effect > (version44 ? 9 : version42 ? 7 : 4); })) return false;
        next.compoundSixEight = compound == 1;
    }
    if (version76) {
        if (!line(in, record)) return false;
        std::istringstream parser(record);
        std::string tag;
        unsigned level = 0;
        if (!(parser >> tag >> next.compositionSeedAlgorithm >> next.compositionSeed >> level >> next.compositionSeedLocks) ||
            !complete(parser) || tag != "SEED_RECIPE" ||
            next.compositionSeedAlgorithm != compositionSeedAlgorithmV1 || level < 1 || level > 3 ||
            (next.compositionSeedLocks & ~compositionSeedKnownLocks) != 0) return false;
        next.compositionSeedRecipeEnabled = true;
        next.compositionSeedLevel = static_cast<std::uint8_t>(level);
    }
    if (version43) {
        if (!line(in, record)) return false;
        std::istringstream parser(record);
        std::string tag;
        if (!(parser >> tag) || tag != "DELAY") return false;
        for (int s = 0; s < stemSlots; ++s) {
            auto& mix = next.stemDelayMix[static_cast<size_t>(s)];
            auto& feedback = next.stemDelayFeedback[static_cast<size_t>(s)];
            if (!(parser >> mix >> feedback) || !std::isfinite(mix) || !std::isfinite(feedback) ||
                mix < 0 || mix > 0.6f || feedback < 0 || feedback > 0.6f) return false;
        }
        if (!complete(parser)) return false;
    }
    if (version45) {
        if (!line(in, record)) return false;
        std::istringstream parser(record);
        std::string tag;
        if (!(parser >> tag) || tag != "FILTER") return false;
        for (int s = 0; s < stemSlots; ++s) {
            auto& hz = next.stemFilterCutoff[static_cast<size_t>(s)];
            if (!(parser >> hz) || !std::isfinite(hz) || (hz != 0 && (hz < 80 || hz > 16000))) return false;
        }
        if (!complete(parser)) return false;
    }
    if (version67) {
        if (!line(in, record)) return false;
        std::istringstream parser(record);
        std::string tag;
        if (!(parser >> tag) || tag != "BITCRUSH") return false;
        for (int s = 0; s < stemSlots; ++s) {
            auto& bits = next.stemBitcrushBits[static_cast<size_t>(s)];
            if (!(parser >> bits) || (bits != 0 && (bits < 2 || bits > 16))) return false;
        }
        if (!complete(parser)) return false;
    }
    if (version68) {
        if (!line(in, record)) return false;
        std::istringstream parser(record);
        std::string tag;
        if (!(parser >> tag) || tag != "BITCRUSH_PARAMS") return false;
        for (int s = 0; s < stemSlots; ++s) {
            auto& curve = next.stemBitcrushAutomation[static_cast<size_t>(s)];
            int enabled = 0;
            if (!(parser >> enabled >> curve.values[0] >> curve.values[1] >> curve.values[2]) || (enabled != 0 && enabled != 1)) return false;
            curve.enabled = enabled == 1;
            for (float value : curve.values)
                if (!std::isfinite(value) || (value != 0.0f && (value < 2.0f || value > 16.0f))) return false;
        }
        if (!complete(parser)) return false;
    }
    if (version46) {
        if (!line(in, record)) return false;
        std::istringstream parser(record);
        std::string tag;
        if (!(parser >> tag) || tag != "REVERB") return false;
        for (int s = 0; s < stemSlots; ++s) {
            auto& mix = next.stemReverbMix[static_cast<size_t>(s)];
            if (!(parser >> mix) || !std::isfinite(mix) || mix < 0 || mix > 0.6f) return false;
        }
        if (version47)
            for (int s = 0; s < stemSlots; ++s) {
                auto& seconds = next.stemReverbDecay[static_cast<size_t>(s)];
                if (!(parser >> seconds) || !std::isfinite(seconds) || (seconds != 0 && (seconds < 0.25f || seconds > 3))) return false;
            }
        if (!complete(parser)) return false;
    }
    if (version48) {
        if (!line(in, record)) return false;
        std::istringstream parser(record);
        std::string tag;
        if (!(parser >> tag) || tag != "AUTOMATION") return false;
        for (int s = 0; s < stemSlots; ++s) {
            auto& curve = next.stemAutomation[static_cast<size_t>(s)];
            int enabled;
            if (!(parser >> enabled) || enabled < 0 || enabled > 1) return false;
            curve.enabled = enabled == 1;
            for (auto& level : curve.levels)
                if (!(parser >> level) || !std::isfinite(level) || level < 0 || level > 1) return false;
        }
        if (!complete(parser)) return false;
    }
    if (version56) {
        if (!line(in,record)) return false;
        {
            std::istringstream parser(record); std::string tag;
            if (!(parser >> tag) || tag != "PAN_AUTOMATION") return false;
            for (int s = 0; s < stemSlots; ++s) {
                auto& curve = next.stemPanAutomation[static_cast<size_t>(s)];
                int enabled = 0;
                if (!(parser >> enabled) || enabled < 0 || enabled > 1) return false;
                curve.enabled = enabled == 1;
                for (auto& level : curve.levels)
                    if (!(parser >> level) || !std::isfinite(level) || level < -1 || level > 1) return false;
            }
            if (!complete(parser)) return false;
        }
        if (!line(in,record)) return false;
        {
            std::istringstream parser(record); std::string tag;
            if (!(parser >> tag) || tag != "FX_AUTOMATION") return false;
            for (int s = 0; s < stemSlots; ++s) {
                auto& curve = next.stemFxAutomation[static_cast<size_t>(s)];
                int enabled = 0;
                if (!(parser >> enabled) || enabled < 0 || enabled > 1) return false;
                curve.enabled = enabled == 1;
                for (size_t point = 0; point < curve.active.size(); ++point) {
                    int active = 0;
                    if (!(parser >> active) || active < 0 || active > 1) return false;
                    curve.active[point] = active == 1;
                }
            }
            if (!complete(parser)) return false;
        }
    }
    if (version63) {
        if (!line(in, record)) return false;
        std::istringstream parser(record);
        std::string tag;
        if (!(parser >> tag) || tag != "FX_PARAMS") return false;
        for (int s = 0; s < stemSlots; ++s) {
            auto& curve = next.stemFilterAutomation[static_cast<size_t>(s)];
            int enabled = 0;
            if (!(parser >> enabled >> curve.values[0] >> curve.values[1] >> curve.values[2]) ||
                (enabled != 0 && enabled != 1)) return false;
            curve.enabled = enabled == 1;
            for (float value : curve.values)
                if (!std::isfinite(value) || value < 80.0f || value > 16000.0f) return false;
        }
        if (!complete(parser)) return false;
    }
    if (version64) {
        if (!line(in, record)) return false;
        std::istringstream parser(record);
        std::string tag;
        if (!(parser >> tag) || tag != "DELAY_PARAMS") return false;
        for (int s = 0; s < stemSlots; ++s) {
            auto& mix = next.stemDelayMixAutomation[static_cast<size_t>(s)];
            auto& feedback = next.stemDelayFeedbackAutomation[static_cast<size_t>(s)];
            int mixEnabled = 0, feedbackEnabled = 0;
            if (!(parser >> mixEnabled >> mix.values[0] >> mix.values[1] >> mix.values[2]
                  >> feedbackEnabled >> feedback.values[0] >> feedback.values[1] >> feedback.values[2]) ||
                (mixEnabled != 0 && mixEnabled != 1) || (feedbackEnabled != 0 && feedbackEnabled != 1)) return false;
            mix.enabled = mixEnabled == 1;
            feedback.enabled = feedbackEnabled == 1;
            for (float value : mix.values) if (!std::isfinite(value) || value < 0 || value > 0.6f) return false;
            for (float value : feedback.values) if (!std::isfinite(value) || value < 0 || value > 0.6f) return false;
        }
        if (!complete(parser)) return false;
    }
    if (version65) {
        if (!line(in, record)) return false;
        std::istringstream parser(record);
        std::string tag;
        if (!(parser >> tag) || tag != "REVERB_PARAMS") return false;
        for (int s = 0; s < stemSlots; ++s) {
            auto& curve = next.stemReverbMixAutomation[static_cast<size_t>(s)];
            int enabled = 0;
            if (!(parser >> enabled >> curve.values[0] >> curve.values[1] >> curve.values[2]) || (enabled != 0 && enabled != 1)) return false;
            curve.enabled = enabled == 1;
            for (float value : curve.values) if (!std::isfinite(value) || value < 0 || value > 0.6f) return false;
        }
        if (!complete(parser)) return false;
    }
    if (version66) {
        if (!line(in, record)) return false;
        std::istringstream parser(record);
        std::string tag;
        if (!(parser >> tag) || tag != "REVERB_DECAY_PARAMS") return false;
        for (int s = 0; s < stemSlots; ++s) {
            auto& curve = next.stemReverbDecayAutomation[static_cast<size_t>(s)];
            int enabled = 0;
            if (!(parser >> enabled >> curve.values[0] >> curve.values[1] >> curve.values[2]) || (enabled != 0 && enabled != 1)) return false;
            curve.enabled = enabled == 1;
            for (float value : curve.values)
                if (!std::isfinite(value) || (value != 0.0f && (value < 0.25f || value > 3.0f))) return false;
        }
        if (!complete(parser)) return false;
    }
    if (version51) {
        if (!line(in, record)) return false;
        std::istringstream parser(record);
        std::string tag;
        if (!(parser >> tag >> next.ayNoisePeriod) || tag != "AY" || next.ayNoisePeriod < 1 || next.ayNoisePeriod > 31) return false;
        if (version52 && (!(parser >> next.ayEnvelopeMode >> next.ayEnvelopeRamp) || next.ayEnvelopeMode < 0 || next.ayEnvelopeMode > 4 ||
            !std::isfinite(next.ayEnvelopeRamp) || next.ayEnvelopeRamp < 0.125f || next.ayEnvelopeRamp > 2)) return false;
        if (!complete(parser)) return false;
    }
    if (version60) {
        if (!line(in, record)) return false;
        std::istringstream parser(record);
        std::string tag;
        if (!(parser >> tag >> next.historicalAyTexture >> next.historicalAyNoisePeriod
              >> next.historicalAyEnvelope >> next.historicalAyEnvelopeRamp) ||
            tag != "HISTORICAL_AY" || !complete(parser) ||
            next.historicalAyTexture < 0 || next.historicalAyTexture > 2 ||
            next.historicalAyNoisePeriod < 1 || next.historicalAyNoisePeriod > 31 ||
            next.historicalAyEnvelope < 0 || next.historicalAyEnvelope > 4 ||
            !std::isfinite(next.historicalAyEnvelopeRamp) ||
            next.historicalAyEnvelopeRamp < 0.125f || next.historicalAyEnvelopeRamp > 2) return false;
    }
    if (version61) {
        if (!line(in, record)) return false;
        std::istringstream parser(record);
        std::string tag;
        if (!(parser >> tag) || tag != "METRIC") return false;
        for (int s = 0; s < stemSlots; ++s) {
            auto& meter = next.stemMeter[static_cast<size_t>(s)];
            if (!(parser >> meter) || (meter != 0 && (meter < 2 || meter > 8))) return false;
        }
        for (int i = 0; i < cells; ++i)
            for (auto& bars : loadedSectionBars[static_cast<size_t>(i)])
                if (!(parser >> bars) || bars < 1 || bars > 4) return false;
        if (!complete(parser)) return false;
    }
    if (version62) {
        if (!line(in, record)) return false;
        std::istringstream parser(record);
        std::string tag;
        if (!(parser >> tag >> next.responseMemory) || tag != "DIALOGUE" ||
            next.responseMemory < 0 || next.responseMemory > 2) return false;
        for (auto& intent : next.responseSectionIntent)
            if (!(parser >> intent) || intent < 0 || intent > 5) return false;
        if (!complete(parser)) return false;
    }
    if (version54) {
        if (!line(in,record)) return false;
        std::istringstream parser(record);
        std::string tag, id;
        if (!(parser >> tag >> id) || tag != "TAKE" || !complete(parser) ||
            id.empty() || id.size() > 10 || id.find_first_not_of("0123456789") != std::string::npos) return false;
        std::uint64_t value = 0;
        for (char digit : id) value = value * 10 + unsigned(digit - '0');
        if (value > std::numeric_limits<std::uint32_t>::max()) return false;
        next.preferredTakeId = static_cast<std::uint32_t>(value);
    }
    if (version58) {
        if (!line(in,record)) return false;
        std::istringstream parser(record); std::string tag;
        if (!(parser >> tag >> next.responseMotif >> next.responseDensity) || tag != "RESPONSE" || !complete(parser) ||
            next.responseMotif < 0 || next.responseMotif > 2 || next.responseDensity < 0 || next.responseDensity > 2) return false;
    }
    for (int i = 0; i < cells; ++i) {
        if (!line(in, record)) return false;
        Cell cell;
        int strokeCount = 0;
        int generated = 0;
        int waveformCount = 0;
        int formula = 0;
        int role = 0;
        int formulaEvents = 0, scale = 0, tonic = 0, motion = 0, eventCount = 0, sectionMask = 15;
        std::string tag;
        {
            std::istringstream parser(record);
            if (!(parser >> tag >> cell.id >> cell.revision >> std::quoted(cell.name)
                         >> std::quoted(cell.inscription.ascii) >> strokeCount) ||
                (version2 && !(parser >> generated >> cell.phraseSeed >> cell.phraseVariation)) ||
                (version3 && !(parser >> waveformCount)) ||
                (version6 && !(parser >> formula >> std::quoted(cell.formulaExpression))) ||
                (version7 && !(parser >> std::quoted(cell.formulaOrigin.author)
                                     >> std::quoted(cell.formulaOrigin.source)
                                     >> std::quoted(cell.formulaOrigin.sourceId)
                                     >> cell.formulaOrigin.parentA
                                     >> cell.formulaOrigin.parentB
                                     >> std::quoted(cell.formulaOrigin.license))) ||
                (version13 && !(parser >> cell.phraseVersion)) ||
                (version14 && !(parser >> role)) ||
                (version15 && !(parser >> formulaEvents >> scale)) ||
                (version19 && !(parser >> tonic)) ||
                (version15 && !(parser >> motion >> eventCount >> cell.formulaEvents.end
                                       >> cell.formulaEvents.repeatPeriod >> cell.formulaEventBpm)) ||
                (version25 && !(parser >> sectionMask)) ||
                (version31 && !(parser >> cell.formulaProgression >> cell.formulaHarmonicBars)) ||
                (version32 && !(parser >> cell.formulaSwing)) ||
                !std::isfinite(cell.formulaSwing) || cell.formulaSwing < 0 || cell.formulaSwing > 0.5 ||
                cell.formulaProgression < 0 || cell.formulaProgression > 2 ||
                cell.formulaHarmonicBars < 1 || cell.formulaHarmonicBars > 2 ||
                !complete(parser) || tag != "CELL" || cell.id == 0 || cell.revision == 0 ||
                (cell.phraseVersion < 1 || cell.phraseVersion > (version55 ? 9u : version41 ? 7u : version36 ? 6u : version26 ? 5u : version20 ? 4u : 3u)) ||
                role < int(CellRole::free) || role > int(CellRole::texture) ||
                (generated == 0 && cell.phraseVersion != 1) ||
                !safeText(cell.name, 128) ||
                !safeText(cell.inscription.ascii, Cell::maxTextBytes) ||
                !safeText(cell.formulaOrigin.author, 128) ||
                !safeText(cell.formulaOrigin.source, 256) ||
                !safeText(cell.formulaOrigin.sourceId, 64) ||
                !safeText(cell.formulaOrigin.license, 64) ||
                strokeCount < 0 || strokeCount > int(cell.inscription.strokes.size()) ||
                generated < 0 || generated > 1 ||
                waveformCount < 0 || waveformCount > Cell::waveformSize ||
                formula < 0 || formula > 1 ||
                formulaEvents < 0 || formulaEvents > 1 ||
                scale < int(FormulaScalePreset::major) || scale > maximumScalePreset ||
                tonic < 0 || tonic > 11 ||
                motion < int(FormulaScaleMotion::follow) ||
                motion > int(version30 ? FormulaScaleMotion::chordGuide : version29 ? FormulaScaleMotion::accompany : version28 ? FormulaScaleMotion::cadence :
                             version27 ? FormulaScaleMotion::connect : FormulaScaleMotion::transfigure) ||
                eventCount < 0 || eventCount > int(cell.formulaEvents.notes.size()) ||
                sectionMask < 1 || sectionMask > 15 ||
                findCell(next, cell.id)) return false;
        }
        if (version33) {
            if (!line(in, record)) return false;
            std::istringstream parser(record);
            int favorite = 0;
            if (!(parser >> tag >> cell.formulaSubdivision >> favorite >> cell.inputInterpretation) || tag != "EDIT" ||
                favorite < 0 || favorite > 1 || cell.inputInterpretation < 0 ||
                cell.inputInterpretation > (version74 ? 13 : version73 ? 12 : version71 ? 10 : version70 ? 9 : version69 ? 8 : version57 ? 7 : 4)) return false;
            cell.favorite = favorite == 1;
            for (auto& root : cell.formulaChordRoots)
                if (!(parser >> root) || root < -2 || root > 11) return false;
            for (auto& edit : cell.sectionEdits)
                if (!(parser >> edit.density >> edit.gain >> edit.octaves)) return false;
            if (version34) {
                int syncopation = 0;
                if (!(parser >> syncopation) || syncopation < 0 || syncopation > 1) return false;
                cell.formulaSyncopation = syncopation == 1;
            }
            if (version35 && (!(parser >> cell.formulaPulsesPerBar) ||
                (cell.formulaPulsesPerBar != 0 && cell.formulaPulsesPerBar != 2 && cell.formulaPulsesPerBar != 3 && cell.formulaPulsesPerBar != 5 && cell.formulaPulsesPerBar != 7))) return false;
            if (version37) for (auto& meter : cell.phraseMeters) if (!(parser >> meter)) return false;
            if (!validPhraseMeters(cell.phraseMeters)) return false;
            cell.phraseSectionBars = loadedSectionBars[static_cast<size_t>(i)];
            if (!validPhraseSectionBars(cell.phraseSectionBars)) return false;
            if (version49 && (!(parser >> cell.inputInterpretationStrength) || !std::isfinite(cell.inputInterpretationStrength) ||
                cell.inputInterpretationStrength < 0 || cell.inputInterpretationStrength > 1)) return false;
            if (version69) {
                int enabled = 0;
                if (!(parser >> enabled >> cell.inputGestureCurve.values[0] >> cell.inputGestureCurve.values[1] >>
                      cell.inputGestureCurve.values[2]) || enabled < 0 || enabled > 1) return false;
                cell.inputGestureCurve.enabled = enabled == 1;
                if (!validTemporalGestureCurve(cell.inputGestureCurve)) return false;
            }
            if (version73 && (!(parser >> cell.inputGestureRangeSemitones) ||
                !validTemporalGestureRange(cell.inputGestureRangeSemitones))) return false;
            if ((cell.inputInterpretation >= 11 && !version73) ||
                (cell.inputInterpretation >= 13 && !version74)) return false;
            if (!complete(parser) || !validSectionEdits(cell.sectionEdits) ||
                (cell.formulaSubdivision != 0 && cell.formulaSubdivision != 1 && cell.formulaSubdivision != 2 &&
                 cell.formulaSubdivision != 3 && cell.formulaSubdivision != 4 && cell.formulaSubdivision != 6)) return false;
        }
        if (version50) {
            if (!line(in, record)) return false;
            std::istringstream parser(record);
            if (!(parser >> tag >> std::quoted(cell.importProvenance)) || tag != "IMPORT" ||
                !safeText(cell.importProvenance, 2048) || !complete(parser)) return false;
        }
        cell.bytebeatFormula = formula == 1;
        cell.formulaEventPhrase = formulaEvents == 1;
        cell.formulaScale = static_cast<FormulaScalePreset>(scale);
        cell.formulaTonic = static_cast<std::uint8_t>(tonic);
        cell.formulaMotion = static_cast<FormulaScaleMotion>(motion);
        cell.formulaEvents.count = eventCount;
        cell.role = static_cast<CellRole>(role);
        cell.waveformCount = waveformCount;
        for (int p = 0; p < waveformCount; ++p) {
            if (!line(in, record)) return false;
            int value = 0;
            std::istringstream parser(record);
            if (!(parser >> tag >> value) || !complete(parser) || tag != "W" ||
                value < -32768 || value > 32767) return false;
            cell.waveform[static_cast<size_t>(p)] = static_cast<std::int16_t>(value);
        }
        cell.generatedPhrase = generated == 1;
        cell.phraseSectionMask = static_cast<std::uint8_t>(sectionMask);
        cell.inscription.count = strokeCount;
        for (int s = 0; s < strokeCount; ++s) {
            if (!line(in, record)) return false;
            auto& stroke = cell.inscription.strokes[static_cast<size_t>(s)];
            int crossed = 0, count = 0;
            {
                std::istringstream parser(record);
                if (!(parser >> tag >> crossed >> count) || !complete(parser) ||
                    tag != "STROKE" || crossed < 0 || crossed > 1 ||
                    count < 2 || count > int(stroke.points.size())) return false;
            }
            stroke.crossed = crossed == 1;
            stroke.count = count;
            for (int p = 0; p < count; ++p) {
                if (!line(in, record)) return false;
                auto& point = stroke.points[static_cast<size_t>(p)];
                std::istringstream parser(record);
                if (!(parser >> tag >> point.x >> point.y) || tag != "P") return false;
                point.pressure = -1.0f;
                if (version77 && !(parser >> point.pressure)) return false;
                if (!complete(parser) ||
                    !std::isfinite(point.x) || !std::isfinite(point.y) ||
                    point.x < 0.0f || point.x > 1.0f || point.y < 0.0f || point.y > 1.0f ||
                    !std::isfinite(point.pressure) ||
                    (point.pressure != -1.0f && (point.pressure < 0.0f || point.pressure > 1.0f)))
                    return false;
            }
        }
        for (int n = 0; n < eventCount; ++n) {
            if (!line(in, record)) return false;
            auto& note = cell.formulaEvents.notes[static_cast<size_t>(n)];
            int roleValue = 0;
            std::istringstream parser(record);
            if (!(parser >> tag >> note.start >> note.duration >> note.frequency >> note.gain >> roleValue >> note.pulseDuty) ||
                !complete(parser) || tag != "N" || roleValue < int(VoiceRole::lead) ||
                roleValue > int(VoiceRole::harmony)) return false;
            note.role = static_cast<VoiceRole>(roleValue);
        }
        if (!validFormulaEvents(cell) || (cell.formulaEventPhrase && !cell.bytebeatFormula)) return false;
        if (cell.bytebeatFormula) {
            BytebeatProgram program;
            std::string error;
            if (cell.generatedPhrase || cell.inscription.ascii != "FML" || strokeCount != 0 ||
                cell.phraseSeed != 0 || cell.phraseVariation != 0 ||
                !safeText(cell.formulaExpression, 128) || cell.formulaExpression.empty() ||
                !compileBytebeat(cell.formulaExpression, program, error)) return false;
            if (cell.formulaEventPhrase != (eventCount > 0)) return false;
        } else if (cell.generatedPhrase) {
            if (cell.inscription.ascii != "GEN" || strokeCount != 0 ||
                !cell.formulaExpression.empty()) return false;
        } else if (!cell.formulaExpression.empty() || !validInscription(cell.inscription) || cell.phraseSeed != 0 ||
                   cell.phraseVariation != 0) return false;
        next.bank[static_cast<size_t>(next.cellCount++)] = std::move(cell);
        if (next.bank[static_cast<size_t>(i)].id == std::numeric_limits<std::uint32_t>::max()) return false;
        next.nextCellId = std::max(next.nextCellId, next.bank[static_cast<size_t>(i)].id + 1);
    }
    for (int i = 0; i < next.cellCount; ++i) {
        const auto& cell = next.bank[static_cast<size_t>(i)];
        if (!cell.bytebeatFormula && (!cell.formulaOrigin.author.empty() ||
                                      !cell.formulaOrigin.source.empty() ||
                                      !cell.formulaOrigin.sourceId.empty() ||
                                      !cell.formulaOrigin.license.empty() ||
                                      cell.formulaOrigin.parentA != 0 ||
                                      cell.formulaOrigin.parentB != 0)) return false;
        if ((cell.formulaOrigin.parentA != 0 &&
             (cell.formulaOrigin.parentA == cell.id || !findCell(next, cell.formulaOrigin.parentA))) ||
            (cell.formulaOrigin.parentB != 0 &&
             (cell.formulaOrigin.parentB == cell.id || !findCell(next, cell.formulaOrigin.parentB))))
            return false;
    }
    if (next.preferredTakeId != 0 && !findCell(next,next.preferredTakeId)) return false;
    std::string tag;
    for (int i = 0; i < placements; ++i) {
        if (!line(in, record)) return false;
        Placement placement;
        int span = 0;
        std::istringstream parser(record);
        if (!(parser >> tag >> placement.cellId >> placement.startBeat >> span >> placement.repeats) ||
            (version9 && !(parser >> placement.sourceStartBeat >> placement.lengthBeats)) ||
            (version10 && !(parser >> placement.stem)) ||
            !complete(parser) || tag != "PLACE" || span < 0 || span > (version20 ? 3 : 2)) return false;
        if (!version10) placement.stem = i % Song::stemCount;
        placement.span = static_cast<CellSpan>(span);
        if (!placeCell(next, placement)) return false;
    }
    std::vector<NamedSongVersionRecord> versions;
    int activeVersion = -1;
    if (version72) {
        if (!line(in,record)) return false;
        int count = 0;
        std::istringstream parser(record);
        if (!(parser >> tag >> count >> activeVersion) || !complete(parser) || tag != "VERSIONS" ||
            count < 0 || count > int(maxNamedSongVersions) || activeVersion < -1 || activeVersion >= count ||
            (!allowVersions && count != 0)) return false;
        size_t totalBytes = 0;
        std::vector<std::string> names;
        for (int i = 0; i < count; ++i) {
            if (!line(in,record)) return false;
            NamedSongVersionRecord version;
            std::size_t payloadBytes = 0;
            std::istringstream versionLine(record);
            if (version75) {
                if (!(versionLine >> tag >> std::quoted(version.name) >> version.parentIndex >> payloadBytes) ||
                    tag != "VERSION" || version.parentIndex < -1 || version.parentIndex >= i) return false;
            } else if (!(versionLine >> tag >> std::quoted(version.name) >> payloadBytes)) return false;
            if (!complete(versionLine) ||
                tag != "VERSION" || !safeText(version.name,128) || version.name.empty() ||
                payloadBytes == 0 || payloadBytes > 4 * 1024 * 1024 ||
                payloadBytes > maxNamedSongVersionBytes - totalBytes) return false;
            for (const auto& name : names)
                if (name == version.name) return false;
            names.push_back(version.name);
            version.serializedSong.resize(payloadBytes);
            in.read(version.serializedSong.data(),static_cast<std::streamsize>(payloadBytes));
            if (in.gcount() != static_cast<std::streamsize>(payloadBytes) || in.get() != '\n') return false;
            std::istringstream snapshotStream(version.serializedSong);
            auto snapshot = std::make_unique<Song>();
            if (!loadSongImpl(snapshotStream,*snapshot,nullptr,nullptr,false)) return false;
            totalBytes += payloadBytes;
            versions.push_back(std::move(version));
        }
        if (activeVersion >= count) return false;
    }
    if (line(in, record)) {
        std::istringstream header(record);
        int extraCount = 0;
        if (!(header >> tag >> extraCount) || !complete(header) || tag != "STEM_EXTRAS" || extraCount != 2)
            return false;
        for (int s = 3; s < Song::stemCount; ++s) {
            if (!line(in, record)) return false;
            std::istringstream parser(record);
            int stem = -1, role = 0, gainEnabled = 0, panEnabled = 0, fxEnabled = 0;
            int filterEnabled = 0, crushEnabled = 0, delayMixEnabled = 0, delayFeedbackEnabled = 0;
            int reverbMixEnabled = 0, reverbDecayEnabled = 0;
            auto& nextSong = next;
            const auto index = static_cast<size_t>(s);
            if (!(parser >> tag >> stem >> role >> nextSong.stemMeter[index] >> nextSong.stemPan[index]
                  >> nextSong.stemGain[index] >> nextSong.stemEffect[index] >> nextSong.stemFilterCutoff[index]
                  >> nextSong.stemBitcrushBits[index] >> nextSong.stemReverbMix[index] >> nextSong.stemReverbDecay[index]
                  >> nextSong.stemDelayMix[index] >> nextSong.stemDelayFeedback[index]
                  >> gainEnabled >> nextSong.stemAutomation[index].levels[0]
                  >> nextSong.stemAutomation[index].levels[1] >> nextSong.stemAutomation[index].levels[2]
                  >> panEnabled >> nextSong.stemPanAutomation[index].levels[0]
                  >> nextSong.stemPanAutomation[index].levels[1] >> nextSong.stemPanAutomation[index].levels[2]
                  >> fxEnabled) || tag != "STEM" || stem != s ||
                role < int(CellRole::free) || role > int(CellRole::texture) ||
                (nextSong.stemMeter[index] != 0 && (nextSong.stemMeter[index] < 2 || nextSong.stemMeter[index] > 8)) ||
                !std::isfinite(nextSong.stemPan[index]) || nextSong.stemPan[index] < -1 || nextSong.stemPan[index] > 1 ||
                !std::isfinite(nextSong.stemGain[index]) || nextSong.stemGain[index] < 0 || nextSong.stemGain[index] > 1.5f ||
                nextSong.stemEffect[index] < 0 || nextSong.stemEffect[index] > 9 ||
                !std::isfinite(nextSong.stemFilterCutoff[index]) ||
                (nextSong.stemFilterCutoff[index] != 0 && (nextSong.stemFilterCutoff[index] < 80 || nextSong.stemFilterCutoff[index] > 16000)) ||
                (nextSong.stemBitcrushBits[index] != 0 && (nextSong.stemBitcrushBits[index] < 2 || nextSong.stemBitcrushBits[index] > 16)) ||
                !std::isfinite(nextSong.stemReverbMix[index]) || nextSong.stemReverbMix[index] < 0 || nextSong.stemReverbMix[index] > 0.6f ||
                !std::isfinite(nextSong.stemReverbDecay[index]) ||
                (nextSong.stemReverbDecay[index] != 0 && (nextSong.stemReverbDecay[index] < 0.25f || nextSong.stemReverbDecay[index] > 3)) ||
                !std::isfinite(nextSong.stemDelayMix[index]) || nextSong.stemDelayMix[index] < 0 || nextSong.stemDelayMix[index] > 0.6f ||
                !std::isfinite(nextSong.stemDelayFeedback[index]) || nextSong.stemDelayFeedback[index] < 0 || nextSong.stemDelayFeedback[index] > 0.6f ||
                (gainEnabled != 0 && gainEnabled != 1) || (panEnabled != 0 && panEnabled != 1) ||
                (fxEnabled != 0 && fxEnabled != 1)) return false;
            auto& gainAuto = nextSong.stemAutomation[index];
            auto& panAuto = nextSong.stemPanAutomation[index];
            auto& fxAuto = nextSong.stemFxAutomation[index];
            gainAuto.enabled = gainEnabled == 1;
            panAuto.enabled = panEnabled == 1;
            fxAuto.enabled = fxEnabled == 1;
            for (float value : gainAuto.levels) if (!std::isfinite(value) || value < 0 || value > 1) return false;
            for (float value : panAuto.levels) if (!std::isfinite(value) || value < -1 || value > 1) return false;
            for (size_t point = 0; point < fxAuto.active.size(); ++point) {
                int active = 0;
                if (!(parser >> active) || (active != 0 && active != 1)) return false;
                fxAuto.active[point] = active == 1;
            }
            auto readParameters = [&parser](ParameterAutomation& curve, int& enabled,
                                             float minimum, float maximum, bool allowZero) {
                if (!(parser >> enabled >> curve.values[0] >> curve.values[1] >> curve.values[2]) ||
                    (enabled != 0 && enabled != 1)) return false;
                curve.enabled = enabled == 1;
                for (const float value : curve.values)
                    if (!std::isfinite(value) ||
                        (!(allowZero && value == 0.0f) && (value < minimum || value > maximum))) return false;
                return true;
            };
            if (!readParameters(nextSong.stemFilterAutomation[index], filterEnabled, 80, 16000, false) ||
                !readParameters(nextSong.stemBitcrushAutomation[index], crushEnabled, 2, 16, true) ||
                !readParameters(nextSong.stemDelayMixAutomation[index], delayMixEnabled, 0, 0.6f, false) ||
                !readParameters(nextSong.stemDelayFeedbackAutomation[index], delayFeedbackEnabled, 0, 0.6f, false) ||
                !readParameters(nextSong.stemReverbMixAutomation[index], reverbMixEnabled, 0, 0.6f, false) ||
                !readParameters(nextSong.stemReverbDecayAutomation[index], reverbDecayEnabled, 0.25f, 3, true) ||
                !complete(parser)) return false;
            nextSong.stemRole[index] = static_cast<CellRole>(role);
            nextSong.stemFilterAutomation[index].enabled = filterEnabled == 1;
            nextSong.stemBitcrushAutomation[index].enabled = crushEnabled == 1;
            nextSong.stemDelayMixAutomation[index].enabled = delayMixEnabled == 1;
            nextSong.stemDelayFeedbackAutomation[index].enabled = delayFeedbackEnabled == 1;
            nextSong.stemReverbMixAutomation[index].enabled = reverbMixEnabled == 1;
            nextSong.stemReverbDecayAutomation[index].enabled = reverbDecayEnabled == 1;
        }
    } else if (!in.eof()) return false;
    if (line(in, record)) return false;
    if (!in.eof()) return false;
    destination = std::move(next);
    if (loadedVersions) *loadedVersions = std::move(versions);
    if (loadedActiveVersion) *loadedActiveVersion = activeVersion;
    return true;
}

bool loadSong(std::istream& in, Song& destination,
              std::vector<NamedSongVersionRecord>* versions, int* activeVersion) {
    return loadSongImpl(in,destination,versions,activeVersion,true);
}

std::uint32_t storePhraseTake(Song& song, const Cell& recipe) {
    if (!recipe.generatedPhrase || recipe.bytebeatFormula || recipe.inscription.count != 0 || song.cellCount < 0 ||
        song.cellCount >= int(song.bank.size()) || song.nextCellId == 0 ||
        song.nextCellId == std::numeric_limits<std::uint32_t>::max() ||
        findCell(song,song.nextCellId)) return 0;
    auto candidate = std::make_unique<Song>();
    candidate->cellCount = 1;
    candidate->bank[0] = recipe;
    candidate->bank[0].id = 1;
    candidate->bank[0].revision = 1;
    std::ostringstream validation;
    if (!saveSong(validation,*candidate)) return 0;
    // A take must also be readable before it can be committed to the bank.
    std::istringstream input(validation.str());
    auto verified = std::make_unique<Song>();
    if (!loadSong(input,*verified)) return 0;
    auto& copy = candidate->bank[0];
    copy.id = song.nextCellId;
    const auto id = copy.id;
    song.bank[static_cast<size_t>(song.cellCount)] = std::move(copy);
    ++song.cellCount;
    ++song.nextCellId;
    song.preferredTakeId = id;
    return id;
}

std::uint32_t importCell(Song& destination, const Cell& source, const std::string& description) {
    if (source.id == 0 || source.revision == 0 || !safeText(description, 512) || description.empty() || destination.cellCount < 0 ||
        destination.cellCount >= int(destination.bank.size()) || destination.nextCellId == 0 ||
        destination.nextCellId == std::numeric_limits<std::uint32_t>::max() ||
        findCell(destination, destination.nextCellId)) return 0;
    auto candidate = std::make_unique<Song>();
    candidate->cellCount = 1;
    auto& copy = candidate->bank[0];
    copy = source;
    const auto entry = description + "; cell #" + std::to_string(source.id) + " rev " + std::to_string(source.revision) +
        "; parents #" + std::to_string(source.formulaOrigin.parentA) + "/#" + std::to_string(source.formulaOrigin.parentB);
    copy.importProvenance += (copy.importProvenance.empty() ? "" : " | ") + entry;
    if (!safeText(copy.importProvenance, 2048)) return 0;
    copy.id = 1; copy.revision = 1;
    copy.formulaOrigin.parentA = copy.formulaOrigin.parentB = 0;
    std::ostringstream validation;
    if (!saveSong(validation, *candidate)) return 0;
    copy.id = destination.nextCellId;
    destination.bank[static_cast<size_t>(destination.cellCount)] = std::move(copy);
    ++destination.cellCount;
    return destination.nextCellId++;
}
}
