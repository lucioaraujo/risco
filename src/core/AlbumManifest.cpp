// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Lúcio de Araújo
#include "AlbumManifest.hpp"

#include "Utf8.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <istream>
#include <locale>
#include <ostream>
#include <set>
#include <sstream>
#include <utility>

namespace risco {
namespace {

bool safeTitle(const std::string& text) noexcept {
    return !text.empty() && text.size() <= 128 && validUtf8(text) &&
           text.find_first_of("\r\n\0", 0, 3) == std::string::npos;
}

bool safeLeaf(const std::string& text) noexcept {
    return !text.empty() && text.size() <= 255 && validUtf8(text) &&
           text != "." && text != ".." &&
           text.find_first_of("/\\:\r\n\0", 0, 6) == std::string::npos;
}

bool complete(std::istringstream& parser) {
    parser >> std::ws;
    return parser.eof();
}

bool readLine(std::istream& in, std::string& line, std::size_t limit = 1024) {
    line.clear();
    char value = 0;
    while (in.get(value)) {
        if (value == '\n') return true;
        if (line.size() >= limit) return false;
        line.push_back(value);
    }
    return false;
}

bool samePlan(const AlbumPlan& left, const AlbumPlan& right) noexcept {
    if (left.count != right.count || left.request.seed != right.request.seed ||
        left.request.trackCount != right.request.trackCount ||
        left.request.centerBpm != right.request.centerBpm ||
        left.request.tempoSpread != right.request.tempoSpread ||
        left.request.contour != right.request.contour ||
        left.request.profile != right.request.profile ||
        left.request.trackProfiles != right.request.trackProfiles ||
        left.request.arrangeByParts != right.request.arrangeByParts ||
        left.request.generatorVersion != right.request.generatorVersion ||
        !(left.request.source == right.request.source)) return false;
    for (int index = 0; index < left.count; ++index)
        if (left.tracks[static_cast<size_t>(index)] != right.tracks[static_cast<size_t>(index)])
            return false;
    return true;
}

bool validManifest(const AlbumManifest& manifest) {
    if (!safeTitle(manifest.title) || manifest.count < 2 ||
        manifest.count > AlbumPlan::maxTracks || manifest.plan.count != manifest.count ||
        manifest.plan.request.trackCount != manifest.count) return false;
    AlbumPlan regenerated;
    if (!makeAlbumPlan(manifest.plan.request, regenerated) ||
        !samePlan(manifest.plan, regenerated)) return false;

    std::set<std::string> names;
    for (int index = 0; index < manifest.count; ++index) {
        const auto& item = manifest.tracks[static_cast<size_t>(index)];
        if (item.plan != manifest.plan.tracks[static_cast<size_t>(index)] ||
            !std::isfinite(item.plannedSeconds) || item.plannedSeconds <= 0.0 ||
            std::abs(item.plannedSeconds - item.plan.durationBars * 4.0 * 60.0 / item.plan.bpm) > 1.0e-8 ||
            !std::isfinite(item.renderedSeconds) || item.renderedSeconds < 0.0 ||
            static_cast<unsigned>(item.state) > static_cast<unsigned>(AlbumTrackState::rendered))
            return false;
        if (!item.projectFile.empty() && (!safeLeaf(item.projectFile) || !names.insert(item.projectFile).second))
            return false;
        if (!item.renderFile.empty() && (!safeLeaf(item.renderFile) || !names.insert(item.renderFile).second))
            return false;
        if (item.state == AlbumTrackState::planned &&
            (!item.projectFile.empty() || !item.renderFile.empty() || item.hasMeasurements ||
             item.renderedSeconds != 0.0)) return false;
        if (item.state == AlbumTrackState::projectReady &&
            (item.projectFile.empty() || !item.renderFile.empty() || item.hasMeasurements ||
             item.renderedSeconds != 0.0)) return false;
        if (item.state == AlbumTrackState::rendered &&
            (item.projectFile.empty() || item.renderFile.empty() || !item.hasMeasurements ||
             item.renderedSeconds <= 0.0 || !std::isfinite(item.integratedLufs) ||
             item.integratedLufs < -70.0 || item.integratedLufs > 6.0 ||
             !std::isfinite(item.truePeakDbtp) || item.truePeakDbtp < -120.0 ||
             item.truePeakDbtp > 12.0)) return false;
        if (!item.hasMeasurements && (item.integratedLufs != 0.0 || item.truePeakDbtp != 0.0))
            return false;
    }
    return true;
}

} // namespace

bool createAlbumManifest(const AlbumPlan& plan, std::string title,
                         AlbumManifest& destination) {
    AlbumPlan canonical;
    if (!safeTitle(title) || !makeAlbumPlan(plan.request, canonical) || !samePlan(plan, canonical))
        return false;
    AlbumManifest candidate;
    candidate.title = std::move(title);
    candidate.plan = plan;
    candidate.count = plan.count;
    for (int index = 0; index < candidate.count; ++index) {
        auto& item = candidate.tracks[static_cast<size_t>(index)];
        item.plan = plan.tracks[static_cast<size_t>(index)];
        item.plannedSeconds = item.plan.durationBars * 4.0 * 60.0 / item.plan.bpm;
    }
    destination = std::move(candidate);
    return true;
}

bool recordAlbumProject(AlbumManifest& manifest, int trackIndex,
                        std::string projectFile) {
    if (!validManifest(manifest) || trackIndex < 0 || trackIndex >= manifest.count ||
        !safeLeaf(projectFile)) return false;
    auto candidate = manifest;
    auto& track = candidate.tracks[static_cast<size_t>(trackIndex)];
    if (track.state == AlbumTrackState::rendered) return false;
    track.projectFile = std::move(projectFile);
    track.renderFile.clear();
    track.renderedSeconds = 0.0;
    track.hasMeasurements = false;
    track.integratedLufs = 0.0;
    track.truePeakDbtp = 0.0;
    track.state = AlbumTrackState::projectReady;
    if (!validManifest(candidate)) return false;
    manifest = std::move(candidate);
    return true;
}

bool invalidateAlbumRender(AlbumManifest& manifest, int trackIndex) {
    if (!validManifest(manifest) || trackIndex < 0 || trackIndex >= manifest.count)
        return false;
    auto candidate = manifest;
    auto& track = candidate.tracks[static_cast<size_t>(trackIndex)];
    if (track.state != AlbumTrackState::rendered) return false;
    track.renderFile.clear();
    track.renderedSeconds = 0.0;
    track.hasMeasurements = false;
    track.integratedLufs = 0.0;
    track.truePeakDbtp = 0.0;
    track.state = AlbumTrackState::projectReady;
    if (!validManifest(candidate)) return false;
    manifest = std::move(candidate);
    return true;
}

bool recordAlbumRender(AlbumManifest& manifest, int trackIndex,
                       std::string renderFile, double actualSeconds,
                       double integratedLufs, double truePeakDbtp) {
    if (!validManifest(manifest) || trackIndex < 0 || trackIndex >= manifest.count ||
        !safeLeaf(renderFile) || !std::isfinite(actualSeconds) || actualSeconds <= 0.0 ||
        !std::isfinite(integratedLufs) || !std::isfinite(truePeakDbtp)) return false;
    auto candidate = manifest;
    auto& track = candidate.tracks[static_cast<size_t>(trackIndex)];
    if (track.state != AlbumTrackState::projectReady && track.state != AlbumTrackState::rendered)
        return false;
    track.renderFile = std::move(renderFile);
    track.renderedSeconds = actualSeconds;
    track.integratedLufs = integratedLufs;
    track.truePeakDbtp = truePeakDbtp;
    track.hasMeasurements = true;
    track.state = AlbumTrackState::rendered;
    if (!validManifest(candidate)) return false;
    manifest = std::move(candidate);
    return true;
}

bool saveAlbumManifest(std::ostream& out, const AlbumManifest& manifest) {
    if (!validManifest(manifest)) return false;
    const bool hasTrackOverrides = std::any_of(
        manifest.plan.request.trackProfiles.begin(),
        manifest.plan.request.trackProfiles.begin() + manifest.count,
        [](AlbumMusicProfile profile) { return profile != AlbumMusicProfile::automatic; });
    const bool byParts = manifest.plan.request.arrangeByParts;
    const auto& source = manifest.plan.request.source;
    const bool withSource = source.material != SeedTrackMaterial::none;
    const bool perTrackProfiles = withSource || byParts || hasTrackOverrides;
    const bool detailed = manifest.plan.request.profile != AlbumMusicProfile::original || perTrackProfiles;
    const unsigned version = withSource ?
        (manifest.plan.request.generatorVersion >= 2 ? AlbumManifest::schemaVersion : 5u) :
        (byParts ? 4u : (perTrackProfiles ? 3u : (detailed ? 2u : 1u)));
    out.imbue(std::locale::classic());
    out << "RISCO_ALBUM_V" << version << '\n'
        << std::setprecision(17)
        << "ALBUM " << std::quoted(manifest.title) << ' '
        << manifest.plan.request.seed << ' ' << manifest.count << ' '
        << manifest.plan.request.centerBpm << ' ' << manifest.plan.request.tempoSpread << ' '
        << static_cast<unsigned>(manifest.plan.request.contour);
    if (detailed) out << ' ' << static_cast<unsigned>(manifest.plan.request.profile);
    if (perTrackProfiles) {
        for (int index = 0; index < manifest.count; ++index)
            out << ' ' << static_cast<unsigned>(manifest.plan.request.trackProfiles[static_cast<size_t>(index)]);
    }
    if (byParts || withSource) out << (byParts ? " parts" : " continuous");
    if (withSource) {
        out << ' ' << static_cast<unsigned>(source.material) << ' ' << source.fingerprint
            << ' ' << static_cast<unsigned>(source.cue) << ' ' << source.motif.count;
        for (int i = 0; i < source.motif.count; ++i) {
            const auto& note = source.motif.notes[static_cast<std::size_t>(i)];
            out << ' ' << note.tick << ' ' << note.semitones;
        }
    }
    out << '\n';
    for (int index = 0; index < manifest.count; ++index) {
        const auto& item = manifest.tracks[static_cast<size_t>(index)];
        out << "TRACK " << index << ' ' << item.plan.seed << ' ' << item.plan.bpm << ' '
            << item.plan.durationBars << ' ' << static_cast<unsigned>(item.state) << ' '
            << std::quoted(item.projectFile) << ' ' << std::quoted(item.renderFile) << ' '
            << item.plannedSeconds << ' ' << item.renderedSeconds << ' '
            << int(item.hasMeasurements) << ' ' << item.integratedLufs << ' '
            << item.truePeakDbtp;
        if (detailed) {
            out << ' ' << static_cast<unsigned>(item.plan.form) << ' ' << item.plan.surprise
                << ' ' << static_cast<unsigned>(item.plan.pulse) << ' '
                << static_cast<unsigned>(item.plan.palette);
        }
        if (withSource) out << ' ' << static_cast<unsigned>(item.plan.cue) << ' ' << item.plan.motifStem;
        if (version >= 6) out << ' ' << item.plan.generatorVersion;
        out << '\n';
    }
    return out.good();
}

bool loadAlbumManifest(std::istream& in, AlbumManifest& manifest) {
    std::string line;
    if (!readLine(in, line, 32)) return false;
    const bool withSource = line == "RISCO_ALBUM_V5" || line == "RISCO_ALBUM_V6";
    const bool generatorV2 = line == "RISCO_ALBUM_V6";
    bool byParts = line == "RISCO_ALBUM_V4" || line == "RISCO_ALBUM_V6";
    const bool perTrackProfiles = withSource || byParts || line == "RISCO_ALBUM_V3";
    const bool detailed = perTrackProfiles || line == "RISCO_ALBUM_V2";
    if ((!detailed && line != "RISCO_ALBUM_V1") ||
        (detailed && line != "RISCO_ALBUM_V2" && !perTrackProfiles) ||
        !readLine(in, line, 512)) return false;
    AlbumManifest candidate;
    std::istringstream albumLine(line);
    albumLine.imbue(std::locale::classic());
    int count = 0;
    unsigned contour = 0;
    if (!(albumLine >> line >> std::quoted(candidate.title) >> candidate.plan.request.seed >> count
          >> candidate.plan.request.centerBpm >> candidate.plan.request.tempoSpread >> contour) ||
        line != "ALBUM" || contour > static_cast<unsigned>(AlbumTempoContour::arc) ||
        count < 2 || count > AlbumPlan::maxTracks)
        return false;
    if (detailed) {
        unsigned profile = 0;
        if (!(albumLine >> profile) ||
            (!perTrackProfiles && profile < static_cast<unsigned>(AlbumMusicProfile::related)) ||
            profile > static_cast<unsigned>(AlbumMusicProfile::contrasting)) return false;
        candidate.plan.request.profile = static_cast<AlbumMusicProfile>(profile);
    }
    if (perTrackProfiles) {
        for (int index = 0; index < count; ++index) {
            unsigned profile = 0;
            if (!(albumLine >> profile) ||
                (profile != static_cast<unsigned>(AlbumMusicProfile::automatic) &&
                 profile > static_cast<unsigned>(AlbumMusicProfile::contrasting))) return false;
            candidate.plan.request.trackProfiles[static_cast<size_t>(index)] =
                static_cast<AlbumMusicProfile>(profile);
        }
    }
    if (byParts || withSource) {
        if (!(albumLine >> line) || (line != "parts" && (!withSource || line != "continuous")))
            return false;
        byParts = line == "parts";
    }
    if (withSource) {
        auto& source = candidate.plan.request.source;
        unsigned material = 0, cue = 0;
        if (!(albumLine >> material >> source.fingerprint >> cue >> source.motif.count) ||
            material < static_cast<unsigned>(SeedTrackMaterial::text) ||
            material > static_cast<unsigned>(SeedTrackMaterial::drawing) ||
            cue > static_cast<unsigned>(SeedTrackCue::fall) ||
        source.motif.count < 1 || source.motif.count > int(source.motif.notes.size()))
            return false;
        source.material = static_cast<SeedTrackMaterial>(material);
        source.cue = static_cast<SeedTrackCue>(cue);
        for (int i = 0; i < source.motif.count; ++i) {
            auto& note = source.motif.notes[static_cast<std::size_t>(i)];
            if (!(albumLine >> note.tick >> note.semitones)) return false;
        }
    }
    if (!complete(albumLine)) return false;
    candidate.count = count;
    candidate.plan.request.trackCount = count;
    candidate.plan.request.contour = static_cast<AlbumTempoContour>(contour);
    candidate.plan.request.arrangeByParts = byParts;
    candidate.plan.request.generatorVersion = generatorV2 ? 2 : 1;
    if (!makeAlbumPlan(candidate.plan.request, candidate.plan) || candidate.plan.count != count)
        return false;

    for (int index = 0; index < count; ++index) {
        if (!readLine(in, line, 1024)) return false;
        std::istringstream trackLine(line);
        trackLine.imbue(std::locale::classic());
        int parsedIndex = -1, bars = 0, state = -1, measurements = 0;
        auto& item = candidate.tracks[static_cast<size_t>(index)];
        if (!(trackLine >> line >> parsedIndex >> item.plan.seed >> item.plan.bpm >> bars >> state
              >> std::quoted(item.projectFile) >> std::quoted(item.renderFile)
              >> item.plannedSeconds >> item.renderedSeconds >> measurements
              >> item.integratedLufs >> item.truePeakDbtp) ||
            line != "TRACK" || parsedIndex != index ||
            measurements < 0 || measurements > 1 || state < 0 || state > 2) return false;
        if (detailed) {
            unsigned form = 0, pulse = 0, palette = 0;
            if (!(trackLine >> form >> item.plan.surprise >> pulse >> palette) ||
                form > static_cast<unsigned>(SeedTrackForm::collage) ||
                pulse > static_cast<unsigned>(SeedTrackPulse::cut) ||
                palette > static_cast<unsigned>(SeedTrackPalette::grain)) return false;
            item.plan.form = static_cast<SeedTrackForm>(form);
            item.plan.pulse = static_cast<SeedTrackPulse>(pulse);
            item.plan.palette = static_cast<SeedTrackPalette>(palette);
        }
        if (withSource) {
            unsigned cue = 0;
            if (!(trackLine >> cue >> item.plan.motifStem) ||
                cue > static_cast<unsigned>(SeedTrackCue::fall)) return false;
            item.plan.source = candidate.plan.request.source;
            item.plan.cue = static_cast<SeedTrackCue>(cue);
            item.plan.generatorVersion = candidate.plan.request.generatorVersion;
        }
        if (generatorV2) {
            int generatorVersion = 0;
            if (!(trackLine >> generatorVersion) || generatorVersion != 2) return false;
            item.plan.generatorVersion = generatorVersion;
        }
        if (!complete(trackLine)) return false;
        item.plan.durationBars = bars;
        item.plan.profile = candidate.plan.tracks[static_cast<size_t>(index)].profile;
        item.plan.arrangeByParts = byParts;
        item.state = static_cast<AlbumTrackState>(state);
        item.hasMeasurements = measurements == 1;
        if (item.plan != candidate.plan.tracks[static_cast<size_t>(index)]) return false;
    }
    if (in.peek() != std::char_traits<char>::eof() || !validManifest(candidate)) return false;
    manifest = std::move(candidate);
    return true;
}

} // namespace risco
