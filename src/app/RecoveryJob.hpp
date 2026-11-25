// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Lúcio de Araújo
#pragma once
#include <juce_core/juce_core.h>
#include "core/SongIO.hpp"
#include <atomic>
#include <memory>
#include <sstream>
#include <cmath>
#include <charconv>
#include <vector>

struct RecoveryGridState {
    std::array<risco::Placement, 128> placements{};
    int count = 0;
    int selected = -1;
};
inline juce::var recoveryBankHistory(const std::vector<std::unique_ptr<risco::Cell>>& history) {
    juce::Array<juce::var> rows;
    for (const auto& cell : history) {
        auto single = std::make_unique<risco::Song>();
        single->bank[0] = *cell; single->cellCount = 1;
        // Keep referenced IDs separately so the isolated cell remains a valid Song.
        single->bank[0].formulaOrigin.parentA = single->bank[0].formulaOrigin.parentB = 0;
        std::ostringstream bytes;
        if (!risco::saveSong(bytes, *single)) continue;
        juce::Array<juce::var> fields;
        fields.add(juce::String::fromUTF8(bytes.str().c_str()));
        fields.add(static_cast<juce::int64>(cell->formulaOrigin.parentA));
        fields.add(static_cast<juce::int64>(cell->formulaOrigin.parentB));
        rows.add(juce::var(fields));
    }
    return juce::var(rows);
}
inline bool readRecoveryBankHistory(const juce::var& encoded, const risco::Song& current,
                                    std::vector<std::unique_ptr<risco::Cell>>& destination) {
    const auto* rows = encoded.getArray();
    if (!rows || rows->size() > 8) return false;
    std::vector<std::unique_ptr<risco::Cell>> next;
    for (const auto& row : *rows) {
        const auto* fields = row.getArray();
        if (!fields || fields->size() != 3 || !(*fields)[0].isString()) return false;
        const auto bytes = (*fields)[0].toString().toStdString();
        if (bytes.size() > 256 * 1024) return false;
        auto single = std::make_unique<risco::Song>();
        std::istringstream stream(bytes);
        if (!risco::loadSong(stream, *single) || single->cellCount != 1 || single->placementCount != 0) return false;
        auto& cell = single->bank[0];
        for (int p = 1; p <= 2; ++p) {
            const auto& field = (*fields)[p];
            if (!field.isInt() && !field.isInt64()) return false;
            const auto id = static_cast<juce::int64>(field);
            if (id < 0 || id > 0xffffffffLL) return false;
            if (p == 1) cell.formulaOrigin.parentA = std::uint32_t(id);
            else cell.formulaOrigin.parentB = std::uint32_t(id);
        }
        // A deleted cell or parent cannot be restored by this edit-only history.
        if (!risco::findCell(current, cell.id) ||
            (cell.formulaOrigin.parentA && !risco::findCell(current, cell.formulaOrigin.parentA)) ||
            (cell.formulaOrigin.parentB && !risco::findCell(current, cell.formulaOrigin.parentB))) continue;
        next.push_back(std::make_unique<risco::Cell>(cell));
    }
    destination = std::move(next);
    return true;
}
inline juce::var recoveryGridHistory(const std::array<RecoveryGridState, 32>& history, int count) {
    juce::Array<juce::var> states;
    for (int h = 0; h < std::clamp(count, 0, 32); ++h) {
        const auto& state = history[static_cast<size_t>(h)];
        juce::Array<juce::var> placements;
        for (int i = 0; i < state.count; ++i) {
            const auto& p = state.placements[static_cast<size_t>(i)];
            juce::Array<juce::var> row;
            row.add(static_cast<juce::int64>(p.cellId)); row.add(p.startBeat); row.add(int(p.span));
            row.add(p.repeats); row.add(p.sourceStartBeat); row.add(p.lengthBeats); row.add(p.stem);
            placements.add(juce::var(row));
        }
        juce::var object(new juce::DynamicObject());
        object.getDynamicObject()->setProperty("placements", juce::var(placements));
        object.getDynamicObject()->setProperty("selected", state.selected);
        states.add(object);
    }
    return juce::var(states);
}
inline bool readRecoveryGridHistory(const juce::var& encoded, const risco::Song& current,
                                    std::array<RecoveryGridState, 32>& destination, int& count) {
    const auto* states = encoded.getArray();
    if (!states || states->size() > 32) return false;
    auto next = std::make_unique<std::array<RecoveryGridState, 32>>();
    int accepted = 0;
    for (const auto& object : *states) {
        const auto placements = object.getProperty("placements", {});
        const auto* rows = placements.getArray();
        if (!rows || rows->size() > 128) return false;
        auto validation = std::make_unique<risco::Song>(current);
        validation->placementCount = 0;
        for (const auto& row : *rows) {
            const auto* fields = row.getArray();
            if (!fields || fields->size() != 7) return false;
            for (int f = 0; f < 7; ++f) {
                const auto& value = (*fields)[f];
                if (!value.isInt() && !value.isInt64()) return false;
                const auto number = static_cast<juce::int64>(value);
                if (number < 0 || number > (f == 0 ? 0xffffffffLL : 4096)) return false;
            }
            risco::Placement p{std::uint32_t(static_cast<juce::int64>((*fields)[0])), int((*fields)[1]),
                static_cast<risco::CellSpan>(int((*fields)[2])), int((*fields)[3]), int((*fields)[4]), int((*fields)[5]), int((*fields)[6])};
            if (!risco::placeCell(*validation, p)) return false;
        }
        auto& state = (*next)[static_cast<size_t>(accepted++)];
        state.count = validation->placementCount;
        state.placements = validation->grid;
        const int selected = int(object.getProperty("selected", -1));
        state.selected = selected >= 0 && selected < state.count ? selected : -1;
    }
    destination = std::move(*next); count = accepted;
    return true;
}

