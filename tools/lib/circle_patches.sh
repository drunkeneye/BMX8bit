#!/bin/bash

circle_patchset_hash() {
  local patch_dir="$1"

  if [ ! -d "$patch_dir" ]; then
    printf '%s\n' "no-patches"
    return
  fi

  (
    cd "$patch_dir"
    find . -maxdepth 1 -type f -name '*.patch' -print | sort | while IFS= read -r patch; do
      sha256sum "$patch"
    done
  ) | sha256sum | awk '{ print $1 }'
}

circle_stdlib_patchset_matches() {
  local circle_stdlib_home="$1"
  local expected_hash="$2"
  local marker_dir="$circle_stdlib_home/.bmc64-circle-patches"
  local patchset_marker="$marker_dir/patchset.sha256"

  if [ ! -d "$circle_stdlib_home" ] || [ ! -d "$marker_dir" ]; then
    return 0
  fi

  [ -f "$patchset_marker" ] && [ "$(cat "$patchset_marker")" = "$expected_hash" ]
}

record_circle_stdlib_patchset() {
  local circle_stdlib_home="$1"
  local patchset_hash="$2"
  local marker_dir="$circle_stdlib_home/.bmc64-circle-patches"

  mkdir -p "$marker_dir"
  printf '%s\n' "$patchset_hash" > "$marker_dir/patchset.sha256"
}

apply_circle_stdlib_patches() {
  local circle_stdlib_home="$1"
  local patch_dir="$2"
  local patch
  local git_root
  local marker_dir

  if [ ! -d "$patch_dir" ]; then
    return
  fi

  patch_dir="$(cd "$patch_dir" && pwd)"
  circle_stdlib_home="$(cd "$circle_stdlib_home" && pwd)"
  git_root="$(git -C "$circle_stdlib_home" rev-parse --show-toplevel 2>/dev/null || true)"
  marker_dir="$circle_stdlib_home/.bmc64-circle-patches"
  mkdir -p "$marker_dir"

  for patch in "$patch_dir"/*.patch; do
    [ -e "$patch" ] || continue

    local patch_basename
    local patch_hash
    local patch_marker

    patch_basename="$(basename "$patch")"
    patch_hash="$(sha256sum "$patch" | awk '{ print $1 }')"
    patch_marker="$marker_dir/$patch_basename.sha256"
    if [ -f "$patch_marker" ] && [ "$(cat "$patch_marker")" = "$patch_hash" ]; then
      continue
    fi

    if [ "$git_root" = "$circle_stdlib_home" ]; then
      if git -C "$circle_stdlib_home" apply --check "$patch" >/dev/null 2>&1; then
        echo "Applying Circle patch: $patch_basename"
        git -C "$circle_stdlib_home" apply "$patch"
        printf '%s\n' "$patch_hash" > "$patch_marker"
      elif git -C "$circle_stdlib_home" apply --reverse --check "$patch" >/dev/null 2>&1; then
        printf '%s\n' "$patch_hash" > "$patch_marker"
      else
        echo "Circle patch cannot be applied: $patch" >&2
        git -C "$circle_stdlib_home" apply --check "$patch"
        exit 1
      fi
    else
      local patch_check_output
      patch_check_output="$(mktemp)"
      if patch -d "$circle_stdlib_home" -p1 --forward --dry-run < "$patch" >"$patch_check_output" 2>&1; then
        echo "Applying Circle patch: $patch_basename"
        patch -d "$circle_stdlib_home" -p1 --forward < "$patch"
        printf '%s\n' "$patch_hash" > "$patch_marker"
        rm -f "$patch_check_output"
        continue
      fi

      if patch -d "$circle_stdlib_home" -p1 --reverse --dry-run < "$patch" >/dev/null 2>&1; then
        printf '%s\n' "$patch_hash" > "$patch_marker"
        rm -f "$patch_check_output"
        continue
      fi

      if grep -q "Reversed (or previously applied) patch detected" "$patch_check_output" &&
         ! grep -q "FAILED" "$patch_check_output"; then
        printf '%s\n' "$patch_hash" > "$patch_marker"
        rm -f "$patch_check_output"
        continue
      fi

      echo "Circle patch cannot be applied: $patch" >&2
      cat "$patch_check_output" >&2
      rm -f "$patch_check_output"
      patch -d "$circle_stdlib_home" -p1 --forward --dry-run < "$patch"
      exit 1
    fi
  done
}
