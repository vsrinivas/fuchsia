#!/bin/bash
# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

help="This script finds directories that don’t have OWNERS files. Usage:

  $0

Searches the top-level directories underneath the current working directory.

  $0 directory [...]

Searches the top-level directories underneath the given \`directory\`
arguments. Example usage:

  cd ~/fuchsia
  ./scripts/owner/missing-owners.sh third_party"

find_missing_owners() {
  if [[ ! -d "$1" ]]; then
    echo "Not a directory: $1" > /dev/stderr
    return 1
  fi

  find "$1" -maxdepth 1 -type d | while read -r d; do
    local owners="$d/OWNERS"
    if [[ ! -f "$owners" || ! -s "$owners" ]]; then
      echo "$d"
    fi
  done
}

if [[ $# -gt 0 && ("$1" == "-h" || "$1" == "--help") ]]; then
  echo "$help"
  exit 0
fi

for d in "${@-.}"; do
  find_missing_owners "$d"
done
