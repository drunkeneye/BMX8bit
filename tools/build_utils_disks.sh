#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SOURCE_DIR="$ROOT/utils-disk"
STAGE_DIR="${1:-}"

usage() {
  cat <<EOF
Usage: $(basename "$0") STAGE_DIR

Build per-machine BMX utility disk images from utils-disk/<machine>/ into
STAGE_DIR/utils/<machine>/utils.d64.

Set C1541=/path/to/c1541 to override c1541 discovery.
EOF
}

die() {
  echo "build_utils_disks: $*" >&2
  exit 1
}

find_c1541() {
  if [[ -n "${C1541:-}" ]]; then
    [[ -x "$C1541" ]] || die "C1541 is set but not executable: $C1541"
    printf '%s\n' "$C1541"
    return
  fi

  if [[ -x "$ROOT/third_party/vice-3.10/src/c1541" ]]; then
    printf '%s\n' "$ROOT/third_party/vice-3.10/src/c1541"
    return
  fi

  if command -v c1541 >/dev/null 2>&1; then
    command -v c1541
    return
  fi

  die "c1541 not found; build VICE c1541 or install it, or set C1541"
}

machine_has_files() {
  local machine="$1"
  local src_dir="$SOURCE_DIR/$machine"

  [[ -d "$src_dir" ]] || return 1
  find "$src_dir" -maxdepth 1 -type f ! -name '.*' -print -quit | grep -q .
}

build_machine_disk() {
  local c1541="$1"
  local machine="$2"
  local src_dir="$SOURCE_DIR/$machine"
  local dest_dir="$STAGE_DIR/utils/$machine"
  local out="$dest_dir/utils.d64"
  local tmp="$out.tmp"
  local file name
  local args=(-format "bmx utils,ut" d64 "$tmp")

  mkdir -p "$dest_dir"
  rm -f "$tmp" "$out"

  while IFS= read -r -d '' file; do
    name="$(basename "$file")"
    name="${name%.*}"
    args+=(-write "$file" "$name")
  done < <(find "$src_dir" -maxdepth 1 -type f ! -name '.*' -print0 | sort -z)

  "$c1541" "${args[@]}" >/dev/null
  mv "$tmp" "$out"
  echo "Built $out"
}

if [[ -z "$STAGE_DIR" || "${STAGE_DIR:-}" == "-h" || "${STAGE_DIR:-}" == "--help" ]]; then
  usage
  [[ -n "$STAGE_DIR" ]] && exit 0
  exit 1
fi

for machine in c64 scpu64 c128 vic20 plus4 pet; do
  mkdir -p "$STAGE_DIR/utils/$machine"
done

have_sources=0
for machine in c64 scpu64 c128 vic20 plus4 pet; do
  if machine_has_files "$machine"; then
    have_sources=1
    break
  fi
done

if [[ "$have_sources" -eq 0 ]]; then
  exit 0
fi

c1541="$(find_c1541)"

for machine in c64 scpu64 c128 vic20 plus4 pet; do
  if machine_has_files "$machine"; then
    build_machine_disk "$c1541" "$machine"
  fi
done
