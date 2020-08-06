#!/bin/bash
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -eu -o pipefail # Error checking

# Used to evaluate the cipd string
outdir="$1"
yaml="$2"

data_dir="$(grep root: "${yaml}" |  cut -d\  -f2)"

files=()
while IFS='' read -r line; do files+=("$line"); done < <(grep file: "${yaml}" |  cut -d\  -f3)

for f in "${files[@]}"
do
  #if there is a variable, eval the string.
  if [[ "${data_dir}" =~ \${}/.* ]]; then
    eval far_file="${data_dir}/${f}"
  else
    # if the root is relative, then prepend the outdir
    if [[ "${data_dir}" != "/"* ]]; then
      far_file="${outdir}/${data_dir}/${f}"
    else
      far_file="${data_dir}/${f}"
    fi
  fi

  if [[ ! -e "${far_file}" ]]; then
    echo "Cannot read $yaml to find far file: $far_file"
    exit 1
  fi
done
exit 0
