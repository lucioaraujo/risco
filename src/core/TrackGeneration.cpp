// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Lúcio de Araújo
#include "TrackGeneration.hpp"

#include "FormulaSeed.hpp"
#include "SongIO.hpp"
#include "Sound.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace risco {
namespace {

std::uint64_t mixSeed(std::uint64_t value) noexcept {
    value += 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31);
}

bool isMotifStem(int stem) noexcept {
    return stem == 0 || stem == 2 || stem == 4;
}

bool hasPartEdits(const SeedTrackOptions& options) noexcept {
    return std::any_of(options.partEdits.begin(), options.partEdits.end(),
        [](const SeedTrackPartEdit& edit) { return !edit.automatic(); });
}

Sequence voiceMotif(const SeedTrackMotif& motif, const Sequence& accompaniment,
                    int stem, double bpm) noexcept {
    Sequence result;
    result.end = accompaniment.end;
    const double halfBeat = 30.0 / bpm;
    const auto& anchor = accompaniment.notes.front();
    const double anchorFrequency = stem == 4 ? anchor.frequency * 0.5 : anchor.frequency;
    const auto role = stem == 2 ? VoiceRole::bass : (stem == 4 ? VoiceRole::harmony : VoiceRole::lead);
    for (int i = 0; i < motif.count; ++i) {
        const auto& source = motif.notes[static_cast<std::size_t>(i)];
        const int nextTick = i + 1 < motif.count ? motif.notes[static_cast<std::size_t>(i + 1)].tick : source.tick + 2;
        const double start = source.tick * halfBeat;
        const auto articulation = stem == 4 ? 0.94 : (stem == 2 ? 0.70 : 0.82);
        const double duration = std::min(result.end - start, (nextTick - source.tick) * halfBeat * articulation);
        const double frequency = anchorFrequency * std::exp2(source.semitones / 12.0);
        const float gain = stem == 4 ? 0.25f : (stem == 2 ? 0.42f : 0.48f);
        result.notes[static_cast<std::size_t>(result.count++)] = {
            start, duration, frequency, gain, role, anchor.pulseDuty};
    }
    return result;
}

SeedTrackMotifReturn effectiveMotifReturn(const SeedTrackOptions& options) noexcept {
    if (options.motifReturn != SeedTrackMotifReturn::automatic) return options.motifReturn;
    return options.surprise <= 0 ? SeedTrackMotifReturn::literal :
        options.surprise == 1 ? SeedTrackMotifReturn::mirror : SeedTrackMotifReturn::mirrorRetrograde;
}

SeedTrackMotif developedReturnMotif(const SeedTrackMotif& motif, SeedTrackMotifReturn transform) noexcept {
    if (transform == SeedTrackMotifReturn::literal || motif.count <= 1) return motif;
    SeedTrackMotif result = motif;
    if (transform == SeedTrackMotifReturn::mirror) {
        for (int i = 0; i < motif.count; ++i)
            result.notes[static_cast<std::size_t>(i)].semitones =
                -motif.notes[static_cast<std::size_t>(i)].semitones;
        return result;
    }
    const auto& last = motif.notes[static_cast<std::size_t>(motif.count - 1)];
    for (int i = 0; i < motif.count; ++i) {
        const auto& source = motif.notes[static_cast<std::size_t>(motif.count - 1 - i)];
        result.notes[static_cast<std::size_t>(i)] = {
            last.tick - source.tick,
            last.semitones - source.semitones};
    }
    return result;
}

Sequence roleSequence(const Sequence& source, VoiceRole role) noexcept {
    Sequence result;
    result.end = source.end;
    for (int index = 0; index < std::clamp(source.count, 0, int(source.notes.size())); ++index) {
        const auto& note = source.notes[static_cast<size_t>(index)];
        if (note.role == role && result.count < int(result.notes.size())) {
            auto adjusted = note;
            if (!std::isfinite(adjusted.start) || !std::isfinite(adjusted.duration) ||
                adjusted.start < 0.0 || adjusted.start >= source.end) continue;
            adjusted.duration = std::min(adjusted.duration, source.end - adjusted.start);
            if (adjusted.duration <= 0.0) continue;
            const float roleGain = role == VoiceRole::harmony ? 3.0f :
                                   role == VoiceRole::bass ? 1.5f : 1.0f;
            adjusted.gain = std::min(0.9f, adjusted.gain * roleGain);
            result.notes[static_cast<size_t>(result.count++)] = adjusted;
        }
    }
    return result;
}

Sequence rhythmSequence(const Sequence& source, double bpm, int bars,
                        std::uint64_t seed, int surprise) noexcept {
    Sequence result;
    result.end = source.end;
    const double beat = 60.0 / bpm;
    auto add = [&](double start, double duration, double frequency, float gain) {
        if (result.count >= int(result.notes.size()) || start >= result.end) return;
        result.notes[static_cast<size_t>(result.count++)] = {
            start, std::min(duration, result.end - start), frequency, gain,
            VoiceRole::lead, 0.22f};
    };
    for (int index = 0; index < bars; ++index) {
        const double barStart = index * beat * 4.0;
        for (int pulse = 0; pulse < 4; ++pulse) {
            const bool downbeat = pulse == 0 || (pulse == 2 && ((mixSeed(seed ^ std::uint64_t(index)) & 3u) != 0));
            const bool backbeat = pulse == 1 || pulse == 3;
            if (!downbeat && !backbeat) continue;
            add(barStart + pulse * beat, downbeat ? 0.16 : 0.095,
                downbeat ? 58.0 : 164.0, downbeat ? 0.34f : 0.23f);
            const auto hatField = seed ^ (std::uint64_t(index) * 0x4841540000000001ULL) ^
                                  (std::uint64_t(pulse) * 0x9e3779b97f4a7c15ULL);
            if (mixSeed(hatField) % 4u < static_cast<unsigned>(surprise + 1))
                add(barStart + (pulse + 0.5) * beat, 0.045, 880.0, 0.075f);
        }
    }
    return result;
}

