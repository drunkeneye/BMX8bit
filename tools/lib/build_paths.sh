#!/bin/bash

# Shared path contract for BMX build, staging and install scripts.
# Scripts sourcing this file must define SRC_DIR first.

if [ -z "${SRC_DIR:-}" ]; then
  echo "tools/lib/build_paths.sh requires SRC_DIR to be set" >&2
  exit 1
fi

BMC64_BUILD_ROOT="${BMC64_BUILD_ROOT:-$SRC_DIR/build}"

bmc64_image_dir() {
  local board="$1"
  printf '%s/%s/images\n' "$BMC64_BUILD_ROOT" "$board"
}

bmc64_vice310_image_dir() {
  local board="$1"
  printf '%s/%s/vice310-images\n' "$BMC64_BUILD_ROOT" "$board"
}

bmc64_stage_dir() {
  local board="$1"

  case "$board" in
    pi4)
      printf '%s\n' "${PI4_STAGE_DIR:-${BMC64_STAGE_DIR:-$SRC_DIR/pi4-test/sdcard}}"
      ;;
    pi5)
      printf '%s\n' "${PI5_STAGE_DIR:-${BMC64_STAGE_DIR:-$SRC_DIR/pi5-test/sdcard}}"
      ;;
    *)
      echo "unsupported board for stage directory: $board" >&2
      return 1
      ;;
  esac
}

bmc64_machine_makefile() {
  local machine="$1"
  printf '%s/mk/machines/Makefile-%s\n' "$SRC_DIR" "$machine"
}
