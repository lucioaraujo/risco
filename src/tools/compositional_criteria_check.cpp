// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Lúcio de Araújo
#include "core/SongIO.hpp"
#include "core/FormulaSeed.hpp"
#include "core/SongPlayback.hpp"
#include "core/FormulaComposition.hpp"
#include "core/StemEffect.hpp"
#include "core/OutputStage.hpp"
#include "core/LogisticControl.hpp"
#include "core/MathControl.hpp"
#include "core/SongRender.hpp"
#include <array>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

static bool legacyVersion72From75(std::string& text) {
    if (text.rfind("RISCO_SONG_V75\n",0) != 0) return false;
    text.replace(0,std::string("RISCO_SONG_V75").size(),"RISCO_SONG_V72");
    size_t search = text.find("\nVERSION ");
    while (search != std::string::npos) {
        const size_t start = search + 1;
        const size_t end = text.find('\n',start);
        if (end == std::string::npos) return false;
        std::istringstream parser(text.substr(start,end-start));
        std::string tag,name;
        int parent = -1;
        size_t bytes = 0;
        if (!(parser >> tag >> std::quoted(name) >> parent >> bytes) || tag != "VERSION") return false;
        std::ostringstream oldHeader;
        oldHeader << "VERSION " << std::quoted(name) << ' ' << bytes;
        const auto replacement = oldHeader.str();
        text.replace(start,end-start,replacement);
        const size_t payloadStart = start + replacement.size() + 1;
        search = text.find("\nVERSION ",payloadStart + bytes);
    }
    const auto trailer = text.find("\nVERSIONS ");
    if (trailer == std::string::npos) return false;
    std::istringstream rootInput(text.substr(0,trailer + 1));
    std::ostringstream rootOutput;
    std::string rootRecord;
    while (std::getline(rootInput,rootRecord)) {
        if (rootRecord.rfind("EDIT ",0) == 0) {
            const auto separator = rootRecord.find_last_of(' ');
            if (separator == std::string::npos) return false;
            rootRecord.erase(separator);
        }
        rootOutput << rootRecord << '\n';
    }
    text = rootOutput.str() + text.substr(trailer + 1);
    return true;
}

static void removeMetricRecord(std::string& text) {
    for (const char* tag : {"METRIC ", "DIALOGUE ", "FX_PARAMS ", "DELAY_PARAMS ", "REVERB_PARAMS ", "REVERB_DECAY_PARAMS ", "BITCRUSH ", "BITCRUSH_PARAMS "}) {
        const auto start = text.find(tag);
        if (start != std::string::npos) { const auto end = text.find('\n', start); text.erase(start, end == std::string::npos ? text.size() - start : end - start + 1); }
    }
}
static void removeInputGestureCurveFields(std::string& text) {
    std::istringstream in(text);
    std::ostringstream out;
    std::string record;
    while (std::getline(in, record)) {
        if (record.rfind("EDIT ", 0) == 0)
            for (int field = 0; field < 4; ++field) {
                const auto separator = record.find_last_of(' ');
                if (separator == std::string::npos) break;
                record.erase(separator);
            }
        out << record << '\n';
    }
    text = out.str();
}
static void removeResponseRecord(std::string& text) {
    removeMetricRecord(text);
    const auto start = text.find("RESPONSE ");
    if (start != std::string::npos) { const auto end = text.find('\n', start); text.erase(start, end == std::string::npos ? text.size() - start : end - start + 1); }
    const auto historical = text.find("HISTORICAL_AY ");
    if (historical != std::string::npos) {
        const auto end = text.find('\n', historical);
        text.erase(historical, end == std::string::npos ? text.size() - historical : end - historical + 1);
    }
}
static void removeTakeRecord(std::string& text) {
    const auto prefix = text.find("\nTAKE ");
    if (prefix != std::string::npos) { const auto end = text.find('\n', prefix + 1); text.erase(prefix + 1, end == std::string::npos ? text.size() - prefix - 1 : end - prefix); }
    removeResponseRecord(text);
    for (const char* tag : {"PAN_AUTOMATION ","FX_AUTOMATION ","HISTORICAL_AY "}) {
        const auto start = text.find(tag);
        if (start != std::string::npos) { const auto end = text.find('\n', start); text.erase(start, end == std::string::npos ? text.size() - start : end - start + 1); }
    }
}
static void removeAyRecord(std::string& text) {
    removeTakeRecord(text);
    const auto prefix = text.find("\nAY ");
    if (prefix != std::string::npos) text.erase(prefix + 1, text.find('\n', prefix + 1) - prefix);
}
static void removeImportRecords(std::string& text) {
    removeAyRecord(text);
    std::istringstream in(text);
    std::ostringstream out;
    std::string record;
    while (std::getline(in, record))
        if (record.rfind("IMPORT ",0) != 0 && record.rfind("PAN_AUTOMATION ",0) != 0 &&
            record.rfind("FX_AUTOMATION ",0) != 0) out << record << '\n';
    text = out.str();
}
static void removeReadingStrength(std::string& text) {
    removeImportRecords(text);
    std::istringstream in(text);
    std::ostringstream out;
    std::string record;
    while (std::getline(in, record)) {
        if (record.rfind("EDIT ", 0) == 0)
            // Pre-V49 fixtures omit reading strength and the four V69 curve fields.
            for (int field = 0; field < 5; ++field) {
                const auto separator = record.find_last_of(' ');
                if (separator == std::string::npos) break;
                record.erase(separator);
            }
        out << record << '\n';
    }
    text = out.str();
}
static void removeLastField(std::string& value) {
    const auto separator = value.find_last_of(' ');
    if (separator != std::string::npos) value.erase(separator);
}
static void removeAutomationRecord(std::string& text) {
    removeReadingStrength(text);
    for (const char* tag : {"AUTOMATION ","PAN_AUTOMATION ","FX_AUTOMATION "}) {
        const auto start = text.find(tag);
        if (start != std::string::npos) text.erase(start,text.find('\n',start)-start+1);
    }
}
static void removeFilterRecord(std::string& text) {
    removeAutomationRecord(text);
    const auto reverbStart = text.find("REVERB ");
    if (reverbStart != std::string::npos) text.erase(reverbStart, text.find('\n', reverbStart) - reverbStart + 1);
    const auto start = text.find("FILTER ");
    if (start != std::string::npos) text.erase(start, text.find('\n', start) - start + 1);
}

static bool checkInternalRests() {
    auto song = std::make_unique<risco::Song>();
    song->bpm = 120; song->beatsPerBar = 4;
    song->interStemResponse = 1; song->responsePolicy = 7;
    song->stemRole[1] = risco::CellRole::bass;
    risco::Sequence call, destination;
    call.end = destination.end = 4;
    call.count = 2;
    call.notes[0] = {0, 0.25, 440, 0.2f};
    call.notes[1] = {0.75, 0.25, 550, 0.2f};
    destination.count = 1; destination.notes[0] = {2, 0.25, 220, 0.2f};
    const auto store = [&](const risco::Sequence& sequence) {
        return risco::storeFormulaEventCell(*song, "t", "Rest test", {}, sequence,
            risco::FormulaScalePreset::major, risco::FormulaScaleMotion::accompany, 0, 120);
    };
    const auto callId = store(call), destinationId = store(destination);
    if (!callId || !destinationId ||
        !risco::placeCell(*song,{callId,0,risco::CellSpan::twoBars,1,0,0,0}) ||
        !risco::placeCell(*song,{destinationId,0,risco::CellSpan::twoBars,1,0,0,1})) return false;
    auto result = std::make_unique<risco::SongPlayback>(risco::prepareSongPlayback(*song));
    if (result->responseCount != 1 || result->responses[0].cell.count != 1 ||
        result->responses[0].start != 2 || result->responses[0].cell.notes[0].start != 0.75 ||
        result->responseNotesRejected != 1 || result->responseGapBudgetExceeded ||
        result->responses[0].cell.notes[0].role != risco::VoiceRole::bass) {
        return false;
    }
    // The old block-level policy remains conservative even when notes leave gaps.
    song->responsePolicy = 4;
    if (risco::prepareSongPlayback(*song).responseCount != 0) return false;
    song->responsePolicy = 7;
    song->bank[1].formulaEvents.notes[0] = {2.1,0.05,220,0.2f};
    auto partialOverlap = std::make_unique<risco::SongPlayback>(risco::prepareSongPlayback(*song));
    if (partialOverlap->responseCount != 1 || partialOverlap->responses[0].cell.count != 1 ||
        partialOverlap->responses[0].cell.notes[0].start != 0.75) return false;
    song->bank[1].formulaEvents.notes[0] = {2,0.25,220,0.2f};
    std::swap(song->grid[0],song->grid[1]);
    auto reordered = std::make_unique<risco::SongPlayback>(risco::prepareSongPlayback(*song));
    if (reordered->responseCount != 1 || reordered->responses[0].cell.count != 1 ||
        reordered->responses[0].cell.notes[0].frequency != result->responses[0].cell.notes[0].frequency ||
        reordered->responses[0].cell.notes[0].start != 0.75) return false;
    std::swap(song->grid[0],song->grid[1]);
    std::stringstream saved;
    risco::Song loaded;
    if (!risco::saveSong(saved,*song) || !risco::loadSong(saved,loaded) || loaded.responsePolicy != 7) return false;
    auto older = saved.str();
    removeTakeRecord(older);
    removeInputGestureCurveFields(older);
    older.replace(0,std::string("RISCO_SONG_V65").size(),"RISCO_SONG_V52");
    std::istringstream incompatible(older);
    if (risco::loadSong(incompatible,loaded)) return false;
    song->responsePolicy = 6;
    std::stringstream oldPolicy; if (!risco::saveSong(oldPolicy,*song)) return false;
    older = oldPolicy.str();
    removeTakeRecord(older);
    removeInputGestureCurveFields(older);
    older.replace(0,std::string("RISCO_SONG_V65").size(),"RISCO_SONG_V52");
    std::istringstream compatible(older);
    if (!risco::loadSong(compatible,loaded) || loaded.responsePolicy != 6) return false;
    song->responsePolicy = 7;
    // Boundaries are half-open: an ending note can meet a response attack.
    song->bank[1].formulaEvents.notes[0] = {2.25,0.5,220,0.2f};
    result = std::make_unique<risco::SongPlayback>(risco::prepareSongPlayback(*song));
    if (result->responseCount != 1 || result->responses[0].cell.count != 2) return false;
    // Cropping a repeated cell keeps a note already held at the crop entrance.
    song->bank[1].formulaEvents.end = 1;
    song->bank[1].formulaEvents.notes[0] = {0,0.75,220,0.2f};
    song->grid[1].sourceStartBeat = 1; song->grid[1].repeats = 4;
    result = std::make_unique<risco::SongPlayback>(risco::prepareSongPlayback(*song));
    if (result->responseCount != 0 || result->responseNotesRejected != 2) return false;
    song->bank[1].formulaEvents.notes[0] = {0.5,0.5,220,0.2f};
    result = std::make_unique<risco::SongPlayback>(risco::prepareSongPlayback(*song));
    if (result->responseCount != 1 || result->responses[0].cell.count != 1 ||
        result->responses[0].cell.notes[0].start != 0.75) return false;
    // A full section leads to a later answer, without cutting or moving notes.
    song->grid[1].sourceStartBeat = 0; song->grid[1].repeats = 1;
    song->bank[1].formulaEvents.end = 6;
    song->bank[1].formulaEvents.notes[0] = {2,2,220,0.2f};
    song->bank[0].formulaEvents.end = 6; song->bank[0].formulaEvents.count = 4;
    song->bank[0].formulaEvents.notes[2] = {2,0.25,440,0.2f};
    song->bank[0].formulaEvents.notes[3] = {2.75,0.25,550,0.2f};
    result = std::make_unique<risco::SongPlayback>(risco::prepareSongPlayback(*song));
    if (result->responseCount != 1 || result->responses[0].start != 4 ||
        result->responses[0].cell.count != 2 || result->responseNotesRejected != 2) return false;
    const auto rawId = risco::storeFormulaCell(*song,"t","Raw",{});
    song->grid[1].cellId = rawId; song->grid[1].span = risco::CellSpan::fourBars;
    if (!rawId || risco::prepareSongPlayback(*song).responseCount != 0) return false;
    // Previous generated answers reserve actual notes, including with duplicate calls.
    song->placementCount = 2; song->grid[1] = song->grid[0];
    result = std::make_unique<risco::SongPlayback>(risco::prepareSongPlayback(*song));
    if (result->responseCount != 2 || result->responses[0].start != 2 ||
        result->responses[1].start != 4) return false;
    // Worst-case explicit silent material reaches the global budget deterministically.
    song->bank[0].formulaEvents = call;
    song->bank[0].formulaEvents.count = 64;
    song->bank[1].formulaEvents = call; song->bank[1].formulaEvents.count = 64;
    for (int n = 0; n < 64; ++n) {
        song->bank[0].formulaEvents.notes[static_cast<size_t>(n)] = {n/40.0,0.005,440,0.2f};
        song->bank[1].formulaEvents.notes[static_cast<size_t>(n)] = {n/40.0,0.005,220,0};
    }
    song->placementCount = 128;
    for (int p = 0; p < 128; ++p)
        song->grid[static_cast<size_t>(p)] = {p < 4 ? callId : destinationId,0,risco::CellSpan::twoBars,1,0,0,p < 4 ? 0 : 1};
    result = std::make_unique<risco::SongPlayback>(risco::prepareSongPlayback(*song));
    if (!result->responseGapBudgetExceeded || result->responseGapChecks != risco::SongPlayback::gapCheckLimit ||
        result->responseNotesRejected == 0) return false;
    return true;
}

static bool checkBoundedMathControl() {
    for (int map = 0; map < 5; ++map) {
        std::uint32_t previous = 0;
        for (std::uint32_t state : {0u, 1u, 32768u, 65535u, 70000u}) {
            const auto value = risco::boundedMathMap(state, static_cast<risco::BoundedMathMap>(map));
            if (value > 65535u) return false;
            previous = value;
        }
        (void) previous;
    }
    for (int destination = 0; destination < 6; ++destination) {
        for (std::uint32_t state : {0u, 32768u, 65535u}) {
            const auto value = risco::applyMathDestination(state, static_cast<risco::MathDestination>(destination));
            if (destination == 0 && (value < 0 || value > 6)) return false;
            if (destination == 1 && (value < 0 || value > 1)) return false;
            if (destination == 2 && (value < 1 || value > 4)) return false;
            if (destination == 3 && (value < 55 || value > 100)) return false;
            if (destination == 4 && (value < -2 || value > 2)) return false;
            if (destination == 5 && (value < 1 || value > 4)) return false;
        }
    }
    return true;
}

static bool checkExtendedForms() {
    const auto form8 = risco::phraseFromSeed(417,3,8,120,15,4);
    const auto form8Again = risco::phraseFromSeed(417,3,8,120,15,4);
    const auto form9 = risco::phraseFromSeed(417,3,9,120,15,4);
    if (form8.count <= 0 || form9.count <= 0 || form8.count > 64 || form9.count > 64 ||
        form8.count != form8Again.count || form8.end != 8 || form9.end != 8) return false;
    for (int n = 0; n < form8.count; ++n) {
        const auto& a = form8.notes[static_cast<size_t>(n)];
        const auto& b = form8Again.notes[static_cast<size_t>(n)];
        if (a.start != b.start || a.duration != b.duration || a.frequency != b.frequency || a.gain != b.gain ||
            !std::isfinite(a.start) || !std::isfinite(a.duration) || !std::isfinite(a.frequency) ||
            !std::isfinite(a.gain) || a.start < 0 || a.duration <= 0 || a.start + a.duration > form8.end + 1e-9) return false;
    }
    const auto attackMask = [](const risco::Sequence& phrase, int bar) {
        std::array<bool,8> result{};
        for (int n = 0; n < phrase.count; ++n) {
            const auto& note = phrase.notes[static_cast<size_t>(n)];
            if (note.role != risco::VoiceRole::lead || note.start < bar * 2 || note.start >= (bar + 1) * 2) continue;
            const int slot = int(std::lround((note.start - bar * 2) * 4));
            if (slot >= 0 && slot < 8) result[static_cast<size_t>(slot)] = true;
        }
        return result;
    };
    // V8 repeats A's rhythm as A' and return; V9 brings A's rhythm back only at the end.
    if (attackMask(form8,0) != attackMask(form8,1) || attackMask(form8,0) != attackMask(form8,3) ||
        attackMask(form9,0) != attackMask(form9,3)) return false;
    std::array<float,4> bass8{}, bass9{};
    int bassCount8 = 0, bassCount9 = 0;
    for (int n = 0; n < form8.count; ++n) if (form8.notes[static_cast<size_t>(n)].role == risco::VoiceRole::bass)
        bass8[static_cast<size_t>(bassCount8++)] = form8.notes[static_cast<size_t>(n)].gain;
    for (int n = 0; n < form9.count; ++n) if (form9.notes[static_cast<size_t>(n)].role == risco::VoiceRole::bass)
        bass9[static_cast<size_t>(bassCount9++)] = form9.notes[static_cast<size_t>(n)].gain;
    if (bassCount8 != 4 || bassCount9 != 4 || !(bass8[2] > bass8[1] && bass8[1] > bass8[0]) ||
        !(bass9[2] > bass9[1] && bass9[1] > bass9[0] && bass9[2] > bass9[3])) return false;
    float finalA = 0, finalB = 0;
    for (int n = 0; n < form8.count; ++n) if (form8.notes[static_cast<size_t>(n)].role == risco::VoiceRole::lead && form8.notes[static_cast<size_t>(n)].start >= 6) {
        finalA = finalB; finalB = form8.notes[static_cast<size_t>(n)].frequency;
    }
    if (finalA <= 0 || finalA != finalB) return false;
    bool differs = form8.count != form9.count;
    for (int n = 0; n < std::min(form8.count,form9.count) && !differs; ++n)
        differs = form8.notes[static_cast<size_t>(n)].frequency != form9.notes[static_cast<size_t>(n)].frequency ||
                  form8.notes[static_cast<size_t>(n)].gain != form9.notes[static_cast<size_t>(n)].gain;
    if (!differs) return false;
    const risco::PhraseMeters meters{{3,5,8,7}};
    const auto mixed = risco::phraseFromSeed(417,3,9,120,15,4,false,meters);
    const auto masked = risco::phraseFromSeed(417,3,9,120,14,4,false,meters);
    if (mixed.end != 9 || masked.end != mixed.end || masked.count >= mixed.count) return false;
    for (int n = 0; n < masked.count; ++n) if (masked.notes[static_cast<size_t>(n)].start < 1.5) return false;
    const risco::PhraseSectionBars sectionBars{{1,2,1,1}};
    const auto stretched = risco::stretchSections(mixed, risco::phraseMeterWeights(meters,4), sectionBars);
    if (stretched.count != mixed.count || std::abs(stretched.end - 11.5) > 1e-9 ||
        stretched.notes[0].start != mixed.notes[0].start) return false;
    auto song = std::make_unique<risco::Song>();
    const auto id8 = risco::storeGeneratedCell(*song,417,3,"Arco",{},8);
    const auto id9 = risco::storeGeneratedCell(*song,417,3,"Espiral",{},9);
    if (!id8 || !id9) return false;
    song->bank[0].phraseMeters = meters; song->bank[1].phraseMeters = meters;
    song->bank[0].phraseSectionBars = sectionBars;
    std::stringstream file;
    risco::Song loaded;
    if (!risco::saveSong(file,*song) || !risco::loadSong(file,loaded) ||
        loaded.bank[0].phraseVersion != 8 || loaded.bank[1].phraseVersion != 9 ||
        loaded.bank[0].phraseMeters != meters || loaded.bank[0].phraseSectionBars != sectionBars) return false;
    auto old = file.str();
    old.replace(0,std::string("RISCO_SONG_V65").size(),"RISCO_SONG_V54");
    std::istringstream unsupported(old);
    if (risco::loadSong(unsupported,loaded)) return false;
    return true;
}