Sequence textureSequence(const Sequence& source, double bpm) noexcept {
    auto result = roleSequence(source, VoiceRole::harmony);
    const double sustain = 60.0 / bpm * 2.0;
    for (int index = 0; index < result.count; ++index) {
        auto& note = result.notes[static_cast<size_t>(index)];
        const double nextStart = index + 1 < result.count
            ? result.notes[static_cast<size_t>(index + 1)].start : result.end;
        note.duration = std::min(std::max(note.duration, sustain),
                                 std::max(0.02, nextStart - note.start));
        note.gain = std::min(0.28f, note.gain * 0.24f);
    }
    return result;
}

std::array<std::int16_t, Cell::waveformSize> waveformFor(const Sequence& sequence, WaveMode mode) noexcept {
    std::array<std::int16_t, Cell::waveformSize> result{};
    for (int index = 0; index < Cell::waveformSize; ++index) {
        const auto time = sequence.end * (double(index) + 0.5) / Cell::waveformSize;
        result[static_cast<size_t>(index)] = static_cast<std::int16_t>(std::lround(
            sampleAt(sequence, time, mode, nullptr, 16) * 32767.0f));
    }
    return result;
}

Sequence shapedPhrase(std::uint64_t seed, const SeedTrackOptions& options, int block,
                      int section, bool reprise, double bpm) {
    const auto motif = mixSeed(seed ^ 0x4d4f544946415243ULL);
    auto phraseSeed = motif;
    unsigned variation = static_cast<unsigned>(block % 4);
    unsigned version = 8;
    if (reprise) {
        variation = 0;
    } else if (options.form == SeedTrackForm::collage && block > 0) {
        const auto group = static_cast<std::uint64_t>(block / (3 - options.surprise));
        phraseSeed = mixSeed(motif ^ (0x434f4c4c414745ULL + group));
        version = 7u + static_cast<unsigned>(phraseSeed % 3);
    } else if (section == 2 && options.surprise > 0) {
        phraseSeed = mixSeed(motif ^ (block / (3 - options.surprise) + 0x434f4e5452415354ULL));
        version = 9;
    }
    return phraseFromSeed(phraseSeed, variation, version, bpm, 0x0f, 4, false, {});
}

void shapeTrackRole(Sequence& events, VoiceRole role, int section, bool ending,
                    std::uint64_t blockSeed, const SeedTrackOptions& options) {
    const bool breakInMiddle = section == 2 && options.surprise > 0 &&
        blockSeed % 4 < static_cast<unsigned>(options.surprise);
    const float weight = ending ? 0.72f :
        section == 0 && role != VoiceRole::lead ? 0.60f :
        section == 2 && role == VoiceRole::lead ? 0.80f : 1.0f;
    const double cutoff = ending ? events.end * 0.75 :
                          breakInMiddle ? events.end * 0.62 : events.end;
    int kept = 0;
    for (int index = 0; index < events.count; ++index) {
        auto note = events.notes[static_cast<std::size_t>(index)];
        if (note.start >= cutoff) continue;
        note.duration = std::min(note.duration, cutoff - note.start);
        note.gain = std::clamp(note.gain * weight, 0.05f, 0.9f);
        events.notes[static_cast<std::size_t>(kept++)] = note;
    }
    if (kept > 0) events.count = kept;
}

void shapePulse(Sequence& events, VoiceRole role, SeedTrackPulse pulse, double bpm) {
    if (pulse == SeedTrackPulse::straight) return;
    const double beatSeconds = 60.0 / bpm;
    const auto swingTime = [beatSeconds](double seconds) {
        const double beat = seconds / beatSeconds;
        const double whole = std::floor(beat);
        const double part = beat - whole;
        const double shaped = part < 0.5 ? part * 1.24 : 0.62 + (part - 0.5) * 0.76;
        return (whole + shaped) * beatSeconds;
    };
    for (int i = 0; i < events.count; ++i) {
        auto& note = events.notes[static_cast<std::size_t>(i)];
        if (pulse == SeedTrackPulse::swing) {
            const double end = std::min(events.end, swingTime(note.start + note.duration));
            note.start = swingTime(note.start);
            note.duration = end - note.start;
        } else {
            note.duration *= role == VoiceRole::harmony ? 0.83 : 0.66;
        }
    }
}

void applyTrackPalette(Song& song, SeedTrackPalette palette) {
    switch (palette) {
        case SeedTrackPalette::original: break;
        case SeedTrackPalette::resonant:
            song.timbreMode = 0;
            song.stemGain = {{0.76f, 0.74f, 0.70f, 0.52f, 0.42f}};
            song.stemPan = {{-0.22f, 0.26f, 0.0f, 0.0f, 0.24f}};
            song.stemEffect[1] = 8;
            song.stemReverbMix[1] = 0.21f;
            song.stemFxAutomation[1] = {};
            break;
        case SeedTrackPalette::chip:
            song.timbreMode = 2;
            song.stemGain = {{0.72f, 0.55f, 0.83f, 0.52f, 0.38f}};
            song.stemPan = {{0.22f, -0.18f, 0.0f, 0.0f, 0.24f}};
            song.stemEffect[0] = 5;
            song.stemDelayMix[0] = 0.14f;
            song.stemFxAutomation[0] = {true, {{false, true, false}}};
            break;
        case SeedTrackPalette::grain:
            song.stemGain = {{0.72f, 0.69f, 0.72f, 0.48f, 0.34f}};
            song.stemPan = {{-0.29f, 0.16f, 0.0f, 0.0f, 0.24f}};
            song.stemEffect[0] = 3;
            song.stemFxAutomation[0] = {true, {{true, false, true}}};
            song.stemEffect[1] = 2;
            song.stemFxAutomation[1] = {};
            break;
    }
}

} // namespace

