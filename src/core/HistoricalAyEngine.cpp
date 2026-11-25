// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Lúcio de Araújo
#include "HistoricalAyEngine.hpp"
#include "SongPlayback.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace risco {
namespace {
struct ActiveNote { const Note* note = nullptr; int index = -1; };

bool later(const ActiveNote& a, const ActiveNote& b) noexcept {
    if (a.note == nullptr) return false;
    if (b.note == nullptr) return true;
    if (a.note->start != b.note->start) return a.note->start > b.note->start;
    return a.index < b.index;
}

int envelopeShape(int preset) noexcept {
    constexpr int shapes[] = {0, 9, 13, 14, 8};
    return shapes[std::clamp(preset, 0, 4)];
}
}

bool HistoricalAyEngine::prepare(HistoricalAyModel newModel, double newSampleRate,
                                 double newClockRate) noexcept {
    if (!std::isfinite(newSampleRate) || newSampleRate < 8000.0 || newSampleRate > 384000.0 ||
        !std::isfinite(newClockRate) || newClockRate < 100000.0 || newClockRate > 10000000.0)
        return false;
    selectedModel = newModel;
    configuredSampleRate = newSampleRate;
    configuredClockRate = newClockRate;
    configured = true;
    reset();
    return configured;
}

void HistoricalAyEngine::reset() noexcept {
    if (!configured) return;
    std::memset(&chip, 0, sizeof(chip));
    configured = ayumi_configure(&chip, selectedModel == HistoricalAyModel::ym2149 ? 1 : 0,
                                 configuredClockRate, int(std::lround(configuredSampleRate))) != 0;
    if (!configured) return;
    for (int channel = 0; channel < 3; ++channel) {
        ayumi_set_pan(&chip, channel, 0.5, 0);
        ayumi_set_tone(&chip, channel, 1);
        ayumi_set_mixer(&chip, channel, 1, 1, 0);
        ayumi_set_volume(&chip, channel, 0);
        voices[static_cast<std::size_t>(channel)] = {};
    }
    ayumi_set_noise(&chip, 1);
    ayumi_set_envelope(&chip, 1);
    appliedNoisePeriod = appliedEnvelopePeriod = appliedEnvelopePreset = -1;
    previousSeconds = -1.0;
}

void HistoricalAyEngine::silenceVoices() noexcept {
    for (int channel = 0; channel < 3; ++channel) {
        auto& state = voices[static_cast<std::size_t>(channel)];
        if (state.enabled || state.level != 0) {
            ayumi_set_mixer(&chip, channel, 1, 1, 0);
            ayumi_set_volume(&chip, channel, 0);
            state.enabled = false;
            state.level = 0;
            state.mixerKey = -1;
            state.attack = -1.0;
        }
    }
}

HistoricalAyFrame HistoricalAyEngine::process(const Sequence& sequence, double seconds,
                                               HistoricalAySettings settings) noexcept {
    if (!configured || !std::isfinite(seconds)) return {};
    seconds = std::round(seconds * configuredSampleRate) / configuredSampleRate;
    if (seconds < 0.0 || seconds >= sequence.end) {
        silenceVoices();
        ayumi_process(&chip); ayumi_remove_dc(&chip);
        return {float(chip.left * 0.5), float(chip.right * 0.5)};
    }
    if (sequence.repeatPeriod > 0.0 && std::isfinite(sequence.repeatPeriod))
        seconds = std::fmod(seconds, sequence.repeatPeriod);
    if (previousSeconds >= 0.0 && seconds + 1.0e-12 < previousSeconds) reset();
    previousSeconds = seconds;

    settings.texture = std::clamp(settings.texture, 0, 2);
    settings.noisePeriod = std::clamp(settings.noisePeriod, 1, 31);
    settings.envelope = std::clamp(settings.envelope, 0, 4);
    settings.envelopeRamp = std::clamp(std::isfinite(settings.envelopeRamp)
        ? settings.envelopeRamp : 0.5f, 0.125f, 2.0f);
    const int envelopePeriod = std::clamp(int(std::lround(
        double(settings.envelopeRamp) * configuredClockRate / 4096.0)), 1, 65535);
    if (settings.noisePeriod != appliedNoisePeriod) {
        ayumi_set_noise(&chip, settings.noisePeriod);
        appliedNoisePeriod = settings.noisePeriod;
    }
    if (envelopePeriod != appliedEnvelopePeriod) {
        ayumi_set_envelope(&chip, envelopePeriod);
        appliedEnvelopePeriod = envelopePeriod;
    }

    std::array<ActiveNote, 3> active{};
    const int count = std::clamp(sequence.count, 0, int(sequence.notes.size()));
    for (int index = 0; index < count; ++index) {
        const auto& note = sequence.notes[static_cast<std::size_t>(index)];
        const double local = seconds - note.start;
        if (local < 0.0 || local >= note.duration || note.duration <= 0.0 ||
            !std::isfinite(note.frequency) || note.frequency <= 0.0 || note.gain <= 0.0f) continue;
        ActiveNote candidate{&note, index};
        for (std::size_t slot = 0; slot < active.size(); ++slot) {
            if (later(candidate, active[slot])) {
                for (std::size_t move = active.size() - 1; move > slot; --move)
                    active[move] = active[move - 1];
                active[slot] = candidate;
                break;
            }
        }
    }

    bool envelopeAttack = settings.envelope != appliedEnvelopePreset;
    bool anyActive = false;
    for (int channel = 0; channel < 3; ++channel) {
        auto& state = voices[static_cast<std::size_t>(channel)];
        const auto selected = active[static_cast<std::size_t>(channel)];
        if (selected.note == nullptr) {
            if (state.enabled || state.level != 0) {
                ayumi_set_mixer(&chip, channel, 1, 1, 0);
                ayumi_set_volume(&chip, channel, 0);
                state.enabled = false; state.level = 0;
                state.mixerKey = -1; state.attack = -1.0;
            }
            continue;
        }
        const auto& note = *selected.note;
        const double local = seconds - note.start;
        const double edge = std::clamp(std::min({local / 0.002,
            (note.duration - local) / 0.005, 1.0}), 0.0, 1.0);
        const int period = std::clamp(int(std::lround(configuredClockRate /
            (16.0 * note.frequency))), 1, 4095);
        const int level = std::clamp(int(std::lround(double(note.gain) * edge * 60.0)), 0, 15);
        const int mixerKey = settings.texture * 2 + (settings.envelope > 0 ? 1 : 0);
        if (period != state.period) { ayumi_set_tone(&chip, channel, period); state.period = period; }
        if (level != state.level) { ayumi_set_volume(&chip, channel, level); state.level = level; }
        if (!state.enabled || mixerKey != state.mixerKey) {
            ayumi_set_mixer(&chip, channel, settings.texture == 2, settings.texture == 0,
                            settings.envelope > 0);
            state.enabled = true;
            state.mixerKey = mixerKey;
        }
        if (state.attack != note.start) envelopeAttack = true;
        state.attack = note.start;
        anyActive = true;
    }
    if (settings.envelope > 0 && anyActive && envelopeAttack)
        ayumi_set_envelope_shape(&chip, envelopeShape(settings.envelope));
    appliedEnvelopePreset = settings.envelope;
    ayumi_process(&chip); ayumi_remove_dc(&chip);
    return {float(chip.left * 0.5), float(chip.right * 0.5)};
}