static bool checkPhraseTakes() {
    auto song = std::make_unique<risco::Song>();
    const auto originalId = risco::storeGeneratedCell(*song,1234,2,"Original",{},6);
    if (!originalId || !risco::placeCell(*song,{originalId,0,risco::CellSpan::fourBars,1,0,0,0})) return false;
    auto original = std::make_unique<risco::Cell>(song->bank[0]);
    auto baseline = std::make_unique<risco::SongPlayback>(risco::prepareSongPlayback(*song));
    auto recipe = std::make_unique<risco::Cell>(*original);
    recipe->name = "Take · edição";
    recipe->sectionEdits[1] = {0.5f,0.6f,-1};
    recipe->phraseMeters = {{3,5,8,7}};
    recipe->phraseSectionMask = 5;
    recipe->role = risco::CellRole::melody;
    const auto takeId = risco::storePhraseTake(*song,*recipe);
    if (!takeId || takeId == originalId || song->preferredTakeId != takeId || song->cellCount != 2 ||
        song->placementCount != 1 || song->grid[0].cellId != originalId ||
        song->bank[0].revision != original->revision || song->bank[0].sectionEdits[1].gain != 1 ||
        song->bank[0].phraseMeters != original->phraseMeters || song->bank[1].revision != 1) return false;
    auto unchanged = std::make_unique<risco::SongPlayback>(risco::prepareSongPlayback(*song));
    if (unchanged->end != baseline->end || unchanged->placements[0].cell.count != baseline->placements[0].cell.count) return false;
    for (int sample = 0; sample < 2000; ++sample) {
        const double time = sample / 250.0;
        if (risco::songSampleAt(*unchanged,time,risco::WaveMode::ayToneStudy) !=
            risco::songSampleAt(*baseline,time,risco::WaveMode::ayToneStudy)) return false;
    }
    std::stringstream file;
    auto loaded = std::make_unique<risco::Song>();
    if (!risco::saveSong(file,*song) || !risco::loadSong(file,*loaded) || loaded->preferredTakeId != takeId ||
        loaded->bank[1].name != recipe->name || loaded->bank[1].phraseSeed != 1234 ||
        loaded->bank[1].phraseVariation != 2 || loaded->bank[1].phraseVersion != 6 ||
        loaded->bank[1].phraseSectionMask != 5 || loaded->bank[1].phraseMeters != recipe->phraseMeters ||
        loaded->bank[1].sectionEdits[1].density != 0.5f || loaded->bank[1].sectionEdits[1].gain != 0.6f ||
        loaded->bank[1].sectionEdits[1].octaves != -1 || loaded->bank[1].role != recipe->role) return false;
    loaded->grid[0].cellId = takeId;
    auto alternative = std::make_unique<risco::SongPlayback>(risco::prepareSongPlayback(*loaded));
    if (alternative->end == baseline->end) return false;
    auto legacy = file.str(); removeTakeRecord(legacy); removeInputGestureCurveFields(legacy);
    legacy.replace(0,std::string("RISCO_SONG_V65").size(),"RISCO_SONG_V53");
    std::istringstream v53(legacy);
    if (!risco::loadSong(v53,*loaded) || loaded->preferredTakeId != 0 || loaded->cellCount != 2) return false;
    for (const char* bad : {"TAKE -1","TAKE +1","TAKE nan","TAKE 4294967296","TAKE 9999","TAKE","TAKE 0 extra"}) {
        auto text = file.str(); const auto pos = text.find("\nTAKE ") + 1;
        text.replace(pos,text.find('\n',pos)-pos,bad);
        std::istringstream invalid(text);
        if (risco::loadSong(invalid,*loaded) || loaded->cellCount != 2 || loaded->preferredTakeId != 0) return false;
    }
    recipe->sectionEdits[0].gain = std::nanf("");
    const auto nextId = song->nextCellId;
    if (risco::storePhraseTake(*song,*recipe) || song->cellCount != 2 || song->nextCellId != nextId ||
        song->preferredTakeId != takeId) return false;
    recipe->sectionEdits[0].gain = 1;
    recipe->generatedPhrase = false;
    if (risco::storePhraseTake(*song,*recipe)) return false;
    recipe->generatedPhrase = true;
    recipe->phraseSectionMask = 0;
    if (risco::storePhraseTake(*song,*recipe)) return false;
    recipe->phraseSectionMask = 5;
    recipe->inscription.count = 13;
    if (risco::storePhraseTake(*song,*recipe) || song->cellCount != 2 || song->preferredTakeId != takeId) return false;
    recipe->inscription.count = 0;
    song->preferredTakeId = 9999;
    std::stringstream orphan;
    if (risco::saveSong(orphan,*song) || !orphan.str().empty()) return false;
    song->preferredTakeId = takeId;
    while (song->cellCount < int(song->bank.size()))
        if (!risco::storeGeneratedCell(*song,1234,0,"Fill",{},6)) return false;
    const auto fullNext = song->nextCellId;
    if (risco::storePhraseTake(*song,*recipe) || song->cellCount != 64 ||
        song->nextCellId != fullNext || song->preferredTakeId != takeId || song->placementCount != 1) return false;
    return true;
}