SeedTrackCue seedTrackTextCue(std::string_view text) noexcept {
    bool hasText = false;
    bool foundPause = false;
    for (const char character : text) {
        if (std::string_view(",;:.!?\n").find(character) != std::string_view::npos) {
            foundPause = hasText;
        } else if (std::string_view(" \t\r\"").find(character) == std::string_view::npos) {
            if (foundPause) return SeedTrackCue::breath;
            hasText = true;
        }
    }
    return SeedTrackCue::none;
}

SeedTrackCue seedTrackDrawingCue(const Inscription& drawing) noexcept {
    float displacement = 0.0f;
    int strokes = 0;
    for (int i = 0; i < std::clamp(drawing.count, 0, int(drawing.strokes.size())); ++i) {
        const auto& stroke = drawing.strokes[static_cast<std::size_t>(i)];
        const int points = std::clamp(stroke.count, 0, int(stroke.points.size()));
        if (stroke.crossed || points < 2) continue;
        const float first = stroke.points.front().y;
        const float last = stroke.points[static_cast<std::size_t>(points - 1)].y;
        if (!std::isfinite(first) || !std::isfinite(last)) continue;
        displacement += std::clamp(first, 0.0f, 1.0f) - std::clamp(last, 0.0f, 1.0f);
        ++strokes;
    }
    if (strokes == 0) return SeedTrackCue::none;
    const auto slope = displacement / static_cast<float>(strokes);
    if (slope >= 0.12f) return SeedTrackCue::rise;
    if (slope <= -0.12f) return SeedTrackCue::fall;
    return SeedTrackCue::none;
}

SeedTrackMotif seedTrackMotif(const Sequence& source) noexcept {
    SeedTrackMotif motif;
    double firstFrequency = 0.0;
    double lastStart = 0.0;
    int tick = 0;
    const int count = std::clamp(source.count, 0, int(source.notes.size()));
    for (int i = 0; i < count && motif.count < int(motif.notes.size()); ++i) {
        const auto& note = source.notes[static_cast<std::size_t>(i)];
        if (note.role != VoiceRole::lead || !std::isfinite(note.frequency) || note.frequency < 1.0 ||
            !std::isfinite(note.start) || (motif.count > 0 && note.start <= lastStart)) continue;
        if (motif.count == 0) firstFrequency = note.frequency;
        else tick += static_cast<int>(std::lround(std::clamp((note.start - lastStart) * 5.0, 1.0, 4.0)));
        const double semitones = 12.0 * std::log2(note.frequency / firstFrequency);
        motif.notes[static_cast<std::size_t>(motif.count++)] = {
            tick, static_cast<int>(std::lround(std::clamp(semitones, -24.0, 24.0)))};
        lastStart = note.start;
    }
    return motif;
}

bool validSeedTrackMotif(const SeedTrackMotif& motif) noexcept {
    if (motif.count < 0 || motif.count > int(motif.notes.size()) ||
        !(motif.notes.front() == SeedTrackMotifNote{})) return false;
    for (int i = 1; i < int(motif.notes.size()); ++i) {
        const auto& note = motif.notes[static_cast<std::size_t>(i)];
        if (i >= motif.count) {
            if (!(note == SeedTrackMotifNote{})) return false;
            continue;
        }
        if (note.tick <= motif.notes[static_cast<std::size_t>(i - 1)].tick || note.tick > 16 ||
            note.semitones < -24 || note.semitones > 24) return false;
    }
    return true;
}

int alternateMotifStem(std::uint64_t seed, int openingStem) noexcept {
    if (!isMotifStem(openingStem)) return -1;
    const int step = 1 + static_cast<int>(mixSeed(seed ^ 0x4d4f54494652544eULL) % 2u);
    return ((openingStem / 2 + step) % 3) * 2;
}