inline juce::var recoverySectionEdits(const risco::SectionEdits& edits) {
    juce::Array<juce::var> result;
    for (const auto& edit : edits) {
        juce::Array<juce::var> values;
        values.add(edit.density); values.add(edit.gain); values.add(edit.octaves);
        result.add(juce::var(values));
    }
    return juce::var(result);
}
inline bool readRecoverySectionEdits(const juce::var& value, risco::SectionEdits& edits) {
    const auto* sections = value.getArray();
    if (!sections || sections->size() != 4) return false;
    for (int i = 0; i < 4; ++i) {
        const auto* row = (*sections)[i].getArray();
        if (!row || row->size() != 3) return false;
        for (const auto& number : *row)
            if (!number.isInt() && !number.isInt64() && !number.isDouble()) return false;
        if (double((*row)[2]) != int((*row)[2])) return false;
        edits[static_cast<size_t>(i)] = {float(double((*row)[0])), float(double((*row)[1])), int((*row)[2])};
    }
    return risco::validSectionEdits(edits);
}

inline bool validRecoveryDraft(const juce::var& draft) {
    if (draft.isVoid()) return true; // Older recovery files have no draft.
    if (!draft.isObject()) return false;
    const auto strength = draft.getProperty("inputInterpretationStrength", 1.0);
    if ((!strength.isInt() && !strength.isInt64() && !strength.isDouble()) ||
        !std::isfinite(double(strength)) || double(strength) < 0 || double(strength) > 1) return false;
    const auto interpretation = draft.getProperty("inputInterpretation",0);
    if ((!interpretation.isInt() && !interpretation.isInt64()) || int(interpretation) < 0 || int(interpretation) > 13) return false;
    const auto gestureRange = draft.getProperty("inputGestureRangeSemitones",12);
    if ((!gestureRange.isInt() && !gestureRange.isInt64()) ||
        !risco::validTemporalGestureRange(int(gestureRange))) return false;
    const auto gestureCurveValue = draft.getProperty("inputGestureCurve", {});
    const auto gestureCurveEnabled = bool(draft.getProperty("inputGestureCurveEnabled", false));
    if (int(interpretation) >= 8 && (gestureCurveValue.isVoid() || !gestureCurveEnabled)) return false;
    if (!gestureCurveValue.isVoid()) {
        const auto* values = gestureCurveValue.getArray();
        if (!values || values->size() != 3) return false;
        risco::TemporalGestureCurve curve;
        curve.enabled = gestureCurveEnabled;
        for (int i = 0; i < 3; ++i) {
            if ((!(*values)[i].isInt() && !(*values)[i].isInt64() && !(*values)[i].isDouble()) ||
                !std::isfinite(double((*values)[i]))) return false;
            curve.values[static_cast<size_t>(i)] = float(double((*values)[i]));
        }
        if (!risco::validTemporalGestureCurve(curve)) return false;
    }
    const auto meterValues = draft.getProperty("meters", {});
    if (!meterValues.isVoid()) {
        const auto* values = meterValues.getArray();
        if (!values || values->size() != 4) return false;
        risco::PhraseMeters meters{};
        for (int i = 0; i < 4; ++i) {
            if (!(*values)[i].isInt() && !(*values)[i].isInt64()) return false;
            const auto number = static_cast<juce::int64>((*values)[i]);
            if (number != 0 && (number < 2 || number > 8)) return false;
            meters[static_cast<size_t>(i)] = int((*values)[i]);
        }
        if (!risco::validPhraseMeters(meters)) return false;
    }
    const auto rootsValue = draft.getProperty("chordRoots", {});
    if (!rootsValue.isVoid()) {
        const auto* roots = rootsValue.getArray();
        if (!roots || roots->size() != 4) return false;
        for (const auto& root : *roots)
            if ((!root.isInt() && !root.isInt64()) || int(root) < -2 || int(root) > 11) return false;
    }
    if (bool(draft.getProperty("generated", false))) {
        const auto seed = draft.getProperty("phraseSeed", "").toString().toStdString();
        std::uint64_t number = 0;
        const auto parsed = std::from_chars(seed.data(), seed.data() + seed.size(), number);
        const auto variation = static_cast<juce::int64>(draft.getProperty("phraseVariation", -1));
        const int version = int(draft.getProperty("phraseVersion", 0));
        const int mask = int(draft.getProperty("phraseMask", 0));
        risco::SectionEdits edits;
        if (seed.empty() || seed.size() > 20 || parsed.ec != std::errc{} || parsed.ptr != seed.data() + seed.size() ||
            variation < 0 || variation > 0xffffffffLL || version < 2 || version > 9 || mask < 1 || mask > 15 ||
            (version == 4 && (mask & 3) == 0) ||
            !readRecoverySectionEdits(draft.getProperty("sections", {}), edits)) return false;
    }
    for (const auto& limit : {std::pair<const char*, int>{"text", 4096}, {"formula", 128},
                              {"name", 64}, {"seed", 20}, {"seedBeforeGlitch", 20}}) {
        const auto value = draft.getProperty(limit.first, "");
        if (!value.isString() || value.toString().length() > limit.second) return false;
    }
    const auto strokesValue = draft.getProperty("strokes", {});
    const auto* strokes = strokesValue.getArray();
    if (!strokes || strokes->size() > 12) return false;
    for (const auto& stroke : *strokes) {
        const auto pointsValue = stroke.getProperty("points", {});
        const auto* points = pointsValue.getArray();
        if (!points || points->size() > 128) return false;
        for (const auto& point : *points) {
            const auto* coordinates = point.getArray();
            if (!coordinates || coordinates->size() != 2) return false;
            for (const auto& value : *coordinates) {
                if (!(value.isDouble() || value.isInt() || value.isInt64())) return false;
                const double number = double(value);
                if (!std::isfinite(number) || number < 0 || number > 1) return false;
            }
        }
    }
    return true;
}

