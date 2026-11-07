#!/usr/bin/env bash
# Run the 3DGS renderer in VR through the WiVRn OpenXR runtime.
#
# Usage:
#   ./scripts/run-vr.sh [scene] [extra renderer flags]
#
# Examples:
#   ./scripts/run-vr.sh room
#   ./scripts/run-vr.sh room --vr-world-scale 1.4
#   ./scripts/run-vr.sh counter --no-cull
#   ./scripts/run-vr.sh counter --max-splat-fraction 0.8
#   ./scripts/run-vr.sh counter --min-opacity 0.02
#   ./scripts/run-vr.sh counter --max-distance 1.5
#   ./scripts/run-vr.sh room --vr-shading-rate 2
#   ./scripts/run-vr.sh room --vr-foveation 2
#   ./scripts/run-vr.sh room --vr-foveation 2 --vr-foveation-size 0.35
#   ./scripts/run-vr.sh room --vr-foveation 2 --vr-foveation-ring 0.32
#   ./scripts/run-vr.sh room --vr-foveation 2 --vr-foveation-debug
#   ./scripts/run-vr.sh room --vr-foveation 4 --vr-foveation-blur off
#   ./scripts/run-vr.sh room --vr-foveation 4 --vr-refresh 90
#   ./scripts/run-vr.sh room --nosort
#   ./scripts/run-vr.sh counter --frames 120
#
# Environment:
#   XR_RUNTIME_JSON   Override the WiVRn runtime manifest
#   BUILD             Build directory (default: <repo>/build)
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${BUILD:-$ROOT/build}"
BIN="$BUILD/3DGSRendererVulkan"
FLATPAK_DIR="$HOME/.local/share/flatpak/app/io.github.wivrn.wivrn/current/active/files"

SCENE="${1:-room}"
# Resolve a relative .ply path against the invocation directory before we cd.
if [[ -f "$SCENE" ]]; then
    SCENE="$(realpath "$SCENE")"
elif [[ -f "$ROOT/$SCENE" ]]; then
    SCENE="$ROOT/$SCENE"
fi

[[ -x "$BIN" ]] || { echo "Missing $BIN (build first: cmake --build build)" >&2; exit 1; }

export XR_RUNTIME_JSON="${XR_RUNTIME_JSON:-$FLATPAK_DIR/share/openxr/1/openxr_wivrn.json}"
[[ -s "$XR_RUNTIME_JSON" ]] || {
    echo "WiVRn runtime manifest not found: $XR_RUNTIME_JSON" >&2
    echo "Install WiVRn (flatpak install --user flathub io.github.wivrn.wivrn) or set XR_RUNTIME_JSON." >&2
    exit 1
}

if command -v busctl >/dev/null 2>&1 && \
   busctl --user --json=short call io.github.wivrn.Server /io/github/wivrn/Server \
     org.freedesktop.DBus.Properties GetAll s io.github.wivrn.Server >/dev/null 2>&1; then
    echo "WiVRn server: running"
else
    echo "WiVRn server: NOT running. Start it with:"
    echo "  flatpak run --user --command=wivrn-server io.github.wivrn.wivrn"
fi

# The app resolves shaders relative to the working directory.
cd "$BUILD"
exec "$BIN" "$SCENE" --vr "${@:2}"
