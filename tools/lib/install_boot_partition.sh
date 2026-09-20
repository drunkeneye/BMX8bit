#!/bin/bash

# Shared safety checks for copying staged BMX boot files to a mounted FAT boot
# partition. Scripts sourcing this file must define SRC_DIR first.

if [ -z "${SRC_DIR:-}" ]; then
  echo "tools/lib/install_boot_partition.sh requires SRC_DIR to be set" >&2
  exit 1
fi

bmc64_die() {
  echo "$*" >&2
  exit 1
}

bmc64_canonical_dir() {
  local dir="$1"
  [ -d "$dir" ] || return 1
  (cd "$dir" && pwd -P)
}

bmc64_parent_block_device() {
  local source="$1"
  local device
  device="$(readlink -f "$source" 2>/dev/null || true)"
  [ -b "$device" ] || return 1

  local parent
  parent="$(lsblk -no PKNAME "$device" 2>/dev/null | head -n 1 || true)"
  if [ -n "$parent" ]; then
    printf '/dev/%s\n' "$parent"
  else
    printf '%s\n' "$device"
  fi
}

bmc64_install_boot_partition() {
  local board_name="$1"
  local stage_dir="$2"
  local mountpoint="$3"
  local kernel_name="$4"

  [ -n "$mountpoint" ] || bmc64_die "missing mountpoint"

  local stage_real
  stage_real="$(bmc64_canonical_dir "$stage_dir")" ||
    bmc64_die "stage directory not found: $stage_dir"

  local mount_real
  mount_real="$(bmc64_canonical_dir "$mountpoint")" ||
    bmc64_die "mountpoint not found: $mountpoint"

  [ -w "$mount_real" ] || bmc64_die "mountpoint not writable: $mount_real"

  for required in cmdline.txt config.txt "$kernel_name"; do
    [ -f "$stage_real/$required" ] ||
      bmc64_die "stage directory is missing required file: $required"
  done

  case "$stage_real/" in
    "$mount_real"/*)
      bmc64_die "refusing to install: stage directory is inside target mountpoint"
      ;;
  esac
  case "$mount_real/" in
    "$stage_real"/*)
      bmc64_die "refusing to install: target mountpoint is inside stage directory"
      ;;
  esac

  command -v mountpoint >/dev/null 2>&1 ||
    bmc64_die "required tool not found: mountpoint"
  command -v findmnt >/dev/null 2>&1 ||
    bmc64_die "required tool not found: findmnt"
  command -v lsblk >/dev/null 2>&1 ||
    bmc64_die "required tool not found: lsblk"

  if ! mountpoint -q -- "$mount_real"; then
    bmc64_die "refusing to install: target is not an exact mountpoint: $mount_real"
  fi

  local fstype
  fstype="$(findmnt -n --target "$mount_real" -o FSTYPE | head -n 1 || true)"
  case "$fstype" in
    vfat|msdos)
      ;;
    *)
      bmc64_die "refusing to install: target filesystem is '$fstype', expected FAT/vfat"
      ;;
  esac

  local target_source
  target_source="$(findmnt -n --target "$mount_real" -o SOURCE | head -n 1 || true)"
  [ -n "$target_source" ] ||
    bmc64_die "refusing to install: cannot determine target block device"

  local target_parent root_source root_parent
  target_parent="$(bmc64_parent_block_device "$target_source" || true)"
  root_source="$(findmnt -n --target / -o SOURCE | head -n 1 || true)"
  root_parent="$(bmc64_parent_block_device "$root_source" || true)"
  if [ -n "$target_parent" ] && [ -n "$root_parent" ] &&
     [ "$target_parent" = "$root_parent" ]; then
    bmc64_die "refusing to install: target appears to be on the current system disk ($target_parent)"
  fi

  echo "installing $board_name stage:"
  echo "  source: $stage_real"
  echo "  target: $mount_real ($target_source, $fstype)"

  find "$mount_real" -mindepth 1 -maxdepth 1 -exec rm -rf {} +
  cp -r "$stage_real"/. "$mount_real"/
  sync

  echo "installed $stage_real -> $mount_real"
}