struct RecoveryState {
    std::string lastBytes; // Only accessed by the single worker.
    std::atomic<int> notification{0}; // 1 written, 2 failed.
};
inline bool decodeRecovery(const std::string& bytes, std::string& song, juce::var& context) {
    if (bytes.size() > 4 * 1024 * 1024) return false;
    const auto text = juce::String::fromUTF8(bytes.data(), static_cast<int>(bytes.size()));
    if (!text.trimStart().startsWithChar('{')) { song = bytes; context = juce::var(); return true; }
    const auto bundle = juce::JSON::parse(text);
    if (int(bundle.getProperty("recoveryVersion", 0)) != 1 ||
        !bundle.getProperty("song", {}).isString() || !bundle.getProperty("context", {}).isObject()) return false;
    song = bundle.getProperty("song", {}).toString().toStdString();
    context = bundle.getProperty("context", {});
    return !song.empty();
}
class RecoveryJob final : public juce::ThreadPoolJob {
public:
    RecoveryJob(std::unique_ptr<risco::Song> snapshot, juce::File target,
                std::shared_ptr<RecoveryState> stateIn, juce::var contextIn = {})
        : ThreadPoolJob("Risco recovery"), song(std::move(snapshot)), file(std::move(target)),
          state(std::move(stateIn)), context(std::move(contextIn)) {}
    JobStatus runJob() override {
        if (shouldExit()) return jobHasFinished;
        if (context.isObject() && !validRecoveryDraft(context.getProperty("draft", {}))) return fail();
        std::ostringstream stream;
        if (!risco::saveSong(stream, *song)) return fail();
        auto bytes = stream.str();
        if (context.isObject()) {
            juce::var bundle(new juce::DynamicObject());
            bundle.getDynamicObject()->setProperty("recoveryVersion", 1);
            bundle.getDynamicObject()->setProperty("song", juce::String::fromUTF8(bytes.data(), static_cast<int>(bytes.size())));
            bundle.getDynamicObject()->setProperty("context", context);
            bytes = juce::JSON::toString(bundle).toStdString();
            if (bytes.size() > 4 * 1024 * 1024 && context.getDynamicObject()->hasProperty("history")) {
                context.getDynamicObject()->removeProperty("history");
                context.getDynamicObject()->setProperty("historyTruncated", true);
                bundle.getDynamicObject()->setProperty("context", context);
                bytes = juce::JSON::toString(bundle).toStdString();
            }
        }
        if ((bytes == state->lastBytes && file.existsAsFile()) || shouldExit()) return jobHasFinished;
        if (bytes.size() > 4 * 1024 * 1024 || file.getParentDirectory().createDirectory().failed()) return fail();
        juce::TemporaryFile temporary(file);
        auto output = temporary.getFile().createOutputStream();
        if (!output || !output->write(bytes.data(), bytes.size())) return fail();
        output->flush();
        if (output->getStatus().failed()) return fail();
        output.reset();
        if (shouldExit()) return jobHasFinished;
        if (!temporary.overwriteTargetFileWithTemporary()) return fail();
        state->lastBytes = bytes;
        state->notification.store(1);
        return jobHasFinished;
    }
private:
    JobStatus fail() { state->notification.store(2); return jobHasFinished; }
    std::unique_ptr<risco::Song> song;
    juce::File file;
    std::shared_ptr<RecoveryState> state;
    juce::var context;
};
