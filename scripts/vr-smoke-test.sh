#!/usr/bin/env bash
# VR smoke test: run the Khronos OpenXR "hello_xr" sample (Vulkan) through the
# WiVRn OpenXR runtime, streaming to a Quest 3.
#
# This proves the whole path (OpenXR loader -> WiVRn runtime -> Vulkan -> NVENC
# -> headset) before touching the 3DGS renderer.
#
# Prerequisites (one-time, see README notes):
#   - WiVRn server Flatpak installed (io.github.wivrn.wivrn)
#   - OpenXR-SDK-Source built with hello_xr: -DBUILD_SDK_TESTS=ON
#   - adb (Android platform-tools) and the WiVRn Quest APK
#
# Usage:
#   ./scripts/vr-smoke-test.sh                 # run hello_xr in the headset
#   INSTALL_APK=1 ./scripts/vr-smoke-test.sh   # also (re)install the WiVRn APK
#   GRAPHICS_API=Vulkan2 ./scripts/vr-smoke-test.sh
#
# Env overrides:
#   OPENXR_SDK, ADB, APK, WIVRN_MANIFEST, GRAPHICS_API
set -euo pipefail

OPENXR_SDK="${OPENXR_SDK:-$HOME/Develop/TDT16_XR/openxr-sdk}"
HELLO_XR="$OPENXR_SDK/build/src/tests/hello_xr/hello_xr"
LOADER_DIR="$OPENXR_SDK/build/src/loader"
ADB="${ADB:-$HOME/.local/share/android-platform-tools/platform-tools/adb}"
APK="${APK:-$HOME/.local/share/wivrn-apk/WiVRn-release.apk}"
FLATPAK_DIR="$HOME/.local/share/flatpak/app/io.github.wivrn.wivrn/current/active/files"
MANIFEST="${WIVRN_MANIFEST:-$FLATPAK_DIR/share/openxr/1/openxr_wivrn.json}"

[[ -x "$HELLO_XR" ]] || { echo "hello_xr not built: $HELLO_XR" >&2; exit 1; }
[[ -s "$MANIFEST" ]] || { echo "WiVRn manifest missing: $MANIFEST" >&2; exit 1; }

echo "WiVRn manifest: $MANIFEST"
if command -v busctl >/dev/null 2>&1 && \
   busctl --user --json=short call io.github.wivrn.Server /io/github/wivrn/Server \
     org.freedesktop.DBus.Properties GetAll s io.github.wivrn.Server >/dev/null 2>&1; then
    echo "WiVRn server: running"
else
    echo "WiVRn server: NOT running. Start it with:"
    echo "  flatpak run --user --command=wivrn-server io.github.wivrn.wivrn"
fi

echo "--- adb devices ---"
"$ADB" devices || true

if [[ "${INSTALL_APK:-0}" == "1" ]]; then
    dev=$("$ADB" devices | awk '$2 == "device" {print $1; exit}')
    [[ -n "$dev" ]] || { echo "no headset on adb (plug in USB-C and accept the prompt)" >&2; exit 1; }
    [[ -s "$APK" ]] || { echo "APK missing: $APK" >&2; exit 1; }
    "$ADB" -s "$dev" install -r "$APK"
fi

export XR_RUNTIME_JSON="$MANIFEST"
export LD_LIBRARY_PATH="$LOADER_DIR${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
echo "--- launching hello_xr (${GRAPHICS_API:-Vulkan}) ---"
exec "$HELLO_XR" -g "${GRAPHICS_API:-Vulkan}" "$@"
