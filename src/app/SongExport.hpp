// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Lúcio de Araújo
#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "core/SongPlayback.hpp"
#include "core/SongRender.hpp"
#include "core/TpdfDither.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cmath>
#include <functional>
#include <memory>

// Owns a frozen project; no access to the live audio state from the worker.
class SongExport final : public juce::ThreadWithProgressWindow {
public:
    SongExport(std::unique_ptr<risco::Song> source, juce::File destination, int stem = -1, int bits = 16,
               bool beforeMaster = false)
        : ThreadWithProgressWindow("Exportar WAV", true, true, 10000, "Cancelar"),
          song(std::move(source)), file(std::move(destination)), selectedStem(stem),
          bitDepth(beforeMaster || bits == 32 ? 32 : bits == 24 ? 24 : 16), preMaster(beforeMaster) {}
    ~SongExport() override { signalThreadShouldExit(); stopThread(10000); }
    std::function<void(juce::String)> completed;
    void run() override {
        deadline = juce::Time::getMillisecondCounterHiRes() + 120000;
        auto playback = std::make_unique<risco::SongPlayback>(risco::prepareSongPlayback(*song));
        if (threadShouldExit()) return;
        if (playback->count == 0 || !risco::SongRender::validDuration(playback->end)) {
            result = "A grade deve conter ate 10 minutos de musica.";
            return;
        }
        const int tasks = selectedStem == -2 ? risco::Song::stemCount : 1;
        const auto estimated = static_cast<juce::int64>((playback->end + risco::SongRender::tailSecondsFor(*song)) * 48000) * 2 * (bitDepth / 8) * tasks + 1024;
        constexpr juce::int64 reserve = 5000000000LL;
        const auto freeBytes = file.getParentDirectory().getBytesFreeOnVolume();
        if (freeBytes < estimated + reserve) {
            result = "Espaco insuficiente: mantenha 5 GB livres alem do WAV/lote. Escolha outro volume.";
            return;
        }
        if (tasks > 1) {
            for (int s = 0; s < tasks; ++s) {
                if (stemFile(s).exists()) {
                    result = "Arquivo ja existe; escolha outro nome-base: " + stemFile(s).getFullPathName();
                    return;
                }
            }
        }
        for (int task = 0; task < tasks; ++task) {
            if (threadShouldExit()) return;
            const auto target = tasks > 1 ? stemFile(task) : file;
            setStatusMessage("Exportando " + target.getFileName());
            if (!renderFile(*playback, target, tasks > 1 ? task : selectedStem, task, tasks)) return;
            ++finishedFiles;
        }
        result = tasks > 1 ? "Cinco canais exportados em: " + file.getParentDirectory().getFullPathName()
                           : "WAV exportado: " + file.getFullPathName();
        result += summaries;
        setProgress(1.0);
    }
    void threadComplete(bool cancelled) override {
        if (completed) completed((cancelled ? juce::String("Exportacao cancelada.") : result) +
            (selectedStem == -2 ? " Arquivos concluidos: " + juce::String(finishedFiles) + "/3." : juce::String()));
    }
private:
    juce::File stemFile(int stem) const {
        return file.getSiblingFile(file.getFileNameWithoutExtension() +
                                   "-stem-" + juce::String(stem + 1) + ".wav");
    }
    bool renderFile(const risco::SongPlayback& playback, const juce::File& target,
                    int stem, int task, int tasks) {
        juce::TemporaryFile temporary(target);
        auto stream = temporary.getFile().createOutputStream();
        if (!stream || !stream->openedOk()) { result = "Nao foi possivel criar o WAV."; return false; }
        constexpr int rate = 48000;
        const auto bytesPerFrame = static_cast<std::uint32_t>(2 * bitDepth / 8);
        const auto frames = static_cast<std::uint32_t>((playback.end + risco::SongRender::tailSecondsFor(*song)) * rate);
        stream->write("RIFF", 4); stream->writeInt(static_cast<int>(36u +
            (bitDepth == 32 ? 12u : 0u) + frames * bytesPerFrame));
        stream->write("WAVEfmt ", 8); stream->writeInt(16);
        stream->writeShort(bitDepth == 32 ? 3 : 1); stream->writeShort(2); stream->writeInt(rate);
        stream->writeInt(rate * static_cast<int>(bytesPerFrame));
        stream->writeShort(static_cast<short>(bytesPerFrame)); stream->writeShort(static_cast<short>(bitDepth));
        if (bitDepth == 32) {
            stream->write("fact", 4); stream->writeInt(4); stream->writeInt(static_cast<int>(frames));
        }
        stream->write("data", 4); stream->writeInt(static_cast<int>(frames * bytesPerFrame));
        risco::SongRender renderer(*song, playback, stem, preMaster);
        risco::TpdfDither leftDither(bitDepth, 0x52495343u + static_cast<std::uint32_t>((task + 1) * 2));
        risco::TpdfDither rightDither(bitDepth, 0x4f415544u + static_cast<std::uint32_t>((task + 1) * 2));
        risco::LoudnessMeter writtenLoudness;
        writtenLoudness.prepare(rate);
        const auto latency = renderer.latencySamples();
        for (std::uint32_t i = 0; i < frames + latency; ++i) {
            if (i % 256 == 0) {
                if (threadShouldExit()) return false;
                if (juce::Time::getMillisecondCounterHiRes() > deadline) { result = "Limite de 120 segundos de processamento atingido; WAV atual nao publicado."; return false; }
                setProgress((task + double(i) / (frames + latency)) / tasks);
            }
            const auto audio = renderer.frame(i);
            if (i >= latency) {
                writtenLoudness.push(audio.left, audio.right);
                if (bitDepth == 32) {
                    stream->writeFloat(std::isfinite(audio.left) ? audio.left : 0.0f);
                    stream->writeFloat(std::isfinite(audio.right) ? audio.right : 0.0f);
                } else if (bitDepth == 24) {
                    int channel = 0;
                    for (const float sample : {audio.left, audio.right}) {
                        const auto value = static_cast<std::uint32_t>((channel++ == 0 ? leftDither : rightDither).quantize(sample));
                        const unsigned char bytes[] = {static_cast<unsigned char>(value),
                            static_cast<unsigned char>(value >> 8), static_cast<unsigned char>(value >> 16)};
                        stream->write(bytes, 3);
                    }
                } else {
                    stream->writeShort(static_cast<short>(leftDither.quantize(audio.left)));
                    stream->writeShort(static_cast<short>(rightDither.quantize(audio.right)));
                }
            }
            if (stream->getStatus().failed()) { result = "Falha ao escrever o WAV."; return false; }
        }
        stream->flush();
        if (stream->getStatus().failed()) { result = "Falha ao finalizar o WAV."; return false; }
        stream.reset();
        if (threadShouldExit()) return false;
        if (!temporary.overwriteTargetFileWithTemporary()) {
            result = "Falha ao concluir o WAV: " + target.getFullPathName();
            return false;
        }
        const auto measured = writtenLoudness.readings();
        summaries += "\n" + target.getFileName() + " | I " +
            (measured.integratedBlocks == 0 ? juce::String("-- (<400 ms)") : measured.integratedLufs <= -70 ? juce::String("<=-70") : juce::String(measured.integratedLufs,1)) +
            " LUFS" + (preMaster ? " (antes do MASTER)" : " (apos MASTER)");
        return true;
    }
    std::unique_ptr<risco::Song> song;
    juce::File file;
    int selectedStem = -1;
    int finishedFiles = 0;
    int bitDepth = 16;
    bool preMaster = false;
    double deadline = 0;
    juce::String summaries;
    juce::String result = "Exportacao interrompida.";
};
