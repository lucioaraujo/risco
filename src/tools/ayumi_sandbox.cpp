// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Lúcio de Araújo
// Audit sandbox for the pinned Ayumi MIT core vendored by Risco.
#include "core/AyRegisterContract.hpp"
extern "C" {
#include "ayumi.h"
}

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {
constexpr std::uint32_t sampleRate = 48000;
constexpr double chipClock = 2000000.0;
constexpr std::size_t maxEvents = 4096;
constexpr std::uintmax_t maxAgendaBytes = 1024 * 1024;

struct Event { std::uint32_t sample{}; int reg{}; std::uint8_t value{}; std::size_t order{}; };
struct Result { std::uint64_t hash{14695981039346656037ULL}; std::uint64_t frames{}; std::uint64_t clipped{}; double peak{}; double elapsed{}; };
struct MatrixRow { std::string id; bool ym{}; Result result; };

void write16(std::ostream& out, std::uint16_t value) {
    out.put(char(value & 0xffu)); out.put(char((value >> 8) & 0xffu));
}
void write32(std::ostream& out, std::uint32_t value) {
    write16(out, std::uint16_t(value)); write16(out, std::uint16_t(value >> 16));
}
void hash16(std::uint64_t& hash, std::uint16_t value) {
    for (int shift : {0, 8}) { hash ^= (value >> shift) & 0xffu; hash *= 1099511628211ULL; }
}

void applyRegister(ayumi& chip, std::array<std::uint8_t, risco::ay_contract::registerCount>& regs,
                   int reg, std::uint8_t value) {
    using namespace risco::ay_contract;
    if (reg < 0 || reg >= registerCount) return;
    regs[static_cast<std::size_t>(reg)] = maskedWrite(reg, value);
    if (reg <= 5) ayumi_set_tone(&chip, reg / 2, rawTonePeriod(regs, reg / 2));
    else if (reg == 6) ayumi_set_noise(&chip, rawNoisePeriod(regs));
    else if (reg == 7 || (reg >= 8 && reg <= 10)) {
        for (int channel = 0; channel < channelCount; ++channel) {
            ayumi_set_mixer(&chip, channel, !toneEnabled(regs, channel), !noiseEnabled(regs, channel),
                            envelopeEnabled(regs, channel));
            ayumi_set_volume(&chip, channel, fixedLevel(regs, channel));
        }
    } else if (reg == 11 || reg == 12) ayumi_set_envelope(&chip, envelopePeriod(regs));
    else if (reg == 13) ayumi_set_envelope_shape(&chip, envelopeShape(regs));
}

std::vector<Event> builtInAgenda(double duration) {
    std::vector<Event> events;
    const auto add = [&](double seconds, int reg, int value) {
        if (seconds < duration) events.push_back({std::uint32_t(seconds * sampleRate), reg,
                                                  std::uint8_t(value), events.size()});
    };
    add(0, 0, 0xf4); add(0, 1, 0x01); add(0, 8, 15); add(0, 7, 0x3e);
    add(0.75, 2, 0x90); add(0.75, 3, 0x01); add(0.75, 9, 12); add(0.75, 7, 0x3c);
    add(1.50, 6, 8); add(1.50, 10, 10); add(1.50, 7, 0x1c);
    add(2.25, 11, 0x80); add(2.25, 12, 0x00); add(2.25, 13, 0x0e); add(2.25, 8, 0x10);
    return events;
}

void addEvent(std::vector<Event>& events, double seconds, int reg, int value) {
    events.push_back({std::uint32_t(seconds * sampleRate), reg, std::uint8_t(value), events.size()});
}

std::vector<Event> toneAgenda(int period) {
    std::vector<Event> events;
    addEvent(events, 0, 0, period & 0xff); addEvent(events, 0, 1, (period >> 8) & 0x0f);
    addEvent(events, 0, 8, 15); addEvent(events, 0, 7, 0x3e);
    return events;
}

std::vector<Event> noiseAgenda(int period) {
    std::vector<Event> events;
    addEvent(events, 0, 6, period); addEvent(events, 0, 8, 15); addEvent(events, 0, 7, 0x37);
    return events;
}

std::vector<Event> envelopeAgenda(int period, int shape, bool retrigger = false) {
    auto events = toneAgenda(500);
    addEvent(events, 0, 11, period & 0xff); addEvent(events, 0, 12, (period >> 8) & 0xff);
    addEvent(events, 0, 13, shape); addEvent(events, 0, 8, 0x10);
    if (retrigger) addEvent(events, 0.5, 13, shape);
    return events;
}

std::vector<Event> mixerAgenda(int mixer) {
    auto events = toneAgenda(500);
    addEvent(events, 0, 6, 8); addEvent(events, 0, 7, mixer);
    return events;
}

std::vector<Event> midCounterAgenda() {
    auto events = toneAgenda(700);
    addEvent(events, 0.50001, 0, 0xfa); addEvent(events, 0.50001, 1, 0x00);
    return events;
}

bool loadAgenda(const std::filesystem::path& path, double duration, std::vector<Event>& events, std::string& error) {
    std::error_code ec;
    const auto bytes = std::filesystem::file_size(path, ec);
    if (ec || bytes > maxAgendaBytes) { error = "agenda ausente ou maior que 1 MiB"; return false; }
    std::ifstream input(path);
    if (!input) { error = "não foi possível abrir a agenda"; return false; }
    std::string line;
    std::size_t lineNumber = 0;
    while (std::getline(input, line)) {
        ++lineNumber;
        const auto comment = line.find('#');
        if (comment != std::string::npos) line.resize(comment);
        std::istringstream row(line);
        std::string timeText, regText, valueText, extra;
        if (!(row >> timeText)) continue;
        if (!(row >> regText >> valueText) || (row >> extra)) { error = "linha inválida: " + std::to_string(lineNumber); return false; }
        try {
            std::size_t usedTime = 0, usedReg = 0, usedValue = 0;
            const double seconds = std::stod(timeText, &usedTime);
            const int reg = std::stoi(regText, &usedReg, 0);
            const int value = std::stoi(valueText, &usedValue, 0);
            if (usedTime != timeText.size() || usedReg != regText.size() || usedValue != valueText.size() ||
                !std::isfinite(seconds) || seconds < 0 || seconds >= duration || reg < 0 || reg >= 16 ||
                value < 0 || value > 255 || events.size() >= maxEvents) {
                error = "valor fora do contrato na linha " + std::to_string(lineNumber); return false;
            }
            events.push_back({std::uint32_t(seconds * sampleRate), reg, std::uint8_t(value), events.size()});
        } catch (...) { error = "número inválido na linha " + std::to_string(lineNumber); return false; }
    }
    std::stable_sort(events.begin(), events.end(), [](const Event& a, const Event& b) {
        return a.sample < b.sample || (a.sample == b.sample && a.order < b.order);
    });
    return true;
}

Result render(bool ym, double duration, const std::vector<Event>& events, std::ostream* wav) {
    ayumi chip{};
    if (!ayumi_configure(&chip, ym ? 1 : 0, chipClock, sampleRate)) throw std::runtime_error("clock/sample rate unsupported by Ayumi");
    for (int channel = 0; channel < 3; ++channel) ayumi_set_pan(&chip, channel, 0.5, 0);
    std::array<std::uint8_t, risco::ay_contract::registerCount> regs{};
    for (int reg = 0; reg <= 13; ++reg) applyRegister(chip, regs, reg, 0);
    const auto frames = std::uint32_t(duration * sampleRate);
    if (wav) {
        wav->write("RIFF", 4); write32(*wav, 36u + frames * 4u); wav->write("WAVEfmt ", 8);
        write32(*wav, 16); write16(*wav, 1); write16(*wav, 2); write32(*wav, sampleRate);
        write32(*wav, sampleRate * 4u); write16(*wav, 4); write16(*wav, 16);
        wav->write("data", 4); write32(*wav, frames * 4u);
    }
    Result result; result.frames = frames;
    std::size_t next = 0;
    const auto started = std::chrono::steady_clock::now();
    for (std::uint32_t frame = 0; frame < frames; ++frame) {
        while (next < events.size() && events[next].sample == frame) {
            applyRegister(chip, regs, events[next].reg, events[next].value); ++next;
        }
        ayumi_process(&chip); ayumi_remove_dc(&chip);
        for (double sample : {chip.left * 0.5, chip.right * 0.5}) {
            result.peak = std::max(result.peak, std::abs(sample));
            if (sample < -1 || sample > 1) ++result.clipped;
            const auto pcm = std::int16_t(std::lround(std::clamp(sample, -1.0, 1.0) * 32767.0));
            hash16(result.hash, std::uint16_t(pcm));
            if (wav) write16(*wav, std::uint16_t(pcm));
        }
    }
    result.elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    return result;
}

std::vector<MatrixRow> matrixRows() {
    std::vector<MatrixRow> rows;
    const auto add = [&](const std::string& id, const std::vector<Event>& agenda, double duration = 1.0) {
        for (const bool ym : {false, true}) rows.push_back({id, ym, render(ym, duration, agenda, nullptr)});
    };
    add("tone_period_0", toneAgenda(0));
    add("tone_period_1", toneAgenda(1));
    add("tone_period_4095", toneAgenda(4095));
    add("noise_period_0", noiseAgenda(0));
    add("noise_period_1", noiseAgenda(1));
    add("noise_period_31", noiseAgenda(31));
    for (int shape = 0; shape < 16; ++shape)
        add("envelope_shape_" + std::to_string(shape), envelopeAgenda(128, shape));
    add("envelope_period_0", envelopeAgenda(0, 14));
    add("envelope_period_1", envelopeAgenda(1, 14));
    add("envelope_retrigger", envelopeAgenda(128, 14, true));
    add("write_mid_counter", midCounterAgenda());
    add("mixer_tone", mixerAgenda(0x3e));
    add("mixer_noise", mixerAgenda(0x37));
    add("mixer_both", mixerAgenda(0x36));
    add("mixer_off", mixerAgenda(0x3f));
    return rows;
}

const MatrixRow* findRow(const std::vector<MatrixRow>& rows, const std::string& id, bool ym) {
    const auto found = std::find_if(rows.begin(), rows.end(), [&](const MatrixRow& row) {
        return row.id == id && row.ym == ym;
    });
    return found == rows.end() ? nullptr : &*found;
}

bool matrixCheck(const std::vector<MatrixRow>& rows) {
    for (const auto& row : rows)
        if (row.result.clipped != 0 || !std::isfinite(row.result.peak)) return false;
    for (const bool ym : {false, true}) {
        const auto* tone0 = findRow(rows, "tone_period_0", ym);
        const auto* tone1 = findRow(rows, "tone_period_1", ym);
        const auto* noise0 = findRow(rows, "noise_period_0", ym);
        const auto* noise1 = findRow(rows, "noise_period_1", ym);
        const auto* env0 = findRow(rows, "envelope_period_0", ym);
        const auto* env1 = findRow(rows, "envelope_period_1", ym);
        if (!tone0 || !tone1 || !noise0 || !noise1 || !env0 || !env1 ||
            tone0->result.hash != tone1->result.hash || noise0->result.hash != noise1->result.hash ||
            env0->result.hash != env1->result.hash) return false;
    }
    const auto* ay = findRow(rows, "envelope_shape_14", false);
    const auto* ym = findRow(rows, "envelope_shape_14", true);
    return ay && ym && ay->result.hash != ym->result.hash && rows.size() == 60;
}

bool printMatrix() {
    const auto rows = matrixRows();
    std::cout << "case,model,fnv1a64,peak,clipped,frames\n";
    for (const auto& row : rows)
        std::cout << row.id << ',' << (row.ym ? "YM2149" : "AY-3-8910") << ','
                  << std::hex << std::setw(16) << std::setfill('0') << row.result.hash << std::dec
                  << ',' << row.result.peak << ',' << row.result.clipped << ',' << row.result.frames << '\n';
    std::cerr << "matrix_note: this Ayumi revision maps tone 0=1, noise 0=1 and envelope 0=1; "
                 "the last relation requires external verification.\n";
    return matrixCheck(rows);
}

bool selfCheck() {
    const auto agenda = builtInAgenda(3.0);
    const auto ay1 = render(false, 3.0, agenda, nullptr), ay2 = render(false, 3.0, agenda, nullptr);
    const auto ym = render(true, 3.0, agenda, nullptr);
    return ay1.hash == ay2.hash && ay1.hash != ym.hash && ay1.frames == 144000 &&
           ay1.peak > 0 && ym.peak > 0 && ay1.clipped == 0 && ym.clipped == 0 &&
           matrixCheck(matrixRows());
}
}

