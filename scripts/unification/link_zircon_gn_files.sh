#!/bin/bash
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Find all BUILD.zircon.gn files that have no matching BUILD.gn under //zircon/
# and create a BUILD.gn symlink to them. Additionally, look at each input file
# and if a zx_library() call is found, inject an import to migrated_targets.gni
# if one doesn't already exist.

set -e

# Find all BUILD.zircon.gn that don't have a corresponding BUILD.gn file.
ZN_FILES="$(find zircon -name "BUILD.zircon.gn" -type f)"
NO_GN_FILES=()

for ZN_FILE in ${ZN_FILES}; do
  # Do not link //zircon/BUILD.zircon.gn
  if [[ "${ZN_FILE}" == "zircon/BUILD.zircon.gn" ]]; then
    continue
  fi
  GN_FILE="${ZN_FILE%.zircon.gn}.gn"
  if ! [[ -e "${GN_FILE}" ]]; then
    NO_GN_FILES+=("${GN_FILE}")
  else
    if [[ -h "${GN_FILE}" ]]; then
      TARGET="$(readlink "${GN_FILE}")"
      if [[ "${TARGET}" != "BUILD.zircon.gn" ]]; then
        echo >&2 "ERROR: ${GN_FILE} is a symlink that does not point to ${ZN_FILE}"
      fi
    fi
  fi
done

# Inject import of migrated_targets.gni into a BUILD.gn file.
# $1: Path to target file.
inject_migrated_targets () {
  TMPFILE=$1.tmp
  awk 'BEGIN { INJECT=1 } { if (INJECT == 1 && NF == 0) { INJECT=0; print "\nimport(\"$zx_build/public/gn/migrated_targets.gni\")\n"; } else { print $0; } }' "$1" > "${TMPFILE}"
  unlink "$1"
  mv "${TMPFILE}" "$1"
}

# Add a BUILD.gn symlink to an existing BUILD.zircon.gn file
# and inject a migrated_targets.gni
# $1: path to BUILD.gn file.
relink () {
  GN_FILE=$1
  if [[ -e "${GN_FILE}" ]]; then
    echo >&2 "ERROR: File already exists: ${GN_FILE}"
    return 1
  fi
  ZN_FILE="${GN_FILE%.gn}.zircon.gn"
  ln -sf BUILD.zircon.gn "${GN_FILE}"
}

for GN_FILE in ${NO_GN_FILES[*]}; do
  echo "Relink: ${GN_FILE}"
  relink "${GN_FILE}"

  ZN_FILE=${GN_FILE%.gn}.zircon.gn
  if grep -q zx_library "${ZN_FILE}"; then
    if ! grep -q "migrated_targets.gni" "${ZN_FILE}"; then
      inject_migrated_targets "${ZN_FILE}"
    fi
  fi
done