std::array<SeedTrackPart, seedTrackPartCount> seedTrackParts(
    std::uint64_t seed, const SeedTrackOptions& options) noexcept {
    const int blocks = options.durationBars / 4;
    constexpr unsigned melody = 1u << 0;
    constexpr unsigned harmony = 1u << 1;
    constexpr unsigned bass = 1u << 2;
    constexpr unsigned rhythm = 1u << 3;
    constexpr unsigned texture = 1u << 4;
    constexpr unsigned all = (1u << Song::stemCount) - 1u;
    std::array<SeedTrackPart, seedTrackPartCount> parts{};
    if (!options.arrangeByParts) {
        parts[0] = {SeedTrackPartName::opening, 0, blocks, all};
        return parts;
    }

    const auto openingChoice = mixSeed(seed ^ 0x4f50454e494e4701ULL);
    const auto breakChoice = mixSeed(seed ^ 0x425245414b000001ULL);
    const unsigned openings[] = {melody, texture, melody | texture};
    const unsigned breaks[] = {melody, bass, texture};
    const bool sparseBreak = options.form == SeedTrackForm::collage ||
                             options.surprise == 2 || options.cue == SeedTrackCue::breath;
    const auto base = bass | rhythm;
    const unsigned development = base | (options.form == SeedTrackForm::collage
        ? harmony | texture : melody | (options.surprise == 0 ? harmony : 0u));
    const int developmentStart = (blocks + 3) / 4;
    const int breakStart = (blocks * 2 + 3) / 4;
    const int returnStart = (blocks * 3 + 3) / 4;
    const int endingStart = blocks >= 8 ? blocks - 1 : blocks;
    const bool falling = options.cue == SeedTrackCue::fall;
    const bool rising = options.cue == SeedTrackCue::rise;
    const auto motifChannel = options.motifStem >= 0 && options.motifStem < Song::stemCount
        ? (1u << options.motifStem) : 0u;
    parts[0] = {SeedTrackPartName::opening, 0, developmentStart,
                openings[openingChoice % 3u] | motifChannel, 0.80f, 0.88f};
    parts[1] = {SeedTrackPartName::development, developmentStart, breakStart,
                development, falling ? 1.0f : (rising ? 0.62f : 0.82f),
                falling ? 0.63f : 1.0f};
    parts[2] = {SeedTrackPartName::breakSection, breakStart, returnStart,
                breaks[breakChoice % 3u] | (sparseBreak ? 0u : harmony), 0.78f, 0.70f};
    parts[3] = {SeedTrackPartName::returnSection, returnStart, endingStart,
                all, 1.0f, 0.92f};
    parts[4] = {SeedTrackPartName::ending, endingStart, blocks,
                texture | (falling ? 0u : (openingChoice % 2u == 0 ? melody : harmony)),
                0.76f, falling ? 0.40f : 0.54f};
    int firstBlock = 0;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        auto& part = parts[i];
        const auto& edit = options.partEdits[i];
        const int length = edit.blocks > 0 ? edit.blocks : part.endBlock - part.firstBlock;
        part.sourceFirstBlock = part.firstBlock;
        part.dynamics = edit.dynamics;
        part.firstBlock = firstBlock;
        firstBlock += length;
        part.endBlock = firstBlock;
        if (edit.channels != 0) part.channels = edit.channels;
        part.repeatFirst = edit.repeatFirst;
        switch (edit.dynamics) {
            case SeedTrackPartDynamics::automatic: break;
            case SeedTrackPartDynamics::steady:
                part.firstGain = part.lastGain = 0.84f;
                break;
            case SeedTrackPartDynamics::crescendo:
                part.firstGain = 0.58f;
                part.lastGain = 1.0f;
                break;
            case SeedTrackPartDynamics::diminuendo:
                part.firstGain = 1.0f;
                part.lastGain = 0.52f;
                break;
            case SeedTrackPartDynamics::arch:
                part.firstGain = part.lastGain = 0.58f;
                break;
        }
    }
    return parts;
}

