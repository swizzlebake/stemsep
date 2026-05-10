#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"

BUILD_DIR="Builds"
ARTIFACT="$BUILD_DIR/StemSep_artefacts/Release/VST3/StemSep.vst3"
VST3_DIR="$HOME/.vst3"
LINK="$VST3_DIR/StemSep.vst3"

if [[ ! -f "$BUILD_DIR/CMakeCache.txt" ]]; then
    echo ">>> Configuring (Ninja, Release)"
    cmake -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Release
fi

echo ">>> Building"
cmake --build "$BUILD_DIR"

if [[ ! -d "$ARTIFACT" ]]; then
    echo "Build did not produce $ARTIFACT" >&2
    exit 1
fi

mkdir -p "$VST3_DIR"
ln -sfn "$PWD/$ARTIFACT" "$LINK"

echo ">>> Linked $LINK -> $PWD/$ARTIFACT"
echo ">>> Open Reaper, then: Options -> Preferences -> Plug-ins -> VST -> Re-scan"
