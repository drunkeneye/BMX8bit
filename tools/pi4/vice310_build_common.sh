#!/bin/bash

# Board adapter for the shared incremental VICE 3.10 build implementation.

set -euo pipefail

if [ -z "${SRC_DIR:-}" ]; then
  _BMX_VICE310_COMMON_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
  SRC_DIR="$(cd "$_BMX_VICE310_COMMON_DIR/../.." && pwd)"
fi

BMX_BUILD_BOARD=pi4
. "$SRC_DIR/tools/lib/vice310_build_common.sh"
