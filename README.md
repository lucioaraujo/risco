# Risco

Risco is a musical instrument for composing with text, drawings, formulas and
generative forms. It combines a live Bytebeat and Floatbeat player, an editable
arrangement, three independent stems, historical AY/YM synthesis, effects,
project recovery and WAV export. Generated tracks can be reviewed before they
are saved. Album projects and WAV renders use resumable checkpoints.

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

The native application currently targets Linux. Windows and macOS have not
been validated. The historical AY/YM engine documents its fidelity limits in
[the AY/YM research notes](PESQUISA_NUCLEO_AY_FIEL.md). Additional research
and design documents are included in this repository.

## Credits

Created by Lúcio de Araújo. Risco's original code is AGPL-3.0-or-later. Ayumi
is by Peter Sovietov and is MIT-licensed. Bundled DejaVu fonts retain their
upstream notices in `resources/fonts/`. See
[CREDITOS_E_LICENCAS.md](CREDITOS_E_LICENCAS.md) and
[third_party/ayumi/README-RISCO.md](third_party/ayumi/README-RISCO.md).