bool seedTrackOptionsFromSong(const Song& song, SeedTrackOptions& destination) {
    if (song.compositionSeed == 0 || song.cellCount <= 0 || !std::isfinite(song.bpm) ||
        song.bpm < 40.0 || song.bpm > 240.0) return false;
    const auto& first = song.bank[0].formulaOrigin;
    if (first.sourceId != "risco.track.seed.v4" && first.sourceId != "risco.track.seed.v5" &&
        first.sourceId != "risco.track.seed.v6" && first.sourceId != "risco.track.seed.v7" &&
        first.sourceId != "risco.track.seed.v8" && first.sourceId != "risco.track.seed.v9") return false;
    for (int i = 0; i < song.cellCount; ++i) {
        const auto& origin = song.bank[static_cast<std::size_t>(i)].formulaOrigin;
        if (origin.sourceId != first.sourceId || origin.source != first.source) return false;
    }
    SeedTrackOptions options;
    options.bpm = song.bpm;
    options.generatorVersion = first.sourceId == "risco.track.seed.v8" ||
                               first.sourceId == "risco.track.seed.v9" ? 2 : 1;
    std::vector<std::string_view> fields;
    const std::string_view description(first.source);
    for (std::size_t start = 0; start <= description.size();) {
        const auto end = description.find("; ", start);
        fields.push_back(description.substr(start, end == std::string_view::npos ? end : end - start));
        if (end == std::string_view::npos) break;
        start = end + 2;
    }
    auto integer = [](std::string_view value, int& result) {
        const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
        return !value.empty() && parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size();
    };
    auto unsignedInteger = [](std::string_view value, std::uint64_t& result) {
        const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
        return !value.empty() && parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size();
    };
    bool hasForm = false, hasSurprise = false, hasDuration = false, hasPulse = false, hasPalette = false;
    bool hasGeneratorVersion = false, hasMotifReturn = false;
    for (const auto field : fields) {
        int value = 0;
        if (field.rfind("Form: ", 0) == 0) {
            const auto form = field.substr(6);
            options.form = form == "original" ? SeedTrackForm::original :
                form == "arc" ? SeedTrackForm::arc : form == "collage" ? SeedTrackForm::collage :
                static_cast<SeedTrackForm>(-1);
            hasForm = options.form >= SeedTrackForm::original && options.form <= SeedTrackForm::collage;
        } else if (field.rfind("surprise: ", 0) == 0) {
            hasSurprise = integer(field.substr(10), options.surprise);
        } else if (field.rfind("duration-bars: ", 0) == 0) {
            hasDuration = integer(field.substr(15), options.durationBars);
        } else if (field.rfind("pulse: ", 0) == 0) {
            hasPulse = integer(field.substr(7), value) && value >= 0 && value <= 2;
            if (hasPulse) options.pulse = static_cast<SeedTrackPulse>(value);
        } else if (field.rfind("palette: ", 0) == 0) {
            hasPalette = integer(field.substr(9), value) && value >= 0 && value <= 3;
            if (hasPalette) options.palette = static_cast<SeedTrackPalette>(value);
        } else if (field.rfind("generator: ", 0) == 0) {
            hasGeneratorVersion = integer(field.substr(11), options.generatorVersion) &&
                                  options.generatorVersion == 2;
        } else if (field.rfind("motif-return-mode: ", 0) == 0) {
            hasMotifReturn = integer(field.substr(19), value) && value >= 1 && value <= 3;
            if (hasMotifReturn) options.motifReturn = static_cast<SeedTrackMotifReturn>(value);
        } else if (field == "arrangement: parts") {
            options.arrangeByParts = true;
        } else if (field.rfind("cue: ", 0) == 0) {
            const auto cue = field.substr(5);
            options.cue = cue == "none" ? SeedTrackCue::none : cue == "breath" ? SeedTrackCue::breath :
                cue == "rise" ? SeedTrackCue::rise : cue == "fall" ? SeedTrackCue::fall :
                static_cast<SeedTrackCue>(-1);
            if (options.cue < SeedTrackCue::none || options.cue > SeedTrackCue::fall) return false;
        } else if (field.rfind("source: ", 0) == 0) {
            const auto source = field.substr(8);
            const auto fingerprint = source.find(" fingerprint: ");
            if (fingerprint == std::string_view::npos ||
                !unsignedInteger(source.substr(fingerprint + 14), options.materialSeed) || options.materialSeed == 0)
                return false;
            const auto kind = source.substr(0, fingerprint);
            options.material = kind == "text" ? SeedTrackMaterial::text :
                kind == "drawing" ? SeedTrackMaterial::drawing : SeedTrackMaterial::none;
            if (options.material == SeedTrackMaterial::none) return false;
        } else if (field.rfind("motif: ", 0) == 0) {
            std::istringstream input(std::string(field.substr(7)));
            if (!(input >> options.motifStem) || options.motifStem < 0 || options.motifStem >= Song::stemCount)
                return false;
            std::string note;
            while (input >> note) {
                if (options.motif.count >= static_cast<int>(options.motif.notes.size())) return false;
                const auto colon = note.find(':');
                if (colon == std::string::npos) return false;
                int tick = 0, semitones = 0;
                if (!integer(std::string_view(note).substr(0, colon), tick) ||
                    !integer(std::string_view(note).substr(colon + 1), semitones)) return false;
                options.motif.notes[static_cast<std::size_t>(options.motif.count++)] = {tick, semitones};
            }
        } else if (field.rfind("motif-return: ", 0) == 0) {
            if (!integer(field.substr(14), options.motifReturnStem) ||
                options.motifReturnStem < 0 || options.motifReturnStem >= Song::stemCount) return false;
        } else if (field.rfind("parts: ", 0) == 0) {
            constexpr std::string_view symbols = "0123456789abcdefghijklmnopqrstuv";
            const auto encoded = field.substr(7);
            const bool extended = encoded.size() == seedTrackPartCount * 4u;
            if ((!extended && encoded.size() != seedTrackPartCount * 3u) ||
                (first.sourceId == "risco.track.seed.v7" && !extended)) return false;
            for (std::size_t i = 0; i < seedTrackPartCount; ++i) {
                const auto width = extended ? 4u : 3u;
                const auto a = symbols.find(encoded[i * width]);
                const auto b = symbols.find(encoded[i * width + 1]);
                const auto c = extended ? symbols.find(encoded[i * width + 2]) :
                    symbols.find(encoded[i * width + 2]);
                const auto d = extended ? symbols.find(encoded[i * width + 3]) : 0u;
                if (a == std::string_view::npos || b == std::string_view::npos || c == std::string_view::npos ||
                    (extended ? (c > 4 || d > 1) : c > 7))
                    return false;
                options.partEdits[i] = {static_cast<int>(a), static_cast<unsigned>(b),
                    static_cast<SeedTrackPartDynamics>(extended ? c : c & 3u),
                    extended ? d != 0 : (c & 4u) != 0};
            }
        }
    }
    if (!hasForm || !hasSurprise || !hasDuration || !hasPulse || !hasPalette || !options.arrangeByParts ||
        options.surprise < 0 || options.surprise > 2 || options.durationBars < seedTrackMinBars ||
        options.durationBars > seedTrackMaxBars || options.durationBars % 4 != 0 ||
        (options.motif.count > 0 && !validSeedTrackMotif(options.motif)) ||
        (options.generatorVersion < 1 || options.generatorVersion > 2 ||
         ((first.sourceId == "risco.track.seed.v8" || first.sourceId == "risco.track.seed.v9") &&
          !hasGeneratorVersion) ||
         (first.sourceId == "risco.track.seed.v9" && !hasMotifReturn) ||
         ((first.sourceId == "risco.track.seed.v6" || first.sourceId == "risco.track.seed.v7") &&
         std::none_of(options.partEdits.begin(), options.partEdits.end(),
                      [](const SeedTrackPartEdit& edit) { return !edit.automatic(); })))) return false;
    const auto parts = seedTrackParts(song.compositionSeed, options);
    const int bars = parts.back().endBlock * 4;
    if (bars < seedTrackMinBars || bars > seedTrackMaxBars) return false;

    Song regenerated;
    if (!generateSeedTrack(song.compositionSeed, options, regenerated) ||
        regenerated.cellCount != song.cellCount || regenerated.placementCount != song.placementCount ||
        regenerated.beatsPerBar != song.beatsPerBar) return false;
    const auto sameSavedNumber = [](double expected, double actual) {
        return std::isfinite(expected) && std::isfinite(actual) &&
               std::abs(expected - actual) <= 1.0e-5 * std::max({1.0, std::abs(expected), std::abs(actual)});
    };
    if (!sameSavedNumber(regenerated.bpm, song.bpm)) return false;
    for (int i = 0; i < song.placementCount; ++i) {
        const auto& expected = regenerated.grid[static_cast<std::size_t>(i)];
        const auto& actual = song.grid[static_cast<std::size_t>(i)];
        if (expected.cellId != actual.cellId || expected.startBeat != actual.startBeat ||
            expected.span != actual.span || expected.repeats != actual.repeats ||
            expected.sourceStartBeat != actual.sourceStartBeat || expected.lengthBeats != actual.lengthBeats ||
            expected.stem != actual.stem) return false;
    }
    for (int i = 0; i < song.cellCount; ++i) {
        const auto& expected = regenerated.bank[static_cast<std::size_t>(i)];
        const auto& actual = song.bank[static_cast<std::size_t>(i)];
        const auto& a = expected.formulaEvents;
        const auto& b = actual.formulaEvents;
        if (expected.id != actual.id || expected.name != actual.name || expected.bytebeatFormula != actual.bytebeatFormula ||
            expected.formulaEventPhrase != actual.formulaEventPhrase || expected.formulaExpression != actual.formulaExpression ||
            expected.formulaOrigin.sourceId != actual.formulaOrigin.sourceId ||
            expected.formulaOrigin.source != actual.formulaOrigin.source || expected.role != actual.role ||
            expected.formulaEventBpm != actual.formulaEventBpm || expected.formulaScale != actual.formulaScale ||
            expected.formulaTonic != actual.formulaTonic || expected.formulaMotion != actual.formulaMotion ||
            a.count != b.count || !sameSavedNumber(a.end, b.end) ||
            !sameSavedNumber(a.repeatPeriod, b.repeatPeriod)) return false;
        for (int note = 0; note < a.count; ++note) {
            const auto& x = a.notes[static_cast<std::size_t>(note)];
            const auto& y = b.notes[static_cast<std::size_t>(note)];
            if (!sameSavedNumber(x.start, y.start) || !sameSavedNumber(x.duration, y.duration) ||
                !sameSavedNumber(x.frequency, y.frequency) || !sameSavedNumber(x.gain, y.gain) ||
                !sameSavedNumber(x.pulseDuty, y.pulseDuty) || x.role != y.role) return false;
        }
    }
    destination = options;
    return true;
}

