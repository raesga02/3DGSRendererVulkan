#!/usr/bin/env bash
#
# Download pretrained INRIA 3DGS scenes into assets/models/.
# Defaults to the smaller scenes that comfortably fit in 8 GB of VRAM.
#
# Usage:
#   ./scripts/download_models.sh                 # room + counter
#   ./scripts/download_models.sh room truck      # specific scenes
#   ./scripts/download_models.sh --list          # show all scenes
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT_DIR="${OUT_DIR:-$SCRIPT_DIR/../assets/models}"

if [[ "${1:-}" == "--list" ]]; then
    exec python3 "$SCRIPT_DIR/download_models.py" --list
fi

if [[ $# -eq 0 ]]; then
    set -- room counter
fi

exec python3 "$SCRIPT_DIR/download_models.py" --scenes "$@" --output-dir "$OUT_DIR"
