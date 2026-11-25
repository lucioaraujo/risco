#!/usr/bin/env bash
# Build AppImage and user-local-installable tar.gz from the same CMake install tree as the .deb.
# Usage: empacotar.sh <build-dir> <output-dir> <linuxdeploy.AppImage> <runtime> <runtime-license>
set -euo pipefail

BUILD=${1:?missing build directory}
OUTPUT=${2:?missing output directory}
LINUXDEPLOY=${3:?missing pinned linuxdeploy}
RUNTIME=${4:?missing pinned AppImage runtime}
RUNTIME_LICENSE=${5:?missing AppImage runtime license}
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd -P)
LINUXDEPLOY=$(realpath "$LINUXDEPLOY")
RUNTIME=$(realpath "$RUNTIME")
RUNTIME_LICENSE=$(realpath "$RUNTIME_LICENSE")
VERSION=$(sed -n 's/^CMAKE_PROJECT_VERSION:STATIC=//p' "$BUILD/CMakeCache.txt")
[[ -n "$VERSION" ]] || { echo "version not found in CMakeCache" >&2; exit 1; }
ARCH=$(uname -m)
[[ "$ARCH" == x86_64 ]] || { echo "unsupported Linux architecture: $ARCH" >&2; exit 1; }
NAME="risco-${VERSION}-linux-${ARCH}"
mkdir -p "$OUTPUT"
OUTPUT=$(cd "$OUTPUT" && pwd -P)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

APPDIR="$WORK/AppDir"
cmake --install "$BUILD" --config Release --prefix "$APPDIR/usr"
BIN="$APPDIR/usr/bin/Risco"
DESKTOP="$APPDIR/usr/share/applications/risco.desktop"
ICON="$APPDIR/usr/share/icons/hicolor/512x512/apps/risco.png"
test -x "$BIN" && test -f "$DESKTOP" && test -f "$ICON"

# The .tar.gz contains the same installed /usr tree, plus a reversible
# per-user installer. User compositions and preferences remain outside it.
PORTABLE="$WORK/$NAME"
mkdir -p "$PORTABLE"
cp -a "$APPDIR/usr" "$PORTABLE/usr"
install -m 755 "$ROOT/packaging/linux/install.sh" "$PORTABLE/install.sh"
install -m 644 "$ROOT/packaging/linux/install_portable.py" "$PORTABLE/install_portable.py"
cat >"$PORTABLE/README.txt" <<EOF
Risco $VERSION — Linux x86_64

Install for this user, without root: ./install.sh
Remove this installation: ./install.sh --remove
The app is also available as a direct executable at ./usr/bin/Risco.
The archive includes licenses and credits under ./usr/share/doc/risco/.
User projects and preferences are stored separately and are not removed.
EOF
(cd "$PORTABLE" && find usr -type f -print0 | sort -z | xargs -0 sha256sum > SHA256SUMS)
tar --sort=name --owner=0 --group=0 --numeric-owner --mtime='UTC 1970-01-01' \
    -czf "$OUTPUT/$NAME.tar.gz" -C "$WORK" "$NAME"

# Preserve the exact AppImage runtime revision and its license in the app.
mkdir -p "$APPDIR/usr/share/doc/risco/appimage-runtime"
install -m 644 "$RUNTIME_LICENSE" "$APPDIR/usr/share/doc/risco/appimage-runtime/LICENSE"
test "$(sha256sum "$LINUXDEPLOY" | cut -d ' ' -f 1)" = \
    c20cd71e3a4e3b80c3483cef793cda3f4e990aca14014d23c544ca3ce1270b4d
test "$(sha256sum "$RUNTIME" | cut -d ' ' -f 1)" = \
    156f4bdbde9c52d01814600013e0a273f0118dc2de98975f3c8c63427ec79074
test "$(sha256sum "$RUNTIME_LICENSE" | cut -d ' ' -f 1)" = \
    aa154fc9070614bbe7921f89db11efd1dba7a1f3a41685958110e2230f9c0ca1
chmod +x "$LINUXDEPLOY"
export APPIMAGE_EXTRACT_AND_RUN=1 ARCH=x86_64 LDAI_OUTPUT="$OUTPUT/$NAME.AppImage"
(cd "$WORK" && "$LINUXDEPLOY" --appdir "$APPDIR" --executable "$BIN" \
    --desktop-file "$DESKTOP" --icon-file "$ICON" --runtime "$RUNTIME" \
    --runtime-license "$RUNTIME_LICENSE" --output appimage)
chmod +x "$OUTPUT/$NAME.AppImage"

(cd "$OUTPUT" && sha256sum "$NAME.AppImage" "$NAME.tar.gz" > SHA256SUMS)
(cd "$OUTPUT" && sha256sum -c SHA256SUMS)
