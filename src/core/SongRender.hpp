// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Lúcio de Araújo
#pragma once
#include "SongPlayback.hpp"
#include "HistoricalAyEngine.hpp"
#include "OutputStage.hpp"
#include "StemEffect.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace risco {
// The caller keeps the frozen song and playback alive for this renderer.
// Consume frames in order; discard latencySamples() frames, then flush with silence.
class SongRender final {
public:
    static constexpr std::uint32_t rate = 48000;
    static constexpr double tailSeconds = 0.1;
    static constexpr double maximumSongSeconds = 600.0;
    static bool validDuration(double seconds) noexcept { return std::isfinite(seconds) && seconds > 0 && seconds <= maximumSongSeconds; }
    static double tailSecondsFor(const Song& song) noexcept {
        double tail = tailSeconds;
        const double bpm = std::clamp(std::isfinite(song.bpm) ? song.bpm : 120.0, 40.0, 240.0);
        for (std::size_t s = 0; s < song.stemEffect.size(); ++s) {
            const int effect = song.stemEffect[s];
            if (effect == 8 || effect == 9) {
                const double decay = std::clamp(std::isfinite(song.stemReverbDecay[s]) ? double(song.stemReverbDecay[s]) : 0.0, 0.0, 3.0);
                tail = std::max(tail, std::max(4.1, 2 * decay + 0.1));
                if (song.stemReverbDecayAutomation[s].enabled)
                    for (float point : song.stemReverbDecayAutomation[s].values)
                        if (std::isfinite(point)) tail = std::max(tail, std::max(4.1, 2 * std::clamp(double(point), 0.0, 3.0) + 0.1));
            }
            double feedback = std::clamp(std::isfinite(song.stemDelayFeedback[s]) ? double(song.stemDelayFeedback[s]) : 0.35, 0.0, 0.6);
            if (song.stemDelayFeedbackAutomation[s].enabled)
                for (float point : song.stemDelayFeedbackAutomation[s].values)
                    if (std::isfinite(point)) feedback = std::max(feedback, double(std::clamp(point, 0.0f, 0.6f)));
            const double repeats = feedback > 0 ? std::max(10.0, std::ceil(std::log(0.001) / std::log(feedback))) : 10.0;
            if (effect >= 5 && effect <= 7)
                tail = std::max(tail, tailSeconds + repeats * 60.0 / bpm * (effect == 5 ? 0.5 : effect == 6 ? 0.75 : 1.0));
        }
        return tail;
    }
    SongRender(const Song& source, const SongPlayback& prepared, int stem = -1,
               bool beforeMaster = false, double rateHz = rate)
        : song(source), playback(prepared), selectedStem(stem), preMaster(beforeMaster),
          sampleRate(rateHz) {
        output.prepare(sampleRate);
        for (int s = 0; s < Song::stemCount; ++s) {
            const auto index = static_cast<std::size_t>(s);
            effects[index].prepare(sampleRate);
            automation[index].prepare(sampleRate);
            panAutomation[index].prepare(sampleRate);
            curves[index] = song.stemAutomation[index];
            panCurves[index] = song.stemPanAutomation[index];
            fxCurves[index] = song.stemFxAutomation[index];
            filterCurves[index] = song.stemFilterAutomation[index];
            delayMixCurves[index] = song.stemDelayMixAutomation[index];
            delayFeedbackCurves[index] = song.stemDelayFeedbackAutomation[index];
            reverbMixCurves[index] = song.stemReverbMixAutomation[index];
            reverbDecayCurves[index] = song.stemReverbDecayAutomation[index];
            bitcrushCurves[index] = song.stemBitcrushAutomation[index];
            effectPresets[index] = song.stemEffect[index];
            filterBase[index] = song.stemFilterCutoff[index];
            delayMixBase[index] = song.stemDelayMix[index];
            delayFeedbackBase[index] = song.stemDelayFeedback[index];
            reverbMixBase[index] = song.stemReverbMix[index];
            if (!playback.applyStemAutomation) {
                curves[index].enabled = false;
                panCurves[index].enabled = false;
                fxCurves[index].enabled = false;
                filterCurves[index].enabled = false;
                delayMixCurves[index].enabled = false;
                delayFeedbackCurves[index].enabled = false;
                reverbMixCurves[index].enabled = false;
                reverbDecayCurves[index].enabled = false;
                bitcrushCurves[index].enabled = false;
            }
            effects[index].setPreset(effectPresets[index]);
            effects[index].setBitcrushBits(song.stemBitcrushBits[index]);
            effects[index].setFilterCutoff(filterBase[index]);
            effects[index].setReverbMix(reverbMixBase[index]);
            effects[index].setReverbDecay(song.stemReverbDecay[index]);
            effects[index].setTempo(song.bpm);
            effects[index].setDelayParameters(delayMixBase[index], delayFeedbackBase[index]);
            if (isHistoricalAyTimbre(playback.timbreMode))
                historicalAy[index].prepare(historicalAyModel(playback.timbreMode), sampleRate);
        }
    }
    std::uint32_t latencySamples() const noexcept { return preMaster ? 0 : output.latencySamples(); }
    OutputStage::Meter meter() const noexcept { return output.meter(); }
    OutputStage::Meter takeMeterWindow() noexcept { return output.takeMeterWindow(); }
    static std::int16_t pcm16(float value) noexcept {
        if (!std::isfinite(value)) return 0;
        return static_cast<std::int16_t>(std::lround(std::clamp(value, -1.0f, 1.0f) * 32767.0f));
    }
    static std::int32_t pcm24(float value) noexcept {
        if (!std::isfinite(value)) return 0;
        return static_cast<std::int32_t>(std::lround(std::clamp(value, -1.0f, 1.0f) * 8388607.0));
    }
    OutputStage::Stereo frame(std::uint32_t sample) noexcept {
        const auto mode = noteWaveMode(playback.timbreMode);
        const bool historical = isHistoricalAyTimbre(playback.timbreMode);
        float left = 0.0f, right = 0.0f;
        for (int s = 0; s < Song::stemCount; ++s) {
            if (selectedStem >= 0 && selectedStem != s) continue;
            const auto index = static_cast<std::size_t>(s);
            const double seconds = double(sample) / sampleRate;
            const float envelope = automation[index].process(curves[index], seconds, playback.end);
            effects[index].setPreset(automationFxActiveAt(fxCurves[index],seconds,playback.end) ? effectPresets[index] : 0);
            effects[index].setFilterCutoff(automationParameterAt(filterCurves[index], seconds, playback.end,
                filterBase[index], 80.0f, 16000.0f));
            effects[index].setDelayParameters(
                automationParameterAt(delayMixCurves[index], seconds, playback.end, delayMixBase[index], 0.0f, 0.6f),
                automationParameterAt(delayFeedbackCurves[index], seconds, playback.end, delayFeedbackBase[index], 0.0f, 0.6f));
            effects[index].setReverbMix(automationParameterAt(reverbMixCurves[index], seconds, playback.end,
                reverbMixBase[index], 0.0f, 0.6f));
            if (reverbDecayCurves[index].enabled)
                effects[index].setReverbDecay(automationReverbDecayAt(reverbDecayCurves[index], seconds, playback.end,
                    song.stemReverbDecay[index]));
            if (bitcrushCurves[index].enabled)
                effects[index].setBitcrushBits(automationBitcrushBitsAt(bitcrushCurves[index], seconds, playback.end,
                    song.stemBitcrushBits[index]));
            float source = 0.0f;
            if (historical) {
                const auto chip = historicalAy[index].processStem(playback, seconds, s,
                    playback.historicalAySettings);
                source = (chip.left + chip.right) * 0.5f +
                    songStemDirectFormulaSampleAt(playback, seconds, s);
            } else source = songStemSampleAt(playback, seconds, s, mode);
            const float raw = effects[index].process(source * envelope);
            const float pan = panAutomation[index].process(panCurves[index],seconds,playback.end,song.stemPan[index]);
            const float angle = (pan+1.0f)*0.78539816339f;
            left += raw * song.stemGain[index] * std::cos(angle);
            right += raw * song.stemGain[index] * std::sin(angle);
        }
        if (preMaster)
            return {std::isfinite(left) ? left : 0.0f, std::isfinite(right) ? right : 0.0f};
        if (song.shapeMode != 0) {
            left = shapeSample(std::clamp(left, -1.0f, 1.0f), song.shapeMode, song.shapeDrive);
            right = shapeSample(std::clamp(right, -1.0f, 1.0f), song.shapeMode, song.shapeDrive);
        }
        return output.process(left, right, song.masterGain);
    }
private:
    const Song& song;
    const SongPlayback& playback;
    int selectedStem;
    bool preMaster;
    double sampleRate;
    OutputStage output;
    std::array<StemEffect, Song::stemCount> effects;
    std::array<StemAutomation, Song::stemCount> automation;
    std::array<StemPanAutomation, Song::stemCount> panAutomation;
    std::array<GainAutomation, Song::stemCount> curves;
    std::array<PanAutomation, Song::stemCount> panCurves;
    std::array<FxAutomation, Song::stemCount> fxCurves;
    std::array<ParameterAutomation, Song::stemCount> filterCurves, delayMixCurves, delayFeedbackCurves, reverbMixCurves, reverbDecayCurves, bitcrushCurves;
    std::array<int, Song::stemCount> effectPresets{};
    std::array<float, Song::stemCount> filterBase{}, delayMixBase{}, delayFeedbackBase{}, reverbMixBase{};
    std::array<HistoricalAyEngine, Song::stemCount> historicalAy;
};
}
