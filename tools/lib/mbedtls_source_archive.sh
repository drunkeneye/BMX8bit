#!/bin/bash

# Install the repository-pinned, checksum-verified Mbed TLS release into a
# generated circle-stdlib tree. The upstream source itself intentionally stays
# out of the Circle patch set.

BMX_MBEDTLS_VERSION=3.6.7
BMX_MBEDTLS_ARCHIVE="mbedtls-$BMX_MBEDTLS_VERSION.tar.bz2"
BMX_MBEDTLS_ARCHIVE_REL="third_party/source-cache/$BMX_MBEDTLS_ARCHIVE"
BMX_MBEDTLS_CHECKSUM_REL=third_party/source-cache/SHA256SUMS

mbedtls_die() {
  echo "Mbed TLS source error: $*" >&2
  return 1
}

mbedtls_load_pin() {
  local source_root="$1"
  local checksum_file="$source_root/$BMX_MBEDTLS_CHECKSUM_REL"
  local archive="$source_root/$BMX_MBEDTLS_ARCHIVE_REL"
  local checksum_count

  if [ ! -f "$checksum_file" ]; then
    mbedtls_die "missing source checksum registry: $checksum_file"
    return 1
  fi
  if [ ! -f "$archive" ]; then
    mbedtls_die "missing repository Mbed TLS source archive: $archive"
    return 1
  fi

  checksum_count="$(awk -v archive="$BMX_MBEDTLS_ARCHIVE_REL" \
    '$2 == archive { count++ } END { print count + 0 }' "$checksum_file")"
  if [ "$checksum_count" != 1 ]; then
    mbedtls_die "expected one checksum entry for $BMX_MBEDTLS_ARCHIVE_REL"
    return 1
  fi
  BMX_MBEDTLS_SHA256="$(awk -v archive="$BMX_MBEDTLS_ARCHIVE_REL" \
    '$2 == archive { print $1; exit }' "$checksum_file")"
  if [[ ! "$BMX_MBEDTLS_SHA256" =~ ^[0-9a-f]{64}$ ]]; then
    mbedtls_die "invalid pinned SHA-256"
    return 1
  fi
  if [ "$(sha256sum "$archive" | awk '{print $1}')" != \
       "$BMX_MBEDTLS_SHA256" ]; then
    mbedtls_die "archive checksum mismatch: $archive"
    return 1
  fi

  BMX_MBEDTLS_RESOLVED_ARCHIVE="$archive"
  mbedtls_validate_archive_paths "$archive"
}

mbedtls_validate_archive_paths() {
  local archive="$1"
  local expected_root="mbedtls-$BMX_MBEDTLS_VERSION"

  if ! tar -tjf "$archive" | awk -v root="$expected_root" '
    BEGIN { count = 0 }
    {
      count++
      name = $0
      if (name ~ /^\// || name ~ /(^|\/)\.\.($|\/)/ || name ~ /(^|\/)\.($|\/)/) {
        exit 1
      }
      split(name, parts, "/")
      if (parts[1] != root) {
        exit 1
      }
    }
    END { if (count == 0) exit 1 }
  '; then
    mbedtls_die "unsafe or unexpected archive layout: $archive"
    return 1
  fi
}

circle_stdlib_install_pinned_mbedtls() {
  local source_root="$1"
  local circle_stdlib_home="$2"
  local destination="$circle_stdlib_home/libs/mbedtls"
  local marker="$destination/.bmx-source-archive.sha256"
  local extract_parent="$circle_stdlib_home/libs"
  local extract_dir
  local extracted_root

  if [ ! -f "$circle_stdlib_home/libs/circle/Rules.mk" ]; then
    mbedtls_die "not a prepared circle-stdlib tree: $circle_stdlib_home"
    return 1
  fi

  mbedtls_load_pin "$source_root" || return 1

  if [ -f "$marker" ] && [ "$(cat "$marker")" = "$BMX_MBEDTLS_SHA256" ] && \
     awk -v version="$BMX_MBEDTLS_VERSION" \
       '$1 == "#define" && $2 == "MBEDTLS_VERSION_STRING" && $3 == "\"" version "\"" { found = 1 }
        END { exit found ? 0 : 1 }' \
       "$destination/include/mbedtls/build_info.h" 2>/dev/null; then
    return 0
  fi

  extract_dir="$(mktemp -d "$extract_parent/.mbedtls-extract.XXXXXX")"
  if ! tar -C "$extract_dir" -xjf "$BMX_MBEDTLS_RESOLVED_ARCHIVE"; then
    rm -rf "$extract_dir"
    mbedtls_die "cannot extract source archive"
    return 1
  fi
  extracted_root="$extract_dir/mbedtls-$BMX_MBEDTLS_VERSION"
  if [ ! -f "$extracted_root/README.md" ] || \
     [ ! -f "$extracted_root/library/Makefile" ] || \
     [ ! -f "$extracted_root/include/mbedtls/version.h" ]; then
    rm -rf "$extract_dir"
    mbedtls_die "release archive is incomplete"
    return 1
  fi

  # destination is deliberately derived from the validated Circle tree above.
  rm -rf "$destination"
  mv "$extracted_root" "$destination"
  rm -rf "$extract_dir"
  printf '%s\n' "$BMX_MBEDTLS_SHA256" > "$marker"
  echo "Installed pinned Mbed TLS $BMX_MBEDTLS_VERSION into $circle_stdlib_home"
}
