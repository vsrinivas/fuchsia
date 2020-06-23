#!/bin/bash
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -eu -o pipefail # Error checking

outdir="$1"
yaml="$2"

data_dir="$(grep root "${yaml}" |  cut -d\  -f2)"

dirs=()
while IFS='' read -r line; do dirs+=("$line"); done < <(grep dir: "${yaml}" |  cut -d\  -f3)

for d in "${dirs[@]}"
do
  #if there is a variable, eval the string.
  if [[ "${data_dir}" =~ \${}/.* ]]; then
    eval symbol_dir="${data_dir}/${d}"
  else
    symbol_dir="${outdir}/${data_dir}/${d}"
  fi

  if [[ ! -d "${symbol_dir}" ]]; then
    echo "Cannot read $yaml to find directory: $symbol_dir"
    exit 1
  fi

  debug_files=()
  while IFS='' read -r line; do debug_files+=("$line"); done < <(find "${symbol_dir}" -name "*.debug" -type f)

  EXPECTED_NUM=6
  if (( ${#debug_files[@]} != "${EXPECTED_NUM}" )); then
    echo "Expected ${EXPECTED_NUM} files, but got ${#debug_files[@]}"
    echo "${debug_files[*]}"
    exit 2
  fi

  for f in "${debug_files[@]}"
  do
    description="$(file "$f")"
    if [[ ! "$description" == *"not stripped"* ]]; then
      echo "Expected not stripped binary for $f, got $description"
      exit 2
    fi
  done
done
exit 0
