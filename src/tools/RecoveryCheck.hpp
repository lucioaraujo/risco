// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Lúcio de Araújo
#pragma once
#include "../app/RecoveryJob.hpp"
#include "../app/ProjectLibrary.hpp"

namespace risco::diagnostics {
// Executed only by --check-recovery; never touches the user's recovery file.
inline int checkRecovery() {
    juce::TemporaryFile temporary(".risco");
    const auto file = temporary.getFile();
    auto state = std::make_shared<RecoveryState>();
    auto source = std::make_unique<Song>();
    Inscription text;
    text.ascii = "Recovery check";
    const auto id = storeCell(*source, text, "Session");
    if (!id || !placeCell(*source, {id, 0, CellSpan::bar, 1})) return 1;
    juce::ThreadPool pool{1};
    auto run = [&](std::unique_ptr<Song> snapshot, juce::var context = {}) {
        RecoveryJob job(std::move(snapshot), file, state, context);
        pool.addJob(&job, false);
        if (!pool.waitForJobToFinish(&job, 5000)) {
            pool.removeJob(&job, true, -1);
            return false;
        }
        return true;
    };
    if (!run(std::make_unique<Song>(*source)) || state->notification.exchange(0) != 1) return 2;
    const auto bytes = file.loadFileAsString().toStdString();
    auto loaded = std::make_unique<Song>();
    std::istringstream input(bytes);
    if (!loadSong(input, *loaded) || loaded->cellCount != 1 || loaded->placementCount != 1 ||
        loaded->bank[0].inscription.ascii != text.ascii) return 3;
    if (!run(std::make_unique<Song>(*source)) || state->notification.exchange(0) != 0) return 4;
    auto invalid = std::make_unique<Song>(*source);
    invalid->bitDepth = 7;
    if (!run(std::move(invalid)) || state->notification.exchange(0) != 2 ||
        file.loadFileAsString().toStdString() != bytes) return 5;
    auto changed = std::make_unique<Song>(*source);
    changed->bank[0].name = "Cancelled";
    RecoveryJob cancelled(std::move(changed), file, state);
    cancelled.signalJobShouldExit();
    cancelled.runJob();
    if (file.loadFileAsString().toStdString() != bytes) return 6;
    if (!file.deleteFile() || !run(std::make_unique<Song>(*source)) ||
        state->notification.exchange(0) != 1 || file.loadFileAsString().toStdString() != bytes) return 7;
    juce::var context(new juce::DynamicObject());
    context.getDynamicObject()->setProperty("selectedCellId", static_cast<juce::int64>(id));
    context.getDynamicObject()->setProperty("gridZoomBeats", 32);
    context.getDynamicObject()->setProperty("gridScrollBeat", 8.0);
    if (!run(std::make_unique<Song>(*source), context)) return 8;
    std::string songBytes;
    juce::var decoded;
    if (!decodeRecovery(file.loadFileAsString().toStdString(), songBytes, decoded) || songBytes != bytes ||
        int(decoded.getProperty("gridZoomBeats", 0)) != 32 ||
        double(decoded.getProperty("gridScrollBeat", 0)) != 8.0 ||
        static_cast<juce::int64>(decoded.getProperty("selectedCellId", 0)) != id) return 9;
    if (decodeRecovery("{\"recoveryVersion\":999}", songBytes, decoded)) return 10;
    if (!decodeRecovery(bytes, songBytes, decoded) || songBytes != bytes || decoded.isObject()) return 11;
    juce::var draft(new juce::DynamicObject());
    draft.getDynamicObject()->setProperty("text", juce::String::fromUTF8("Rascunho: ação"));
    draft.getDynamicObject()->setProperty("formula", "t*("); // An unfinished formula must survive.
    draft.getDynamicObject()->setProperty("inputInterpretation", 7);
    draft.getDynamicObject()->setProperty("inputInterpretationStrength", 0.5);
    juce::var stroke(new juce::DynamicObject());
    juce::Array<juce::var> coordinates; coordinates.add(0.25); coordinates.add(0.75);
    juce::Array<juce::var> points; points.add(juce::var(coordinates));
    stroke.getDynamicObject()->setProperty("points", juce::var(points));
    juce::Array<juce::var> strokes; strokes.add(stroke);
    draft.getDynamicObject()->setProperty("strokes", juce::var(strokes));
    if (!validRecoveryDraft(draft)) return 12;
    context.getDynamicObject()->setProperty("draft", draft);
    if (!run(std::make_unique<Song>(), context) ||
        !decodeRecovery(file.loadFileAsString().toStdString(), songBytes, decoded)) return 13;
    const auto recoveredDraft = decoded.getProperty("draft", {});
    if (!validRecoveryDraft(recoveredDraft) ||
        recoveredDraft.getProperty("text", "").toString() != juce::String::fromUTF8("Rascunho: ação") ||
        recoveredDraft.getProperty("formula", "").toString() != "t*(" ||
        int(recoveredDraft.getProperty("inputInterpretation",0)) != 7 ||
        double(recoveredDraft.getProperty("inputInterpretationStrength", 1.0)) != 0.5) return 14;
    draft.getDynamicObject()->setProperty("inputInterpretation",8);
    if (validRecoveryDraft(draft)) return 44;
    juce::Array<juce::var> draftGestureCurve;
    draftGestureCurve.add(0.0); draftGestureCurve.add(0.8); draftGestureCurve.add(1.0);
    draft.getDynamicObject()->setProperty("inputGestureCurve",juce::var(draftGestureCurve));
    draft.getDynamicObject()->setProperty("inputGestureCurveEnabled",true);
    if (!validRecoveryDraft(draft)) return 50;
    draft.getDynamicObject()->setProperty("inputInterpretation",9);
    if (!validRecoveryDraft(draft)) return 51;
    draft.getDynamicObject()->setProperty("inputInterpretation",10);
    if (!validRecoveryDraft(draft)) return 56;
    draft.getDynamicObject()->setProperty("inputInterpretation",11);
    draft.getDynamicObject()->setProperty("inputGestureRangeSemitones",24);
    if (!validRecoveryDraft(draft)) return 57;
    draft.getDynamicObject()->setProperty("inputInterpretation",12);
    if (!validRecoveryDraft(draft)) return 58;
    draft.getDynamicObject()->setProperty("inputGestureRangeSemitones",7);
    if (validRecoveryDraft(draft)) return 59;
    draft.getDynamicObject()->setProperty("inputGestureRangeSemitones",12);
    draft.getDynamicObject()->setProperty("inputInterpretation",13);
    if (!validRecoveryDraft(draft)) return 60;
    draft.getDynamicObject()->setProperty("inputGestureCurveEnabled",false);
    if (validRecoveryDraft(draft)) return 52;
    draft.getDynamicObject()->setProperty("inputGestureCurveEnabled",true);
    draft.getDynamicObject()->setProperty("inputInterpretation",7);
    draft.getDynamicObject()->setProperty("inputInterpretationStrength", 1.1);
    if (validRecoveryDraft(draft)) return 30;
    draft.getDynamicObject()->setProperty("inputInterpretationStrength", "invalid");
    if (validRecoveryDraft(draft)) return 31;
    draft.getDynamicObject()->setProperty("inputInterpretationStrength", 0.5);
    draft.getDynamicObject()->setProperty("generated", true);
    draft.getDynamicObject()->setProperty("phraseSeed", "18446744073709551615");
    draft.getDynamicObject()->setProperty("phraseVariation", 123);
    draft.getDynamicObject()->setProperty("phraseVersion", 9);
    juce::Array<juce::var> meters;
    for (int meter : {3, 5, 8, 7}) meters.add(meter);
    draft.getDynamicObject()->setProperty("meters", juce::var(meters));
    draft.getDynamicObject()->setProperty("phraseMask", 5);
    risco::SectionEdits edits{};
    edits[1] = {0.5f, 0.6f, -1};
    draft.getDynamicObject()->setProperty("sections", recoverySectionEdits(edits));
    if (!validRecoveryDraft(draft) || !run(std::make_unique<Song>(), context) ||
        !decodeRecovery(file.loadFileAsString().toStdString(), songBytes, decoded)) return 17;
    const auto generated = decoded.getProperty("draft", {});
    risco::SectionEdits restored{};
    if (!validRecoveryDraft(generated) || !readRecoverySectionEdits(generated.getProperty("sections", {}), restored) ||
        restored[1].octaves != -1 || generated.getProperty("phraseSeed", "").toString() != "18446744073709551615" ||
        int((*generated.getProperty("meters", {}).getArray())[2]) != 8 || int(generated.getProperty("phraseVersion", 0)) != 9) return 18;
    draft.getDynamicObject()->setProperty("phraseVersion", 10);
    if (validRecoveryDraft(draft)) return 28;
    draft.getDynamicObject()->setProperty("phraseVersion", 9);
    meters.set(0, static_cast<juce::int64>(0x100000008LL));
    draft.getDynamicObject()->setProperty("meters", juce::var(meters));
    if (validRecoveryDraft(draft)) return 27;
    meters.set(0, 3);
    draft.getDynamicObject()->setProperty("meters", juce::var(meters));
    draft.getDynamicObject()->setProperty("phraseSeed", "18446744073709551616");
    if (validRecoveryDraft(draft)) return 19;
    draft.getDynamicObject()->setProperty("generated", false);
    coordinates.set(0, 2.0);
    points.set(0, juce::var(coordinates));
    stroke.getDynamicObject()->setProperty("points", juce::var(points));
    if (validRecoveryDraft(draft)) return 15;
    if (validRecoveryDraft(juce::var(42))) return 16;
    std::vector<std::unique_ptr<Cell>> undo, restoredUndo;
    undo.push_back(std::make_unique<Cell>(source->bank[0]));
    undo[0]->name = "Before edit";
    const auto encodedUndo = recoveryBankHistory(undo);
    if (!readRecoveryBankHistory(encodedUndo, *source, restoredUndo) || restoredUndo.size() != 1 ||
        restoredUndo[0]->name != "Before edit" || source->bank[0].name != "Session") return 20;
    auto history = std::make_unique<std::array<RecoveryGridState, 32>>();
    (*history)[0].placements = source->grid; (*history)[0].count = source->placementCount; (*history)[0].selected = 0;
    const auto encodedGrid = recoveryGridHistory(*history, 1);
    int historyCount = 0;
    if (!readRecoveryGridHistory(encodedGrid, *source, *history, historyCount) || historyCount != 1 ||
        (*history)[0].placements[0].cellId != id || (*history)[0].selected != 0) return 21;
    if (readRecoveryBankHistory(juce::var(1), *source, restoredUndo) ||
        readRecoveryGridHistory(juce::var(1), *source, *history, historyCount)) return 22;
    juce::var histories(new juce::DynamicObject());
    histories.getDynamicObject()->setProperty("bankUndo", encodedUndo);
    histories.getDynamicObject()->setProperty("gridUndo", encodedGrid);
    context.getDynamicObject()->removeProperty("draft"); // The prior malformed draft was deliberate.
    context.getDynamicObject()->setProperty("history", histories);
    if (!run(std::make_unique<Song>(*source), context) ||
        !decodeRecovery(file.loadFileAsString().toStdString(), songBytes, decoded)) return 23;
    const auto decodedHistory = decoded.getProperty("history", {});
    if (!readRecoveryBankHistory(decodedHistory.getProperty("bankUndo", {}), *source, restoredUndo) ||
        !readRecoveryGridHistory(decodedHistory.getProperty("gridUndo", {}), *source, *history, historyCount)) return 24;
    histories.getDynamicObject()->setProperty("padding", juce::String::repeatedString("x", 4 * 1024 * 1024));
    if (!run(std::make_unique<Song>(*source), context) ||
        !decodeRecovery(file.loadFileAsString().toStdString(), songBytes, decoded) ||
        !bool(decoded.getProperty("historyTruncated", false)) || !decoded.getProperty("history", {}).isVoid()) return 25;
    std::istringstream preserved(songBytes);
    if (!loadSong(preserved, *loaded) || loaded->cellCount != 1 || loaded->bank[0].id != id) return 26;
    juce::TemporaryFile libraryTemporary(".risco");
    const auto libraryFile = libraryTemporary.getFile();
    if (!libraryFile.replaceWithText(juce::String::fromUTF8(bytes.c_str()))) return 32;
    auto library = std::make_shared<ProjectLibraryResult>();
    ProjectLibraryJob libraryJob(libraryFile, library);
    pool.addJob(&libraryJob, false);
    if (!pool.waitForJobToFinish(&libraryJob, 5000)) { pool.removeJob(&libraryJob, true, -1); return 33; }
    if (!library->done.load(std::memory_order_acquire) || !library->song || library->song->cellCount != 1 ||
        library->song->bank[0].inscription.ascii != text.ascii || library->description.find("BPM") == std::string::npos) return 34;
    std::vector<ProjectLibraryEntry> index;
    auto firstProject = std::make_shared<Song>(*library->song);
    auto secondProject = std::make_shared<Song>(*library->song);
    secondProject->bank[0].name = "Second project cell";
    index.push_back({firstProject,"first provenance","first.risco","/first.risco"});
    index.push_back({secondProject,"second provenance","second.risco","/second.risco"});
    const auto namedMatches = searchProjectLibraries(index,"second",128);
    const auto limitedMatches = searchProjectLibraries(index,"",1);
    if (namedMatches.size() != 1 || namedMatches[0].song != secondProject || namedMatches[0].cellIndex != 0 ||
        limitedMatches.size() != 1 || !searchProjectLibraries(index,"not present",128).empty()) return 45;
    juce::TemporaryFile libraryCatalogTemporary(".json");
    juce::StringArray savedLibrarySources;
    savedLibrarySources.add(juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
        .getChildFile("first.risco").getFullPathName());
    savedLibrarySources.add(juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
        .getChildFile("second.risco").getFullPathName());
    if (!writeProjectLibrarySources(libraryCatalogTemporary.getFile(),savedLibrarySources) ||
        readProjectLibrarySources(libraryCatalogTemporary.getFile()) != savedLibrarySources) return 46;
    const auto migratedLibraryCatalog = readProjectLibraryCatalog(libraryCatalogTemporary.getFile());
    if (migratedLibraryCatalog.sources != savedLibrarySources || !migratedLibraryCatalog.groups.empty()) return 56;
    ProjectLibraryCatalog groupedCatalog;
    groupedCatalog.sources = savedLibrarySources;
    groupedCatalog.groups.push_back({"Estudos",juce::StringArray{savedLibrarySources[0],savedLibrarySources[1]}});
    groupedCatalog.groups.push_back({juce::String::fromUTF8("Variações"),juce::StringArray{savedLibrarySources[1]}});
    if (!writeProjectLibraryCatalog(libraryCatalogTemporary.getFile(),groupedCatalog)) return 57;
    const auto restoredGroupedCatalog = readProjectLibraryCatalog(libraryCatalogTemporary.getFile());
    if (restoredGroupedCatalog.sources != groupedCatalog.sources) return 58;
    if (restoredGroupedCatalog.groups.size() != 2) return 61;
    if (restoredGroupedCatalog.groups[0].name != "Estudos" || restoredGroupedCatalog.groups[0].sources.size() != 2) return 62;
    if (restoredGroupedCatalog.groups[1].name != juce::String::fromUTF8("Variações")) return 63;
    if (restoredGroupedCatalog.groups[1].sources[0] != savedLibrarySources[1]) return 64;
    juce::var duplicateGroups(new juce::DynamicObject());
    juce::Array<juce::var> duplicateGroupValues;
    for (int i = 0; i < 2; ++i) {
        juce::var group(new juce::DynamicObject());
        group.getDynamicObject()->setProperty("name",i == 0 ? "Mesmo" : "mesmo");
        group.getDynamicObject()->setProperty("sources",juce::var(juce::Array<juce::var>{savedLibrarySources[0]}));
        duplicateGroupValues.add(group);
    }
    duplicateGroups.getDynamicObject()->setProperty("version",2);
    duplicateGroups.getDynamicObject()->setProperty("groups",juce::var(duplicateGroupValues));
    if (!parseProjectLibraryGroups(duplicateGroups,savedLibrarySources).empty() ||
        validProjectLibraryGroupName(" ") || validProjectLibraryGroupName(juce::String::repeatedString("x",65))) return 59;
    const juce::StringArray onlyFirstSource{"/first.risco"};
    if (searchProjectLibraries(index,"",128,&onlyFirstSource).size() != 1 ||
        searchProjectLibraries(index,"",128).size() != 2) return 60;
    juce::var compatibleV2Catalog(new juce::DynamicObject());
    compatibleV2Catalog.getDynamicObject()->setProperty("version",2);
    compatibleV2Catalog.getDynamicObject()->setProperty("sources",juce::var(juce::Array<juce::var>{savedLibrarySources[0]}));
    if (parseProjectLibrarySources(compatibleV2Catalog).size() != 1) return 47;
    juce::var relativeCatalog(new juce::DynamicObject());
    juce::Array<juce::var> relativeValues; relativeValues.add("relative/project.risco");
    relativeCatalog.getDynamicObject()->setProperty("version",1);
    relativeCatalog.getDynamicObject()->setProperty("sources",juce::var(relativeValues));
    if (!parseProjectLibrarySources(relativeCatalog).isEmpty()) return 53;
    juce::var wrongExtensionCatalog(new juce::DynamicObject());
    juce::Array<juce::var> wrongExtensionValues;
    wrongExtensionValues.add(juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
        .getChildFile("project.txt").getFullPathName());
    wrongExtensionCatalog.getDynamicObject()->setProperty("version",1);
    wrongExtensionCatalog.getDynamicObject()->setProperty("sources",juce::var(wrongExtensionValues));
    if (!parseProjectLibrarySources(wrongExtensionCatalog).isEmpty()) return 54;
    juce::StringArray duplicateLibrarySources;
    duplicateLibrarySources.add(savedLibrarySources[0]);
    duplicateLibrarySources.add(savedLibrarySources[0]);
    juce::Array<juce::var> duplicateValues;
    duplicateValues.add(duplicateLibrarySources[0]); duplicateValues.add(duplicateLibrarySources[1]);
    juce::var duplicateCatalog(new juce::DynamicObject());
    duplicateCatalog.getDynamicObject()->setProperty("version",1);
    duplicateCatalog.getDynamicObject()->setProperty("sources",juce::var(duplicateValues));
    if (!parseProjectLibrarySources(duplicateCatalog).isEmpty()) return 48;
    juce::StringArray oversizedLibrarySources;
    for (int i = 0; i < 9; ++i) oversizedLibrarySources.add(savedLibrarySources[0] + juce::String(i) + ".risco");
    if (writeProjectLibrarySources(libraryCatalogTemporary.getFile(),oversizedLibrarySources)) return 49;
    if (!libraryCatalogTemporary.getFile().replaceWithText(juce::String::repeatedString("x",65537)) ||
        !readProjectLibrarySources(libraryCatalogTemporary.getFile()).isEmpty()) return 55;
    if (!libraryFile.replaceWithText("invalid project")) return 35;
    auto invalidLibrary = std::make_shared<ProjectLibraryResult>();
    ProjectLibraryJob invalidLibraryJob(libraryFile, invalidLibrary);
    invalidLibraryJob.runJob();
    if (!invalidLibrary->done.load() || invalidLibrary->song || invalidLibrary->error.empty()) return 36;
    if (!libraryFile.replaceWithText(juce::String::repeatedString("x", 4 * 1024 * 1024 + 1))) return 39;
    auto oversizedLibrary = std::make_shared<ProjectLibraryResult>();
    ProjectLibraryJob oversizedLibraryJob(libraryFile, oversizedLibrary);
    oversizedLibraryJob.runJob();
    if (oversizedLibrary->song || oversizedLibrary->error.empty()) return 40;
    if (!libraryFile.replaceWithText(juce::String::fromUTF8(bytes.c_str()))) return 37;
    auto cancelledLibrary = std::make_shared<ProjectLibraryResult>();
    ProjectLibraryJob cancelledLibraryJob(libraryFile, cancelledLibrary);
    cancelledLibraryJob.signalJobShouldExit(); cancelledLibraryJob.runJob();
    if (cancelledLibrary->song || cancelledLibrary->error.empty()) return 38;
    auto takes = std::make_unique<Song>();
    takes->responseMotif = 2;
    takes->responseDensity = 1;
    takes->stemPanAutomation[0] = {true, {{-1.0f, 0.0f, 1.0f}}};
    takes->stemFxAutomation[0] = {true, {{true, false, true}}};
    const auto original = storeGeneratedCell(*takes,1234,2,"Original",{},6);
    if (!original) return 41;
    auto recipe = std::make_unique<Cell>(takes->bank[0]);
    recipe->name = "Take";
    recipe->sectionEdits[1] = {0.5f,0.6f,-1};
    const auto take = storePhraseTake(*takes,*recipe);
    if (!take || !run(std::make_unique<Song>(*takes)) ||
        !decodeRecovery(file.loadFileAsString().toStdString(),songBytes,decoded)) return 42;
    std::istringstream takeStream(songBytes);
    if (!loadSong(takeStream,*loaded) || loaded->preferredTakeId != take || loaded->cellCount != 2 ||
        loaded->bank[1].sectionEdits[1].octaves != -1 || loaded->bank[0].id != original ||
        !loaded->stemPanAutomation[0].enabled ||
        loaded->stemPanAutomation[0].levels != std::array<float,3>{{-1.0f,0.0f,1.0f}} ||
        !loaded->stemFxAutomation[0].enabled ||
        loaded->stemFxAutomation[0].active != std::array<bool,3>{{true,false,true}} ||
        loaded->responseMotif != 2 || loaded->responseDensity != 1) return 43;
    return 0;
}
}
