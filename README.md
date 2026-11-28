# Risco

<p align="center"><img src="resources/RiscoPixelLogo.svg" width="212" alt="Risco"></p>

Risco is a musical instrument for composing with text, drawings, formulas and
generative forms. It combines a live Bytebeat and Floatbeat player, an editable
arrangement, five independent stems for melody, harmony, bass, rhythm and
texture, historical AY/YM synthesis, effects,
project recovery and WAV export. Generated tracks can be reviewed before they
are saved. Album projects and WAV renders use resumable checkpoints.

## Generative albums

The full track and album dialogs offer **By parts** and **Continuous** arrangements.
By parts alternates groups of channels through an opening, development, break,
return and ending. The full track review lists each part's bars and channels.
**Edit parts** changes channels, length, repetition and dynamics, including
**Arch**, which rises to the center and falls away. Saved arrangements can
reopen this editor through the menu **Salvar → Reeditar partes da faixa salva**
when the project still matches its recorded recipe. The text source can prompt a pause
before the return and the drawing can shape the development's dynamics. A
motif from the source starts in one channel and returns in another. Use **New
seed** and **Swap A/B** in the review to compare two proposals with the same
motif. In generation V2, medium surprise mirrors the motif's intervals on its
return; high surprise also reverses the attack order. The opening remains the
original motif. The **Retorno do motivo** choice defaults to Automatic, or can
be set manually to Literal, Mirror, or Mirror + retrograde independently of
surprise. Older V1 tracks retain their original behavior.

The album planner creates up to 12 independent `.risco` projects from a
numeric seed, the current text or the current drawing. A text or drawing
supplies a suggested seed and a short motif that moves between melody,
bass and texture across the album. Set an album-wide musical profile, then optionally override it
per track with a comma-separated sequence starting at track 1: `O` for
Original, `R` for Related, `C` for Contrasting, and `-` to use the album-wide
profile for that track. For example, `C,R,-,O` overrides the first four
tracks; later tracks use the album-wide profile. The plan review lists each
track's effective profile, form, BPM and duration. For non-original profiles,
it also lists the generated pulse, palette and surprise level before files
are created.

The `album.risco-manifest` records the plan and project/render checkpoints.
Manifest V6 marks new text- and drawing-based albums whose returns develop by
surprise level; V5 preserves the earlier literal motif return. Numeric albums
use V1 to V4 according to their profile and arrangement. Choose **Resume
projects** in the Album menu and open the album folder to continue generation
from its manifest. WAV rendering can be resumed from the same folder.

Risco is written in C++17 with JUCE 9.0.3. Its own code is licensed under
AGPL-3.0-or-later. The audited Ayumi core remains under its MIT license in
`third_party/ayumi`. See [credits and licenses](CREDITOS_E_LICENCAS.md).

## Build

Install CMake 3.22 or newer, a C++17 compiler, Ninja or Make, ALSA, X11,
FreeType, fontconfig and OpenGL development packages. Obtain JUCE 9.0.3 from
the official JUCE distribution and pass its source directory to CMake.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DRISCO_JUCE_SOURCE_DIR=/path/to/JUCE-9.0.3
cmake --build build --parallel 1
ctest --test-dir build --output-on-failure
```

For the framework-independent core and offline renderer, set
`-DRISCO_BUILD_JUCE_APP=OFF`. On Linux, `./run` configures a local Release
build and launches the application. Its default build directory is
`/tmp/risco-juce-release-build`.

## Use and limits

Open Tutorial and Learn in the application for guided exercises. The sample
project is in `examples/primeira_musica.risco`. Imported formulas retain their
provenance and rights status. A formula library may contain third-party or
user-created work and is not included here.

Version 0.1.0 is available for Linux x86_64, Windows x86_64 and macOS arm64
on the [Risco website](https://lucioaraujo.github.io/risco/#versoes) and
[GitHub Releases](https://github.com/lucioaraujo/risco/releases/latest).
The full GitHub Actions matrix compiled all three targets and passed 12/12
automated tests per platform in run
[45734641224](https://github.com/lucioaraujo/risco/actions/runs/45734641224).
The Linux `.deb`, AppImage and user-local `.tar.gz` opened on Debian 12,
Ubuntu 24.04, Fedora and Arch in that run. Windows and macOS builds passed
CI but have not been manually opened on personal devices. The Windows and
macOS archives are portable ZIPs, not installers; macOS is Apple Silicon only,
unsigned and not notarized. Audio hardware and interface behavior still need
evaluation on personal computers; some advanced menus and messages remain in
Portuguese. The historical AY/YM engine documents its fidelity limits in
[the AY/YM research notes](PESQUISA_NUCLEO_AY_FIEL.md). Additional research
and design documents are included in this repository.

## Credits

Created by Lúcio de Araújo. Risco's original code is AGPL-3.0-or-later. Ayumi
is by Peter Sovietov and is MIT-licensed. Bundled DejaVu fonts retain their
upstream notices in `resources/fonts/`. See
[CREDITOS_E_LICENCAS.md](CREDITOS_E_LICENCAS.md) and
[third_party/ayumi/README-RISCO.md](third_party/ayumi/README-RISCO.md).
