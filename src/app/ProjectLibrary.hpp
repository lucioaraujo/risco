// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Lúcio de Araújo
#pragma once
#include <juce_core/juce_core.h>
#include "core/SongIO.hpp"
#include <algorithm>
#include <atomic>
#include <memory>
#include <sstream>
#include <vector>

struct ProjectLibraryResult {
    std::atomic<bool> done{false};
    std::unique_ptr<risco::Song> song;
    std::string description, error;
};
struct ProjectLibraryEntry {
    std::shared_ptr<risco::Song> song;
    std::string description;
    std::string displayName;
    std::string canonicalPath;
};
struct ProjectLibraryMatch {
    std::shared_ptr<risco::Song> song;
    std::string description;
    std::string displayName;
    int cellIndex = -1;
};
struct ProjectLibraryGroup {
    juce::String name;
    juce::StringArray sources;
};
struct ProjectLibraryCatalog {
    juce::StringArray sources;
    std::vector<ProjectLibraryGroup> groups;
};
inline bool validProjectLibrarySources(const juce::StringArray& paths) {
    if (paths.size() > 8) return false;
    juce::StringArray unique;
    for (auto path : paths) {
        path = path.trim();
        if (path.isEmpty() || path.length() > 4096 || !juce::File::isAbsolutePath(path) ||
            !path.endsWithIgnoreCase(".risco") || unique.contains(path,true)) return false;
        unique.add(path);
    }
    return true;
}
inline juce::StringArray parseProjectLibrarySources(const juce::var& encoded) {
    juce::StringArray paths;
    const int version = int(encoded.getProperty("version", 0));
    if (!encoded.isObject() || (version != 1 && version != 2)) return paths;
    const auto sourceValues = encoded.getProperty("sources", {});
    const auto* values = sourceValues.getArray();
    if (!values || values->size() > 8) return paths;
    for (const auto& value : *values) {
        if (!value.isString()) return {};
        const auto path = value.toString();
        if (path.isEmpty() || path.length() > 4096 || !juce::File::isAbsolutePath(path) ||
            !path.endsWithIgnoreCase(".risco")) return {};
        for (auto existing : paths)
            if (existing.compareIgnoreCase(path) == 0) return {};
        paths.add(path);
    }
    return paths;
}
inline bool writeProjectLibrarySources(const juce::File& file, const juce::StringArray& paths) {
    if (!validProjectLibrarySources(paths)) return false;
    juce::Array<juce::var> values;
    for (auto path : paths) {
        path = path.trim();
        values.add(path);
    }
    juce::var document(new juce::DynamicObject());
    document.getDynamicObject()->setProperty("version", 1);
    document.getDynamicObject()->setProperty("sources", juce::var(values));
    if (!file.getParentDirectory().createDirectory().wasOk()) return false;
    return file.replaceWithText(juce::JSON::toString(document));
}
inline bool validProjectLibraryGroupName(const juce::String& name) {
    const auto trimmed = name.trim();
    return !trimmed.isEmpty() && trimmed.length() <= 64 && !trimmed.containsAnyOf("\n\r\t");
}
inline std::vector<ProjectLibraryGroup> parseProjectLibraryGroups(const juce::var& encoded,
                                                                   const juce::StringArray& sources) {
    std::vector<ProjectLibraryGroup> groups;
    if (int(encoded.getProperty("version", 0)) == 1) return groups;
    if (int(encoded.getProperty("version", 0)) != 2) return groups;
    const auto groupValue = encoded.getProperty("groups", {});
    const auto* values = groupValue.getArray();
    if (!values || values->size() > 16) return {};
    for (const auto& value : *values) {
        if (!value.isObject()) return {};
        const auto name = value.getProperty("name", {}).toString().trim();
        const auto membersValue = value.getProperty("sources", {});
        const auto* members = membersValue.getArray();
        if (!validProjectLibraryGroupName(name) || !members || members->isEmpty() || members->size() > 8) return {};
        for (const auto& existing : groups)
            if (existing.name.compareIgnoreCase(name) == 0) return {};
        ProjectLibraryGroup group;
        group.name = name;
        for (const auto& member : *members) {
            if (!member.isString()) return {};
            const auto path = member.toString();
            if (!sources.contains(path, true)) return {};
            if (group.sources.contains(path, true)) return {};
            group.sources.add(path);
        }
        groups.push_back(std::move(group));
    }
    return groups;
}
inline bool writeProjectLibraryCatalog(const juce::File& file, const ProjectLibraryCatalog& catalog) {
    if (!validProjectLibrarySources(catalog.sources) || catalog.groups.size() > 16) return false;
    juce::Array<juce::var> sources;
    for (const auto& path : catalog.sources) sources.add(path);
    juce::Array<juce::var> groups;
    juce::StringArray names;
    for (const auto& group : catalog.groups) {
        const auto name = group.name.trim();
        if (!validProjectLibraryGroupName(name) || group.sources.isEmpty() || group.sources.size() > 8 ||
            names.contains(name,true)) return false;
        names.add(name);
        juce::Array<juce::var> members;
        for (const auto& path : group.sources) {
            if (!catalog.sources.contains(path,true) || members.size() >= 8) return false;
            for (const auto& previous : members)
                if (previous.toString().compareIgnoreCase(path) == 0) return false;
            members.add(path);
        }
        juce::var item(new juce::DynamicObject());
        item.getDynamicObject()->setProperty("name",name);
        item.getDynamicObject()->setProperty("sources",juce::var(members));
        groups.add(item);
    }
    juce::var document(new juce::DynamicObject());
    document.getDynamicObject()->setProperty("version",2);
    document.getDynamicObject()->setProperty("sources",juce::var(sources));
    document.getDynamicObject()->setProperty("groups",juce::var(groups));
    if (!file.getParentDirectory().createDirectory().wasOk()) return false;
    return file.replaceWithText(juce::JSON::toString(document));
}
inline ProjectLibraryCatalog readProjectLibraryCatalog(const juce::File& file) {
    ProjectLibraryCatalog catalog;
    const auto size = file.getSize();
    if (size <= 0 || size > 65536) return catalog;
    const auto encoded = juce::JSON::parse(file.loadFileAsString());
    catalog.sources = parseProjectLibrarySources(encoded);
    catalog.groups = parseProjectLibraryGroups(encoded,catalog.sources);
    return catalog;
}
inline juce::StringArray readProjectLibrarySources(const juce::File& file) {
    const auto size = file.getSize();
    if (size <= 0 || size > 65536) return {};
    return parseProjectLibrarySources(juce::JSON::parse(file.loadFileAsString()));
}
inline std::vector<ProjectLibraryMatch> searchProjectLibraries(
    const std::vector<ProjectLibraryEntry>& entries, const juce::String& query,
    int maximum = 128, const juce::StringArray* allowedSources = nullptr) {
    std::vector<ProjectLibraryMatch> matches;
    maximum = std::clamp(maximum,0,128);
    const auto needle = query.trim().substring(0,128);
    for (const auto& entry : entries) {
        if (!entry.song) continue;
        const auto sourcePath = juce::String::fromUTF8(entry.canonicalPath.c_str());
        if (allowedSources && !allowedSources->contains(sourcePath,true)) continue;
        for (int i = 0; i < entry.song->cellCount && int(matches.size()) < maximum; ++i) {
            const auto& cell = entry.song->bank[static_cast<size_t>(i)];
            const auto searchable = juce::String::fromUTF8(cell.name.c_str()) + " " + juce::String(cell.id) + " " +
                juce::String::fromUTF8(cell.formulaExpression.c_str()) + " " +
                juce::String::fromUTF8(cell.formulaOrigin.author.c_str()) + " " +
                juce::String::fromUTF8(cell.formulaOrigin.source.c_str()) + " " +
                juce::String::fromUTF8(cell.importProvenance.c_str()) + " " +
                juce::String::fromUTF8(entry.displayName.c_str());
            if (!needle.isEmpty() && !searchable.containsIgnoreCase(needle)) continue;
            matches.push_back({entry.song,entry.description,entry.displayName,i});
        }
        if (int(matches.size()) >= maximum) break;
    }
    return matches;
}
class ProjectLibraryJob final : public juce::ThreadPoolJob {
public:
    ProjectLibraryJob(juce::File source, std::shared_ptr<ProjectLibraryResult> output)
        : ThreadPoolJob("Risco project library"), file(std::move(source)), result(std::move(output)) {}
    JobStatus runJob() override {
        constexpr int limit = 4 * 1024 * 1024;
        const auto length = file.getSize();
        if (length <= 0 || length > limit) return fail("Escolha um projeto .risco de ate 4 MiB.");
        auto input = file.createInputStream();
        if (!input) return fail("Nao foi possivel ler o projeto escolhido.");
        const auto deadline = juce::Time::getMillisecondCounterHiRes() + 5000;
        std::string bytes;
        bytes.reserve(static_cast<size_t>(length));
        std::array<char, 65536> block;
        while (!input->isExhausted()) {
            if (shouldExit() || juce::Time::getMillisecondCounterHiRes() > deadline)
                return fail("Leitura cancelada ou tempo de leitura esgotado.");
            const int count = input->read(block.data(), int(block.size()));
            if (count <= 0) break;
            bytes.append(block.data(), static_cast<size_t>(count));
            if (bytes.size() > limit) return fail("Projeto excede 4 MiB.");
        }
        if (bytes.size() != static_cast<size_t>(length)) return fail("Leitura incompleta ou arquivo alterado durante a leitura.");
        const auto bytesRead = bytes.size();
        // Text-mode writers on Windows may store CRLF. Preserve all other bytes.
        size_t written = 0;
        for (size_t i = 0; i < bytes.size(); ++i) {
            if (bytes[i] == '\r' && i + 1 < bytes.size() && bytes[i + 1] == '\n') continue;
            bytes[written++] = bytes[i];
        }
        bytes.resize(written);
        auto loaded = std::make_unique<risco::Song>();
        std::istringstream stream(bytes);
        if (shouldExit() || !risco::loadSong(stream, *loaded)) return fail("Projeto invalido ou versao nao suportada.");
        if (juce::Time::getMillisecondCounterHiRes() > deadline) return fail("Tempo de leitura esgotado.");
        auto description = file.getFileName().substring(0, 128) + " [" + juce::String(bytesRead) + " bytes; modified " +
            file.getLastModificationTime().toISO8601(true) + "; " + juce::String(loaded->bpm, 2) + " BPM; meter " +
            juce::String(loaded->compoundSixEight ? 6 : loaded->beatsPerBar) + (loaded->compoundSixEight ? "/8]" : "/4]");
        result->description = description.toStdString();
        result->song = std::move(loaded);
        result->done.store(true, std::memory_order_release);
        return jobHasFinished;
    }
private:
    JobStatus fail(const char* message) {
        result->error = message;
        result->done.store(true, std::memory_order_release);
        return jobHasFinished;
    }
    juce::File file;
    std::shared_ptr<ProjectLibraryResult> result;
};
