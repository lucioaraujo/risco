// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Lúcio de Araújo
#pragma once

#include "Song.hpp"
#include "Sound.hpp"
#include "HistoricalAyEngine.hpp"
#include <array>

namespace risco {

struct PlaybackPlacement {
    Sequence cell;
    BytebeatProgram formula;
    bool bytebeatFormula = false;
    std::uint32_t cellId = 0;
    int gridIndex = -1;
    int stem = 0;
    double start = 0.0;
    double duration = 0.0;
    double playLength = 0.0;
    double sourceStart = 0.0;
    double formulaTimeOffset = 0.0; // Pré-escuta pode iniciar em outro trecho sem alterar t global da grade.
    int repeats = 1;
};

struct PlaybackResponse {
    Sequence cell;
    double start = 0.0;
    int stem = 1;
    int sourceSection = -1; // V6 section index; -1 uses legacy song-bar windows.
    std::uint32_t harmonicReferenceId = 0; // Explicit sounding harmony at the response entrance.
};

// Build off the audio thread. The snapshot owns all notes needed for playback.
struct SongPlayback {
    bool applyStemAutomation = true; // Cell-only preview keeps its original dynamics.
    std::array<PlaybackPlacement, 128> placements{};
    int count = 0;
    std::array<PlaybackResponse, 64> responses{};
    int responseCount = 0;
    int responsesSkipped = 0;
    static constexpr std::uint32_t gapCheckLimit = 1000000;
    std::uint32_t responseGapChecks = 0;
    int responseNotesRejected = 0;
    bool responseGapBudgetExceeded = false;
    double end = 0.0;
    int bitDepth = 8;
    int timbreMode = 0;
    int ayNoisePeriod = 16;
    int ayEnvelopeMode = 0;
    float ayEnvelopeRamp = 0.5f;
    HistoricalAySettings historicalAySettings{};
};

SongPlayback prepareSongPlayback(const Song& song);
float songSampleAt(const SongPlayback& playback, double seconds, WaveMode mode,
                   const BytebeatProgram* customProgram = nullptr,
                   double continuousSeconds = -1.0) noexcept;
float songStemSampleAt(const SongPlayback& playback, double seconds, int stem, WaveMode mode,
                       const BytebeatProgram* customProgram = nullptr,
                       double continuousSeconds = -1.0, int ayNoisePeriod = -1,
                       int ayEnvelopeMode = -1, float ayEnvelopeRamp = -1) noexcept;
float songStemDirectFormulaSampleAt(const SongPlayback& playback, double seconds, int stem,
                                    double continuousSeconds = -1.0) noexcept;

}