FormulaOrigin legacySeedTrackOrigin(const SeedTrackOptions& options) {
    const bool shaped = options.form != SeedTrackForm::original;
    auto description = shaped
        ? std::string(options.form == SeedTrackForm::arc ? "Form: arc" : "Form: collage") +
          "; surprise: " + std::to_string(options.surprise)
        : "Seed track generator: embedded generated note events";
    if (options.pulse != SeedTrackPulse::straight || options.palette != SeedTrackPalette::original)
        description += "; pulse: " + std::to_string(static_cast<unsigned>(options.pulse)) +
                       "; palette: " + std::to_string(static_cast<unsigned>(options.palette));
    return {"Risco", std::move(description), shaped ? "risco.track.seed.v2" : "risco.track.seed.v1",
            0, 0, "AGPL-3.0-or-later"};
}

FormulaOrigin seedTrackOrigin(const SeedTrackOptions& options) {
    const bool shaped = options.form != SeedTrackForm::original;
    auto description = std::string(options.form == SeedTrackForm::collage ? "Form: collage" :
                                    options.form == SeedTrackForm::arc ? "Form: arc" : "Form: original") +
        "; surprise: " + std::to_string(options.surprise) +
        "; duration-bars: " + std::to_string(options.durationBars) +
        "; pulse: " + std::to_string(static_cast<unsigned>(options.pulse)) +
        "; palette: " + std::to_string(static_cast<unsigned>(options.palette));
    if (options.materialSeed != 0) {
        const auto source = options.material == SeedTrackMaterial::drawing ? "drawing" :
                            options.material == SeedTrackMaterial::text ? "text" : "material";
        description += "; source: " + std::string(source) + " fingerprint: " +
                       std::to_string(options.materialSeed);
    }
    if (options.arrangeByParts) {
        constexpr const char* cues[] = {"none", "breath", "rise", "fall"};
        description += "; arrangement: parts; cue: ";
        description += cues[std::clamp(static_cast<int>(options.cue), 0, 3)];
    }
    if (options.motif.count > 0) {
        description += "; motif: " + std::to_string(options.motifStem);
        const int count = std::min(options.motif.count, int(options.motif.notes.size()));
        for (int i = 0; i < count; ++i) {
            const auto& note = options.motif.notes[static_cast<std::size_t>(i)];
            description += " " + std::to_string(note.tick) + ":" + std::to_string(note.semitones);
        }
        if (options.motifReturnStem >= 0)
            description += "; motif-return: " + std::to_string(options.motifReturnStem);
    }
    if (options.generatorVersion >= 2) description += "; generator: 2";
    if (options.motifReturn != SeedTrackMotifReturn::automatic)
        description += "; motif-return-mode: " + std::to_string(static_cast<int>(options.motifReturn));
    const bool edited = hasPartEdits(options);
    if (edited) {
        constexpr char symbols[] = "0123456789abcdefghijklmnopqrstuv";
        description += "; parts: ";
        const bool extended = std::any_of(options.partEdits.begin(), options.partEdits.end(),
            [](const SeedTrackPartEdit& part) { return part.dynamics == SeedTrackPartDynamics::arch; });
        for (const auto& part : options.partEdits) {
            description += symbols[static_cast<unsigned>(part.blocks) & 31u];
            description += symbols[part.channels & 31u];
            if (extended) {
                description += symbols[static_cast<unsigned>(part.dynamics)];
                description += part.repeatFirst ? '1' : '0';
            } else {
                const auto dynamics = static_cast<unsigned>(part.dynamics) & 3u;
                description += symbols[dynamics | (part.repeatFirst ? 4u : 0u)];
            }
        }
    }
    const bool extended = edited && std::any_of(options.partEdits.begin(), options.partEdits.end(),
        [](const SeedTrackPartEdit& part) { return part.dynamics == SeedTrackPartDynamics::arch; });
    const auto* sourceId = options.generatorVersion >= 2 && options.motifReturn != SeedTrackMotifReturn::automatic
        ? "risco.track.seed.v9" : options.generatorVersion >= 2 ? "risco.track.seed.v8" :
        extended ? "risco.track.seed.v7" : edited ? "risco.track.seed.v6" :
        (options.motif.count > 0 ? "risco.track.seed.v5" :
        (options.arrangeByParts ? "risco.track.seed.v4" :
        (shaped ? "risco.track.seed.v3" : "risco.track.seed.v2")));
    return {"Risco", std::move(description), sourceId,
            0, 0, "AGPL-3.0-or-later"};
}

