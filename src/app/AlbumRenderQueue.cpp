// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Lúcio de Araújo
#include "AlbumRenderQueue.hpp"

#include "AlbumWaveFile.hpp"
#include "core/SongIO.hpp"
#include "core/SongPlayback.hpp"
#include "core/SongRender.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace risco::app {
namespace {

const char* text(int language, const char* pt, const char* en,
                 const char* fr, const char* es) {
    const char* values[] = {pt, en, fr, es};
    return values[std::clamp(language, 0, 3)];
}

std::string localizeWaveError(const std::string& error, int language) {
    if (error == "empty playback or invalid track index/duration")
        return text(language, "reprodução vazia ou índice/duração da faixa inválidos", "empty playback or invalid track index/duration", "lecture vide ou index/durée du morceau invalide", "reproducción vacía o índice/duración de pieza no válidos");
    if (error == "WAV duration exceeds the RIFF size limit")
        return text(language, "a duração do WAV excede o limite RIFF", "WAV duration exceeds the RIFF size limit", "la durée du WAV dépasse la limite RIFF", "la duración del WAV supera el límite RIFF");
    if (error == "cannot create temporary WAV")
        return text(language, "não foi possível criar o WAV temporário", "cannot create temporary WAV", "impossible de créer le WAV temporaire", "no se pudo crear el WAV temporal");
    if (error == "per-track processing deadline exceeded")
        return text(language, "o limite de processamento por faixa foi excedido", "per-track processing deadline exceeded", "le délai de traitement par morceau est dépassé", "se superó el límite de procesamiento por pieza");
    if (error == "failed to write temporary WAV")
        return text(language, "falha ao gravar o WAV temporário", "failed to write temporary WAV", "échec de l’écriture du WAV temporaire", "no se pudo escribir el WAV temporal");
    if (error == "failed to finish temporary WAV")
        return text(language, "falha ao concluir o WAV temporário", "failed to finish temporary WAV", "échec de finalisation du WAV temporaire", "no se pudo finalizar el WAV temporal");
    if (error == "failed to publish WAV atomically")
        return text(language, "falha ao publicar o WAV atomicamente", "failed to publish WAV atomically", "échec de publication atomique du WAV", "no se pudo publicar el WAV de forma atómica");
    return error;
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

bool writeManifest(const juce::File& target, const AlbumManifest& manifest) {
    std::ostringstream serialized;
    if (!saveAlbumManifest(serialized, manifest)) return false;
    return writeAtomically(target, serialized.str());
}

bool renderTrack(AlbumManifest& manifest, const juce::File& directory, int index,
                 const juce::File& wavFile, const std::function<bool()>& shouldCancel,
                 const std::function<void(double)>& progress, AlbumWaveMetrics& metrics,
                 std::string& error, int language) {
    auto& record = manifest.tracks[static_cast<size_t>(index)];
    const auto projectFile = directory.getChildFile(juce::String::fromUTF8(record.projectFile.c_str()));
    if (!projectFile.existsAsFile() || projectFile.getSize() <= 0 ||
        projectFile.getSize() > 4 * 1024 * 1024) {
        error = std::string(text(language, "Projeto inválido na faixa ", "Invalid project for track ", "Projet invalide pour le morceau ", "Proyecto no válido en la pieza ")) + std::to_string(index + 1) + ".";
        return false;
    }
    juce::MemoryBlock bytes;
    if (!projectFile.loadFileAsData(bytes)) {
        error = text(language, "Não foi possível ler o projeto da faixa.", "Could not read the track project.", "Impossible de lire le projet du morceau.", "No se pudo leer el proyecto de la pieza.");
        return false;
    }
    const std::string projectData(static_cast<const char*>(bytes.getData()), bytes.getSize());
    std::istringstream projectInput(projectData);
    Song song;
    if (!loadSong(projectInput, song) || !albumTrackMatchesProject(record.plan, song)) {
        error = text(language, "Projeto da faixa não corresponde ao plano do manifesto.", "Track project does not match the manifest plan.", "Le projet du morceau ne correspond pas au plan du manifeste.", "El proyecto de la pieza no coincide con el plan del manifiesto.");
        return false;
    }
    const auto playback = prepareSongPlayback(song);
    if (playback.count == 0 || !SongRender::validDuration(playback.end)) {
        error = std::string(text(language, "Grade vazia ou duração inválida na faixa ", "Empty grid or invalid duration for track ", "Grille vide ou durée invalide pour le morceau ", "La cuadrícula está vacía o la duración no es válida en la pieza ")) + std::to_string(index + 1) + ".";
        return false;
    }
    if (std::abs(playback.end - record.plannedSeconds) > 0.02) {
        error = text(language, "Duração musical do projeto diverge do plano do álbum.", "Project's musical duration differs from the album plan.", "La durée musicale du projet diffère du plan de l’album.", "La duración musical del proyecto difiere del plan del álbum.");
        return false;
    }

    const bool rendered = renderAlbumWav24(song, playback, wavFile, index, shouldCancel,
        [&progress, index, count = manifest.count](double fraction) {
            if (progress) progress((index + fraction) / count);
        }, metrics, error);
    if (!rendered && error != "cancelled") error = localizeWaveError(error, language);
    if (!rendered) return false;
    return true;
}

} // namespace

AlbumRenderQueueResult runAlbumRenderQueue(
    AlbumManifest& manifest, const juce::File& directory,
    const std::function<bool()>& shouldCancel,
    const std::function<void(double)>& progress,
    const std::function<void(const std::string&)>& status,
    int language) {
    language = std::clamp(language, 0, 3);
    AlbumRenderQueueResult result{text(language, "Render de álbum interrompido.", "Album render interrupted.", "Rendu de l’album interrompu.", "Render del álbum interrumpido."), false};
    const auto cancelled = [&] { return shouldCancel && shouldCancel(); };
    if (!directory.isDirectory() || manifest.count < 1 ||
        manifest.count > static_cast<int>(manifest.tracks.size())) {
        result.message = text(language, "Pasta ou manifesto de álbum inválido.", "Invalid album folder or manifest.", "Dossier ou manifeste d’album invalide.", "Carpeta o manifiesto del álbum no válido.");
        return result;
    }

    juce::int64 estimate = 0;
    for (int index = 0; index < manifest.count; ++index) {
        const auto& record = manifest.tracks[static_cast<size_t>(index)];
        const auto wav = directory.getChildFile("track-" + juce::String(index + 1).paddedLeft('0', 2) + ".wav");
        if (record.state != AlbumTrackState::rendered || !validAlbumWav24(wav, record.renderedSeconds))
            estimate += static_cast<juce::int64>(std::llround((record.plannedSeconds + 30.0) *
                SongRender::rate)) * 6 + 44;
    }
    constexpr juce::int64 reserve = 5000000000LL;
    if (directory.getBytesFreeOnVolume() < estimate + reserve) {
        result.message = text(language, "Espaço insuficiente: preserve 5 GB livres além dos WAVs estimados.", "Not enough space: keep 5 GB free beyond the estimated WAV size.", "Espace insuffisant : gardez 5 Go libres en plus de la taille estimée des WAV.", "Espacio insuficiente: mantenga 5 GB libres además del tamaño estimado de los WAV.");
        return result;
    }
    const auto manifestFile = directory.getChildFile("album.risco-manifest");
    if (!writeManifest(manifestFile, manifest)) {
        result.message = text(language, "Não foi possível validar/gravar o manifesto antes do render.", "Could not validate or write the manifest before rendering.", "Impossible de valider ou d’écrire le manifeste avant le rendu.", "No se pudo validar o escribir el manifiesto antes del render.");
        return result;
    }

    int finished = 0;
    for (int index = 0; index < manifest.count; ++index) {
        if (cancelled()) {
            result.cancelled = true;
            return result;
        }
        auto& record = manifest.tracks[static_cast<size_t>(index)];
        if (record.projectFile.empty()) {
            result.message = std::string(text(language, "A faixa ", "Track ", "Le morceau ", "La pieza ")) + std::to_string(index + 1) + text(language, " ainda não tem projeto .risco.", " has no .risco project yet.", " n’a pas encore de projet .risco.", " aún no tiene proyecto .risco.");
            return result;
        }
        const auto wavName = "track-" + juce::String(index + 1).paddedLeft('0', 2) + ".wav";
        const auto wavFile = directory.getChildFile(wavName);
        if (record.state == AlbumTrackState::rendered && validAlbumWav24(wavFile, record.renderedSeconds)) {
            ++finished;
            continue;
        }
        if (status) status(std::string(text(language, "Renderizando faixa ", "Rendering track ", "Rendu du morceau ", "Renderizando pieza ")) + std::to_string(index + 1) + "/" +
                           std::to_string(manifest.count));
        std::string error;
        AlbumWaveMetrics metrics;
        if (!renderTrack(manifest, directory, index, wavFile, shouldCancel, progress, metrics, error, language)) {
            if (error == "cancelled") result.cancelled = true;
            else result.message = std::string(text(language, "Falha no render da faixa ", "Failed to render track ", "Échec du rendu du morceau ", "Error al renderizar la pieza ")) + std::to_string(index + 1) + ": " + error;
            return result;
        }
        if (!recordAlbumRender(manifest, index, wavName.toStdString(), metrics.durationSeconds,
                               metrics.integratedLufs, metrics.truePeakDbtp)) {
            result.message = std::string(text(language, "Falha ao validar medições no checkpoint após ", "Could not validate measurements in checkpoint after ", "Échec de validation des mesures dans le point de reprise après ", "No se pudieron validar las mediciones en el punto de control después de ")) +
                             wavName.toStdString() + text(language, "; mantenha WAV e projetos para retomar.", "; keep WAVs and projects to resume.", "; conservez les WAV et les projets pour reprendre.", "; conserve los WAV y proyectos para reanudar.");
            return result;
        }
        if (!writeManifest(manifestFile, manifest)) {
            result.message = std::string(text(language, "Falha ao gravar checkpoint após ", "Could not write checkpoint after ", "Échec d’écriture du point de reprise après ", "No se pudo escribir el punto de control después de ")) + wavName.toStdString() +
                             text(language, "; mantenha WAV e projetos para retomar.", "; keep WAVs and projects to resume.", "; conservez les WAV et les projets pour reprendre.", "; conserve los WAV y proyectos para reanudar.");
            return result;
        }
        ++finished;
        if (progress) progress(double(finished) / manifest.count);
    }
    result.message = std::string(text(language, "WAVs de álbum prontos: ", "Album WAVs ready: ", "WAV de l’album prêts : ", "WAV del álbum listos: ")) + std::to_string(finished) + "/" +
        std::to_string(manifest.count) + " · PCM24/48 kHz · " +
        manifestFile.getFullPathName().toStdString();
    if (progress) progress(1.0);
    return result;
}

} // namespace risco::app
