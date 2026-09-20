#!/bin/bash
# Fetch the Atari 8-bit demos/intros collection and extract it into a
# staged SD tree at disks/atari800/demos.
#
# Best effort by design: missing 7z, offline hosts, or a bad archive all
# skip quietly (one note line) without failing the build. Run it again
# after fixing the cause; the 40MB archive is cached under downloads/.
#
# Usage: fetch_demos.sh <stage-dir>
set -uo pipefail

SRC_DIR="$(cd "$(dirname "$0")/.." && pwd)"
STAGE_DIR="${1:-}"
if [ -z "$STAGE_DIR" ]; then
  echo "fetch_demos: no stage dir given, skipping demos" >&2
  exit 0
fi

ARCHIVE_URL="http://ftp.pigwa.net/stuff/collections/atari_8bit_demos_and_intros/atari_8bit_demos_and_intros_%5B2014-10-30%5D.7z"
ARCHIVE_NAME="atari_8bit_demos_and_intros_[2014-10-30].7z"
CACHE_DIR="$SRC_DIR/downloads"
CACHE_FILE="$CACHE_DIR/$ARCHIVE_NAME"
DEST="$STAGE_DIR/disks/atari800/demos"

if [ -d "$DEST" ] && [ -n "$(ls -A "$DEST" 2>/dev/null)" ]; then
  echo "fetch_demos: $DEST already populated, skipping"
  exit 0
fi

SEVENZ=""
for candidate in 7z 7zz 7zr; do
  if command -v "$candidate" >/dev/null 2>&1; then
    SEVENZ="$candidate"
    break
  fi
done
if [ -z "$SEVENZ" ]; then
  echo "fetch_demos: no 7z installed, skipping demos" >&2
  exit 0
fi

mkdir -p "$CACHE_DIR"
if [ ! -s "$CACHE_FILE" ]; then
  echo "fetch_demos: downloading demos collection (~40MB) ..."
  if ! curl -sSL --fail --max-time 600 "$ARCHIVE_URL" -o "$CACHE_FILE.part"; then
    echo "fetch_demos: download failed, skipping demos" >&2
    rm -f "$CACHE_FILE.part"
    exit 0
  fi
  mv "$CACHE_FILE.part" "$CACHE_FILE"
fi

# Extract to a sibling temp dir first: a failed run must not leave a
# half-filled DEST behind (which would trip the populated check above).
rm -rf "$DEST.part"
mkdir -p "$DEST.part"
if ! "$SEVENZ" x -y -o"$DEST.part" "$CACHE_FILE" >/dev/null 2>&1; then
  echo "fetch_demos: extraction failed, skipping demos" >&2
  rm -rf "$DEST.part"
  exit 0
fi
if [ -z "$(ls -A "$DEST.part" 2>/dev/null)" ]; then
  echo "fetch_demos: archive extracted empty, skipping demos" >&2
  rm -rf "$DEST.part"
  exit 0
fi
rm -rf "$DEST"
mv "$DEST.part" "$DEST"
echo "fetch_demos: demos staged at $DEST"
exit 0
