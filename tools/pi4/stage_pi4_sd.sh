#!/bin/bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SRC_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"

exec "$SRC_DIR/tools/stage_bmx_sd.py" --board pi4 "$@"