int main() {
    auto song = std::make_unique<risco::Song>();
    song->beatsPerBar = 5;
    const auto id = risco::storeGeneratedCell(*song, 26, 0, "Baixo", {}, 2, 0x05);
    if (id == 0) return 1;
    song->bank[0].role = risco::CellRole::bass;
    song->bank[0].formulaScale = risco::FormulaScalePreset::misheberakh;
    song->bank[0].formulaTonic = 9;
    song->masterGain = 0.72f;
    song->interStemResponse = 9;
    song->formulaMetricGrouping = 1;
    song->stemRole = {{risco::CellRole::bass, risco::CellRole::melody, risco::CellRole::harmony}};
    if (!risco::placeCell(*song, {id, 0, risco::CellSpan::bar, 7, 0, 0, 0})) return 2;

    std::stringstream stream;
    if (!risco::saveSong(stream, *song) || stream.str().find("RISCO_SONG_V71\n") != 0) return 3;
    auto loaded = std::make_unique<risco::Song>();
    if (!risco::loadSong(stream, *loaded) || loaded->formulaMetricGrouping != 1 ||
        loaded->stemRole != song->stemRole ||
        loaded->bank[0].role != risco::CellRole::bass ||
        loaded->bank[0].formulaScale != risco::FormulaScalePreset::misheberakh ||
        loaded->bank[0].formulaTonic != 9 ||
        loaded->bank[0].phraseSectionMask != 0x05 ||
        loaded->beatsPerBar != 5 || loaded->grid[0].repeats != 7 ||
        loaded->masterGain < 0.719f || loaded->masterGain > 0.721f ||
        loaded->interStemResponse != 9) return 4;
    auto namedSnapshot = *song;
    namedSnapshot.bpm = 96.0;
    std::stringstream namedSnapshotStream;
    if (!risco::saveSong(namedSnapshotStream,namedSnapshot)) return 328;
    const std::vector<risco::NamedSongVersionRecord> namedVersions{{"Versão autoral",namedSnapshotStream.str()}};
    std::stringstream versionedProject;
    if (!risco::saveSong(versionedProject,*song,namedVersions,0) ||
        versionedProject.str().find("RISCO_SONG_V75\n") != 0) return 329;
    auto versionedLoaded = std::make_unique<risco::Song>();
    std::vector<risco::NamedSongVersionRecord> restoredVersions;
    int restoredActiveVersion = -1;
    if (!risco::loadSong(versionedProject,*versionedLoaded,&restoredVersions,&restoredActiveVersion) ||
        restoredVersions.size() != 1 || restoredVersions[0].name != "Versão autoral" ||
        restoredVersions[0].parentIndex != -1 || restoredActiveVersion != 0) return 330;
    std::istringstream restoredSnapshotStream(restoredVersions[0].serializedSong);
    risco::Song restoredSnapshot;
    if (!risco::loadSong(restoredSnapshotStream,restoredSnapshot) || restoredSnapshot.bpm != 96.0) return 331;
    auto childSnapshot = std::make_unique<risco::Song>(namedSnapshot);
    childSnapshot->bpm = 112.0;
    std::stringstream childSnapshotStream;
    if (!risco::saveSong(childSnapshotStream,*childSnapshot)) return 366;
    auto alternateSnapshot = std::make_unique<risco::Song>(namedSnapshot);
    alternateSnapshot->bpm = 132.0;
    std::stringstream alternateSnapshotStream;
    if (!risco::saveSong(alternateSnapshotStream,*alternateSnapshot)) return 367;
    const std::vector<risco::NamedSongVersionRecord> branchVersions{
        {"Raiz",namedSnapshotStream.str(),-1},
        {"Filha",childSnapshotStream.str(),0},
        {"Alternativa",alternateSnapshotStream.str(),0}};
    std::stringstream branchedProject;
    if (!risco::saveSong(branchedProject,*song,branchVersions,1)) return 368;
    std::vector<risco::NamedSongVersionRecord> restoredBranches;
    int restoredBranchActive = -1;
    auto branchedSong = std::make_unique<risco::Song>();
    if (!risco::loadSong(branchedProject,*branchedSong,&restoredBranches,&restoredBranchActive) ||
        restoredBranches.size() != 3 || restoredBranchActive != 1 ||
        restoredBranches[0].parentIndex != -1 || restoredBranches[1].parentIndex != 0 ||
        restoredBranches[2].parentIndex != 0) return 369;
    auto malformedBranchProject = branchedProject.str();
    const auto childHeader = malformedBranchProject.find("VERSION \"Filha\" 0 ");
    if (childHeader == std::string::npos) return 377;
    malformedBranchProject.replace(childHeader + std::string("VERSION \"Filha\" ").size(),1,"2");
    std::istringstream malformedBranchStream(malformedBranchProject);
    if (risco::loadSong(malformedBranchStream,*branchedSong)) return 378;
    auto legacyBranchedProject = branchedProject.str();
    if (!legacyVersion72From75(legacyBranchedProject)) return 371;
    std::istringstream legacyBranchStream(legacyBranchedProject);
    std::vector<risco::NamedSongVersionRecord> flattenedLegacyVersions;
    int flattenedLegacyActive = -1;
    if (!risco::loadSong(legacyBranchStream,*branchedSong,&flattenedLegacyVersions,&flattenedLegacyActive)) return 373;
    if (flattenedLegacyVersions.size() != 3 || flattenedLegacyActive != 1 ||
        std::any_of(flattenedLegacyVersions.begin(),flattenedLegacyVersions.end(),
            [](const auto& version) { return version.parentIndex != -1; })) return 374;
    auto invalidBranch = branchVersions;
    invalidBranch[1].parentIndex = 2;
    std::stringstream invalidBranchProject;
    if (risco::saveSong(invalidBranchProject,*song,invalidBranch,0) || !invalidBranchProject.str().empty()) return 370;
    auto invalidVersions = namedVersions;
    invalidVersions[0].serializedSong += "hidden data\n";
    std::stringstream invalidVersionedProject;
    if (risco::saveSong(invalidVersionedProject,*song,invalidVersions,0) || !invalidVersionedProject.str().empty()) return 332;
    const std::vector<risco::NamedSongVersionRecord> oversizedVersions{{"Oversized",std::string(risco::maxNamedSongVersionBytes + 1,'x')}};
    std::stringstream oversizedVersionProject;
    if (risco::saveSong(oversizedVersionProject,*song,oversizedVersions,0) || !oversizedVersionProject.str().empty()) return 334;
    std::stringstream legacyVersionShelf;
    std::vector<risco::NamedSongVersionRecord> legacyVersions;
    int legacyActiveVersion = 5;
    if (!risco::saveSong(legacyVersionShelf,*song) ||
        !risco::loadSong(legacyVersionShelf,*versionedLoaded,&legacyVersions,&legacyActiveVersion) ||
        !legacyVersions.empty() || legacyActiveVersion != -1) return 333;
    std::istringstream currentRecords(stream.str());
    std::ostringstream compatible;
    std::string record;
    while (std::getline(currentRecords, record)) {
    if (record == "RISCO_SONG_V68" || record == "RISCO_SONG_V69" || record == "RISCO_SONG_V70" || record == "RISCO_SONG_V71") record = "RISCO_SONG_V30";
        if (record.rfind("AY ", 0) == 0 || record.rfind("HISTORICAL_AY ",0) == 0 || record.rfind("METRIC ",0) == 0 || record.rfind("DIALOGUE ",0) == 0 || record.rfind("FX_PARAMS ",0) == 0 || record.rfind("DELAY_PARAMS ",0) == 0 || record.rfind("REVERB_PARAMS ",0) == 0 || record.rfind("REVERB_DECAY_PARAMS ",0) == 0 || record.rfind("BITCRUSH ",0) == 0 || record.rfind("BITCRUSH_PARAMS ",0) == 0 || record.rfind("TAKE ",0) == 0 || record.rfind("RESPONSE ",0) == 0) continue;
        if (record.rfind("IMPORT ", 0) == 0) continue;
        if (record.rfind("AUTOMATION ", 0) == 0 || record.rfind("PAN_AUTOMATION ",0) == 0 || record.rfind("FX_AUTOMATION ",0) == 0) continue;
        if (record.rfind("REVERB ", 0) == 0) continue;
        if (record.rfind("FILTER ", 0) == 0) continue;
        if (record.rfind("DELAY ", 0) == 0) continue;
        if (record.rfind("EDIT ", 0) == 0) continue;
        if (record.rfind("SETTINGS ", 0) == 0) continue;
        if (record.rfind("CELL ", 0) == 0)
            for (int field = 0; field < 3; ++field) removeLastField(record);
        compatible << record << '\n';
    }
    auto legacyV24 = compatible.str();
    legacyV24.replace(0, std::string("RISCO_SONG_V30").size(), "RISCO_SONG_V24");
    const auto legacyCell = legacyV24.find("CELL ");
    const auto legacyCellEnd = legacyV24.find('\n', legacyCell);
    const auto legacyMask = legacyV24.rfind(' ', legacyCellEnd);
    if (legacyCell == std::string::npos || legacyCellEnd == std::string::npos ||
        legacyMask == std::string::npos) return 14;
    legacyV24.erase(legacyMask, legacyCellEnd - legacyMask);
    std::istringstream legacyV24Stream(legacyV24);
    auto v24Storage = std::make_unique<risco::Song>();
    auto& v24Song = *v24Storage;
    if (!risco::loadSong(legacyV24Stream, v24Song) ||
        v24Song.bank[0].phraseSectionMask != 0x0f) return 15;
    auto legacyV25 = compatible.str();
    legacyV25.replace(0, std::string("RISCO_SONG_V30").size(), "RISCO_SONG_V25");
    std::istringstream legacyV25Stream(legacyV25);
    auto v25Storage = std::make_unique<risco::Song>();
    auto& v25Song = *v25Storage;
    if (!risco::loadSong(legacyV25Stream, v25Song) ||
        v25Song.bank[0].phraseSectionMask != 0x05) return 28;
    const auto reprise = risco::suggestPlacement(*loaded, id, risco::CellSpan::bar, 1, 0);
    if (!reprise.valid || reprise.startBeat != 35 || reprise.stem != 0 ||
        !reprise.motifOnStem || !reprise.roleOnStem || loaded->placementCount != 1) return 5;

    const auto harmonyId = risco::storeGeneratedCell(*loaded, 27, 0, "Resposta", {}, 2);
    loaded->bank[1].role = risco::CellRole::harmony;
    const auto response = risco::suggestPlacement(*loaded, harmonyId,
                                                  risco::CellSpan::bar, 1, 0);
    if (!response.valid || response.startBeat != 0 || response.stem != 2 ||
        !response.stemRoleMatch || response.concurrentStems != 1 ||
        loaded->placementCount != 1) return 6;
    const auto bassId = risco::storeGeneratedCell(*loaded, 28, 0, "Outro baixo", {}, 2);
    loaded->bank[2].role = risco::CellRole::bass;
    const auto nextBass = risco::suggestPlacement(*loaded, bassId,
                                                  risco::CellSpan::bar, 1, 0);
    if (!nextBass.valid || nextBass.startBeat != 35 || nextBass.stem != 0 ||
        !nextBass.roleOnStem || nextBass.sameRoleConcurrent) return 7;

    std::istringstream savedV16(compatible.str());
    std::ostringstream v15;
    std::string line;
    bool songHeader = true;
    while (std::getline(savedV16, line)) {
        if (line == "RISCO_SONG_V30") line = "RISCO_SONG_V15";
        else if (songHeader) {
            for (int field = 0; field < 6; ++field) removeLastField(line);
            songHeader = false;
        }
        if (line.rfind("CELL ", 0) == 0) {
            removeLastField(line); // V25 section mask did not exist in V15.
            const auto spaceFromEnd = [&line](int count) {
                auto position = line.size();
                for (int i = 0; i < count; ++i) {
                    if (position == 0) return std::string::npos;
                    position = line.find_last_of(' ', position - 1);
                    if (position == std::string::npos) return position;
                }
                return position;
            };
            const auto beforeMotion = spaceFromEnd(5);
            const auto beforeTonic = spaceFromEnd(6);
            const auto beforeScale = spaceFromEnd(7);
            if (beforeMotion == std::string::npos || beforeTonic == std::string::npos ||
                beforeScale == std::string::npos) return 8;
            line.erase(beforeTonic, beforeMotion - beforeTonic);
            const auto oldScaleStart = spaceFromEnd(6);
            const auto oldScaleEnd = spaceFromEnd(5);
            if (oldScaleEnd == std::string::npos || oldScaleStart == std::string::npos) return 8;
            line.replace(oldScaleStart + 1, oldScaleEnd - oldScaleStart - 1, "0");
        }
        v15 << line << '\n';
    }
    std::istringstream v15Stream(v15.str());
    auto v15Song = std::make_unique<risco::Song>();
    if (!risco::loadSong(v15Stream, *v15Song)) {
        std::cerr << "V15 fixture rejected by reader:\n" << v15.str();
        return 9;
    }
    if (v15Song->stemRole != risco::Song{}.stemRole) return 9;
    if (v15Song->bank[0].role != risco::CellRole::bass) {
        std::cerr << "V15 role mismatch: " << int(v15Song->bank[0].role) << '\n';
        return 9;
    }
    if (v15Song->masterGain < 0.349f || v15Song->masterGain > 0.351f) {
        std::cerr << "V15 MASTER default mismatch: " << v15Song->masterGain << '\n';
        return 9;
    }

    std::istringstream saved(compatible.str());
    std::ostringstream v13;
    songHeader = true;
    while (std::getline(saved, line)) {
        if (line == "RISCO_SONG_V30") line = "RISCO_SONG_V13";
        else if (songHeader) {
            for (int field = 0; field < 6; ++field) removeLastField(line);
            songHeader = false;
        }
        else if (line.rfind("CELL ", 0) == 0)
            for (int field = 0; field < 10; ++field) removeLastField(line);
        v13 << line << '\n';
    }
    std::istringstream legacy(v13.str());
    auto oldSong = std::make_unique<risco::Song>();
    if (!risco::loadSong(legacy, *oldSong) || oldSong->bank[0].role != risco::CellRole::free ||
        oldSong->stemRole != risco::Song{}.stemRole)
        return 10;

    std::string invalid = stream.str();
    const auto firstCell = invalid.find("CELL ");
    const auto lineEnd = invalid.find('\n', firstCell);
    if (firstCell == std::string::npos || lineEnd == std::string::npos) return 11;
    invalid[lineEnd - 1] = '9'; // Swing above 0.5 is invalid in V32.
    std::istringstream badStream(invalid);
    if (risco::loadSong(badStream, *oldSong)) return 12;

    const auto fullPhrase = risco::phraseFromSeed(26, 0, 2, 120.0, 0x0f);
    const auto editedPhrase = risco::phraseFromSeed(26, 0, 2, 120.0, 0x05);
    if (editedPhrase.count >= fullPhrase.count || editedPhrase.end != fullPhrase.end) return 13;

    auto responseStorage = std::make_unique<risco::Song>();
    auto& responseSong = *responseStorage;
    std::uint64_t responseSeed = 0;
    for (std::uint64_t seed = 1; seed <= 256 && responseSeed == 0; ++seed) {
        const auto phrase = risco::phraseFromSeed(seed, 0, 3, 120.0, 0x02);
        float firstPitch = 0.0f;
        bool hasContour = false;
        for (int n = 0; n < phrase.count; ++n) {
            const auto& note = phrase.notes[static_cast<std::size_t>(n)];
            if (note.start < 2.0 || note.start >= 4.0) continue;
            if (firstPitch == 0.0f) firstPitch = note.frequency;
            else if (std::abs(note.frequency - firstPitch) > 0.01f) hasContour = true;
        }
        if (hasContour) responseSeed = seed;
    }
    const auto responseId = risco::storeGeneratedCell(responseSong, responseSeed, 0,
        "Seção B", {}, 3, 0x02);
    if (responseSeed == 0 || responseId == 0) return 16;
    responseSong.interStemResponse = 1;
    if (!risco::placeCell(responseSong,
            {responseId, 0, risco::CellSpan::fourBars, 1, 0, 0, 0})) return 17;
    const auto responsePlayback = std::make_unique<risco::SongPlayback>(risco::prepareSongPlayback(responseSong));
    if (responsePlayback->responseCount != 1 ||
        responsePlayback->responses[0].stem != 1 ||
        std::abs(responsePlayback->responses[0].start - 4.0) > 1.0e-9 ||
        responsePlayback->responses[0].cell.count == 0) return 18;
    const auto& callCell = responsePlayback->placements[0].cell;
    double pitchLogSum = 0.0;
    int sourceNotes = 0;
    for (int n = 0; n < callCell.count; ++n) {
        const auto& note = callCell.notes[static_cast<std::size_t>(n)];
        if (note.start < 2.0 || note.start >= 4.0 || note.frequency <= 0.0f || note.gain <= 0.0f) continue;
        pitchLogSum += std::log(double(note.frequency));
        ++sourceNotes;
    }
    if (sourceNotes < 2) return 19;
    const double pitchCenter = std::exp(pitchLogSum / sourceNotes);
    const auto& answerCell = responsePlayback->responses[0].cell;
    bool contourChanged = false;
    int matchedNotes = 0;
    std::vector<bool> matchedCalls(static_cast<std::size_t>(callCell.count), false);
    for (int a = 0; a < answerCell.count; ++a) {
        const auto& answer = answerCell.notes[static_cast<std::size_t>(a)];
        bool matched = false;
        for (int n = 0; n < callCell.count && !matched; ++n) {
            const auto& call = callCell.notes[static_cast<std::size_t>(n)];
            if (matchedCalls[static_cast<std::size_t>(n)] || call.start < 2.0 || call.start >= 4.0 ||
                call.frequency <= 0.0f || call.gain <= 0.0f ||
                std::abs((call.start - 2.0) - answer.start) > 1.0e-9) continue;
            const double expected = pitchCenter * pitchCenter / call.frequency * std::pow(2.0, -5.0 / 12.0);
            if (std::abs(expected - answer.frequency) > 0.01 ||
                answer.duration > call.duration + 1.0e-9 ||
                std::abs(answer.gain - call.gain * 0.72f) > 1.0e-5f) continue;
            if (std::abs(answer.frequency - call.frequency * std::pow(2.0, -5.0 / 12.0)) > 0.01)
                contourChanged = true;
            matched = true;
            matchedCalls[static_cast<std::size_t>(n)] = true;
            ++matchedNotes;
        }
        if (!matched) return 21;
    }
    if (!contourChanged || matchedNotes != sourceNotes) return 22;

    auto dynamicStorage = std::make_unique<risco::Song>();
    auto& dynamicSong = *dynamicStorage;
    const auto dynamicId = risco::storeGeneratedCell(dynamicSong, 73, 0, "Arco", {}, 5);
    if (dynamicId == 0) return 23;
    std::stringstream dynamicFile;
    if (!risco::saveSong(dynamicFile, dynamicSong)) return 24;
    auto dynamicLoadedStorage = std::make_unique<risco::Song>();
    auto& dynamicLoaded = *dynamicLoadedStorage;
    if (!risco::loadSong(dynamicFile, dynamicLoaded) ||
        dynamicLoaded.bank[0].phraseVersion != 5) return 25;
    std::array<std::array<float, 4>, 3> arcs{};
    for (unsigned variation = 0; variation < 3; ++variation) {
        const auto phrase = risco::phraseFromSeed(73, variation, 5, 120.0);
        int bar = 0;
        for (int n = 0; n < phrase.count; ++n) {
            const auto& note = phrase.notes[static_cast<std::size_t>(n)];
            if (note.role == risco::VoiceRole::bass && bar < 4)
                arcs[variation][static_cast<std::size_t>(bar++)] = note.gain;
        }
        if (bar != 4) return 26;
    }
    if (arcs[0] == arcs[1] || arcs[1] == arcs[2] || arcs[0] == arcs[2]) return 27;

    risco::BytebeatProgram connectedProgram;
    std::string error;
    if (!risco::compileBytebeat("t^(t>>7)", connectedProgram, error)) return 29;
    for (int preset = 0; preset <= int(risco::FormulaScalePreset::prometheus); ++preset) {
        const auto connected = risco::composeFormulaEvents(connectedProgram, 120, 4,
            static_cast<risco::FormulaScalePreset>(preset), risco::FormulaScaleMotion::connect);
        if (connected.sequence.count < 2) return 30;
        double previous = -1;
        for (int n = 0; n < connected.sequence.count; ++n) {
            const double midi = 69.0 + 12.0 * std::log2(connected.sequence.notes[n].frequency / 440.0);
            if (midi < 47.999 || midi > 84.001 || (previous >= 0 && std::abs(midi - previous) > 12.001)) {
                std::cerr << "Connected range: preset " << preset << " note " << n << " midi " << midi << " previous " << previous << '\n';
                return 31;
            }
            previous = midi;
        }
    }
    const auto connected = risco::composeFormulaEvents(connectedProgram, 120, 4,
        risco::FormulaScalePreset::major, risco::FormulaScaleMotion::connect);
    auto connectedStorage = std::make_unique<risco::Song>();
    auto& connectedSong = *connectedStorage;
    if (!risco::storeFormulaEventCell(connectedSong, "t^(t>>7)", "Encadear", {},
        connected.sequence, risco::FormulaScalePreset::major, risco::FormulaScaleMotion::connect, 0, 120)) return 32;
    std::stringstream connectedFile;
    if (!risco::saveSong(connectedFile, connectedSong)) return 33;
    auto connectedLoadedStorage = std::make_unique<risco::Song>();
    auto& connectedLoaded = *connectedLoadedStorage;
    if (!risco::loadSong(connectedFile, connectedLoaded) ||
        connectedLoaded.bank[0].formulaMotion != risco::FormulaScaleMotion::connect) return 34;
    for (int tonic = 0; tonic < 12; ++tonic) {
        const auto cadence = risco::composeFormulaEvents(connectedProgram, 120, 4,
            risco::FormulaScalePreset::major, risco::FormulaScaleMotion::cadence, tonic);
        const auto control = risco::composeFormulaEvents(connectedProgram, 120, 4,
            risco::FormulaScalePreset::major, risco::FormulaScaleMotion::connect, tonic);
        if (cadence.sequence.count == 0 || cadence.sequence.count != control.sequence.count) return 35;
        for (int n = 0; n < cadence.sequence.count; ++n) {
            const auto& note = cadence.sequence.notes[n];
            const auto& original = control.sequence.notes[n];
            if (note.start != original.start || note.duration != original.duration || note.gain != original.gain) return 36;
            if (n + 1 < cadence.sequence.count && note.frequency != original.frequency) return 37;
        }
        const auto& last = cadence.sequence.notes[cadence.sequence.count - 1];
        const double midi = 69.0 + 12.0 * std::log2(last.frequency / 440.0);
        if (std::abs(std::remainder(midi - tonic, 12.0)) > 0.001) return 38;
    }
    connectedSong.bank[0].formulaMotion = risco::FormulaScaleMotion::cadence;
    std::stringstream cadenceFile;
    if (!risco::saveSong(cadenceFile, connectedSong) || !risco::loadSong(cadenceFile, connectedLoaded) ||
        connectedLoaded.bank[0].formulaMotion != risco::FormulaScaleMotion::cadence) return 39;
    for (int beats = 2; beats <= 7; ++beats) {
        for (int preset = 0; preset <= int(risco::FormulaScalePreset::prometheus); ++preset) {
            const auto phrase = risco::composeFormulaEvents(connectedProgram, 120, beats,
                static_cast<risco::FormulaScalePreset>(preset), risco::FormulaScaleMotion::accompany);
            int bass = 0, harmony = 0, melody = 0;
            if (phrase.sequence.count > int(phrase.sequence.notes.size())) return 40;
            for (int n = 0; n < phrase.sequence.count; ++n) {
                const auto& note = phrase.sequence.notes[n];
                if (note.role == risco::VoiceRole::bass) ++bass;
                else if (note.role == risco::VoiceRole::harmony) ++harmony;
                else ++melody;
                if (!std::isfinite(note.frequency) || note.frequency <= 0 ||
                    note.start + note.duration > phrase.sequence.end + 1e-9) return 41;
            }
            if (bass < 1 || harmony != bass * 2 || melody < 1) return 42;
        }
    }
    risco::BytebeatProgram silence;
    if (!risco::compileBytebeat("0", silence, error)) return 43;
    if (risco::composeFormulaEvents(silence, 120, 4, risco::FormulaScalePreset::major,
        risco::FormulaScaleMotion::accompany).sequence.count != 0) return 44;
    const auto accompanied = risco::composeFormulaEvents(connectedProgram, 120, 4,
        risco::FormulaScalePreset::major, risco::FormulaScaleMotion::accompany);
    const std::array<int, 4> expectedBass{{36, 41, 43, 36}};
    int bassIndex = 0;
    for (int n = 0; n < accompanied.sequence.count; ++n) {
        const auto& note = accompanied.sequence.notes[n];
        if (note.role != risco::VoiceRole::bass) continue;
        if (bassIndex >= 4) return 47;
        const double midi = 69.0 + 12.0 * std::log2(note.frequency / 440.0);
        if (std::abs(midi - expectedBass[static_cast<size_t>(bassIndex++)]) > 0.001) return 48;
    }
    if (bassIndex != 4) return 49;
    auto harmonyStorage = std::make_unique<risco::Song>();
    auto& harmonySong = *harmonyStorage;
    const auto accompaniedId = risco::storeFormulaEventCell(harmonySong, "t^(t>>7)", "Acompanhar", {},
        accompanied.sequence, risco::FormulaScalePreset::major, risco::FormulaScaleMotion::accompany, 0, 120);
    if (accompaniedId == 0) {
        std::cerr << "store accompaniment failed: count=" << accompanied.sequence.count
                  << " end=" << accompanied.sequence.end << " first="
                  << accompanied.sequence.notes[0].start << ',' << accompanied.sequence.notes[0].duration << ','
                  << accompanied.sequence.notes[0].frequency << ',' << accompanied.sequence.notes[0].gain << ','
                  << accompanied.sequence.notes[0].pulseDuty << ',' << int(accompanied.sequence.notes[0].role) << '\n';
        return 145;
    }
    std::stringstream harmonyFile;
    if (!risco::saveSong(harmonyFile, harmonySong) || !risco::loadSong(harmonyFile, connectedLoaded) ||
        connectedLoaded.bank[0].formulaMotion != risco::FormulaScaleMotion::accompany) return 46;
    int checkedAnchors = 0;
    for (int beats = 2; beats <= 7; ++beats) {
        for (int preset = 0; preset <= int(risco::FormulaScalePreset::prometheus); ++preset) {
            const auto guided = risco::composeFormulaEvents(connectedProgram, 120, beats,
                static_cast<risco::FormulaScalePreset>(preset), risco::FormulaScaleMotion::chordGuide);
            const auto plain = risco::composeFormulaEvents(connectedProgram, 120, beats,
                static_cast<risco::FormulaScalePreset>(preset), risco::FormulaScaleMotion::accompany);
            if (guided.sequence.count != plain.sequence.count) return 50;
            for (int n = 0; n < guided.sequence.count; ++n) {
                const auto& note = guided.sequence.notes[n];
                if (note.role != risco::VoiceRole::lead) continue;
                const auto step = static_cast<int>(std::lround(note.start * 8.0));
                const int beat = step / 4 % beats;
                bool strong = beat == 0;
                int groupStart = 0;
                for (const char* c = risco::formulaMetricGrouping(beats, guided.groupingVariant); *c; ++c) {
                    if (*c >= '0' && *c <= '9') groupStart += *c - '0';
                    if (*c == '+' && groupStart == beat) strong = true;
                }
                strong = strong && step % 4 == 0;
                const auto pitch = [](double f) { return 69.0 + 12.0 * std::log2(f / 440.0); };
                bool matchedOriginal = false, matchesChord = false;
                for (int m = 0; m < plain.sequence.count; ++m) {
                    const auto& old = plain.sequence.notes[m];
                    if (old.role == risco::VoiceRole::lead && old.start == note.start) {
                        if (old.duration != note.duration || old.gain != note.gain) return 51;
                        if (!strong && std::abs(std::remainder(pitch(old.frequency) - pitch(note.frequency), 12.0)) > 0.001) return 52;
                        matchedOriginal = true;
                    }
                    const double barStart = (step / (beats * 4)) * beats * 0.5;
                    if (old.role != risco::VoiceRole::lead && std::abs(old.start - barStart) < 1e-9 &&
                        std::abs(std::remainder(pitch(old.frequency) - pitch(note.frequency), 12.0)) < 0.001)
                        matchesChord = true;
                }
                if (!matchedOriginal || (strong && !matchesChord)) return 53;
                if (strong) ++checkedAnchors;
            }
        }
    }
    if (checkedAnchors < 100) return 54;
    harmonySong.bank[0].formulaMotion = risco::FormulaScaleMotion::chordGuide;
    harmonySong.bank[0].formulaEvents = risco::composeFormulaEvents(connectedProgram, 120, 4,
        risco::FormulaScalePreset::major, risco::FormulaScaleMotion::chordGuide).sequence;
    std::stringstream guidedFile;
    if (!risco::saveSong(guidedFile, harmonySong) || !risco::loadSong(guidedFile, connectedLoaded) ||
        connectedLoaded.bank[0].formulaMotion != risco::FormulaScaleMotion::chordGuide) return 55;
    const int expectedRoots[3][4] = {{36,41,43,36}, {36,45,41,43}, {36,36,36,36}};
    for (int progression = 0; progression < 3; ++progression) {
        for (int hold = 1; hold <= 2; ++hold) {
            const auto plan = risco::composeFormulaEvents(connectedProgram, 120, 4,
                risco::FormulaScalePreset::major, risco::FormulaScaleMotion::chordGuide, 0, -1, progression, hold);
            int bassCount = 0;
            for (int n = 0; n < plan.sequence.count; ++n) {
                const auto& note = plan.sequence.notes[n];
                if (note.role != risco::VoiceRole::bass) continue;
                // The current mathematical maps may add a cadential bass echo;
                // validate the canonical first roots while allowing that extension.
                if (bassCount >= 4 / hold) { ++bassCount; continue; }
                const double midi = 69.0 + 12.0 * std::log2(note.frequency / 440.0);
                if (std::abs(midi - expectedRoots[progression][bassCount]) > 0.001 ||
                    std::abs(note.start - bassCount * hold * 2.0) > 1e-9 ||
                    std::abs(note.duration - hold * 2.0 * 0.85) > 1e-9) return 57;
                ++bassCount;
            }
            if (bassCount < 4 / hold) return 58;
        }
    }
    harmonySong.bank[0].formulaProgression = 2;
    harmonySong.bank[0].formulaHarmonicBars = 2;
    std::stringstream planFile;
    if (!risco::saveSong(planFile, harmonySong) || !risco::loadSong(planFile, connectedLoaded) ||
        connectedLoaded.bank[0].formulaProgression != 2 || connectedLoaded.bank[0].formulaHarmonicBars != 2) return 59;
    std::istringstream oldV30(compatible.str());
    if (!risco::loadSong(oldV30, connectedLoaded) || connectedLoaded.bank[0].formulaProgression != 0 ||
        connectedLoaded.bank[0].formulaHarmonicBars != 1) return 60;
    risco::BytebeatProgram pulse;
    if (!risco::compileBytebeat("255", pulse, error)) return 61;
    for (int mode = 0; mode <= int(risco::FormulaScaleMotion::chordGuide); ++mode) {
        for (int beats = 2; beats <= 7; ++beats) {
            const auto motion = static_cast<risco::FormulaScaleMotion>(mode);
            const auto straight = risco::composeFormulaEvents(pulse, 120, beats,
                risco::FormulaScalePreset::major, motion);
            for (const double amount : {0.2, 0.4}) {
                const auto swung = risco::composeFormulaEvents(pulse, 120, beats,
                    risco::FormulaScalePreset::major, motion, 0, -1, 0, 1, amount);
                if (straight.sequence.count != swung.sequence.count || straight.sequence.end != swung.sequence.end) return 62;
                for (int n = 0; n < swung.sequence.count; ++n) {
                    const auto& before = straight.sequence.notes[n];
                    const auto& after = swung.sequence.notes[n];
                    const int step = int(std::lround(before.start / 0.125));
                    const int stride = mode >= int(risco::FormulaScaleMotion::accompany) ? 2 : 1;
                    const double delay = before.role == risco::VoiceRole::lead && (step / stride) % 2 ? amount * stride * 0.125 : 0;
                    if (std::abs(after.start - before.start - delay) > 1e-9 ||
                        after.frequency != before.frequency || after.gain != before.gain ||
                        after.start + after.duration > swung.sequence.end + 1e-9) return 63;
                }
            }
        }
    }
    harmonySong.bank[0].formulaSwing = 0.4;
    std::stringstream swingFile;
    if (!risco::saveSong(swingFile, harmonySong) || !risco::loadSong(swingFile, connectedLoaded) ||
        std::abs(connectedLoaded.bank[0].formulaSwing - 0.4) > 1e-9) return 64;
    std::istringstream legacyStraight(compatible.str());
    if (!risco::loadSong(legacyStraight, connectedLoaded) || connectedLoaded.bank[0].formulaSwing != 0) return 65;
    std::istringstream swingRecords(swingFile.str());
    std::ostringstream v31;
    while (std::getline(swingRecords, record)) {
    if (record == "RISCO_SONG_V68" || record == "RISCO_SONG_V69" || record == "RISCO_SONG_V70" || record == "RISCO_SONG_V71") record = "RISCO_SONG_V31";
        if (record.rfind("AY ", 0) == 0 || record.rfind("HISTORICAL_AY ",0) == 0 || record.rfind("METRIC ",0) == 0 || record.rfind("DIALOGUE ",0) == 0 || record.rfind("FX_PARAMS ",0) == 0 || record.rfind("DELAY_PARAMS ",0) == 0 || record.rfind("REVERB_PARAMS ",0) == 0 || record.rfind("REVERB_DECAY_PARAMS ",0) == 0 || record.rfind("BITCRUSH ",0) == 0 || record.rfind("BITCRUSH_PARAMS ",0) == 0 || record.rfind("TAKE ",0) == 0 || record.rfind("RESPONSE ",0) == 0) continue;
        if (record.rfind("IMPORT ", 0) == 0) continue;
        if (record.rfind("AUTOMATION ", 0) == 0 || record.rfind("PAN_AUTOMATION ",0) == 0 || record.rfind("FX_AUTOMATION ",0) == 0) continue;
        if (record.rfind("REVERB ", 0) == 0) continue;
        if (record.rfind("FILTER ", 0) == 0) continue;
        if (record.rfind("DELAY ", 0) == 0) continue;
        if (record.rfind("EDIT ", 0) == 0) continue;
        if (record.rfind("SETTINGS ", 0) == 0) continue;
        if (record.rfind("CELL ", 0) == 0) record.erase(record.find_last_of(' '));
        v31 << record << '\n';
    }
    std::istringstream priorPlan(v31.str());
    if (!risco::loadSong(priorPlan, connectedLoaded) || connectedLoaded.bank[0].formulaSwing != 0 ||
        connectedLoaded.bank[0].formulaProgression != 2 || connectedLoaded.bank[0].formulaHarmonicBars != 2) return 66;
    for (int subdivision : {1, 2, 3, 4, 6}) {
        for (int meter = 2; meter <= 7; ++meter) {
            for (int motion = 0; motion <= 6; ++motion) {
                const auto grid = risco::composeFormulaEvents(pulse, 120, meter,
                    risco::FormulaScalePreset::major, static_cast<risco::FormulaScaleMotion>(motion),
                    0, -1, 0, 1, 0, subdivision);
                const auto swung = risco::composeFormulaEvents(pulse, 120, meter,
                    risco::FormulaScalePreset::major, static_cast<risco::FormulaScaleMotion>(motion),
                    0, -1, 0, 1, 0.4, subdivision);
                if (grid.sequence.count < 1 || grid.sequence.count > 64 ||
                    grid.steps != grid.bars * meter * subdivision) return 67;
                for (int n = 0; n < grid.sequence.count; ++n) {
                    const auto& note = swung.sequence.notes[static_cast<size_t>(n)];
                    if (note.start < 0 || note.duration <= 0 || note.start + note.duration > swung.sequence.end + 1e-9) return 68;
                    if ((subdivision == 3 || subdivision == 6) && note.start != grid.sequence.notes[static_cast<size_t>(n)].start) return 69;
                }
            }
        }
    }
    risco::Sequence phrase;
    const auto denseAccompanied = risco::composeFormulaEvents(pulse, 120, 7, risco::FormulaScalePreset::major,
        risco::FormulaScaleMotion::accompany, 0, -1, 0, 1, 0, 6);
    if (denseAccompanied.bars != 1 || denseAccompanied.sequence.count != 45) return 106;
    phrase.end = 8;
    phrase.count = 16;
    for (int i = 0; i < 16; ++i) phrase.notes[static_cast<size_t>(i)] = {i * 0.5, 0.25, 440, 0.2f};
    risco::SectionEdits edits{};
    edits[0] = {0.5f, 0.6f, -1};
    edits[1] = {1, 1.25f, 1};
    edits[2] = {0, 1, 0};
    const auto edited = risco::editSections(phrase, edits);
    if (edited.end != phrase.end || edited.count != 10 || phrase.count != 16 ||
        edited.notes[0].frequency != 220 || std::abs(edited.notes[0].gain - 0.12f) > 1e-6 ||
        edited.notes[2].start != 2 || edited.notes[2].frequency != 880 ||
        edited.notes[6].start != 6 || risco::editSections(phrase, {}).count != 16) return 70;
    harmonySong.bank[0].sectionEdits = edits;
    harmonySong.bank[0].formulaSubdivision = 3;
    std::stringstream editsFile;
    if (!risco::saveSong(editsFile, harmonySong) || !risco::loadSong(editsFile, connectedLoaded) ||
        connectedLoaded.bank[0].formulaSubdivision != 3 || connectedLoaded.bank[0].sectionEdits[0].density != 0.5f ||
        connectedLoaded.bank[0].sectionEdits[1].octaves != 1) return 71;
    harmonySong.bank[0].sectionEdits[0].gain = std::nanf("");
    std::stringstream invalidEdits;
    if (risco::saveSong(invalidEdits, harmonySong)) return 72;
    auto dialog = std::make_unique<risco::Song>();
    const auto callId = risco::storeGeneratedCell(*dialog, 26, 0, "Call", {}, 2);
    dialog->interStemResponse = 1;
    dialog->stemRole[1] = risco::CellRole::bass;
    if (!risco::placeCell(*dialog, {callId, 0, risco::CellSpan::fourBars, 1, 0, 0, 0})) return 73;
    dialog->responsePolicy = 2;
    const auto roleAnswer = std::make_unique<risco::SongPlayback>(risco::prepareSongPlayback(*dialog));
    if (roleAnswer->responseCount != 1) return 74;
    const int completeResponseNotes = roleAnswer->responses[0].cell.count;
    for (int n = 0; n < roleAnswer->responses[0].cell.count; ++n) {
        const auto& note = roleAnswer->responses[0].cell.notes[static_cast<size_t>(n)];
        if (note.role != risco::VoiceRole::bass || note.frequency < 65.4 || note.frequency > 261.63) return 75;
    }
    dialog->responseMotif = 1;
    const auto structuralAnswer = std::make_unique<risco::SongPlayback>(risco::prepareSongPlayback(*dialog));
    if (structuralAnswer->responseCount != 1 || structuralAnswer->responses[0].cell.count < 1 ||
        structuralAnswer->responses[0].cell.count > 4 || structuralAnswer->responses[0].cell.count > completeResponseNotes) return 298;
    dialog->responseMotif = 2;
    const auto contourAnswer = std::make_unique<risco::SongPlayback>(risco::prepareSongPlayback(*dialog));
    if (contourAnswer->responseCount != 1 || contourAnswer->responses[0].cell.count < 1 ||
        contourAnswer->responses[0].cell.count > 4 || contourAnswer->responses[0].cell.count > completeResponseNotes) return 299;
    dialog->responseMotif = 0; dialog->responseDensity = 2;
    const auto sparseAnswer = std::make_unique<risco::SongPlayback>(risco::prepareSongPlayback(*dialog));
    if (sparseAnswer->responseCount != 1 || sparseAnswer->responses[0].cell.count < 1 ||
        sparseAnswer->responses[0].cell.count > completeResponseNotes ||
        (completeResponseNotes > 2 && sparseAnswer->responses[0].cell.count >= completeResponseNotes) ||
        sparseAnswer->responses[0].cell.notes[0].start != roleAnswer->responses[0].cell.notes[0].start ||
        sparseAnswer->responses[0].cell.notes[static_cast<size_t>(sparseAnswer->responses[0].cell.count-1)].start !=
            roleAnswer->responses[0].cell.notes[static_cast<size_t>(completeResponseNotes-1)].start) return 300;
    dialog->responseDensity = 0;
    auto memorySong = std::make_unique<risco::Song>(*dialog);
    memorySong->responsePolicy = 0;
    if (!risco::placeCell(*memorySong, {callId, 16, risco::CellSpan::fourBars, 1, 0, 0, 0})) return 308;
    const auto independentMemory = std::make_unique<risco::SongPlayback>(risco::prepareSongPlayback(*memorySong));
    memorySong->responseMemory = 1;
    const auto linkedMemory = std::make_unique<risco::SongPlayback>(risco::prepareSongPlayback(*memorySong));
    if (independentMemory->responseCount != 2 || linkedMemory->responseCount != 2 ||
        linkedMemory->responses[1].cell.notes[0].frequency == independentMemory->responses[1].cell.notes[0].frequency) return 309;
    if (!risco::placeCell(*dialog, {callId, 0, risco::CellSpan::fourBars, 1, 0, 0, 1})) return 76;
    const auto occupiedAnswer = std::make_unique<risco::SongPlayback>(risco::prepareSongPlayback(*dialog));
    if (occupiedAnswer->responseCount != 0 || occupiedAnswer->responsesSkipped == 0) return 77;
    dialog->responsePolicy = 0;
    if (risco::prepareSongPlayback(*dialog).responseCount != 1) return 78;
    dialog->responsePolicy = 2;
    dialog->stemEffect = {1, 3, 4};
    dialog->responseMotif = 2; dialog->responseDensity = 1;
    dialog->responseMemory = 1;
    dialog->responseSectionIntent = {{1,2,3,4}};
    std::stringstream effectsFile;
    const bool effectsSaved = risco::saveSong(effectsFile,*dialog);
    if (!effectsSaved) return 345;
    if (!risco::loadSong(effectsFile,connectedLoaded)) return 344;
    if (connectedLoaded.responsePolicy != 2 || connectedLoaded.stemEffect != dialog->stemEffect ||
        connectedLoaded.responseMotif != 2 || connectedLoaded.responseDensity != 1 ||
        connectedLoaded.responseMemory != 1 || connectedLoaded.responseSectionIntent != std::array<int,4>{{1,2,3,4}}) return 79;
    auto v57Response = effectsFile.str();
    v57Response.replace(0,std::string("RISCO_SONG_V65").size(),"RISCO_SONG_V57");
    removeResponseRecord(v57Response);
    removeInputGestureCurveFields(v57Response);
    std::istringstream legacyResponse(v57Response);
    if (!risco::loadSong(legacyResponse,connectedLoaded) || connectedLoaded.responseMotif != 0 || connectedLoaded.responseDensity != 0 ||
        connectedLoaded.responseMemory != 0 || connectedLoaded.responseSectionIntent != std::array<int,4>{}) return 301;
    for (const char* bad : {"RESPONSE -1 0","RESPONSE 3 0","RESPONSE 0 -1","RESPONSE 0 3","RESPONSE 0","RESPONSE 0 0 extra"}) {
        auto text = effectsFile.str(); const auto start = text.find("RESPONSE ");
        text.replace(start,text.find('\n',start)-start,bad);
        std::istringstream invalid(text);
        if (risco::loadSong(invalid,connectedLoaded)) return 302;
    }
    dialog->responseMotif = 3;
    std::stringstream invalidResponse;
    if (risco::saveSong(invalidResponse,*dialog)) return 303;
    dialog->responseMotif = 2;
    risco::StemEffect bypass, dark;
    bypass.prepare(48000); dark.prepare(48000); dark.setPreset(2);
    double dryEnergy = 0, wetEnergy = 0;
    for (int i = 0; i < 48000; ++i) {
        const float input = float(std::sin(i * 6.283185307179586 * 8000 / 48000) * 0.3);
        if (bypass.process(input) != input) return 80;
        const float output = dark.process(input);
        if (!std::isfinite(output) || std::abs(output) > 1) return 81;
        if (i > 24000) { dryEnergy += input * input; wetEnergy += output * output; }
    }
    if (wetEnergy >= dryEnergy * 0.08 || !std::isfinite(dark.process(std::nanf("")))) return 82;
    for (int preset = 0; preset <= 4; ++preset) {
        dark.setPreset(preset);
        for (int i = 0; i < 4096; ++i)
            if (!std::isfinite(dark.process(0.35f))) return 83;
    }
    const auto sixEight = risco::composeFormulaEvents(pulse, 120, 3, risco::FormulaScalePreset::major,
        risco::FormulaScaleMotion::follow, 0, -1, 0, 1, 0, 2, true);
    const auto threeFour = risco::composeFormulaEvents(pulse, 120, 3, risco::FormulaScalePreset::major,
        risco::FormulaScaleMotion::follow, 0, -1, 0, 1, 0, 2, false);
    if (sixEight.sequence.end != 6 || sixEight.sequence.count != 24 ||
        sixEight.sequence.notes[3].start != 0.75 ||
        sixEight.sequence.notes[3].gain <= threeFour.sequence.notes[3].gain ||
        sixEight.sequence.notes[4].gain >= threeFour.sequence.notes[4].gain) return 84;
    dialog->beatsPerBar = 3; dialog->compoundSixEight = true;
    std::stringstream compoundFile;
    if (!risco::saveSong(compoundFile, *dialog) || !risco::loadSong(compoundFile, connectedLoaded) ||
        !connectedLoaded.compoundSixEight || connectedLoaded.beatsPerBar != 3) return 85;
    risco::BytebeatProgram floating;
    std::string floatError;
    if (!risco::compileBytebeat("f:sin(t*6.283185307179586)*.5", floating, floatError) || !floating.floating ||
        std::abs(risco::evaluateFloatbeat(floating, 0.25) - 0.5) > 1e-9 ||
        std::abs(risco::directBytebeatSample(&floating, 2000) - 0.5) > 1e-6) return 86;
    if (!risco::compileBytebeat("f:cos(0)+abs(-.25)-tanh(0)", floating, floatError) ||
        risco::evaluateFloatbeat(floating, 0) != 1) return 87;
    for (const auto* expression : {"f:1/0", "f:1%0", "f:t*1000000000000*1000000000000"}) {
        if (!risco::compileBytebeat(expression, floating, floatError) ||
            !std::isfinite(risco::evaluateFloatbeat(floating, 100))) return 88;
    }
    for (const auto* expression : {"f:t&1", "f:sin(", "f:.", "f:1.2.3", "f:unknown(t)", "f:~t", "sin(t)", "0.5"})
        if (risco::compileBytebeat(expression, floating, floatError)) return 89;
    const auto floatId = risco::storeFormulaCell(*dialog, "f:sin(t*1382.30076758)*.4", "Float", {});
    dialog->bank[0].favorite = true;
    std::stringstream floatFile;
    if (!floatId || !risco::saveSong(floatFile, *dialog) || !risco::loadSong(floatFile, connectedLoaded) ||
        !connectedLoaded.bank[0].favorite || connectedLoaded.bank[1].formulaExpression != "f:sin(t*1382.30076758)*.4") return 90;
    if (risco::ayTonePeriod(440) != 284 || risco::ayTonePeriod(0) != 4095 ||
        risco::ayTonePeriod(1000000) != 1 || std::abs(risco::ayToneFrequency(440) - 2000000.0 / (16 * 284)) > 1e-9) return 91;
    risco::Sequence tones;
    tones.count = 3; tones.end = 1;
    for (int n = 0; n < 3; ++n) tones.notes[static_cast<size_t>(n)] = {0, 1, 220.0 * (n + 1), 0.12f};
    double toneEnergy = 0;
    for (int i = 0; i < 4800; ++i) {
        const float sample = risco::sampleAt(tones, i / 48000.0, risco::WaveMode::ayToneStudy);
        if (!std::isfinite(sample) || std::abs(sample) > 1) return 92;
        toneEnergy += sample * sample;
    }
    tones.count = 4; tones.notes[3] = {0, 1, 330, 0.24f};
    const float limited = risco::sampleAt(tones, 0.05, risco::WaveMode::ayToneStudy);
    tones.count = 3;
    if (toneEnergy < 1 || limited != risco::sampleAt(tones, 0.05, risco::WaveMode::ayToneStudy)) return 93;
    dialog->timbreMode = 2;
    std::stringstream ayFile;
    if (!risco::saveSong(ayFile, *dialog) || !risco::loadSong(ayFile, connectedLoaded) || connectedLoaded.timbreMode != 2) return 94;
    dialog->timbreMode = 6;
    dialog->historicalAyTexture = 1;
    dialog->historicalAyNoisePeriod = 7;
    dialog->historicalAyEnvelope = 3;
    dialog->historicalAyEnvelopeRamp = 0.25f;
    dialog->stemMeter = {{3,5,8}};
    dialog->bank[0].phraseSectionBars = {{1,2,3,4}};
    std::stringstream historicalFile;
    if (!risco::saveSong(historicalFile, *dialog) || !risco::loadSong(historicalFile, connectedLoaded) ||
        connectedLoaded.timbreMode != 6 || connectedLoaded.historicalAyTexture != 1 ||
        connectedLoaded.historicalAyNoisePeriod != 7 || connectedLoaded.historicalAyEnvelope != 3 ||
        connectedLoaded.historicalAyEnvelopeRamp != 0.25f ||
        connectedLoaded.stemMeter != std::array<int,3>{{3,5,8}} ||
        connectedLoaded.bank[0].phraseSectionBars != risco::PhraseSectionBars{{1,2,3,4}}) return 304;
    auto compatibleV61 = historicalFile.str();
    compatibleV61.replace(0, std::string("RISCO_SONG_V65").size(), "RISCO_SONG_V61");
    const auto dialogueRecord = compatibleV61.find("DIALOGUE ");
    if (dialogueRecord == std::string::npos) return 311;
    compatibleV61.erase(dialogueRecord, compatibleV61.find('\n', dialogueRecord) - dialogueRecord + 1);
    const auto filterParamsRecord = compatibleV61.find("FX_PARAMS ");
    if (filterParamsRecord == std::string::npos) return 313;
    compatibleV61.erase(filterParamsRecord, compatibleV61.find('\n', filterParamsRecord) - filterParamsRecord + 1);
    const auto delayParamsRecord = compatibleV61.find("DELAY_PARAMS ");
    if (delayParamsRecord == std::string::npos) return 314;
    compatibleV61.erase(delayParamsRecord, compatibleV61.find('\n', delayParamsRecord) - delayParamsRecord + 1);
    const auto reverbParamsRecord = compatibleV61.find("REVERB_PARAMS ");
    if (reverbParamsRecord == std::string::npos) return 315;
    compatibleV61.erase(reverbParamsRecord, compatibleV61.find('\n', reverbParamsRecord) - reverbParamsRecord + 1);
    const auto reverbDecayParamsRecord = compatibleV61.find("REVERB_DECAY_PARAMS ");
    if (reverbDecayParamsRecord != std::string::npos)
        compatibleV61.erase(reverbDecayParamsRecord, compatibleV61.find('\n', reverbDecayParamsRecord) - reverbDecayParamsRecord + 1);
    const auto bitcrushRecord = compatibleV61.find("BITCRUSH ");
    if (bitcrushRecord != std::string::npos)
        compatibleV61.erase(bitcrushRecord, compatibleV61.find('\n', bitcrushRecord) - bitcrushRecord + 1);
    const auto bitcrushParamsRecord = compatibleV61.find("BITCRUSH_PARAMS ");
    if (bitcrushParamsRecord != std::string::npos)
        compatibleV61.erase(bitcrushParamsRecord, compatibleV61.find('\n', bitcrushParamsRecord) - bitcrushParamsRecord + 1);
    removeInputGestureCurveFields(compatibleV61);
    std::istringstream compatibleV61File(compatibleV61);
    if (!risco::loadSong(compatibleV61File, connectedLoaded) ||
        connectedLoaded.stemMeter != std::array<int,3>{{3,5,8}} ||
        connectedLoaded.bank[0].phraseSectionBars != risco::PhraseSectionBars{{1,2,3,4}} ||
        connectedLoaded.responseMemory != 0 || connectedLoaded.responseSectionIntent != std::array<int,4>{}) return 312;
    auto compatibleV59 = historicalFile.str();
    compatibleV59.replace(0, std::string("RISCO_SONG_V65").size(), "RISCO_SONG_V59");
    const auto historicalRecord = compatibleV59.find("\nHISTORICAL_AY ");
    if (historicalRecord == std::string::npos) return 306;
    compatibleV59.erase(historicalRecord + 1,
        compatibleV59.find('\n', historicalRecord + 1) - historicalRecord);
    removeMetricRecord(compatibleV59);
    removeInputGestureCurveFields(compatibleV59);
    std::stringstream compatibleV59File(compatibleV59);
    if (!risco::loadSong(compatibleV59File, connectedLoaded) ||
        connectedLoaded.historicalAyTexture != 0 || connectedLoaded.historicalAyNoisePeriod != 16 ||
        connectedLoaded.historicalAyEnvelope != 0 || connectedLoaded.historicalAyEnvelopeRamp != 0.5f ||
        connectedLoaded.stemMeter != std::array<int,3>{} ||
        connectedLoaded.bank[0].phraseSectionBars != risco::PhraseSectionBars{{1,1,1,1}}) return 307;
    auto incompatibleV58 = historicalFile.str();
    incompatibleV58.replace(0, std::string("RISCO_SONG_V65").size(), "RISCO_SONG_V58");
    std::stringstream incompatibleV58File(incompatibleV58);
    if (risco::loadSong(incompatibleV58File, connectedLoaded)) return 305;
    dialog->timbreMode = 2;
    const std::array<int, 4> roots{{0, -2, 4, 0}};
    const auto contextual = risco::composeFormulaEvents(pulse, 120, 4, risco::FormulaScalePreset::major,
        risco::FormulaScaleMotion::chordGuide, 0, -1, 0, 2, 0, 2, false, roots);
    bool fifthBass = false;
    for (int n = 0; n < contextual.sequence.count; ++n) {
        const auto& note = contextual.sequence.notes[static_cast<size_t>(n)];
        if (note.start < 4 && note.start + note.duration > 2 + 1e-9) return 95; // Silent second bar.
        if (note.start >= 4 && note.start < 6 && note.role == risco::VoiceRole::bass) {
            const int midi = int(std::lround(69 + 12 * std::log2(note.frequency / 440)));
            fifthBass = midi % 12 == 7;
        }
    }
    if (!fifthBass || contextual.sequence.end != 8) return 96;
    dialog->bank[1].formulaChordRoots = roots;
    std::stringstream rootsFile;
    if (!risco::saveSong(rootsFile, *dialog) || !risco::loadSong(rootsFile, connectedLoaded) ||
        connectedLoaded.bank[1].formulaChordRoots != roots) return 97;
    phrase.notes[0].frequency = 330;
    const auto rhythmRead = risco::interpretInput(phrase, 2);
    const auto pitchRead = risco::interpretInput(phrase, 1);
    const auto reversed = risco::interpretInput(phrase, 4);
    if (rhythmRead.notes[0].frequency != 220 || rhythmRead.notes[1].start != phrase.notes[1].start ||
        pitchRead.notes[0].frequency != 330 || pitchRead.notes[1].start != 0.5 ||
        reversed.notes[0].start != 0.25 || reversed.notes[15].frequency != 330 ||
        phrase.notes[0].frequency != 330 || reversed.end != phrase.end) return 98;
    risco::Inscription inscription;
    inscription.ascii = "Read this";
    const auto readingId = risco::storeCell(*dialog, inscription, "Reading");
    if (!readingId) return 99;
    dialog->bank[2].inputInterpretation = 2;
    if (!risco::placeCell(*dialog, {readingId, 0, risco::CellSpan::bar, 1, 0, 0, 2})) return 100;
    const auto readingPlayback = std::make_unique<risco::SongPlayback>(risco::prepareSongPlayback(*dialog));
    const auto& readNotes = readingPlayback->placements[2].cell;
    for (int n = 0; n < readNotes.count; ++n) if (readNotes.notes[static_cast<size_t>(n)].frequency != 220) return 101;
    std::stringstream readingFile;
    if (!risco::saveSong(readingFile, *dialog) || !risco::loadSong(readingFile, connectedLoaded) ||
        connectedLoaded.bank[2].inputInterpretation != 2 || connectedLoaded.bank[2].inscription.ascii != inscription.ascii) return 102;
    risco::OutputStage output;
    output.prepare(48000);
    double measured = 0;
    for (int i = 0; i < 192000; ++i) {
        const float input = float(std::sin(i * 6.283185307179586 * 440 / 48000) * 0.1);
        const auto sample = output.process(input, input, 0.5);
        if (i >= 144000) measured += double(sample.left) * sample.left;
    }
    if (std::abs(output.meter().rms - std::sqrt(measured / 48000)) > 0.001) return 103;
    const auto rmsBefore = output.meter().rms;
    output.takeMeterWindow();
    if (output.meter().rms != rmsBefore) return 104;
    output.reset();
    if (output.meter().rms != 0) return 105;
    for (int division : {1, 2, 3, 4, 6}) for (int meter = 2; meter <= 7; ++meter) {
        const auto straight = risco::composeFormulaEvents(pulse, 120, meter, risco::FormulaScalePreset::major,
            risco::FormulaScaleMotion::follow, 0, -1, 0, 1, 0, division);
        const auto sync = risco::composeFormulaEvents(pulse, 120, meter, risco::FormulaScalePreset::major,
            risco::FormulaScaleMotion::follow, 0, -1, 0, 1, 0, division, false, {{-1,-1,-1,-1}}, true);
        if (straight.sequence.count != sync.sequence.count || straight.sequence.end != sync.sequence.end) return 107;
        for (int n = 0; n < sync.sequence.count; ++n) {
            const auto& before = straight.sequence.notes[static_cast<size_t>(n)];
            const auto& after = sync.sequence.notes[static_cast<size_t>(n)];
            const bool shifted = n % (meter * division) != 0 && n % division == 0;
            if (std::abs(before.start - after.start - (shifted ? 0.25 / division : 0)) > 1e-9 ||
                before.frequency != after.frequency || before.gain != after.gain || after.start < 0 ||
                after.start + after.duration > sync.sequence.end + 1e-9) return 108;
        }
    }
    dialog->bank[1].formulaSyncopation = true;
    std::stringstream syncFile;
    if (!risco::saveSong(syncFile, *dialog) || !risco::loadSong(syncFile, connectedLoaded) ||
        !connectedLoaded.bank[1].formulaSyncopation) return 109;
    std::istringstream syncRecords(syncFile.str());
    std::ostringstream v33;
    while (std::getline(syncRecords, record)) {
    if (record == "RISCO_SONG_V68" || record == "RISCO_SONG_V69" || record == "RISCO_SONG_V70" || record == "RISCO_SONG_V71") record = "RISCO_SONG_V33";
        if (record.rfind("AY ", 0) == 0 || record.rfind("HISTORICAL_AY ",0) == 0 || record.rfind("METRIC ",0) == 0 || record.rfind("DIALOGUE ",0) == 0 || record.rfind("FX_PARAMS ",0) == 0 || record.rfind("DELAY_PARAMS ",0) == 0 || record.rfind("REVERB_PARAMS ",0) == 0 || record.rfind("REVERB_DECAY_PARAMS ",0) == 0 || record.rfind("BITCRUSH ",0) == 0 || record.rfind("BITCRUSH_PARAMS ",0) == 0 || record.rfind("TAKE ",0) == 0 || record.rfind("RESPONSE ",0) == 0) continue;
        if (record.rfind("IMPORT ", 0) == 0) continue;
        if (record.rfind("AUTOMATION ", 0) == 0 || record.rfind("PAN_AUTOMATION ",0) == 0 || record.rfind("FX_AUTOMATION ",0) == 0) continue;
        if (record.rfind("REVERB ", 0) == 0) continue;
        if (record.rfind("FILTER ", 0) == 0) continue;
        if (record.rfind("DELAY ", 0) == 0) continue;
        if (record.rfind("EDIT ", 0) == 0) for (int f = 0; f < 11; ++f) removeLastField(record);
        v33 << record << '\n';
    }
    std::istringstream legacySync(v33.str());
    if (!risco::loadSong(legacySync, connectedLoaded) || connectedLoaded.bank[1].formulaSyncopation) return 110;
    for (int pulses : {2, 3, 5, 7}) for (int meter = 2; meter <= 7; ++meter) {
        const auto pattern = risco::composeFormulaEvents(pulse, 120, meter, risco::FormulaScalePreset::major,
            risco::FormulaScaleMotion::follow, 0, -1, 0, 1, 0.4, 6, false, {{-1,-1,-1,-1}}, true, pulses);
        if (pattern.bars != 4 || pattern.sequence.count != 4 * pulses || pattern.sequence.end != 2 * meter) return 111;
        for (int n = 0; n < pattern.sequence.count; ++n)
            if (std::abs(pattern.sequence.notes[static_cast<size_t>(n)].start - n * 0.5 * meter / pulses) > 1e-9) return 112;
    }
    auto polySong = std::make_unique<risco::Song>();
    for (int stem = 0; stem < 2; ++stem) {
        const int pulses = stem == 0 ? 3 : 2;
        const auto mapped = risco::composeFormulaEvents(pulse, 120, 4, risco::FormulaScalePreset::major,
            risco::FormulaScaleMotion::follow, 0, -1, 0, 1, 0, 0, false, {{-1,-1,-1,-1}}, false, pulses);
        const auto id = risco::storeFormulaEventCell(*polySong, "255", "Pulse", {}, mapped.sequence,
            risco::FormulaScalePreset::major, risco::FormulaScaleMotion::follow, 0, 120, {}, 0, 1, 0, 0,
            {{-1,-1,-1,-1}}, false, pulses);
        if (!id || !risco::placeCell(*polySong, {id, 0, risco::CellSpan::fourBars, 1, 0, 0, stem})) return 115;
    }
    const auto polyPlayback = std::make_unique<risco::SongPlayback>(risco::prepareSongPlayback(*polySong));
    if (polyPlayback->count != 2 || polyPlayback->placements[0].cell.count != 12 ||
        polyPlayback->placements[1].cell.count != 8 || polyPlayback->placements[0].duration != 8 ||
        polyPlayback->placements[1].duration != 8 ||
        std::abs(polyPlayback->placements[0].cell.notes[1].start - 2.0 / 3) > 1e-9 ||
        polyPlayback->placements[1].cell.notes[1].start != 1) return 116;
    dialog->bank[1].formulaPulsesPerBar = 3;
    std::stringstream polyFile;
    if (!risco::saveSong(polyFile, *dialog) || !risco::loadSong(polyFile, connectedLoaded) ||
        connectedLoaded.bank[1].formulaPulsesPerBar != 3) return 113;
    std::istringstream polyRecords(polyFile.str());
    std::ostringstream v34;
    while (std::getline(polyRecords, record)) {
    if (record == "RISCO_SONG_V68" || record == "RISCO_SONG_V69" || record == "RISCO_SONG_V70" || record == "RISCO_SONG_V71") record = "RISCO_SONG_V34";
        if (record.rfind("AY ", 0) == 0 || record.rfind("HISTORICAL_AY ",0) == 0 || record.rfind("METRIC ",0) == 0 || record.rfind("DIALOGUE ",0) == 0 || record.rfind("FX_PARAMS ",0) == 0 || record.rfind("DELAY_PARAMS ",0) == 0 || record.rfind("REVERB_PARAMS ",0) == 0 || record.rfind("REVERB_DECAY_PARAMS ",0) == 0 || record.rfind("BITCRUSH ",0) == 0 || record.rfind("BITCRUSH_PARAMS ",0) == 0 || record.rfind("TAKE ",0) == 0 || record.rfind("RESPONSE ",0) == 0) continue;
        if (record.rfind("IMPORT ", 0) == 0) continue;
        if (record.rfind("AUTOMATION ", 0) == 0 || record.rfind("PAN_AUTOMATION ",0) == 0 || record.rfind("FX_AUTOMATION ",0) == 0) continue;
        if (record.rfind("REVERB ", 0) == 0) continue;
        if (record.rfind("FILTER ", 0) == 0) continue;
        if (record.rfind("DELAY ", 0) == 0) continue;
        if (record.rfind("EDIT ", 0) == 0) for (int f = 0; f < 10; ++f) removeLastField(record);
        v34 << record << '\n';
    }
    std::istringstream priorRhythm(v34.str());
    if (!risco::loadSong(priorRhythm, connectedLoaded) || connectedLoaded.bank[1].formulaPulsesPerBar != 0 ||
        !connectedLoaded.bank[1].formulaSyncopation) return 114;
    for (int meter = 2; meter <= 7; ++meter) for (int seed = 0; seed < 6; ++seed) {
        const auto phrase = risco::phraseFromSeed(seed, 0, 6, 120, 15, meter);
        const auto repeat = risco::phraseFromSeed(seed, 0, 6, 120, 15, meter);
        if (phrase.end != 2 * meter || phrase.count < 12 || phrase.count > 64 || repeat.count != phrase.count) return 117;
        for (int n = 0; n < phrase.count; ++n) {
            const auto& note = phrase.notes[static_cast<size_t>(n)];
            if (std::abs(note.start / 0.25 - std::round(note.start / 0.25)) > 1e-9 ||
                note.start < 0 || note.start + note.duration > phrase.end + 1e-9 ||
                note.start != repeat.notes[static_cast<size_t>(n)].start) return 118;
        }
        const auto masked = risco::phraseFromSeed(seed, 0, 6, 120, 5, meter);
        if (masked.end != phrase.end || masked.count >= phrase.count) return 119;
    }
    const auto compoundPhrase = risco::phraseFromSeed(0, 0, 6, 120, 15, 3, true);
    bool secondGroup = false;
    for (int n = 0; n < compoundPhrase.count; ++n) {
        const auto& note = compoundPhrase.notes[static_cast<size_t>(n)];
        if (note.role == risco::VoiceRole::lead && note.start == 0.75 && note.gain > 0.09f) secondGroup = true;
    }
    if (!secondGroup || compoundPhrase.end != 6) return 120;
    const auto nativeId = risco::storeGeneratedCell(*dialog, 0, 0, "Native", {}, 6);
    std::stringstream nativeFile;
    if (!nativeId || !risco::saveSong(nativeFile, *dialog) || !risco::loadSong(nativeFile, connectedLoaded) ||
        connectedLoaded.bank[3].phraseVersion != 6) return 121;
    const risco::PhraseMeters meters{{3,5,8,7}};
    const auto mixed = risco::phraseFromSeed(0, 0, 6, 120, 15, 4, false, meters);
    if (mixed.end != 9 || mixed.count > 64) return 122;
    risco::SectionEdits silentSection{};
    silentSection[2].gain = 0;
    const auto editedMixed = risco::editSections(mixed, silentSection, 4, risco::phraseMeterWeights(meters, 4));
    for (int n = 0; n < editedMixed.count; ++n) {
        const auto& note = editedMixed.notes[static_cast<size_t>(n)];
        if (note.start >= 4 && note.start < 5.5) return 123;
    }
    auto mixedSong = std::make_unique<risco::Song>();
    const auto mixedId = risco::storeGeneratedCell(*mixedSong, 0, 0, "Mixed", {}, 6);
    mixedSong->bank[0].phraseMeters = meters;
    mixedSong->bank[0].sectionEdits = silentSection;
    if (!mixedId || !risco::placeCell(*mixedSong, {mixedId, 0, risco::CellSpan::fourBars, 1, 0, 0, 0}) ||
        risco::placementLengthSeconds(*mixedSong, mixedSong->grid[0]) != 9) return 124;
    const auto mixedPlayback = std::make_unique<risco::SongPlayback>(risco::prepareSongPlayback(*mixedSong));
    if (mixedPlayback->count != 1 || mixedPlayback->end != 9 || mixedPlayback->placements[0].duration != 9) return 125;
    for (int n = 0; n < mixedPlayback->placements[0].cell.count; ++n) {
        const auto& note = mixedPlayback->placements[0].cell.notes[static_cast<size_t>(n)];
        if (note.start >= 4 && note.start < 5.5) return 126;
    }
    std::stringstream mixedFile;
    if (!risco::saveSong(mixedFile, *mixedSong) || !risco::loadSong(mixedFile, connectedLoaded) ||
        connectedLoaded.bank[0].phraseMeters != meters) return 127;
    std::istringstream mixedRecords(mixedFile.str());
    std::ostringstream v36;
    while (std::getline(mixedRecords, record)) {
    if (record == "RISCO_SONG_V68" || record == "RISCO_SONG_V69" || record == "RISCO_SONG_V70" || record == "RISCO_SONG_V71") record = "RISCO_SONG_V36";
        if (record.rfind("AY ", 0) == 0 || record.rfind("HISTORICAL_AY ",0) == 0 || record.rfind("METRIC ",0) == 0 || record.rfind("DIALOGUE ",0) == 0 || record.rfind("FX_PARAMS ",0) == 0 || record.rfind("DELAY_PARAMS ",0) == 0 || record.rfind("REVERB_PARAMS ",0) == 0 || record.rfind("REVERB_DECAY_PARAMS ",0) == 0 || record.rfind("BITCRUSH ",0) == 0 || record.rfind("BITCRUSH_PARAMS ",0) == 0 || record.rfind("TAKE ",0) == 0 || record.rfind("RESPONSE ",0) == 0) continue;
        if (record.rfind("IMPORT ", 0) == 0) continue;
        if (record.rfind("AUTOMATION ", 0) == 0 || record.rfind("PAN_AUTOMATION ",0) == 0 || record.rfind("FX_AUTOMATION ",0) == 0) continue;
        if (record.rfind("REVERB ", 0) == 0) continue;
        if (record.rfind("FILTER ", 0) == 0) continue;
        if (record.rfind("DELAY ", 0) == 0) continue;
        if (record.rfind("EDIT ", 0) == 0) for (int f = 0; f < 9; ++f) removeLastField(record);
        v36 << record << '\n';
    }
    std::istringstream priorMeters(v36.str());
    if (!risco::loadSong(priorMeters, connectedLoaded) || connectedLoaded.bank[0].phraseMeters != risco::PhraseMeters{}) return 128;
    mixedSong->bank[0].phraseMeters[0] = 1;
    std::stringstream invalidMeters;
    if (risco::saveSong(invalidMeters, *mixedSong)) return 129;
    mixedSong->bank[0].phraseMeters = meters;
    mixedSong->interStemResponse = 1;
    mixedSong->responsePolicy = 1;
    const auto mixedResponse = std::make_unique<risco::SongPlayback>(risco::prepareSongPlayback(*mixedSong));
    if (mixedResponse->responseCount != 1 || mixedResponse->responses[0].start != 1.5 ||
        mixedResponse->responses[0].cell.end != 2.5 || mixedResponse->responses[0].sourceSection != 0) return 130;
    auto intentionalSong = std::make_unique<risco::Song>(*mixedSong);
    intentionalSong->responsePolicy = 0;
    intentionalSong->responseSectionIntent[0] = 5;
    const auto intentionalResponse = std::make_unique<risco::SongPlayback>(risco::prepareSongPlayback(*intentionalSong));
    if (intentionalResponse->responseCount != 1 || intentionalResponse->responses[0].sourceSection != 1) return 310;
    const auto& callSequence = mixedResponse->placements[0].cell;
    const auto& answerSequence = mixedResponse->responses[0].cell;
    int matched = 0;
    for (int n = 0; n < callSequence.count; ++n) {
        const auto& call = callSequence.notes[static_cast<size_t>(n)];
        if (call.start >= 1.5) continue;
        if (matched >= answerSequence.count) return 131;
        const auto& answer = answerSequence.notes[static_cast<size_t>(matched++)];
        if (std::abs(answer.start - call.start * 5.0 / 3) > 1e-9 ||
            std::abs(answer.duration - std::min(call.duration, 1.5 - call.start) * 5.0 / 3) > 1e-9 ||
            answer.start + answer.duration > 2.5 + 1e-9) return 132;
    }
    if (matched != answerSequence.count) return 133;
    if (!risco::placeCell(*mixedSong, {mixedId, 7, risco::CellSpan::fourBars, 1, 0, 1, 1})) return 134;
    const auto occupiedMixed = std::make_unique<risco::SongPlayback>(risco::prepareSongPlayback(*mixedSong));
    if (occupiedMixed->responseCount != 1 || occupiedMixed->responses[0].start != 4 ||
        occupiedMixed->responses[0].cell.end != 1.5 || occupiedMixed->responses[0].sourceSection != 1 ||
        occupiedMixed->responsesSkipped != 1) return 135;
    mixedSong->placementCount = 1;
    mixedSong->bank[0].phraseSectionMask = 14;
    const auto maskedMixed = std::make_unique<risco::SongPlayback>(risco::prepareSongPlayback(*mixedSong));
    if (maskedMixed->responseCount != 1 || maskedMixed->responses[0].start != 4 ||
        maskedMixed->responses[0].sourceSection != 1) return 136;
    mixedSong->interStemResponse = 0;
    if (risco::prepareSongPlayback(*mixedSong).responseCount != 0) return 137;
    mixedSong->interStemResponse = 1;
    mixedSong->responsePolicy = 3;
    risco::Sequence chord;
    chord.end = 8; chord.count = 3;
    for (int n = 0; n < 3; ++n) {
        const int midi[] = {62,66,69};
        chord.notes[static_cast<size_t>(n)] = {0, 8, 440 * std::pow(2.0, (midi[n] - 69) / 12.0), 0.04f, risco::VoiceRole::harmony};
    }
    const auto referenceId = risco::storeFormulaEventCell(*mixedSong, "255", "Context", {}, chord,
        risco::FormulaScalePreset::major, risco::FormulaScaleMotion::accompany, 2, 120);
    if (!referenceId) return 138;
    mixedSong->bank[1].role = risco::CellRole::harmony;
    if (!risco::placeCell(*mixedSong, {referenceId, 0, risco::CellSpan::fourBars, 1, 0, 0, 2})) return 139;
    const auto contextualAnswer = std::make_unique<risco::SongPlayback>(risco::prepareSongPlayback(*mixedSong));
    if (contextualAnswer->responseCount != 1 || contextualAnswer->responses[0].harmonicReferenceId != referenceId) return 140;
    for (int n = 0; n < contextualAnswer->responses[0].cell.count; ++n) {
        const auto& note = contextualAnswer->responses[0].cell.notes[static_cast<size_t>(n)];
        const int pc = int(std::lround(69 + 12 * std::log2(note.frequency / 440))) % 12;
        if (pc != 2 && pc != 6 && pc != 9) return 141;
        if (note.start != maskedMixed->responses[0].cell.notes[static_cast<size_t>(n)].start ||
            note.gain != maskedMixed->responses[0].cell.notes[static_cast<size_t>(n)].gain) return 142;
    }
    std::swap(mixedSong->grid[0], mixedSong->grid[1]);
    if (risco::prepareSongPlayback(*mixedSong).responses[0].harmonicReferenceId != referenceId) return 143;
    std::stringstream contextFile;
    if (!risco::saveSong(contextFile, *mixedSong) || !risco::loadSong(contextFile, connectedLoaded) ||
        connectedLoaded.responsePolicy != 3) return 144;
    polySong->interStemResponse = 1;
    polySong->responsePolicy = 3;
    // Move the other lead-only lane away from the response destination.
    polySong->grid[1].stem = 2;
    const auto noChord = std::make_unique<risco::SongPlayback>(risco::prepareSongPlayback(*polySong));
    if (noChord->responseCount != 1 || noChord->responses[0].harmonicReferenceId != 0) return 145;
    auto& movingChord = mixedSong->bank[1].formulaEvents;
    movingChord.count = 6;
    for (int n = 0; n < 3; ++n) {
        movingChord.notes[static_cast<size_t>(n)].duration = 4.75;
        auto next = movingChord.notes[static_cast<size_t>(n)];
        next.start = 4.75; next.duration = 3.25; next.frequency *= std::pow(2.0, 1.0 / 12);
        movingChord.notes[static_cast<size_t>(n + 3)] = next;
    }
    mixedSong->responsePolicy = 4;
    const auto movingAnswer = std::make_unique<risco::SongPlayback>(risco::prepareSongPlayback(*mixedSong));
    int early = 0, late = 0;
    for (int n = 0; n < movingAnswer->responses[0].cell.count; ++n) {
        const auto& note = movingAnswer->responses[0].cell.notes[static_cast<size_t>(n)];
        const int pc = int(std::lround(69 + 12 * std::log2(note.frequency / 440))) % 12;
        if (note.start < 0.75) { if (pc != 2 && pc != 6 && pc != 9) return 146; ++early; }
        else { if (pc != 3 && pc != 7 && pc != 10) return 147; ++late; }
    }
    if (!early || !late) return 148;
    mixedSong->responsePolicy = 2;
    const auto registerAnswer = std::make_unique<risco::SongPlayback>(risco::prepareSongPlayback(*mixedSong));
    mixedSong->responsePolicy = 4;
    for (int n = 0; n < 3; ++n) {
        movingChord.notes[static_cast<size_t>(n)].duration = 4.4;
        movingChord.notes[static_cast<size_t>(n + 3)].start = 5.1;
        movingChord.notes[static_cast<size_t>(n + 3)].duration = 2.9;
    }
    const auto gapAnswer = std::make_unique<risco::SongPlayback>(risco::prepareSongPlayback(*mixedSong));
    int gaps = 0;
    for (int n = 0; n < gapAnswer->responses[0].cell.count; ++n) {
        const auto& note = gapAnswer->responses[0].cell.notes[static_cast<size_t>(n)];
        if (note.start >= 0.4 && note.start < 1.1) {
            if (note.frequency != registerAnswer->responses[0].cell.notes[static_cast<size_t>(n)].frequency) return 149;
            ++gaps;
        }
    }
    if (!gaps) return 150;
    std::stringstream movingFile;
    if (!risco::saveSong(movingFile, *mixedSong) || !risco::loadSong(movingFile, connectedLoaded) || connectedLoaded.responsePolicy != 4) return 151;
    auto legacyMoving = movingFile.str();
    legacyMoving.replace(0, std::string("RISCO_SONG_V65").size(), "RISCO_SONG_V39");
    removeFilterRecord(legacyMoving);
    const auto delayRecord = legacyMoving.find("DELAY ");
    legacyMoving.erase(delayRecord, legacyMoving.find('\n', delayRecord) - delayRecord + 1);
    std::istringstream legacyMovingStream(legacyMoving);
    const bool loadedLegacyMoving = risco::loadSong(legacyMovingStream, connectedLoaded);
    if (!loadedLegacyMoving || connectedLoaded.responsePolicy != 4) return 160;
    movingChord.count = 3;
    for (int n = 0; n < 3; ++n) movingChord.notes[static_cast<size_t>(n)].duration = 8;
    mixedSong->responsePolicy = 5;
    const auto tension = std::make_unique<risco::SongPlayback>(risco::prepareSongPlayback(*mixedSong));
    int tensionNotes = 0;
    const auto& tensionSequence = tension->responses[0].cell;
    for (int n = 0; n < tensionSequence.count; ++n) {
        const auto& note = tensionSequence.notes[static_cast<size_t>(n)];
        const int pc = int(std::lround(69 + 12 * std::log2(note.frequency / 440))) % 12;
        const bool member = pc == 2 || pc == 6 || pc == 9;
        if (note.start < 0.75 && note.start + note.duration <= 0.75 + 1e-9) { if (member) return 152; ++tensionNotes; }
        else if (!member) return 153;
        if (note.start != registerAnswer->responses[0].cell.notes[static_cast<size_t>(n)].start ||
            note.duration != registerAnswer->responses[0].cell.notes[static_cast<size_t>(n)].duration ||
            note.gain != registerAnswer->responses[0].cell.notes[static_cast<size_t>(n)].gain) return 154;
    }
    if (!tensionNotes) return 155;
    mixedSong->responsePolicy = 6;
    const auto finalSupport = std::make_unique<risco::SongPlayback>(risco::prepareSongPlayback(*mixedSong));
    double lastAttack = 0;
    for (int n = 0; n < finalSupport->responses[0].cell.count; ++n)
        lastAttack = std::max(lastAttack, finalSupport->responses[0].cell.notes[static_cast<size_t>(n)].start);
    for (int n = 0; n < finalSupport->responses[0].cell.count; ++n) {
        const auto& note = finalSupport->responses[0].cell.notes[static_cast<size_t>(n)];
        if (note.start != lastAttack) {
            if (note.frequency != registerAnswer->responses[0].cell.notes[static_cast<size_t>(n)].frequency) return 156;
        } else {
            const int pc = int(std::lround(69 + 12 * std::log2(note.frequency / 440))) % 12;
            if (pc != 2 && pc != 6 && pc != 9) return 157;
        }
    }
    for (int policy : {5,6}) {
        mixedSong->responsePolicy = policy;
        std::stringstream intentFile;
        if (!risco::saveSong(intentFile, *mixedSong) || !risco::loadSong(intentFile, connectedLoaded) || connectedLoaded.responsePolicy != policy) return 158;
        polySong->responsePolicy = policy;
        const auto fallback = std::make_unique<risco::SongPlayback>(risco::prepareSongPlayback(*polySong));
        for (int n = 0; n < fallback->responses[0].cell.count; ++n)
            if (fallback->responses[0].cell.notes[static_cast<size_t>(n)].frequency != noChord->responses[0].cell.notes[static_cast<size_t>(n)].frequency) return 159;
    }
    risco::LogisticControl control;
    if (!control.restore(32768) || control.next() != 64716) return 161;
    control.reset(26);
    std::array<std::uint32_t, 32> trajectory{};
    for (auto& value : trajectory) value = control.next();
    const auto checkpoint = control.state(), following = control.next();
    if (!control.restore(checkpoint) || control.next() != following || control.restore(0) || control.restore(65536)) return 162;
    control.reset(26);
    for (auto value : trajectory) if (control.next() != value) return 163;
    for (int i = 0; i < 100000; ++i) {
        const auto value = control.next();
        if (value < 1 || value > 65535) return 164;
    }
    const auto statePhrase = risco::phraseFromSeed(26, 0, 7, 120, 15, 4, false, meters);
    const auto stateRepeat = risco::phraseFromSeed(26, 0, 7, 120, 15, 4, false, meters);
    const auto stateMasked = risco::phraseFromSeed(26, 0, 7, 120, 14, 4, false, meters);
    const auto stateVariation = risco::phraseFromSeed(26, 1, 7, 120, 15, 4, false, meters);
    if (statePhrase.end != 9 || statePhrase.count > 64 || statePhrase.count != stateRepeat.count) return 165;
    int kept = 0;
    bool varied = stateVariation.count != statePhrase.count;
    for (int n = 0; n < statePhrase.count; ++n) {
        const auto& note = statePhrase.notes[static_cast<size_t>(n)];
        const auto& repeat = stateRepeat.notes[static_cast<size_t>(n)];
        if (note.start != repeat.start || note.frequency != repeat.frequency || note.duration != repeat.duration || note.gain != repeat.gain ||
            note.start < 0 || note.start + note.duration > 9 + 1e-9 || !std::isfinite(note.frequency)) return 166;
        if (n < stateVariation.count && stateVariation.notes[static_cast<size_t>(n)].frequency != note.frequency) varied = true;
        if (note.start >= 1.5) {
            if (kept >= stateMasked.count) return 167;
            const auto& masked = stateMasked.notes[static_cast<size_t>(kept++)];
            if (note.start != masked.start || note.frequency != masked.frequency || note.duration != masked.duration || note.gain != masked.gain) return 168;
        }
    }
    if (!varied || kept != stateMasked.count) return 169;
    const auto stateId = risco::storeGeneratedCell(*mixedSong, 26, 0, "State", {}, 7);
    if (!stateId) return 170;
    mixedSong->bank[2].phraseMeters = meters;
    std::stringstream stateFile;
    if (!risco::saveSong(stateFile, *mixedSong) || !risco::loadSong(stateFile, connectedLoaded) ||
        connectedLoaded.bank[2].phraseVersion != 7 || connectedLoaded.bank[2].phraseMeters != meters) return 171;
    for (int meter = 2; meter <= 7; ++meter) for (int seed = 0; seed < 16; ++seed) {
        const auto sequence = risco::phraseFromSeed(seed, 3, 7, 120, 15, meter);
        if (sequence.count < 12 || sequence.count > 64 || sequence.end != meter * 2) return 172;
        for (int n = 0; n < sequence.count; ++n) {
            const auto& note = sequence.notes[static_cast<size_t>(n)];
            if (note.duration <= 0 || note.start < 0 || note.start + note.duration > sequence.end + 1e-9 ||
                note.gain <= 0 || note.gain > 1 || note.pulseDuty < 0.05f || note.pulseDuty > 0.95f) return 173;
        }
    }
    for (int preset : {5,6,7}) for (double bpm : {90.0,120.0}) {
        risco::StemEffect delay;
        delay.prepare(48000); delay.setPreset(preset); delay.setTempo(bpm);
        for (int i = 0; i < 24000; ++i) delay.process(0);
        const double ratio = preset == 5 ? 0.5 : preset == 6 ? 0.75 : 1.0;
        const int period = int(std::lround(48000 * 60 / bpm * ratio));
        for (int i = 0; i <= 2 * period; ++i) {
            const float output = delay.process(i == 0 ? 1.0f : 0.0f);
            if (!std::isfinite(output) || std::abs(output) > 1) return 174;
            if (i == period && std::abs(output - 0.3f) > 0.0002f) return 175;
            if (i == 2 * period && std::abs(output - 0.105f) > 0.0002f) return 176;
            if (i > 0 && i < period && std::abs(output) > 1e-6f) return 177;
        }
        delay.reset(); delay.setPreset(preset); delay.setTempo(bpm);
        for (int i = 0; i < 2 * period; ++i) if (delay.process(0) != 0) return 178;
        for (int i = 0; i < 24000; ++i) {
            if (i % 128 == 0) delay.setTempo(i % 256 == 0 ? 40 : 240);
            const float output = delay.process(0.2f);
            if (!std::isfinite(output) || std::abs(output) > 2) return 179;
        }
        delay.setPreset(0);
        float output = 0;
        for (int i = 0; i < 24000; ++i) output = delay.process(0);
        if (output != 0) return 180;
    }
    mixedSong->stemEffect = {5,6,7};
    mixedSong->bpm = 40;
    if (std::abs(risco::SongRender::tailSecondsFor(*mixedSong) - 15.1) > 1e-9) return 181;
    std::stringstream delayFile;
    if (!risco::saveSong(delayFile, *mixedSong) || !risco::loadSong(delayFile, connectedLoaded) ||
        connectedLoaded.stemEffect != mixedSong->stemEffect) return 182;
    mixedSong->stemEffect = {0,0,0};
    if (risco::SongRender::tailSecondsFor(*mixedSong) != 0.1) return 183;
    const auto tailSong = std::make_unique<risco::Song>();
    tailSong->bpm = 90;
    const auto tailPlayback = std::make_unique<risco::SongPlayback>();
    tailPlayback->count = 1; tailPlayback->end = 0.1;
    auto& shortPlacement = tailPlayback->placements[0];
    shortPlacement.duration = shortPlacement.playLength = shortPlacement.cell.end = 0.1;
    shortPlacement.cell.count = 1;
    shortPlacement.cell.notes[0] = {0, 0.1, 440, 0.2f};
    risco::SongRender dryRender(*tailSong, *tailPlayback, 0, true);
    tailSong->stemEffect[0] = 5;
    risco::SongRender echoRender(*tailSong, *tailPlayback, 0, true);
    double tailEnergy = 0;
    for (std::uint32_t i = 0; i < 24000; ++i) {
        const auto dry = dryRender.frame(i), echo = echoRender.frame(i);
        if (!std::isfinite(echo.left) || !std::isfinite(echo.right)) return 184;
        if (i > 5280) {
            if (dry.left != 0 || dry.right != 0) return 185;
            tailEnergy += echo.left * echo.left + echo.right * echo.right;
        }
    }
    if (tailEnergy < 0.01) return 186;
    for (float mix : {0.0f, 0.3f, 0.6f}) for (float feedback : {0.0f, 0.35f, 0.6f}) {
        risco::StemEffect adjustable;
        adjustable.prepare(48000); adjustable.setPreset(5); adjustable.setDelayParameters(mix, feedback);
        for (int i = 0; i < 24000; ++i) adjustable.process(0);
        for (int i = 0; i <= 24000; ++i) {
            const float out = adjustable.process(i == 0 ? 1.0f : 0.0f);
            if (!std::isfinite(out)) return 187;
            if (i == 0 && std::abs(out - (1 - mix)) > 0.0002f) return 188;
            if (i == 12000 && std::abs(out - mix) > 0.0002f) return 189;
            if (i == 24000 && std::abs(out - mix * feedback) > 0.0002f) return 190;
        }
    }
    mixedSong->stemEffect = {5,6,7}; mixedSong->bpm = 40;
    mixedSong->stemDelayMix = {0, 0.3f, 0.6f};
    mixedSong->stemDelayFeedback = {0.6f, 0, 0.6f};
    if (std::abs(risco::SongRender::tailSecondsFor(*mixedSong) - 21.1) > 1e-9) return 191;
    std::stringstream parameters;
    if (!risco::saveSong(parameters, *mixedSong) || !risco::loadSong(parameters, connectedLoaded) ||
        connectedLoaded.stemDelayMix != mixedSong->stemDelayMix ||
        connectedLoaded.stemDelayFeedback != mixedSong->stemDelayFeedback) return 192;
    auto v42 = parameters.str();
    v42.replace(0, std::string("RISCO_SONG_V65").size(), "RISCO_SONG_V42");
    removeFilterRecord(v42);
    const auto recordStart = v42.find("DELAY ");
    v42.erase(recordStart, v42.find('\n', recordStart) - recordStart + 1);
    std::istringstream oldDelay(v42);
    if (!risco::loadSong(oldDelay, connectedLoaded) || connectedLoaded.stemDelayMix[0] != 0.3f ||
        connectedLoaded.stemDelayFeedback[2] != 0.35f || connectedLoaded.stemEffect[2] != 7) return 193;
    for (const char* bad : {"DELAY -0.1 0.35 0.3 0.35 0.3 0.35", "DELAY 0.3 0.61 0.3 0.35 0.3 0.35", "DELAY nan 0.35", "DELAY 0.3 0.35"}) {
        auto corrupt = parameters.str();
        const auto start = corrupt.find("DELAY ");
        corrupt.replace(start, corrupt.find('\n', start) - start, bad);
        std::istringstream invalid(corrupt);
        if (risco::loadSong(invalid, connectedLoaded)) return 194;
    }
    mixedSong->stemDelayFeedback[0] = 0.61f;
    std::stringstream invalidFeedback;
    if (risco::saveSong(invalidFeedback, *mixedSong)) return 195;
    double shortEnergy = 0, longEnergy = 0;
    for (int preset : {8,9}) {
        risco::StemEffect room;
        room.prepare(48000); room.setPreset(preset);
        for (int i = 0; i < 24000; ++i) room.process(0);
        double lateEnergy = 0, finalEnergy = 0;
        for (int i = 0; i < 240000; ++i) {
            const float out = room.process(i == 0 ? 1.0f : 0.0f);
            if (!std::isfinite(out) || std::abs(out) > 2) return 196;
            if (i > 2400 && i < 48000) lateEnergy += out * out;
            if (i > 192000) finalEnergy += out * out;
        }
        if (lateEnergy < 0.00001 || finalEnergy > 1e-9) return 197;
        (preset == 8 ? shortEnergy : longEnergy) = lateEnergy;
        room.reset(); room.setPreset(preset);
        for (int i = 0; i < 4800; ++i) if (room.process(0) != 0) return 198;
        room.setPreset(0);
        float dry = 0;
        for (int i = 0; i < 24000; ++i) dry = room.process(0.2f);
        if (dry != 0.2f) return 199;
    }
    if (longEnergy <= shortEnergy) return 200;
    mixedSong->stemDelayFeedback[0] = 0.35f;
    mixedSong->stemEffect = {8,9,0};
    if (risco::SongRender::tailSecondsFor(*mixedSong) != 4.1) return 201;
    std::stringstream roomFile;
    if (!risco::saveSong(roomFile, *mixedSong) || !risco::loadSong(roomFile, connectedLoaded) ||
        connectedLoaded.stemEffect != mixedSong->stemEffect) return 202;
    auto prior = parameters.str();
    prior.replace(0, std::string("RISCO_SONG_V65").size(), "RISCO_SONG_V43");
    removeFilterRecord(prior);
    std::istringstream v43(prior);
    if (!risco::loadSong(v43, connectedLoaded) || connectedLoaded.stemDelayMix != mixedSong->stemDelayMix) return 203;
    tailSong->stemEffect[0] = 9;
    risco::SongRender roomRender(*tailSong, *tailPlayback, 0, true);
    double renderedTail = 0;
    for (std::uint32_t i = 0; i < 48000; ++i) {
        const auto out = roomRender.frame(i);
        if (!std::isfinite(out.left) || !std::isfinite(out.right)) return 204;
        if (i > 5280) renderedTail += out.left * out.left + out.right * out.right;
    }
    if (renderedTail < 0.001) return 205;
    double darkEnergy = 0, brightEnergy = 0;
    for (float hz : {320.0f, 8000.0f}) {
        risco::StemEffect filter;
        filter.prepare(48000); filter.setPreset(1); filter.setFilterCutoff(hz);
        double energy = 0;
        for (int i = 0; i < 24000; ++i) {
            const float input = float(0.3 * std::sin(6.283185307179586 * 6000 * i / 48000));
            const float out = filter.process(input);
            if (!std::isfinite(out) || std::abs(out) > 1) return 206;
            if (i > 12000) energy += out * out;
        }
        (hz == 320 ? darkEnergy : brightEnergy) = energy;
        filter.setPreset(0);
        float dry = 0;
        for (int i = 0; i < 24000; ++i) dry = filter.process(0.2f);
        if (dry != 0.2f) return 207;
    }
    if (brightEnergy < 20 * darkEnergy || darkEnergy <= 0) return 208;
    risco::StemEffect legacyFilter, restoredFilter;
    legacyFilter.prepare(48000); restoredFilter.prepare(48000);
    legacyFilter.setPreset(2); restoredFilter.setPreset(2);
    restoredFilter.setFilterCutoff(80); restoredFilter.setFilterCutoff(0);
    for (int i = 0; i < 2400; ++i) {
        const float input = i % 13 == 0 ? 0.2f : 0;
        if (legacyFilter.process(input) != restoredFilter.process(input)) return 209;
    }
    mixedSong->stemFilterCutoff = {0, 80, 16000};
    std::stringstream filterFile;
    if (!risco::saveSong(filterFile, *mixedSong) || !risco::loadSong(filterFile, connectedLoaded) ||
        connectedLoaded.stemFilterCutoff != mixedSong->stemFilterCutoff) return 210;
    auto v44Text = filterFile.str();
    v44Text.replace(0, std::string("RISCO_SONG_V65").size(), "RISCO_SONG_V44");
    removeFilterRecord(v44Text);
    std::istringstream oldFilter(v44Text);
    if (!risco::loadSong(oldFilter, connectedLoaded) || connectedLoaded.stemFilterCutoff != std::array<float,3>{}) return 211;
    for (const char* bad : {"FILTER -1 80 16000", "FILTER 1 80 16000", "FILTER 0 80 16001", "FILTER nan 80 16000", "FILTER 0 80", "FILTER 0 80 16000 extra"}) {
        auto text = filterFile.str();
        const auto start = text.find("FILTER ");
        text.replace(start, text.find('\n', start) - start, bad);
        std::istringstream invalid(text);
        if (risco::loadSong(invalid, connectedLoaded)) return 212;
    }
    mixedSong->stemFilterCutoff[0] = 1;
    std::stringstream invalidCutoff;
    if (risco::saveSong(invalidCutoff, *mixedSong)) return 213;
    tailSong->stemEffect[0] = 1; tailSong->stemFilterCutoff[0] = 80;
    risco::SongRender darkRender(*tailSong, *tailPlayback, 0, true);
    tailSong->stemFilterCutoff[0] = 16000;
    risco::SongRender brightRender(*tailSong, *tailPlayback, 0, true);
    double darkRendered = 0, brightRendered = 0;
    for (std::uint32_t i = 0; i < 4800; ++i) {
        const auto dark = darkRender.frame(i), bright = brightRender.frame(i);
        if (i > 2400) { darkRendered += dark.left * dark.left; brightRendered += bright.left * bright.left; }
    }
    if (brightRendered <= 5 * darkRendered) return 214;
    for (int preset : {8,9}) {
        double referenceEnergy = 0;
        for (float mix : {0.0f, 0.25f, 0.6f}) {
            risco::StemEffect adjustableRoom;
            adjustableRoom.prepare(48000); adjustableRoom.setPreset(preset); adjustableRoom.setReverbMix(mix);
            for (int i = 0; i < 24000; ++i) adjustableRoom.process(0);
            double energy = 0;
            for (int i = 0; i < 48000; ++i) {
                const float out = adjustableRoom.process(i == 0 ? 1.0f : 0);
                if (!std::isfinite(out) || std::abs(out) > 2) return 215;
                if (i == 0 && std::abs(out - (1 - mix)) > 0.0002f) return 216;
                if (i > 2400) energy += out * out;
            }
            if (mix == 0 && energy != 0) return 217;
            if (mix == 0.25f) referenceEnergy = energy;
            if (mix == 0.6f && (referenceEnergy <= 0 || std::abs(energy / referenceEnergy - 5.76) > 0.01)) return 218;
            adjustableRoom.reset(); adjustableRoom.setPreset(preset);
            for (int i = 0; i < 4800; ++i) if (adjustableRoom.process(0) != 0) return 219;
        }
    }
    mixedSong->stemFilterCutoff[0] = 0;
    mixedSong->stemReverbMix = {0, 0.25f, 0.6f};
    std::stringstream roomMixFile;
    if (!risco::saveSong(roomMixFile, *mixedSong) || !risco::loadSong(roomMixFile, connectedLoaded) ||
        connectedLoaded.stemReverbMix != mixedSong->stemReverbMix) return 220;
    auto oldMixText = roomMixFile.str();
    oldMixText.replace(0, std::string("RISCO_SONG_V65").size(), "RISCO_SONG_V45");
    removeAutomationRecord(oldMixText);
    const auto reverbRecord = oldMixText.find("REVERB ");
    oldMixText.erase(reverbRecord, oldMixText.find('\n', reverbRecord) - reverbRecord + 1);
    std::istringstream oldMix(oldMixText);
    if (!risco::loadSong(oldMix, connectedLoaded) || connectedLoaded.stemReverbMix != std::array<float,3>{{0.25f,0.25f,0.25f}}) return 221;
    for (const char* bad : {"REVERB -0.1 0.25 0.6", "REVERB 0 0.25 0.61", "REVERB nan 0.25 0.6", "REVERB 0 0.25", "REVERB 0 0.25 0.6 extra"}) {
        auto text = roomMixFile.str();
        const auto start = text.find("REVERB ");
        text.replace(start, text.find('\n', start) - start, bad);
        std::istringstream invalid(text);
        if (risco::loadSong(invalid, connectedLoaded)) return 222;
    }
    mixedSong->stemReverbMix[0] = 0.61f;
    std::stringstream invalidRoomMix;
    if (risco::saveSong(invalidRoomMix, *mixedSong)) return 223;
    tailSong->stemEffect[0] = 9; tailSong->stemReverbMix[0] = 0;
    risco::SongRender dryRoom(*tailSong, *tailPlayback, 0, true);
    tailSong->stemReverbMix[0] = 0.6f;
    risco::SongRender wetRoom(*tailSong, *tailPlayback, 0, true);
    double wetTail = 0;
    for (std::uint32_t i = 0; i < 48000; ++i) {
        const auto dry = dryRoom.frame(i), wet = wetRoom.frame(i);
        if (i > 5280) {
            if (dry.left != 0 || dry.right != 0) return 224;
            wetTail += wet.left * wet.left + wet.right * wet.right;
        }
    }
    if (wetTail < 0.001) return 225;
    for (double rate : {8000.0,48000.0,192000.0}) {
        double fastLate = 0, slowLate = 0;
        for (float decay : {0.25f,3.0f}) {
            risco::StemEffect room;
            room.prepare(rate); room.setPreset(9); room.setReverbDecay(decay);
            for (int i = 0; i < int(rate / 2); ++i) room.process(0);
            double late = 0, final = 0;
            for (int i = 0; i < int(rate * 6.1); ++i) {
                const float out = room.process(i == 0 ? 1.0f : 0);
                if (!std::isfinite(out) || std::abs(out) > 2) return 226;
                if (i > rate && i < rate * 2) late += out * out;
                if (i > rate * 6) final += out * out;
            }
            if (final > 1e-9) return 227;
            (decay == 0.25f ? fastLate : slowLate) = late;
        }
        if (slowLate <= fastLate * 10 || slowLate < 1e-10) return 228;
    }
    risco::StemReverb originalRoom, resetRoom;
    originalRoom.prepare(48000); resetRoom.prepare(48000);
    resetRoom.setDecaySeconds(3); resetRoom.setDecaySeconds(0);
    for (int i = 0; i < 12000; ++i)
        if (originalRoom.process(i == 0 ? 1.0f : 0, true) != resetRoom.process(i == 0 ? 1.0f : 0, true)) return 229;
    mixedSong->stemReverbMix[0] = 0.25f;
    mixedSong->stemReverbDecay = {0,0.25f,3};
    mixedSong->stemEffect = {8,0,9};
    if (risco::SongRender::tailSecondsFor(*mixedSong) != 6.1) return 230;
    std::stringstream decayFile;
    if (!risco::saveSong(decayFile, *mixedSong) || !risco::loadSong(decayFile, connectedLoaded) ||
        connectedLoaded.stemReverbDecay != mixedSong->stemReverbDecay) return 231;
    auto oldDecay = decayFile.str();
    oldDecay.replace(0, std::string("RISCO_SONG_V65").size(), "RISCO_SONG_V46");
    removeAutomationRecord(oldDecay);
    const auto decayRecordStart = oldDecay.find("REVERB ");
    const auto decayRecordEnd = oldDecay.find('\n', decayRecordStart);
    auto oldRecord = oldDecay.substr(decayRecordStart, decayRecordEnd - decayRecordStart);
    for (int i = 0; i < 3; ++i) {
        const auto space = oldRecord.find_last_of(' ');
        if (space == std::string::npos) return 232;
        oldRecord.erase(space);
    }
    oldDecay.replace(decayRecordStart, decayRecordEnd - decayRecordStart, oldRecord);
    std::istringstream v46(oldDecay);
    if (!risco::loadSong(v46, connectedLoaded) || connectedLoaded.stemReverbDecay != std::array<float,3>{} ||
        connectedLoaded.stemReverbMix != mixedSong->stemReverbMix) return 232;
    for (const char* bad : {"REVERB 0.25 0.25 0.6 -1 0.25 3", "REVERB 0.25 0.25 0.6 0.1 0.25 3", "REVERB 0.25 0.25 0.6 0 0.25 3.1", "REVERB 0.25 0.25 0.6 0 nan 3", "REVERB 0.25 0.25 0.6 0 0.25"}) {
        auto text = decayFile.str();
        const auto start = text.find("REVERB ");
        text.replace(start, text.find('\n', start) - start, bad);
        std::istringstream invalid(text);
        if (risco::loadSong(invalid, connectedLoaded)) return 233;
    }
    mixedSong->stemReverbDecay[0] = 0.1f;
    std::stringstream invalidDecay;
    if (risco::saveSong(invalidDecay, *mixedSong)) return 234;
    tailSong->stemReverbDecay[0] = 0.25f;
    risco::SongRender fastRoom(*tailSong, *tailPlayback, 0, true);
    tailSong->stemReverbDecay[0] = 3;
    risco::SongRender slowRoom(*tailSong, *tailPlayback, 0, true);
    double fastTail = 0, slowTail = 0;
    for (std::uint32_t i = 0; i < 96000; ++i) {
        const auto fast = fastRoom.frame(i), slow = slowRoom.frame(i);
        if (i > 48000) { fastTail += fast.left * fast.left; slowTail += slow.left * slow.left; }
    }
    if (slowTail <= 10 * fastTail || slowTail < 1e-8) return 235;
    risco::GainAutomation fade{true, {{0,0.5f,1}}};
    if (risco::automationGainAt(fade, -1, 8) != 0 || risco::automationGainAt(fade, 0, 8) != 0 ||
        risco::automationGainAt(fade, 2, 8) != 0.25f || risco::automationGainAt(fade, 4, 8) != 0.5f ||
        risco::automationGainAt(fade, 8, 8) != 1 || risco::automationGainAt(fade, 20, 8) != 1) return 236;
    risco::StemAutomation smoother;
    smoother.prepare(48000);
    for (int i = 0; i < 4800; ++i) {
        const float value = smoother.process(fade, i / 600.0, 8);
        if (!std::isfinite(value) || value < 0 || value > 1) return 237;
    }
    fade.enabled = false;
    float restoredGain = 0;
    for (int i = 0; i < 12000; ++i) restoredGain = smoother.process(fade, 0, 8);
    if (restoredGain != 1 || fade.levels[0] != 0) return 238;
    smoother.reset(); fade.enabled = true;
    if (smoother.process(fade, 0, 8) != 0) return 239;
    mixedSong->stemReverbDecay[0] = 0;
    mixedSong->stemAutomation[0] = {true, {{0,0.5f,1}}};
    mixedSong->stemAutomation[1] = {false, {{1,0.25f,0}}};
    std::stringstream automationFile;
    if (!risco::saveSong(automationFile, *mixedSong) || !risco::loadSong(automationFile, connectedLoaded)) return 240;
    for (size_t s = 0; s < 3; ++s)
        if (connectedLoaded.stemAutomation[s].enabled != mixedSong->stemAutomation[s].enabled ||
            connectedLoaded.stemAutomation[s].levels != mixedSong->stemAutomation[s].levels) return 241;
    auto oldAutomation = automationFile.str();
    oldAutomation.replace(0, std::string("RISCO_SONG_V65").size(), "RISCO_SONG_V47");
    removeAutomationRecord(oldAutomation);
    std::istringstream v47(oldAutomation);
    if (!risco::loadSong(v47, connectedLoaded) || connectedLoaded.stemAutomation[0].enabled ||
        connectedLoaded.stemAutomation[0].levels != std::array<float,3>{{1,1,1}}) return 242;
    for (const char* bad : {"AUTOMATION 2 0 0.5 1 0 1 1 1 0 1 1 1", "AUTOMATION 1 -1 0.5 1 0 1 1 1 0 1 1 1", "AUTOMATION 1 0 0.5 1.1 0 1 1 1 0 1 1 1", "AUTOMATION 1 0 nan 1 0 1 1 1 0 1 1 1", "AUTOMATION 1 0 0.5 1"}) {
        auto text = automationFile.str();
        const auto start = text.find("AUTOMATION ");
        text.replace(start, text.find('\n', start) - start, bad);
        std::istringstream invalid(text);
        if (risco::loadSong(invalid, connectedLoaded)) return 243;
    }
    mixedSong->stemAutomation[0].levels[0] = 1.1f;
    std::stringstream invalidAutomation;
    if (risco::saveSong(invalidAutomation, *mixedSong)) return 244;
    tailSong->stemEffect[0] = 0; tailSong->stemAutomation[0] = {true, {{0.5f,0.5f,0.5f}}};
    risco::SongRender halfVolume(*tailSong, *tailPlayback, 0, true);
    tailSong->stemAutomation[0].enabled = false;
    risco::SongRender fullVolume(*tailSong, *tailPlayback, 0, true);
    double halfEnergy = 0, fullEnergy = 0;
    for (std::uint32_t i = 0; i < 4800; ++i) {
        const auto half = halfVolume.frame(i), full = fullVolume.frame(i);
        halfEnergy += half.left * half.left; fullEnergy += full.left * full.left;
    }
    if (fullEnergy <= 0 || std::abs(halfEnergy / fullEnergy - 0.25) > 0.00001) return 245;
    risco::PanAutomation sweep{true,{{-1,0,1}}};
    if (risco::automationPanAt(sweep,-1,8) != -1 || risco::automationPanAt(sweep,2,8) != -0.5f ||
        risco::automationPanAt(sweep,4,8) != 0 || risco::automationPanAt(sweep,6,8) != 0.5f ||
        risco::automationPanAt(sweep,8,8) != 1) return 280;
    sweep.enabled = false;
    if (risco::automationPanAt(sweep,4,8,0.25f) != 0.25f) return 281;
    risco::StemPanAutomation panSmoother; panSmoother.prepare(48000); sweep.enabled = true;
    for (int i = 0; i < 4800; ++i) {
        const float value = panSmoother.process(sweep,i/600.0,8,0);
        if (!std::isfinite(value) || value < -1 || value > 1) return 282;
    }
    risco::FxAutomation gate{true,{{true,false,true}}};
    if (!risco::automationFxActiveAt(gate,0,8) || risco::automationFxActiveAt(gate,2,8) ||
        risco::automationFxActiveAt(gate,5.999,8) || !risco::automationFxActiveAt(gate,6,8)) return 283;
    gate.enabled = false;
    if (!risco::automationFxActiveAt(gate,4,8)) return 284;
    auto automationSong = std::make_unique<risco::Song>();
    automationSong->stemPanAutomation[0] = {true,{{-1,0,1}}};
    automationSong->stemFxAutomation[0] = {true,{{true,false,true}}};
    automationSong->stemEffect[0] = 2;
    automationSong->stemFilterAutomation[0] = {true,{{320.0f, 2500.0f, 8000.0f}}};
    automationSong->stemDelayMixAutomation[0] = {true,{{0.1f, 0.3f, 0.5f}}};
    automationSong->stemDelayFeedbackAutomation[0] = {true,{{0.2f, 0.35f, 0.5f}}};
    automationSong->stemReverbMixAutomation[0] = {true,{{0.1f, 0.4f, 0.2f}}};
    std::stringstream spatialFile;
    if (!risco::saveSong(spatialFile,*automationSong) || !risco::loadSong(spatialFile,connectedLoaded) ||
        connectedLoaded.stemPanAutomation[0].levels != automationSong->stemPanAutomation[0].levels ||
        !connectedLoaded.stemPanAutomation[0].enabled ||
        connectedLoaded.stemFxAutomation[0].active != automationSong->stemFxAutomation[0].active ||
        !connectedLoaded.stemFxAutomation[0].enabled ||
        !connectedLoaded.stemFilterAutomation[0].enabled ||
        connectedLoaded.stemFilterAutomation[0].values != automationSong->stemFilterAutomation[0].values ||
        !connectedLoaded.stemDelayMixAutomation[0].enabled ||
        connectedLoaded.stemDelayMixAutomation[0].values != automationSong->stemDelayMixAutomation[0].values ||
        !connectedLoaded.stemDelayFeedbackAutomation[0].enabled ||
        connectedLoaded.stemDelayFeedbackAutomation[0].values != automationSong->stemDelayFeedbackAutomation[0].values ||
        !connectedLoaded.stemReverbMixAutomation[0].enabled ||
        connectedLoaded.stemReverbMixAutomation[0].values != automationSong->stemReverbMixAutomation[0].values) return 285;
    auto v64Text = spatialFile.str();
    v64Text.replace(0, std::string("RISCO_SONG_V65").size(), "RISCO_SONG_V64");
    const auto reverbParameters = v64Text.find("REVERB_PARAMS ");
    if (reverbParameters == std::string::npos) return 316;
    v64Text.erase(reverbParameters, v64Text.find('\n', reverbParameters) - reverbParameters + 1);
    const auto reverbDecayParameters = v64Text.find("REVERB_DECAY_PARAMS ");
    if (reverbDecayParameters != std::string::npos)
        v64Text.erase(reverbDecayParameters, v64Text.find('\n', reverbDecayParameters) - reverbDecayParameters + 1);
    const auto bitcrushParameters = v64Text.find("BITCRUSH ");
    if (bitcrushParameters != std::string::npos)
        v64Text.erase(bitcrushParameters, v64Text.find('\n', bitcrushParameters) - bitcrushParameters + 1);
    const auto bitcrushParams = v64Text.find("BITCRUSH_PARAMS ");
    if (bitcrushParams != std::string::npos)
        v64Text.erase(bitcrushParams, v64Text.find('\n', bitcrushParams) - bitcrushParams + 1);
    std::istringstream v64Spatial(v64Text);
    if (!risco::loadSong(v64Spatial, connectedLoaded) || connectedLoaded.stemReverbMixAutomation[0].enabled)
        return 317;
    auto v55Text = spatialFile.str();
    v55Text.replace(0,std::string("RISCO_SONG_V65").size(),"RISCO_SONG_V55");
    for (const char* tag : {"PAN_AUTOMATION ","FX_AUTOMATION ","HISTORICAL_AY ","METRIC ","DIALOGUE ","FX_PARAMS ","DELAY_PARAMS ","REVERB_PARAMS ","REVERB_DECAY_PARAMS ","BITCRUSH ","BITCRUSH_PARAMS ","RESPONSE "}) {
        const auto start = v55Text.find(tag);
        if (start == std::string::npos) return 286;
        const auto end = v55Text.find('\n', start);
        v55Text.erase(start, end == std::string::npos ? v55Text.size() - start : end - start + 1);
    }
    std::istringstream v55Spatial(v55Text);
    if (!risco::loadSong(v55Spatial,connectedLoaded) || connectedLoaded.stemPanAutomation[0].enabled ||
        connectedLoaded.stemFxAutomation[0].enabled || connectedLoaded.stemPanAutomation[0].levels != std::array<float,3>{{0,0,0}} ||
        connectedLoaded.stemFxAutomation[0].active != std::array<bool,3>{{true,true,true}}) return 287;
    for (const char* bad : {"PAN_AUTOMATION 1 -1 0 2 0 0 0 0 0 0 0 0", "PAN_AUTOMATION 2 -1 0 1 0 0 0 0 0 0 0 0",
                            "PAN_AUTOMATION 1 -1 nan 1 0 0 0 0 0 0 0 0", "PAN_AUTOMATION 1 -1 0",
                            "FX_AUTOMATION 1 1 2 1 0 1 1 1 0 1 1 1", "FX_AUTOMATION 2 1 0 1 0 1 1 1 0 1 1 1",
                            "FX_AUTOMATION 1 1 0"}) {
        auto text = spatialFile.str();
        const auto tag = std::string(bad).rfind("PAN_",0) == 0 ? "PAN_AUTOMATION " : "FX_AUTOMATION ";
        const auto start = text.find(tag);
        text.replace(start,text.find('\n',start)-start,bad);
        std::istringstream invalid(text);
        if (risco::loadSong(invalid,connectedLoaded)) return 288;
    }
    automationSong->stemPanAutomation[0].levels[1] = 1.1f;
    std::stringstream invalidSpatial;
    if (risco::saveSong(invalidSpatial,*automationSong)) return 289;
    auto spatialPlayback = std::make_unique<risco::SongPlayback>();
    spatialPlayback->count = 1; spatialPlayback->end = 1;
    auto& spatialPlacement = spatialPlayback->placements[0];
    spatialPlacement.stem = 0; spatialPlacement.duration = spatialPlacement.playLength = 1;
    spatialPlacement.cell.count = 1; spatialPlacement.cell.end = 1;
    spatialPlacement.cell.notes[0] = {0,1,8000,0.2f};
    auto filterSong = std::make_unique<risco::Song>();
    filterSong->stemEffect[0] = 1;
    filterSong->stemFilterCutoff[0] = 80.0f;
    filterSong->stemFilterAutomation[0] = {true, {{80.0f, 16000.0f, 80.0f}}};
    risco::SongRender movingFilter(*filterSong, *spatialPlayback, 0, true);
    filterSong->stemFilterAutomation[0].enabled = false;
    risco::SongRender fixedFilter(*filterSong, *spatialPlayback, 0, true);
    double filterDifference = 0.0;
    for (std::uint32_t i = 0; i < 24000; ++i) {
        const auto moving = movingFilter.frame(i), fixed = fixedFilter.frame(i);
        if (i > 12000) filterDifference += std::abs(moving.left - fixed.left);
    }
    if (filterDifference < 1.0) return 318;
    auto leftSong = std::make_unique<risco::Song>();
    leftSong->stemPanAutomation[0] = {true,{{-1,-1,-1}}};
    risco::SongRender leftRender(*leftSong,*spatialPlayback,0,true);
    double leftEnergy = 0, rightEnergy = 0;
    for (std::uint32_t i = 0; i < 4800; ++i) { const auto frame = leftRender.frame(i); leftEnergy += frame.left*frame.left; rightEnergy += frame.right*frame.right; }
    if (leftEnergy <= 0 || rightEnergy > leftEnergy*1e-8) return 290;
    auto gatedSong = std::make_unique<risco::Song>();
    gatedSong->stemEffect[0] = 2; gatedSong->stemFxAutomation[0] = {true,{{false,true,false}}};
    auto bypassSong = std::make_unique<risco::Song>();
    risco::SongRender gatedRender(*gatedSong,*spatialPlayback,0,true), bypassRender(*bypassSong,*spatialPlayback,0,true);
    double entranceDifference = 0, middleDifference = 0;
    for (std::uint32_t i = 0; i < 36000; ++i) {
        const auto wet = gatedRender.frame(i), dry = bypassRender.frame(i);
        const double difference = std::abs(wet.left-dry.left)+std::abs(wet.right-dry.right);
        if (i < 8000) entranceDifference += difference;
        if (i >= 18000 && i < 26000) middleDifference += difference;
    }
    if (entranceDifference > 1e-6 || middleDifference < 1) return 291;
    risco::Sequence strengthSource;
    strengthSource.count = 2; strengthSource.end = 2;
    strengthSource.notes[0] = {0.1,0.2,880,0.1f};
    strengthSource.notes[1] = {1.2,0.3,440,0.3f};
    for (int mode = 1; mode <= 7; ++mode) {
        const auto original = risco::interpretInput(strengthSource, mode, 0);
        const auto full = risco::interpretInput(strengthSource, mode, 1);
        const auto legacy = risco::interpretInput(strengthSource, mode);
        for (int n = 0; n < 2; ++n) {
            const auto& a = strengthSource.notes[n]; const auto& b = original.notes[n];
            if (a.start != b.start || a.duration != b.duration || a.frequency != b.frequency || a.gain != b.gain) return 246;
            if (full.notes[n].start != legacy.notes[n].start || full.notes[n].frequency != legacy.notes[n].frequency) return 247;
        }
        for (float strength : {0.25f,0.5f,0.75f}) {
            const auto blend = risco::interpretInput(strengthSource, mode, strength);
            if (blend.count != 2 || blend.end != 2) return 248;
            for (int n = 0; n < 2; ++n) {
                const auto& note = blend.notes[n];
                if (!std::isfinite(note.frequency) || note.frequency <= 0 || note.start < 0 || note.duration <= 0 ||
                    note.start + note.duration > 2 + 1e-9 || note.gain < 0 || note.gain > 1) return 249;
            }
        }
    }
    const auto accelerate = risco::interpretInput(strengthSource,5);
    const auto decelerate = risco::interpretInput(strengthSource,6);
    const auto breathe = risco::interpretInput(strengthSource,7);
    if (accelerate.end != strengthSource.end || decelerate.end != strengthSource.end || breathe.end != strengthSource.end ||
        accelerate.notes[1].start <= strengthSource.notes[1].start ||
        decelerate.notes[1].start >= strengthSource.notes[1].start ||
        breathe.notes[0].start == strengthSource.notes[0].start) return 292;
    for (const auto* gesture : {&accelerate,&decelerate,&breathe})
        for (int n = 0; n < gesture->count; ++n) {
            const auto& note = gesture->notes[static_cast<size_t>(n)];
            if (!std::isfinite(note.start) || !std::isfinite(note.duration) || note.start < 0 ||
                note.duration <= 0 || note.start+note.duration > gesture->end+1e-9) return 293;
        }
    risco::TemporalGestureCurve authorialCurve;
    authorialCurve.enabled = true;
    authorialCurve.values = {{0.0f, 0.8f, 1.0f}};
    if (!risco::validTemporalGestureCurve(authorialCurve)) return 298;
    const auto authorial = risco::interpretInput(strengthSource, 8, 1, authorialCurve);
    if (authorial.end != strengthSource.end || authorial.notes[0].start <= strengthSource.notes[0].start ||
        authorial.notes[1].start <= strengthSource.notes[1].start) return 299;
    auto invalidAuthorial = authorialCurve;
    invalidAuthorial.values[1] = 1.2f;
    if (risco::validTemporalGestureCurve(invalidAuthorial) ||
        risco::interpretInput(strengthSource, 8, 1, invalidAuthorial).notes[1].start != strengthSource.notes[1].start) return 300;
    const auto crescendo = risco::interpretInput(strengthSource, 9, 1, authorialCurve);
    if (crescendo.end != strengthSource.end || crescendo.notes[0].start != strengthSource.notes[0].start ||
        crescendo.notes[0].duration != strengthSource.notes[0].duration ||
        std::abs(crescendo.notes[0].gain - 0.008f) > 1e-6f ||
        crescendo.notes[1].start != strengthSource.notes[1].start || crescendo.notes[1].duration != strengthSource.notes[1].duration ||
        std::abs(crescendo.notes[1].gain - strengthSource.notes[1].gain * 0.84f) > 1e-6f) return 320;
    const auto diminuendo = risco::interpretInput(strengthSource, 10, 1, authorialCurve);
    if (diminuendo.end != strengthSource.end || diminuendo.notes[0].start != strengthSource.notes[0].start ||
        diminuendo.notes[0].duration != strengthSource.notes[0].duration ||
        std::abs(diminuendo.notes[0].gain - 0.092f) > 1e-6f ||
        diminuendo.notes[1].start != strengthSource.notes[1].start || diminuendo.notes[1].duration != strengthSource.notes[1].duration ||
        std::abs(diminuendo.notes[1].gain - 0.048f) > 1e-6f) return 324;
    const auto risingPitch = risco::interpretInput(strengthSource,11,1,authorialCurve,24);
    const auto fallingPitch = risco::interpretInput(strengthSource,12,1,authorialCurve,6);
    if (risingPitch.end != strengthSource.end || fallingPitch.end != strengthSource.end) return 352;
    if (risingPitch.notes[0].start != strengthSource.notes[0].start ||
        risingPitch.notes[0].duration != strengthSource.notes[0].duration) return 353;
    if (risingPitch.notes[0].frequency <= strengthSource.notes[0].frequency ||
        risingPitch.notes[1].frequency <= risingPitch.notes[0].frequency ||
        fallingPitch.notes[0].frequency <= fallingPitch.notes[1].frequency ||
        fallingPitch.notes[1].frequency <= strengthSource.notes[1].frequency) return 354;
    if (std::abs(risingPitch.notes[0].frequency - strengthSource.notes[0].frequency * std::exp2(0.08 * 2.0)) > 1e-4) return 355;
    if (std::abs(fallingPitch.notes[1].frequency - strengthSource.notes[1].frequency * std::exp2(0.08)) > 1e-4) return 356;
    if (!risco::validTemporalGestureRange(6) || !risco::validTemporalGestureRange(12) ||
        !risco::validTemporalGestureRange(24) || risco::validTemporalGestureRange(7)) return 357;
    if (risco::interpretInput(strengthSource,11,1,authorialCurve,7).notes[0].frequency != strengthSource.notes[0].frequency) return 358;
    const auto pitchArch = risco::interpretInput(strengthSource,13,1,authorialCurve,24);
    const auto archProgress = [](double curve) { return 1.0 - std::abs(2.0 * curve - 1.0); };
    if (pitchArch.end != strengthSource.end || pitchArch.notes[0].start != strengthSource.notes[0].start ||
        pitchArch.notes[0].duration != strengthSource.notes[0].duration ||
        pitchArch.notes[1].start != strengthSource.notes[1].start ||
        pitchArch.notes[1].duration != strengthSource.notes[1].duration ||
        std::abs(pitchArch.notes[0].frequency - strengthSource.notes[0].frequency * std::exp2(archProgress(0.08) * 2.0)) > 1e-4 ||
        std::abs(pitchArch.notes[1].frequency - strengthSource.notes[1].frequency * std::exp2(archProgress(0.84) * 2.0)) > 1e-4 ||
        pitchArch.notes[0].frequency <= strengthSource.notes[0].frequency ||
        pitchArch.notes[1].frequency <= strengthSource.notes[1].frequency) return 359;
    risco::Sequence archLandmarks;
    archLandmarks.end = 2.0;
    archLandmarks.count = 3;
    archLandmarks.notes[0] = {0.0,0.1,440.0,0.2f};
    archLandmarks.notes[1] = {0.625,0.1,440.0,0.2f};
    archLandmarks.notes[2] = {1.999,0.001,440.0,0.2f};
    const auto archLandmarkResult = risco::interpretInput(archLandmarks,13,1,authorialCurve,24);
    if (std::abs(archLandmarkResult.notes[0].frequency - 440.0) > 1e-9 ||
        std::abs(archLandmarkResult.notes[1].frequency - 1760.0) > 1e-3 ||
        std::abs(archLandmarkResult.notes[2].frequency - 440.0) > 0.3) return 362;
    const auto risingPitchTwelve = risco::interpretInput(strengthSource,11,1,authorialCurve,12);
    const auto halfPitch = risco::interpretInput(strengthSource,11,0.5f,authorialCurve,12);
    if (std::abs(halfPitch.notes[0].frequency - std::sqrt(risingPitchTwelve.notes[0].frequency * strengthSource.notes[0].frequency)) > 1e-6)
        return 336;
    const auto halfReading = risco::interpretInput(strengthSource, 2, 0.5f);
    if (std::abs(halfReading.notes[0].frequency - 440) > 1e-9 || std::abs(halfReading.notes[0].gain - 0.13f) > 1e-6) return 250;
    dialog->bank[2].inputInterpretationStrength = 0.5f;
    const auto strengthPlayback = std::make_unique<risco::SongPlayback>(risco::prepareSongPlayback(*dialog));
    const auto originalReading = risco::composeInput(inscription);
    if (std::abs(strengthPlayback->placements[2].cell.notes[0].frequency -
        std::sqrt(originalReading.notes[0].frequency * 220)) > 1e-6) return 255;
    std::stringstream strengthFile;
    if (!risco::saveSong(strengthFile, *dialog) || !risco::loadSong(strengthFile, connectedLoaded) ||
        connectedLoaded.bank[2].inputInterpretationStrength != 0.5f || connectedLoaded.bank[2].inscription.ascii != inscription.ascii) return 251;
    auto gestureSong = std::make_unique<risco::Song>(*dialog);
    gestureSong->bank[2].inputInterpretation = 7;
    std::stringstream gestureFile;
    if (!risco::saveSong(gestureFile,*gestureSong) || !risco::loadSong(gestureFile,connectedLoaded) ||
        connectedLoaded.bank[2].inputInterpretation != 7) return 294;
    auto v56Gesture = gestureFile.str();
    v56Gesture.replace(0,std::string("RISCO_SONG_V65").size(),"RISCO_SONG_V56");
    removeResponseRecord(v56Gesture);
    removeInputGestureCurveFields(v56Gesture);
    std::istringstream unsupportedGesture(v56Gesture);
    if (risco::loadSong(unsupportedGesture,connectedLoaded)) return 295;
    gestureSong->bank[2].inputInterpretation = 4;
    std::stringstream legacyGestureFile;
    if (!risco::saveSong(legacyGestureFile,*gestureSong)) return 296;
    auto compatibleGesture = legacyGestureFile.str();
    compatibleGesture.replace(0,std::string("RISCO_SONG_V65").size(),"RISCO_SONG_V56");
    removeResponseRecord(compatibleGesture);
    removeInputGestureCurveFields(compatibleGesture);
    std::istringstream supportedGesture(compatibleGesture);
    if (!risco::loadSong(supportedGesture,connectedLoaded) || connectedLoaded.bank[2].inputInterpretation != 4) return 297;
    gestureSong->bank[2].inputInterpretation = 8;
    gestureSong->bank[2].inputGestureCurve = authorialCurve;
    std::stringstream authorialFile;
    if (!risco::saveSong(authorialFile, *gestureSong) || !risco::loadSong(authorialFile, connectedLoaded) ||
        connectedLoaded.bank[2].inputInterpretation != 8 || !connectedLoaded.bank[2].inputGestureCurve.enabled ||
        std::abs(connectedLoaded.bank[2].inputGestureCurve.values[1] - 0.8f) > 1e-6f) return 241;
    auto v69Authorial = authorialFile.str();
    v69Authorial.replace(0, std::string("RISCO_SONG_V71").size(), "RISCO_SONG_V69");
    std::istringstream compatibleV69(v69Authorial);
    if (!risco::loadSong(compatibleV69, connectedLoaded) || connectedLoaded.bank[2].inputInterpretation != 8) return 321;
    auto v68Authorial = authorialFile.str();
    v68Authorial.replace(0, std::string("RISCO_SONG_V71").size(), "RISCO_SONG_V68");
    std::istringstream unsupportedAuthorial(v68Authorial);
    if (risco::loadSong(unsupportedAuthorial, connectedLoaded)) return 302;
    gestureSong->bank[2].inputInterpretation = 9;
    std::stringstream crescendoFile;
    if (!risco::saveSong(crescendoFile, *gestureSong) || !risco::loadSong(crescendoFile, connectedLoaded) ||
        connectedLoaded.bank[2].inputInterpretation != 9 || !connectedLoaded.bank[2].inputGestureCurve.enabled) return 322;
    auto compatibleV70Crescendo = crescendoFile.str();
    compatibleV70Crescendo.replace(0, std::string("RISCO_SONG_V71").size(), "RISCO_SONG_V70");
    std::istringstream supportedV70Crescendo(compatibleV70Crescendo);
    if (!risco::loadSong(supportedV70Crescendo, connectedLoaded) || connectedLoaded.bank[2].inputInterpretation != 9) return 325;
    auto incompatibleV69 = crescendoFile.str();
    incompatibleV69.replace(0, std::string("RISCO_SONG_V71").size(), "RISCO_SONG_V69");
    std::istringstream unsupportedV69Crescendo(incompatibleV69);
    if (risco::loadSong(unsupportedV69Crescendo, connectedLoaded)) return 323;
    gestureSong->bank[2].inputInterpretation = 10;
    std::stringstream diminuendoFile;
    if (!risco::saveSong(diminuendoFile, *gestureSong) || !risco::loadSong(diminuendoFile, connectedLoaded) ||
        connectedLoaded.bank[2].inputInterpretation != 10 || !connectedLoaded.bank[2].inputGestureCurve.enabled) return 326;
    auto incompatibleV70Diminuendo = diminuendoFile.str();
    incompatibleV70Diminuendo.replace(0, std::string("RISCO_SONG_V71").size(), "RISCO_SONG_V70");
    std::istringstream unsupportedV70Diminuendo(incompatibleV70Diminuendo);
    if (risco::loadSong(unsupportedV70Diminuendo, connectedLoaded)) return 327;
    gestureSong->bank[2].inputInterpretation = 11;
    gestureSong->bank[2].inputGestureRangeSemitones = 24;
    std::stringstream risingPitchFile;
    if (!risco::saveSong(risingPitchFile,*gestureSong) || risingPitchFile.str().find("RISCO_SONG_V73\n") != 0 ||
        !risco::loadSong(risingPitchFile,connectedLoaded) || connectedLoaded.bank[2].inputInterpretation != 11 ||
        connectedLoaded.bank[2].inputGestureRangeSemitones != 24) return 337;
    auto incompatibleV72Pitch = risingPitchFile.str();
    incompatibleV72Pitch.replace(0,std::string("RISCO_SONG_V73").size(),"RISCO_SONG_V72");
    std::istringstream unsupportedV72Pitch(incompatibleV72Pitch);
    if (risco::loadSong(unsupportedV72Pitch,connectedLoaded)) return 338;
    std::stringstream pitchSnapshotPayload;
    if (!risco::saveSong(pitchSnapshotPayload,*gestureSong)) return 340;
    const std::vector<risco::NamedSongVersionRecord> pitchSnapshotRecords{{"Altura",pitchSnapshotPayload.str()}};
    std::stringstream projectWithPitchSnapshot;
    if (!risco::saveSong(projectWithPitchSnapshot,*dialog,pitchSnapshotRecords,0) ||
        projectWithPitchSnapshot.str().find("RISCO_SONG_V75\n") != 0) return 341;
    std::vector<risco::NamedSongVersionRecord> restoredPitchSnapshot;
    int activePitchSnapshot = -1;
    if (!risco::loadSong(projectWithPitchSnapshot,connectedLoaded,&restoredPitchSnapshot,&activePitchSnapshot) ||
        restoredPitchSnapshot.size() != 1 || activePitchSnapshot != 0) return 342;
    std::istringstream nestedPitchSnapshot(restoredPitchSnapshot[0].serializedSong);
    auto nestedPitchSong = std::make_unique<risco::Song>();
    if (!risco::loadSong(nestedPitchSnapshot,*nestedPitchSong) || nestedPitchSong->bank[2].inputInterpretation != 11 ||
        nestedPitchSong->bank[2].inputGestureRangeSemitones != 24) return 343;
    gestureSong->bank[2].inputInterpretation = 12;
    gestureSong->bank[2].inputGestureRangeSemitones = 6;
    std::stringstream fallingPitchFile;
    if (!risco::saveSong(fallingPitchFile,*gestureSong) || !risco::loadSong(fallingPitchFile,connectedLoaded) ||
        connectedLoaded.bank[2].inputInterpretation != 12 || connectedLoaded.bank[2].inputGestureRangeSemitones != 6) return 339;
    gestureSong->bank[2].inputInterpretation = 13;
    gestureSong->bank[2].inputGestureRangeSemitones = 12;
    std::stringstream pitchArchFile;
    if (!risco::saveSong(pitchArchFile,*gestureSong) || pitchArchFile.str().find("RISCO_SONG_V74\n") != 0 ||
        !risco::loadSong(pitchArchFile,connectedLoaded) || connectedLoaded.bank[2].inputInterpretation != 13 ||
        connectedLoaded.bank[2].inputGestureRangeSemitones != 12) return 360;
    auto incompatibleV73PitchArch = pitchArchFile.str();
    incompatibleV73PitchArch.replace(0,std::string("RISCO_SONG_V74").size(),"RISCO_SONG_V73");
    std::istringstream unsupportedV73PitchArch(incompatibleV73PitchArch);
    if (risco::loadSong(unsupportedV73PitchArch,connectedLoaded)) return 361;
    const std::vector<risco::NamedSongVersionRecord> archSnapshotRecords{{"Arco",pitchArchFile.str()}};
    std::stringstream projectWithArchSnapshot;
    if (!risco::saveSong(projectWithArchSnapshot,*dialog,archSnapshotRecords,0) ||
        projectWithArchSnapshot.str().find("RISCO_SONG_V75\n") != 0) return 363;
    std::vector<risco::NamedSongVersionRecord> restoredArchSnapshot;
    if (!risco::loadSong(projectWithArchSnapshot,connectedLoaded,&restoredArchSnapshot) || restoredArchSnapshot.size() != 1)
        return 364;
    std::istringstream nestedArchSnapshot(restoredArchSnapshot[0].serializedSong);
    auto nestedArchSong = std::make_unique<risco::Song>();
    if (!risco::loadSong(nestedArchSnapshot,*nestedArchSong) || nestedArchSong->bank[2].inputInterpretation != 13)
        return 365;
    auto oldStrength = strengthFile.str();
    oldStrength.replace(0, std::string("RISCO_SONG_V65").size(), "RISCO_SONG_V48");
    removeReadingStrength(oldStrength);
    std::istringstream v48(oldStrength);
    if (!risco::loadSong(v48, connectedLoaded) || connectedLoaded.bank[2].inputInterpretationStrength != 1) return 252;
    for (const char* bad : {"-0.1", "1.1", "nan"}) {
        auto text = strengthFile.str();
        const auto lineEnd = text.find('\n', text.find("EDIT "));
        auto strengthStart = text.rfind(' ', lineEnd);
        for (int field = 0; field < 4; ++field)
            strengthStart = text.rfind(' ', strengthStart - 1);
        const auto strengthEnd = text.find(' ', strengthStart + 1);
        text.replace(strengthStart + 1, strengthEnd - strengthStart - 1, bad);
        std::istringstream invalid(text);
        if (risco::loadSong(invalid, connectedLoaded)) return 253;
    }
    dialog->bank[2].inputInterpretationStrength = 1.1f;
    std::stringstream invalidStrength;
    if (risco::saveSong(invalidStrength, *dialog)) return 254;
    const auto external = std::make_unique<risco::Song>();
    const auto parent = risco::storeGeneratedCell(*external, 26, 0, "Parent", {});
    risco::FormulaOrigin credit;
    credit.author = "External author"; credit.source = "Source collection"; credit.license = "AGPL-3.0-or-later"; credit.parentA = parent;
    const auto externalId = risco::storeFormulaCell(*external, "t", "External formula", {}, credit);
    external->bank[1].revision = 7; external->bank[1].favorite = true;
    const auto destination = std::make_unique<risco::Song>();
    destination->bpm = 90; destination->nextCellId = 100;
    const auto importedId = risco::importCell(*destination, external->bank[1], "external.risco [120 BPM; meter 4/4]");
    if (!externalId || importedId != 100 || destination->cellCount != 1 || destination->placementCount != 0 || destination->bpm != 90) return 1;
    const auto& imported = destination->bank[0];
    if (imported.formulaOrigin.author != credit.author || imported.formulaOrigin.source != credit.source ||
        imported.formulaOrigin.license != credit.license || imported.formulaOrigin.parentA != 0 ||
        imported.formulaExpression != "t" || !imported.favorite || imported.revision != 1 ||
        imported.importProvenance.find("cell #2 rev 7; parents #1/#0") == std::string::npos || external->bank[1].formulaOrigin.parentA != parent) return 2;
    std::stringstream importedFile;
    if (!risco::saveSong(importedFile, *destination) || !risco::loadSong(importedFile, connectedLoaded) ||
        connectedLoaded.bank[0].importProvenance != imported.importProvenance) return 3;
    auto v49Text = importedFile.str();
    v49Text.replace(0, std::string("RISCO_SONG_V65").size(), "RISCO_SONG_V49");
    removeImportRecords(v49Text);
    removeInputGestureCurveFields(v49Text);
    std::istringstream v49(v49Text);
    if (!risco::loadSong(v49, connectedLoaded) || !connectedLoaded.bank[0].importProvenance.empty()) return 4;
    const auto secondImport = risco::importCell(*external, imported, "destination.risco");
    if (!secondImport || external->bank[2].importProvenance.find(imported.importProvenance) != 0) return 5;
    const int beforeCount = destination->cellCount;
    const auto beforeId = destination->nextCellId;
    if (risco::importCell(*destination, imported, std::string(513,'x')) || destination->cellCount != beforeCount || destination->nextCellId != beforeId) return 6;
    auto invalidCell = std::make_unique<risco::Cell>(imported);
    invalidCell->importProvenance = std::string(2048,'x');
    if (risco::importCell(*destination, *invalidCell, "another.risco")) return 7;
    invalidCell->importProvenance.clear(); invalidCell->formulaExpression = "bad(";
    if (risco::importCell(*destination, *invalidCell, "another.risco")) return 8;
    destination->bank[0].importProvenance = std::string(2049,'x');
    std::stringstream invalidImport;
    if (risco::saveSong(invalidImport, *destination)) return 9;
    auto malformedImport = importedFile.str();
    const auto importStart = malformedImport.find("IMPORT ");
    malformedImport.replace(importStart, malformedImport.find('\n', importStart) - importStart, "IMPORT \"" + std::string(2049,'x') + "\"");
    std::istringstream oversizedImport(malformedImport);
    if (risco::loadSong(oversizedImport, connectedLoaded)) return 10;
    std::uint32_t noiseState = 1;
    for (std::uint64_t tick = 0; tick < 131071; ++tick) {
        if (risco::ayNoiseBitAt(tick) != int(noiseState & 1) || !noiseState) return 11;
        noiseState = (noiseState >> 1) | (((noiseState ^ (noiseState >> 3)) & 1) << 16);
        if (noiseState == 1 && tick != 131070) return 12;
    }
    if (noiseState != 1 || risco::ayNoiseBitAt(131071) != risco::ayNoiseBitAt(0)) return 13;
    double noiseEnergy = 0, difference = 0;
    for (int i = 0; i < 8000; ++i) {
        const double time = i / 8000.0;
        const float a = risco::ayNoiseStudySampleAt(time, 1), b = risco::ayNoiseStudySampleAt(time, 31);
        if (!std::isfinite(a) || !std::isfinite(b) || std::abs(a) > 1 || std::abs(b) > 1 ||
            a != risco::ayNoiseStudySampleAt(time, 1)) return 14;
        noiseEnergy += a * a; difference += (a - b) * (a - b);
    }
    if (noiseEnergy < 1 || difference < 1 || risco::ayNoiseStudySampleAt(-1,16) != 0) return 15;
    tones.count = 3;
    double mixedDifference = 0;
    for (int i = 0; i < 4800; ++i) {
        const double time = i / 48000.0;
        const float tone = risco::sampleAt(tones, time, risco::WaveMode::ayToneStudy);
        const float mixed = risco::sampleAt(tones, time, risco::WaveMode::ayToneNoiseStudy, nullptr,8,16);
        const float noise = risco::sampleAt(tones, time, risco::WaveMode::ayNoiseStudy, nullptr,8,16);
        if (!std::isfinite(mixed) || !std::isfinite(noise) || std::abs(mixed) > 1 || std::abs(noise) > 1 ||
            std::abs(mixed - (tone * 0.75f + noise * 0.25f)) > 1e-6) return 16;
        mixedDifference += (mixed - tone) * (mixed - tone);
    }
    if (mixedDifference < 0.01) return 17;
    external->timbreMode = 4; external->ayNoisePeriod = 31;
    std::stringstream noiseFile;
    if (!risco::saveSong(noiseFile,*external) || !risco::loadSong(noiseFile,connectedLoaded) ||
        connectedLoaded.timbreMode != 4 || connectedLoaded.ayNoisePeriod != 31) return 18;
    auto oldNoise = importedFile.str();
    oldNoise.replace(0,std::string("RISCO_SONG_V65").size(),"RISCO_SONG_V50");
    removeAyRecord(oldNoise);
    removeInputGestureCurveFields(oldNoise);
    std::istringstream v50(oldNoise);
    if (!risco::loadSong(v50,connectedLoaded) || connectedLoaded.ayNoisePeriod != 16) return 19;
    for (const char* bad : {"AY 0","AY 32","AY -1","AY 16 extra"}) {
        auto text = noiseFile.str();
        const auto start = text.find("\nAY ") + 1;
        text.replace(start,text.find('\n',start)-start,bad);
        std::istringstream invalid(text);
        if (risco::loadSong(invalid,connectedLoaded)) return 20;
    }
    tailSong->stemAutomation[0].enabled = false;
    tailSong->stemEffect = {0,0,0};
    tailPlayback->timbreMode = 4; tailPlayback->ayNoisePeriod = 31;
    risco::SongRender noiseRender(*tailSong,*tailPlayback,0,true);
    double renderedNoise = 0;
    for (std::uint32_t i = 0; i < 4800; ++i) {
        const auto out = noiseRender.frame(i);
        const float expected = risco::songStemSampleAt(*tailPlayback,i/48000.0,0,risco::WaveMode::ayNoiseStudy);
        if (!std::isfinite(out.left) || std::abs(out.left - expected * 0.70710678118f) > 1e-6) return 21;
        renderedNoise += out.left * out.left;
    }
    if (renderedNoise < 0.001) return 22;
    if (risco::ayEnvelopePeriod(0.5f) != 244 || risco::ayEnvelopeLevelAt(100,0) != 1) return 23;
    const double envStep = 256.0 * risco::ayEnvelopePeriod(0.5f) / 2000000.0;
    for (int step = 0; step < 64; ++step) {
        const double time = (step + 0.25) * envStep;
        const int phase = step % 32;
        const float triangle = float(phase < 16 ? phase : 31 - phase) / 15;
        if (std::abs(risco::ayEnvelopeLevelAt(time,1) - float(15 - std::min(step,15))/15) > 1e-6 ||
            std::abs(risco::ayEnvelopeLevelAt(time,2) - float(std::min(step,15))/15) > 1e-6 ||
            std::abs(risco::ayEnvelopeLevelAt(time,3) - triangle) > 1e-6 ||
            std::abs(risco::ayEnvelopeLevelAt(time,4) - float(15 - step%16)/15) > 1e-6) return 24;
    }
    double percussiveEnergy = 0, plainEnergy = 0;
    for (int i = 0; i < 4800; ++i) {
        const double time = 0.6 + i / 48000.0;
        const float plain = risco::sampleAt(tones,time,risco::WaveMode::ayNoiseStudy,nullptr,8,16);
        const float decay = risco::sampleAt(tones,time,risco::WaveMode::ayNoiseStudy,nullptr,8,16,1,0.5f);
        if (!std::isfinite(decay) || std::abs(decay) > 1 ||
            plain != risco::sampleAt(tones,time,risco::WaveMode::ayNoiseStudy,nullptr,8,16,0,2)) return 25;
        percussiveEnergy += decay*decay; plainEnergy += plain*plain;
    }
    if (plainEnergy < 0.001 || percussiveEnergy != 0) return 26;
    external->ayEnvelopeMode = 3; external->ayEnvelopeRamp = 0.25f;
    std::stringstream envelopeFile;
    if (!risco::saveSong(envelopeFile,*external) || !risco::loadSong(envelopeFile,connectedLoaded) ||
        connectedLoaded.ayEnvelopeMode != 3 || connectedLoaded.ayEnvelopeRamp != 0.25f) return 27;
    auto oldEnvelope = envelopeFile.str();
    removeTakeRecord(oldEnvelope);
    removeInputGestureCurveFields(oldEnvelope);
    oldEnvelope.replace(0,std::string("RISCO_SONG_V65").size(),"RISCO_SONG_V51");
    const auto ayStart = oldEnvelope.find("\nAY ") + 1;
    const auto ayEnd = oldEnvelope.find('\n',ayStart);
    oldEnvelope.replace(ayStart,ayEnd-ayStart,"AY 31");
    std::istringstream v51(oldEnvelope);
    if (!risco::loadSong(v51,connectedLoaded) || connectedLoaded.ayEnvelopeMode != 0 ||
        connectedLoaded.ayEnvelopeRamp != 0.5f || connectedLoaded.ayNoisePeriod != 31) return 28;
    for (const char* bad : {"AY 16 -1 0.5","AY 16 5 0.5","AY 16 1 0.1","AY 16 1 3","AY 16 1 nan","AY 16 1"}) {
        auto text = envelopeFile.str();
        const auto start = text.find("\nAY ") + 1;
        text.replace(start,text.find('\n',start)-start,bad);
        std::istringstream invalid(text);
        if (risco::loadSong(invalid,connectedLoaded)) return 29;
    }
    tailPlayback->ayEnvelopeMode = 1; tailPlayback->ayEnvelopeRamp = 0.125f;
    risco::SongRender envelopeRender(*tailSong,*tailPlayback,0,true);
    for (std::uint32_t i = 0; i < 4800; ++i) {
        const auto out = envelopeRender.frame(i);
        const float expected = risco::songStemSampleAt(*tailPlayback,i/48000.0,0,risco::WaveMode::ayNoiseStudy);
        if (!std::isfinite(out.left) || std::abs(out.left - expected * 0.70710678118f) > 1e-6) return 30;
    }
    if (!checkInternalRests()) {
        std::cerr << "Internal-rest response checks failed\n";
        return 31;
    }
    if (!checkBoundedMathControl()) {
        std::cerr << "Bounded mathematical maps failed\n";
        return 34;
    }
    if (!checkExtendedForms()) {
        std::cerr << "Extended form checks failed\n";
        return 32;
    }
    if (!checkPhraseTakes()) {
        std::cerr << "Phrase take checks failed\n";
        return 33;
    }
    std::cout << "V58: response motif/density, temporal gestures and prior checks OK\n";
}
