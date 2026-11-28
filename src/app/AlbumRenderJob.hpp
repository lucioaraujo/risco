// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Lúcio de Araújo
#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "core/AlbumManifest.hpp"
#include "AlbumRenderQueue.hpp"

#include <atomic>
#include <algorithm>
#include <functional>
#include <string>
#include <utility>

class AlbumRenderJob final : public juce::ThreadWithProgressWindow {
public:
    AlbumRenderJob(risco::AlbumManifest initial, juce::File folder, juce::Component* parent,
                   int uiLanguage)
        : ThreadWithProgressWindow(text(uiLanguage, "Renderizar WAV do álbum", "Render album WAVs", "Rendre les WAV de l’album", "Renderizar WAV del álbum"),
                                   true, true, 10000,
                                   text(uiLanguage, "Cancelar", "Cancel", "Annuler", "Cancelar"), parent),
          manifest(std::move(initial)), directory(std::move(folder)),
          language(std::clamp(uiLanguage, 0, 3)),
          activityStage(text(uiLanguage, "Preparando o álbum", "Preparing album", "Préparation de l’album", "Preparando el álbum")),
          resultMessage(text(uiLanguage, "Render de álbum interrompido.", "Album render interrupted.", "Rendu de l’album interrompu.", "Render del álbum interrumpido.")) {
        setStatusMessage(activityStage);
    }
    ~AlbumRenderJob() override { signalThreadShouldExit(); stopThread(10000); }

    std::function<void(juce::String)> completed;

    juce::String activityText() const {
        const juce::ScopedLock lock(activityLock);
        return activityStage + juce::String::fromUTF8(" · ") +
               juce::String(activityPercent.load(std::memory_order_relaxed)) + "%";
    }

    void run() override {
        const auto result = risco::app::runAlbumRenderQueue(manifest, directory,
            [this] { return threadShouldExit(); },
            [this](double fraction) {
                setProgress(fraction);
                activityPercent.store(juce::roundToInt(fraction * 100.0), std::memory_order_relaxed);
            },
            [this](const std::string& message) {
                const auto stage = juce::String::fromUTF8(message.c_str());
                setStatusMessage(stage);
                const juce::ScopedLock lock(activityLock);
                activityStage = stage;
            }, language);
        resultMessage = juce::String::fromUTF8(result.message.c_str());
        wasCancelled = result.cancelled;
    }

    void threadComplete(bool cancelled) override {
        if (completed) completed((cancelled || wasCancelled)
            ? text(language, "Render do álbum cancelado. WAVs concluídos foram preservados.", "Album render cancelled. Completed WAVs were preserved.", "Rendu de l’album annulé. Les WAV terminés ont été conservés.", "Render del álbum cancelado. Se conservaron los WAV terminados.")
            : resultMessage);
    }

private:
    static juce::String text(int language, const char* pt, const char* en,
                             const char* fr, const char* es) {
        const char* values[] = {pt, en, fr, es};
        return juce::String::fromUTF8(values[std::clamp(language, 0, 3)]);
    }

    risco::AlbumManifest manifest;
    juce::File directory;
    int language = 0;
    juce::CriticalSection activityLock;
    juce::String activityStage;
    std::atomic<int> activityPercent{0};
    juce::String resultMessage;
    bool wasCancelled = false;
};
