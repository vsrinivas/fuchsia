#!/bin/bash
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -eu -o pipefail # Error checking

# Used to evaluate the cipd string
# shellcheck disable=SC2034
outdir="$1"
yaml="$2"

data_dir="$(grep root "${yaml}" |  cut -d\  -f2)"

symbol_dir=""
eval symbol_dir="${data_dir}"

if [[ ! -e "${symbol_dir}" ]]; then
  echo "Cannot read $yaml to find far file: $symbol_dir"
  exit 1
fi

debug_files=()
while IFS='' read -r line; do debug_files+=("$line"); done < <(find $symbol_dir -type f)


if (( ${#debug_files[@]} != 6 )); then
  echo "Expected 6 files, but got ${#debug_files[@]}"
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
exit 0