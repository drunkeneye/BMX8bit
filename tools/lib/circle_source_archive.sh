#!/bin/bash

# Extract a pinned circle-stdlib source archive into board-local build trees.
# The archive contains upstream sources plus submodules, but no generated build
# products. Board-specific configuration and build output stays below build/.

circle_stdlib_extract_from_archive() {
  local archive="$1"
  local checksum_file="$2"
  local dest="$3"
  local marker="$dest/.bmx-source-archive.sha256"
  local archive_for_extract="$archive"
  local archive_hash=""
  local cache_lock_fd temporary_archive
  local expected_hash
  local part_glob

  if [ ! -f "$checksum_file" ]; then
    echo "Missing circle-stdlib checksum file: $checksum_file" >&2
    exit 1
  fi

  expected_hash="$(awk -v archive_name="$(basename "$archive")" \
    '$2 ~ archive_name "$" { print $1; exit }' "$checksum_file")"
  if [ -z "$expected_hash" ]; then
    echo "Missing checksum entry for $(basename "$archive")" >&2
    exit 1
  fi

  # A tree carrying the verified source marker no longer needs the split
  # archive just to prove a no-op.  This avoids concatenating and hashing
  # 138 MiB on every target build.
  if [ -f "$marker" ] && [ "$(cat "$marker")" = "$expected_hash" ] && \
     [ -f "$dest/libs/circle/Rules.mk" ]; then
    return
  fi

  if [ ! -f "$archive" ]; then
    part_glob="$archive.part-*"
    if ! compgen -G "$part_glob" >/dev/null; then
      echo "Missing circle-stdlib source archive: $archive" >&2
      echo "Restore third_party/source-cache or recreate the archive before building." >&2
      exit 1
    fi

    archive_for_extract="$BMC64_BUILD_ROOT/source-cache/$(basename "$archive")"
    mkdir -p "$(dirname "$archive_for_extract")"
    command -v flock >/dev/null 2>&1 || {
      echo "flock is required for the shared Circle source cache" >&2
      exit 1
    }
    exec {cache_lock_fd}>"$BMC64_BUILD_ROOT/source-cache/.circle-source-archive.lock"
    flock "$cache_lock_fd"
    if [ -f "$archive_for_extract" ]; then
      archive_hash="$(sha256sum "$archive_for_extract" | awk '{print $1}')"
    fi
    if [ "$archive_hash" != "$expected_hash" ]; then
      temporary_archive="$(mktemp "$BMC64_BUILD_ROOT/source-cache/.circle-archive.XXXXXX")"
      if ! cat "$archive".part-* >"$temporary_archive"; then
        rm -f -- "$temporary_archive"
        exit 1
      fi
      archive_hash="$(sha256sum "$temporary_archive" | awk '{print $1}')"
      if [ "$archive_hash" != "$expected_hash" ]; then
        rm -f -- "$temporary_archive"
        echo "circle-stdlib source archive checksum mismatch" >&2
        exit 1
      fi
      mv -f "$temporary_archive" "$archive_for_extract"
    fi
    flock -u "$cache_lock_fd"
    exec {cache_lock_fd}>&-
  fi

  if [ -z "$archive_hash" ]; then
    archive_hash="$(sha256sum "$archive_for_extract" | awk '{print $1}')"
  fi
  if [ "$archive_hash" != "$expected_hash" ]; then
    echo "circle-stdlib source archive checksum mismatch" >&2
    exit 1
  fi

  rm -rf "$dest"
  mkdir -p "$dest"
  tar -C "$dest" --strip-components=1 -xzf "$archive_for_extract"
  printf '%s\n' "$archive_hash" > "$marker"
}