HistoricalAyFrame HistoricalAyEngine::processStem(const SongPlayback& playback, double seconds,
                                                   int stem, HistoricalAySettings settings) noexcept {
    if (configured && std::isfinite(seconds))
        seconds = std::round(seconds * configuredSampleRate) / configuredSampleRate;
    Sequence active;
    active.end = std::max(playback.end, seconds + 1.0);
    if (!std::isfinite(seconds) || seconds < 0.0 || seconds >= playback.end ||
        stem < 0 || stem >= Song::stemCount)
        return process(active, seconds, settings);

    const auto consider = [&](const Note& source, double age) {
        if (!std::isfinite(age) || age < 0.0 || age >= source.duration ||
            !std::isfinite(source.frequency) || source.frequency <= 0.0 ||
            !std::isfinite(source.gain) || source.gain <= 0.0f) return;
        Note candidate = source;
        candidate.start = seconds - age;
        int insert = active.count;
        for (int i = 0; i < active.count; ++i) {
            if (candidate.start > active.notes[static_cast<std::size_t>(i)].start) {
                insert = i; break;
            }
        }
        if (insert >= 3) return;
        const int last = std::min(active.count, 2);
        for (int move = last; move > insert; --move)
            active.notes[static_cast<std::size_t>(move)] =
                active.notes[static_cast<std::size_t>(move - 1)];
        active.notes[static_cast<std::size_t>(insert)] = candidate;
        active.count = std::min(active.count + 1, 3);
    };

    for (int i = 0; i < std::clamp(playback.count, 0, int(playback.placements.size())); ++i) {
        const auto& placement = playback.placements[static_cast<std::size_t>(i)];
        if (placement.stem != stem || placement.bytebeatFormula || placement.duration <= 0.0) continue;
        const double local = seconds - placement.start;
        if (local < 0.0 || local >= placement.playLength) continue;
        const double within = std::fmod(local + placement.sourceStart, placement.duration);
        for (int note = 0; note < std::clamp(placement.cell.count, 0, int(placement.cell.notes.size())); ++note) {
            const auto& source = placement.cell.notes[static_cast<std::size_t>(note)];
            consider(source, within - source.start);
        }
    }
    for (int i = 0; i < std::clamp(playback.responseCount, 0, int(playback.responses.size())); ++i) {
        const auto& response = playback.responses[static_cast<std::size_t>(i)];
        if (response.stem != stem) continue;
        const double local = seconds - response.start;
        if (local < 0.0 || local >= response.cell.end) continue;
        for (int note = 0; note < std::clamp(response.cell.count, 0, int(response.cell.notes.size())); ++note) {
            const auto& source = response.cell.notes[static_cast<std::size_t>(note)];
            consider(source, local - source.start);
        }
    }
    return process(active, seconds, settings);
}

}
