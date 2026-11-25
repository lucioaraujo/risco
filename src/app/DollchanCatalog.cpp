// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Lúcio de Araújo
#include "DollchanCatalog.hpp"
#include "core/BytebeatExpression.hpp"
#include <array>
#include <string>

namespace risco {
namespace {
juce::String utf8(const char* value) { return juce::String(juce::CharPointer_UTF8(value)); }
bool clean(const std::string& value, size_t limit) {
    return value.size() <= limit && value.find_first_of("\r\n\0", 0, 3) == std::string::npos;
}
}

juce::Result loadDollchanCatalog(const juce::File& file,
                                std::vector<FormulaRecord>& destination) {
    if (file.getSize() <= 0 || file.getSize() > 2 * 1024 * 1024)
        return juce::Result::fail(utf8("Catálogo vazio ou maior que 2 MiB."));
    juce::FileInputStream input(file);
    if (!input.openedOk()) return juce::Result::fail(utf8("Não foi possível ler o catálogo."));
    juce::GZIPDecompressorInputStream gzip(&input, false,
                                           juce::GZIPDecompressorInputStream::gzipFormat);
    juce::MemoryOutputStream output;
    std::array<char, 4096> buffer{};
    for (;;) {
        const int count = gzip.read(buffer.data(), int(buffer.size()));
        if (count <= 0) break;
        if (output.getDataSize() + size_t(count) > 8 * 1024 * 1024)
            return juce::Result::fail(utf8("Catálogo descompactado maior que 8 MiB."));
        output.write(buffer.data(), size_t(count));
    }
    juce::var root;
    if (juce::JSON::parse(output.toString(), root).failed() || !root.isArray())
        return juce::Result::fail(utf8("Catálogo GZIP/JSON inválido."));

    std::vector<FormulaRecord> next;
    for (const auto& group : *root.getArray()) {
        const auto songsValue = group.getProperty("songs", {});
        const auto* songs = songsValue.getArray();
        if (songs == nullptr) continue;
        const auto groupAuthor = group.getProperty("author", {}).toString();
        for (const auto& entry : *songs) {
            if (next.size() >= 256) break;
            const auto mode = entry.getProperty("mode", {}).toString();
            if (mode.isNotEmpty() && mode != "Bytebeat") continue;
            if (int(entry.getProperty("sampleRate", 0)) != 8000 ||
                bool(entry.getProperty("stereo", false))) continue;
            const auto tagsValue = entry.getProperty("tags", {});
            const auto* tags = tagsValue.getArray();
            bool classic = false;
            if (tags != nullptr)
                for (const auto& tag : *tags) classic = classic || tag.toString() == "c";
            if (!classic) continue;
            const auto code = entry.getProperty("code", {}).toString().toStdString();
            if (code.empty() || code.size() > 128) continue;
            BytebeatProgram program;
            std::string error;
            if (!compileBytebeat(code, program, error)) continue;
            const auto hash = entry.getProperty("hash", {}).toString();
            if (hash.length() != 32 || !hash.containsOnly("0123456789abcdef")) continue;
            auto name = entry.getProperty("name", {}).toString();
            if (name.isEmpty()) name = "Dollchan " + hash.substring(0, 8);
            auto author = entry.getProperty("author", {}).toString();
            if (author.isEmpty()) author = groupAuthor;
            if (author.isEmpty()) author = utf8("Autor não indicado");
            auto source = entry.getProperty("url", {});
            if (const auto* urls = source.getArray()) {
                const juce::var chosen = urls->isEmpty() ? juce::var{} : (*urls)[0];
                source = chosen;
            }
            auto sourceText = source.isString() ? source.toString() : juce::String();
            if (sourceText.isEmpty()) sourceText = "https://dollchan.net/bytebeat/";
            FormulaRecord record{name.toStdString(), code, author.toStdString(),
                                 sourceText.toStdString(), "dollchan:" + hash.toStdString(),
                                 "rights-unverified"};
            if (!clean(record.name, 128) || !clean(record.author, 128) ||
                !clean(record.source, 256)) continue;
            next.push_back(std::move(record));
        }
        if (next.size() >= 256) break;
    }
    if (next.empty()) return juce::Result::fail(utf8("Nenhuma fórmula 8 kHz compatível encontrada."));
    destination = std::move(next);
    return juce::Result::ok();
}
}
