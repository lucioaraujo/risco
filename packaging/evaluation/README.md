# Risco 0.1.0 — evaluation builds

These archives contain evaluation builds produced by GitHub Actions from the
source at tag `v0.1.0-evaluation`. The build matrix completed successfully on
Linux x86_64, Windows x86_64 and macOS arm64; all 12 automated tests passed on
each target in [run 45705536064](https://github.com/lucioaraujo/risco/actions/runs/45705536064).

The CI checks compile the app and run the automated suite. They do not verify
startup, audio devices, fonts or the interface on a personal computer. These
are application builds inside archives, not installers. Read the platform
notes below before use.

## Platform notes

- **Linux x86_64:** built on Ubuntu 24.04. Uses the host's X11, ALSA,
  FreeType and Fontconfig libraries. Install the corresponding runtime
  packages for your distribution. Launch `Risco` from a terminal to see
  startup errors.
- **Windows x86_64:** built on Windows Server 2022 with Visual Studio 2022.
  Extract the archive and launch `Risco.exe`. If Windows reports a missing
  MSVC runtime DLL, install the official [Microsoft Visual C++ v14 x64
  Redistributable](https://aka.ms/vc14/vc_redist.x64.exe), then launch again.
  Microsoft recommends this current v14 runtime for applications built with
  Visual Studio 2017, 2019, 2022 or 2026; see its
  [supported downloads and details](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist/).
- **macOS arm64:** built on macOS 15 for Apple Silicon. The app is not signed
  or notarized. Intel Macs are not included in this evaluation build.

## Source and notices

The corresponding Risco source is available in this public repository at the
same tag. Risco code is licensed under GNU AGPL-3.0-or-later. JUCE 9.0.3
modules are licensed under AGPLv3; the audited Ayumi core retains its MIT
license. Bundled DejaVu fonts retain their upstream license. The archives
include the applicable notices and links to the source trees.

The app does not bundle the user's local formula library. Imported formulas
and compositions may carry separate authorship and license terms. See
`NOTICE.md` and the included license texts before redistributing the app.
