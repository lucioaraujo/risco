// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Lúcio de Araújo
#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "core/AlbumManifest.hpp"
#include "core/SongIO.hpp"

#include <cmath>
#include <functional>
#include <sstream>
#include <string>
#include <utility>

class AlbumGenerationJob final : public juce::ThreadWithProgressWindow {
public:
    AlbumGenerationJob(risco::AlbumManifest initial, juce::File folder)
        : ThreadWithProgressWindow("Gerar projetos do álbum", true, true, 10000, "Cancelar"),
          manifest(std::move(initial)), directory(std::move(folder)) {}
    ~AlbumGenerationJob() override { signalThreadShouldExit(); stopThread(10000); }

    std::function<void(juce::String)> completed;

    void run() override {
        const auto manifestFile = directory.getChildFile("album.risco-manifest");
        if (!directory.isDirectory() || !writeManifest(manifestFile)) {
            result = "Não foi possível criar o manifesto do álbum.";
            return;
        }
        int finished = 0;
        for (int index = 0; index < manifest.count; ++index) {
            if (threadShouldExit()) return;
            auto& record = manifest.tracks[static_cast<size_t>(index)];
            if (record.state == risco::AlbumTrackState::rendered) {
                if (validSavedProject(directory.getChildFile(record.projectFile), record)) {
                    ++finished;
                    continue;
                }
                if (!risco::invalidateAlbumRender(manifest, index) || !writeManifest(manifestFile)) {
                    result = "Não foi possível invalidar o checkpoint de render da faixa " +
                             juce::String(index + 1) + "; mantenha os arquivos para retomar.";
                    return;
                }
            }
            if (record.state == risco::AlbumTrackState::projectReady &&
                validSavedProject(directory.getChildFile(record.projectFile), record)) {
                ++finished;
                continue;
            }
            setStatusMessage("Gerando projeto " + juce::String(index + 1) + "/" +
                             juce::String(manifest.count));
            risco::Song song;
            if (!risco::generateAlbumTrack(manifest.plan, index, song)) {
                result = "Falha ao gerar a faixa " + juce::String(index + 1) +
                         "; o manifesto preserva o ponto de retomada.";
                return;
            }
            std::ostringstream serialized;
            if (!risco::saveSong(serialized, song)) {
                result = "A faixa " + juce::String(index + 1) + " não passou na validação .risco.";
                return;
            }
            const auto name = record.projectFile.empty()
                ? "track-" + juce::String(index + 1).paddedLeft('0', 2) + ".risco"
                : juce::String::fromUTF8(record.projectFile.c_str());
            const auto projectFile = directory.getChildFile(name);
            if (!writeAtomically(projectFile, serialized.str())) {
                result = "Não foi possível gravar " + name + "; o manifesto permanece retomável.";
                return;
            }
            if (!risco::recordAlbumProject(manifest, index, name.toStdString()) ||
                !writeManifest(manifestFile)) {
                result = "Falha no checkpoint após " + name + "; mantenha os arquivos para recuperar.";
                return;
            }
            ++finished;
            setProgress(double(finished) / manifest.count);
        }
        result = "Projetos do álbum prontos: " + juce::String(finished) + "/" +
                 juce::String(manifest.count) + " · manifesto salvo em " + manifestFile.getFullPathName();
        setProgress(1.0);
    }

    void threadComplete(bool cancelled) override {
        if (completed) completed(cancelled ? "Geração cancelada entre faixas; manifesto e projetos concluídos preservados."
                                           : result);
    }

private:
    bool validSavedProject(const juce::File& file, const risco::AlbumTrackRecord& record) {
        if (!file.existsAsFile() || file.getSize() <= 0 || file.getSize() > 4 * 1024 * 1024)
            return false;
        juce::MemoryBlock bytes;
        if (!file.loadFileAsData(bytes)) return false;
        std::istringstream input(std::string(static_cast<const char*>(bytes.getData()), bytes.getSize()));
        risco::Song song;
        return risco::loadSong(input, song) && risco::albumTrackMatchesProject(record.plan, song);
    }

    bool writeAtomically(const juce::File& target, const std::string& data) {
        juce::TemporaryFile temporary(target);
        auto stream = temporary.getFile().createOutputStream();
        if (!stream || !stream->openedOk()) return false;
        stream->write(data.data(), data.size());
        stream->flush();
        if (stream->getStatus().failed()) return false;
        stream.reset();
        return temporary.overwriteTargetFileWithTemporary();
    }

    bool writeManifest(const juce::File& target) {
        std::ostringstream serialized;
        if (!risco::saveAlbumManifest(serialized, manifest)) return false;
        const auto data = serialized.str();
        return writeAtomically(target, data);
    }

    risco::AlbumManifest manifest;
    juce::File directory;
    juce::String result = "Geração interrompida.";
};