bool isSeedTrackOrigin(const FormulaOrigin& origin) noexcept {
    constexpr std::string_view prefix = "risco.track.seed.v";
    const std::string_view sourceId(origin.sourceId);
    if (sourceId.size() <= prefix.size() || sourceId.substr(0, prefix.size()) != prefix) return false;
    unsigned version = 0;
    const auto parsed = std::from_chars(sourceId.data() + prefix.size(),
                                        sourceId.data() + sourceId.size(), version);
    return parsed.ec == std::errc{} && parsed.ptr == sourceId.data() + sourceId.size() && version >= 2;
}

bool generateSeedTrack(std::uint64_t seed, const SeedTrackOptions& options,
                       Song& destination) {
    if (seed == 0 || options.durationBars < seedTrackMinBars || options.durationBars > seedTrackMaxBars ||
        options.durationBars % 4 != 0 ||
        options.form < SeedTrackForm::original || options.form > SeedTrackForm::collage ||
        options.surprise < 0 || options.surprise > 2 ||
        options.pulse < SeedTrackPulse::straight || options.pulse > SeedTrackPulse::cut ||
        options.palette < SeedTrackPalette::original || options.palette > SeedTrackPalette::grain ||
        options.cue < SeedTrackCue::none || options.cue > SeedTrackCue::fall ||
        !validSeedTrackMotif(options.motif) ||
        (options.motif.count == 0 ? (options.motifStem != -1 || options.motifReturnStem != -1) :
         (!isMotifStem(options.motifStem) ||
          (options.motifReturnStem != -1 && !isMotifStem(options.motifReturnStem)))) ||
        options.generatorVersion < 1 || options.generatorVersion > 2 ||
        options.motifReturn < SeedTrackMotifReturn::automatic ||
        options.motifReturn > SeedTrackMotifReturn::mirrorRetrograde ||
        (options.bpm != 0.0 && (!std::isfinite(options.bpm) || options.bpm < 40.0 || options.bpm > 240.0)))
        return false;
    const bool edited = hasPartEdits(options);
    if (edited && !options.arrangeByParts) return false;
    const auto allChannels = (1u << Song::stemCount) - 1u;
    for (const auto& edit : options.partEdits) {
        if (edit.blocks < 0 || edit.blocks > seedTrackMaxBars / 4 ||
            (edit.channels & ~allChannels) != 0 ||
            edit.dynamics < SeedTrackPartDynamics::automatic ||
            edit.dynamics > SeedTrackPartDynamics::arch) return false;
    }
    const auto parts = seedTrackParts(seed, options);
    const int phraseBlocks = options.arrangeByParts ? parts.back().endBlock : options.durationBars / 4;
    if (phraseBlocks < seedTrackMinBars / 4 || phraseBlocks > seedTrackMaxBars / 4)
        return false;
    Song candidate;
    candidate.bpm = options.bpm > 0.0 ? options.bpm :
        88.0 + double(mixSeed(seed ^ 0x545241434b54454dULL) % 45u);
    candidate.beatsPerBar = 4;
    candidate.timbreMode = 1;
    candidate.masterGain = 0.28f;
    candidate.stemGain = {{0.82f, 0.62f, 0.78f, 0.52f, 0.38f}};
    candidate.stemPan = {{-0.16f, 0.12f, 0.0f, 0.0f, 0.24f}};
    candidate.stemRole = {{CellRole::melody, CellRole::harmony, CellRole::bass,
                           CellRole::pulse, CellRole::texture}};
    candidate.compositionSeedRecipeEnabled = true;
    candidate.compositionSeed = seed;
    candidate.compositionSeedAlgorithm = compositionSeedAlgorithmV1;
    candidate.compositionSeedLocks = compositionSeedDefaultLocks;
    candidate.compositionSeedLevel = static_cast<std::uint8_t>(CompositionSeedLevel::wide);
    const bool shaped = options.form != SeedTrackForm::original;
    if (shaped && options.surprise > 0) {
        candidate.stemEffect[1] = 8;
        candidate.stemReverbMix[1] = 0.13f;
        candidate.stemFxAutomation[1] = {true, {{false, true, false}}};
        if (options.form == SeedTrackForm::collage && options.surprise == 2) {
            candidate.stemEffect[0] = (mixSeed(seed) & 1u) != 0 ? 3 : 6;
            candidate.stemDelayMix[0] = 0.18f;
            candidate.stemFxAutomation[0] = {true, {{false, true, false}}};
        }
    }
    applyTrackPalette(candidate, options.palette);

    constexpr std::array<VoiceRole, 3> voiceRoles{{
        VoiceRole::lead, VoiceRole::harmony, VoiceRole::bass}};
    constexpr std::array<CellRole, Song::stemCount> cellRoles{{
        CellRole::melody, CellRole::harmony, CellRole::bass, CellRole::pulse, CellRole::texture}};
    constexpr std::array<const char*, Song::stemCount> names{{
        "Melodia", "Harmonia", "Baixo", "Ritmo", "Textura"}};
    const auto origin = seedTrackOrigin(options);
    const auto thumbnailMode = options.palette == SeedTrackPalette::original
        ? WaveMode::quantizedSine : noteWaveMode(candidate.timbreMode);
    auto part = parts.begin();
    const int legacyReprise = (phraseBlocks * 3 + 3) / 4;
    const int returnStem = options.motifReturnStem < 0 ? options.motifStem : options.motifReturnStem;

    for (int block = 0; block < phraseBlocks; ++block) {
        while (block >= part->endBlock) ++part;
        const bool atPartStart = block == part->firstBlock || part->repeatFirst;
        const bool reprise = edited ? part->name == SeedTrackPartName::returnSection && atPartStart
                                    : block == legacyReprise;
        const bool opening = part->name == SeedTrackPartName::opening && atPartStart;
        const int sourceBlock = edited ? part->sourceFirstBlock + (part->repeatFirst ? 0 : block - part->firstBlock)
                                       : block;
        const int section = edited ? static_cast<int>(part->name) : block * 4 / phraseBlocks;
        const bool ending = block == phraseBlocks - 1;
        const auto blockSeed = mixSeed(seed ^ (0x424c4f434b000001ULL + std::uint64_t(sourceBlock)));
        const unsigned version = 7u + static_cast<unsigned>(sourceBlock % 3);
        const auto phrase = shaped ? shapedPhrase(seed, options, sourceBlock, section, reprise, candidate.bpm) :
            phraseFromSeed(blockSeed, static_cast<unsigned>(sourceBlock % 7), version,
                                           candidate.bpm, 0x0f, candidate.beatsPerBar,
                                           candidate.compoundSixEight, {});
        if (phrase.count < 1 || phrase.end <= 0.0) return false;

        for (int roleIndex = 0; roleIndex < Song::stemCount; ++roleIndex) {
            if ((part->channels & (1u << roleIndex)) == 0) continue;
            auto events = roleIndex < int(voiceRoles.size())
                ? roleSequence(phrase, voiceRoles[static_cast<size_t>(roleIndex)])
                : roleIndex == 3 ? rhythmSequence(phrase, candidate.bpm, 4, blockSeed, options.surprise)
                                 : textureSequence(phrase, candidate.bpm);
            if (events.count < 1) return false;
            if (opening && roleIndex == options.motifStem)
                events = voiceMotif(options.motif, events, roleIndex, candidate.bpm);
            else if (reprise && roleIndex == returnStem) {
                const auto motif = options.generatorVersion >= 2
                    ? developedReturnMotif(options.motif, effectiveMotifReturn(options)) : options.motif;
                events = voiceMotif(motif, events, roleIndex, candidate.bpm);
            }
            if (shaped && roleIndex < int(voiceRoles.size()))
                shapeTrackRole(events, voiceRoles[static_cast<size_t>(roleIndex)], section, ending, blockSeed, options);
            if (shaped && roleIndex == 3)
                shapeTrackRole(events, VoiceRole::lead, section, ending, blockSeed, options);
            if (options.arrangeByParts) {
                const bool breath = options.cue == SeedTrackCue::breath &&
                    part->name == SeedTrackPartName::breakSection && block + 1 == part->endBlock;
                const int kept = events.count;
                const double cutoff = events.end * 0.75;
                if (breath) {
                    // Reserve the last bar of the break as a pause before the ensemble returns.
                    events.count = 0;
                    for (int i = 0; i < kept; ++i) {
                        auto note = events.notes[static_cast<std::size_t>(i)];
                        if (note.start >= cutoff) continue;
                        note.duration = std::min(note.duration, cutoff - note.start);
                        events.notes[static_cast<std::size_t>(events.count++)] = note;
                    }
                    if (events.count == 0) {
                        auto& note = events.notes.front();
                        note.start = 0.0;
                        note.duration = std::min(note.duration, cutoff);
                        events.count = 1;
                    }
                }
                const double partBlocks = part->endBlock - part->firstBlock;
                for (int i = 0; i < events.count; ++i) {
                    auto& note = events.notes[static_cast<std::size_t>(i)];
                    const double progress = (block - part->firstBlock + note.start / events.end) / partBlocks;
                    const float gain = part->dynamics == SeedTrackPartDynamics::arch
                        ? 0.58f + 0.42f * static_cast<float>(std::sin(3.14159265358979323846 * progress))
                        : part->firstGain + static_cast<float>(progress) *
                            (part->lastGain - part->firstGain);
                    note.gain = std::clamp(note.gain * gain, 0.01f, 0.9f);
                }
            }
            if (roleIndex < int(voiceRoles.size()) || roleIndex == 3)
                shapePulse(events, VoiceRole::lead, options.pulse, candidate.bpm);
            const auto label = std::string(names[static_cast<size_t>(roleIndex)]) + " " +
                               std::to_string(block + 1);
            const auto cellId = storeFormulaEventCell(candidate, "0", label,
                waveformFor(events, thumbnailMode), events, FormulaScalePreset::major,
                FormulaScaleMotion::follow, 0, candidate.bpm, origin, 0, 1, 0.0, 0,
                {{-1, -1, -1, -1}}, false, 0);
            if (cellId == 0) return false;
            auto* cell = const_cast<Cell*>(findCell(candidate, cellId));
            if (cell == nullptr) return false;
            cell->role = cellRoles[static_cast<size_t>(roleIndex)];
            const int startBeat = block * candidate.beatsPerBar * 4;
            if (!placeCell(candidate, {cellId, startBeat, CellSpan::fourBars, 1, 0, 0,
                                       roleIndex}))
                return false;
        }
    }

    if (candidate.cellCount == 0 || candidate.cellCount != candidate.placementCount) return false;
    destination = std::move(candidate);
    return true;
}

bool generateSeedTrack(std::uint64_t seed, Song& destination) {
    return generateSeedTrack(seed, SeedTrackOptions{}, destination);
}

} // namespace risco
