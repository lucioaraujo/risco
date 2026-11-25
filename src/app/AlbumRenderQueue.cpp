// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Lúcio de Araújo
#include "AlbumRenderQueue.hpp"

#include "AlbumWaveFile.hpp"
#include "core/SongIO.hpp"
#include "core/SongPlayback.hpp"
#include "core/SongRender.hpp"

#include <cmath>
#include <sstream>

namespace risco::app {
namespace {

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
                 std::string& error) {
    auto& record = manifest.tracks[static_cast<size_t>(index)];
    const auto projectFile = directory.getChildFile(juce::String::fromUTF8(record.projectFile.c_str()));
    if (!projectFile.existsAsFile() || projectFile.getSize() <= 0 ||
        projectFile.getSize() > 4 * 1024 * 1024) {
        error = "Projeto inválido na faixa " + std::to_string(index + 1) + ".";
        return false;
    }
    juce::MemoryBlock bytes;
    if (!projectFile.loadFileAsData(bytes)) {
        error = "Não foi possível ler o projeto da faixa.";
        return false;
    }
    const std::string projectData(static_cast<const char*>(bytes.getData()), bytes.getSize());
    std::istringstream projectInput(projectData);
    Song song;
    if (!loadSong(projectInput, song) || !albumTrackMatchesProject(record.plan, song)) {
        error = "Projeto da faixa não corresponde ao plano do manifesto.";
        return false;
    }
    const auto playback = prepareSongPlayback(song);
    if (playback.count == 0 || !SongRender::validDuration(playback.end)) {
        error = "Grade vazia ou duração inválida na faixa " + std::to_string(index + 1) + ".";
        return false;
    }
    if (std::abs(playback.end - record.plannedSeconds) > 0.02) {
        error = "Duração musical do projeto diverge do plano do álbum.";
        return false;
    }

    const bool rendered = renderAlbumWav24(song, playback, wavFile, index, shouldCancel,
        [&progress, index, count = manifest.count](double fraction) {
            if (progress) progress((index + fraction) / count);
        }, metrics, error);
    if (!rendered) return false;
    return true;
}

} // namespace

AlbumRenderQueueResult runAlbumRenderQueue(
    AlbumManifest& manifest, const juce::File& directory,
    const std::function<bool()>& shouldCancel,
    const std::function<void(double)>& progress,
    const std::function<void(const std::string&)>& status) {
    AlbumRenderQueueResult result{"Render de álbum interrompido.", false};
    const auto cancelled = [&] { return shouldCancel && shouldCancel(); };
    if (!directory.isDirectory() || manifest.count < 1 ||
        manifest.count > static_cast<int>(manifest.tracks.size())) {
        result.message = "Pasta ou manifesto de álbum inválido.";
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
        result.message = "Espaço insuficiente: preserve 5 GB livres além dos WAVs estimados.";
        return result;
    }
    const auto manifestFile = directory.getChildFile("album.risco-manifest");
    if (!writeManifest(manifestFile, manifest)) {
        result.message = "Não foi possível validar/gravar o manifesto antes do render.";
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
            result.message = "A faixa " + std::to_string(index + 1) + " ainda não tem projeto .risco.";
            return result;
        }
        const auto wavName = "track-" + juce::String(index + 1).paddedLeft('0', 2) + ".wav";
        const auto wavFile = directory.getChildFile(wavName);
        if (record.state == AlbumTrackState::rendered && validAlbumWav24(wavFile, record.renderedSeconds)) {
            ++finished;
            continue;
        }
        if (status) status("Renderizando faixa " + std::to_string(index + 1) + "/" +
                           std::to_string(manifest.count));
        std::string error;
        AlbumWaveMetrics metrics;
        if (!renderTrack(manifest, directory, index, wavFile, shouldCancel, progress, metrics, error)) {
            if (error == "cancelled") result.cancelled = true;
            else result.message = "Falha no render da faixa " + std::to_string(index + 1) + ": " + error;
            return result;
        }
        if (!recordAlbumRender(manifest, index, wavName.toStdString(), metrics.durationSeconds,
                               metrics.integratedLufs, metrics.truePeakDbtp)) {
            result.message = "Falha ao validar medições no checkpoint após " +
                             wavName.toStdString() + "; mantenha WAV e projetos para retomar.";
            return result;
        }
        if (!writeManifest(manifestFile, manifest)) {
            result.message = "Falha ao gravar checkpoint após " + wavName.toStdString() +
                             "; mantenha WAV e projetos para retomar.";
            return result;
        }
        ++finished;
        if (progress) progress(double(finished) / manifest.count);
    }
    result.message = "WAVs de álbum prontos: " + std::to_string(finished) + "/" +
        std::to_string(manifest.count) + " · PCM24/48 kHz · " +
        manifestFile.getFullPathName().toStdString();
    if (progress) progress(1.0);
    return result;
}

} // namespace risco::app
