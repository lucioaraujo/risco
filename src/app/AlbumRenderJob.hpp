// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Lúcio de Araújo
#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "core/AlbumManifest.hpp"
#include "AlbumRenderQueue.hpp"

#include <atomic>
#include <functional>
#include <string>
#include <utility>

class AlbumRenderJob final : public juce::ThreadWithProgressWindow {
public:
    AlbumRenderJob(risco::AlbumManifest initial, juce::File folder, juce::Component* parent)
        : ThreadWithProgressWindow(juce::String::fromUTF8("Renderizar WAV do álbum"),
                                   true, true, 10000, "Cancelar", parent),
          manifest(std::move(initial)), directory(std::move(folder)) {
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
            });
        resultMessage = juce::String::fromUTF8(result.message.c_str());
        wasCancelled = result.cancelled;
    }

    void threadComplete(bool cancelled) override {
        if (completed) completed((cancelled || wasCancelled)
            ? juce::String::fromUTF8("Render do álbum cancelado. WAVs concluídos foram preservados.")
            : resultMessage);
    }

private:
    risco::AlbumManifest manifest;
    juce::File directory;
    juce::CriticalSection activityLock;
    juce::String activityStage = juce::String::fromUTF8("Preparando o álbum");
    std::atomic<int> activityPercent{0};
    juce::String resultMessage = "Render de álbum interrompido.";
    bool wasCancelled = false;
};
