// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Lúcio de Araújo
#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "core/AlbumManifest.hpp"
#include "core/SongIO.hpp"

#include <cmath>
#include <algorithm>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <utility>

class AlbumGenerationJob final : public juce::ThreadWithProgressWindow {
public:
    AlbumGenerationJob(risco::AlbumManifest initial, juce::File folder, int uiLanguage)
        : ThreadWithProgressWindow(text(uiLanguage, "Gerar projetos do álbum", "Generate album projects", "Générer les projets de l’album", "Generar proyectos del álbum"),
                                   true, true, 10000,
                                   text(uiLanguage, "Cancelar", "Cancel", "Annuler", "Cancelar")),
          manifest(std::move(initial)), directory(std::move(folder)),
          language(std::clamp(uiLanguage, 0, 3)) {}
    ~AlbumGenerationJob() override { signalThreadShouldExit(); stopThread(10000); }

    std::function<void(juce::String)> completed;

    void run() override {
        const auto manifestFile = directory.getChildFile("album.risco-manifest");
        if (!directory.isDirectory() || !writeManifest(manifestFile)) {
            result = text(language, "Não foi possível criar o manifesto do álbum.", "Could not create the album manifest.", "Impossible de créer le manifeste de l’album.", "No se pudo crear el manifiesto del álbum.");
            return;
        }
        int finished = 0;
        for (int index = 0; index < manifest.count; ++index) {
            if (threadShouldExit()) return;
            auto& record = manifest.tracks[static_cast<size_t>(index)];
            const auto name = record.projectFile.empty()
                ? "track-" + juce::String(index + 1).paddedLeft('0', 2) + ".risco"
                : juce::String::fromUTF8(record.projectFile.c_str());
            const auto projectFile = directory.getChildFile(name);
            if (projectFile.exists()) {
                if (!validSavedProject(projectFile, record)) {
                    result = text(language, "Projeto existente incompatível preservado: ",
                        "Existing incompatible project preserved: ", "Projet existant incompatible conservé : ",
                        "Proyecto existente incompatible conservado: ") + name;
                    return;
                }
                if (record.state == risco::AlbumTrackState::planned &&
                    (!risco::recordAlbumProject(manifest, index, name.toStdString()) ||
                     !writeManifest(manifestFile))) {
                    result = text(language, "Não foi possível registrar o projeto existente: ",
                        "Could not record the existing project: ", "Impossible d’enregistrer le projet existant : ",
                        "No se pudo registrar el proyecto existente: ") + name;
                    return;
                }
                setProgress(double(++finished) / manifest.count);
                continue;
            }
            if (record.state == risco::AlbumTrackState::rendered) {
                if (!risco::invalidateAlbumRender(manifest, index) || !writeManifest(manifestFile)) {
                    result = text(language, "Não foi possível invalidar o checkpoint de render da faixa ", "Could not invalidate the render checkpoint for track ", "Impossible d’invalider le point de reprise du rendu du morceau ", "No se pudo invalidar el punto de control del render de la pieza ") +
                             juce::String(index + 1) + text(language, "; mantenha os arquivos para retomar.", "; keep the files to resume.", "; conservez les fichiers pour reprendre.", "; conserve los archivos para reanudar.");
                    return;
                }
            }
            setStatusMessage(text(language, "Gerando projeto ", "Generating project ", "Génération du projet ", "Generando proyecto ") + juce::String(index + 1) + "/" +
                             juce::String(manifest.count));
            auto song = std::make_unique<risco::Song>();
            if (!risco::generateAlbumTrack(manifest.plan, index, *song)) {
                result = text(language, "Falha ao gerar a faixa ", "Failed to generate track ", "Échec de génération du morceau ", "Error al generar la pieza ") + juce::String(index + 1) +
                         text(language, "; o manifesto preserva o ponto de retomada.", "; the manifest preserves the resume point.", "; le manifeste conserve le point de reprise.", "; el manifiesto conserva el punto de reanudación.");
                return;
            }
            std::ostringstream serialized;
            if (!risco::saveSong(serialized, *song)) {
                result = text(language, "A faixa ", "Track ", "Le morceau ", "La pieza ") + juce::String(index + 1) + text(language, " não passou na validação .risco.", " failed .risco validation.", " n’a pas passé la validation .risco.", " no pasó la validación .risco.");
                return;
            }
            if (projectFile.exists() || !writeAtomically(projectFile, serialized.str())) {
                result = text(language, "Não foi possível gravar ", "Could not write ", "Impossible d’écrire ", "No se pudo escribir ") + name + text(language, "; o manifesto permanece retomável.", "; the manifest can still be resumed.", "; le manifeste reste récupérable.", "; el manifiesto se puede reanudar.");
                return;
            }
            if (!risco::recordAlbumProject(manifest, index, name.toStdString()) ||
                !writeManifest(manifestFile)) {
                result = text(language, "Falha no checkpoint após ", "Checkpoint failed after ", "Échec du point de reprise après ", "Falló el punto de control después de ") + name + text(language, "; mantenha os arquivos para recuperar.", "; keep the files for recovery.", "; conservez les fichiers pour la récupération.", "; conserve los archivos para recuperarlos.");
                return;
            }
            ++finished;
            setProgress(double(finished) / manifest.count);
        }
        result = text(language, "Projetos do álbum prontos: ", "Album projects ready: ", "Projets de l’album prêts : ", "Proyectos del álbum listos: ") + juce::String(finished) + "/" +
                 juce::String(manifest.count) + text(language, " · manifesto salvo em ", " · manifest saved to ", " · manifeste enregistré dans ", " · manifiesto guardado en ") + manifestFile.getFullPathName();
        setProgress(1.0);
    }

    void threadComplete(bool cancelled) override {
        if (completed) completed(cancelled ? text(language, "Geração cancelada entre faixas; manifesto e projetos concluídos preservados.", "Generation cancelled between tracks; manifest and completed projects were preserved.", "Génération annulée entre les morceaux ; manifeste et projets terminés conservés.", "Generación cancelada entre piezas; se conservaron el manifiesto y los proyectos terminados.")
                                           : result);
    }

private:
    static juce::String text(int language, const char* pt, const char* en,
                             const char* fr, const char* es) {
        const char* values[] = {pt, en, fr, es};
        return juce::String::fromUTF8(values[std::clamp(language, 0, 3)]);
    }

    bool validSavedProject(const juce::File& file, const risco::AlbumTrackRecord& record) {
        if (!file.existsAsFile() || file.getSize() <= 0 || file.getSize() > 4 * 1024 * 1024)
            return false;
        juce::MemoryBlock bytes;
        if (!file.loadFileAsData(bytes)) return false;
        std::istringstream input(std::string(static_cast<const char*>(bytes.getData()), bytes.getSize()));
        auto song = std::make_unique<risco::Song>();
        return risco::loadSong(input, *song) && risco::albumTrackMatchesProject(record.plan, *song);
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
    int language = 0;
    juce::String result;
};