int main(int argc, char** argv) {
    if (argc == 2 && std::string(argv[1]) == "--matrix") return printMatrix() ? 0 : 1;
    if (argc == 2 && std::string(argv[1]) == "--self-check") {
        if (!selfCheck()) { std::cerr << "Ayumi sandbox self-check failed\n"; return 1; }
        std::cout << "Ayumi sandbox: PASS (determinism and AY/YM distinction; no fidelity claim)\n"; return 0;
    }
    if (argc < 4 || argc > 5 || (std::string(argv[1]) != "ay" && std::string(argv[1]) != "ym")) {
        std::cerr << "usage: risco_ayumi_sandbox --matrix|--self-check\n"
                     "       risco_ayumi_sandbox ay|ym duration_seconds output.wav [agenda.txt]\n"
                     "agenda: one 'seconds register value' write per line; register/value accept 0x prefix\n";
        return 2;
    }
    double duration = 0;
    try {
        std::size_t used = 0;
        duration = std::stod(argv[2], &used);
        if (used != std::string(argv[2]).size()) duration = 0;
    } catch (...) {}
    if (!std::isfinite(duration) || duration < 0.05 || duration > 30.0) {
        std::cerr << "duration must be 0.05..30 seconds\n"; return 2;
    }
    const std::filesystem::path target(argv[3]);
    if (std::filesystem::exists(target)) { std::cerr << "refusing to overwrite output\n"; return 1; }
    std::vector<Event> events;
    std::string error;
    if (argc == 5 ? !loadAgenda(argv[4], duration, events, error) : (events = builtInAgenda(duration), false)) {
        std::cerr << error << '\n'; return 2;
    }
    const auto partial = target.string() + ".partial";
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ec; std::filesystem::remove(path, ec); } } cleanup{partial};
    std::ofstream output(partial, std::ios::binary);
    if (!output) { std::cerr << "cannot open partial output\n"; return 1; }
    Result result;
    try { result = render(std::string(argv[1]) == "ym", duration, events, &output); }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
    output.close();
    if (!output) { std::cerr << "WAV write failed\n"; return 1; }
    std::error_code ec; std::filesystem::rename(partial, target, ec);
    if (ec) { std::cerr << "cannot publish WAV: " << ec.message() << '\n'; return 1; }
    cleanup.path.clear();
    std::cout << "frames=" << result.frames << " events=" << events.size() << " peak=" << result.peak
              << " clipped=" << result.clipped << " fnv1a64=" << std::hex << std::setw(16) << std::setfill('0')
              << result.hash << std::dec << " elapsed=" << result.elapsed
              << "s realtime_x=" << (duration / std::max(result.elapsed, 1e-9)) << '\n';
}
