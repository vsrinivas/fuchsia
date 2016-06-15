#!/bin/bash

# Copyright 2016 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

if [[ $# -lt 1 ]]; then
  echo "Insufficient number of arguments" >&2
  exit 1
fi

readonly RUNTIME_DEPS="$1"
readonly DEFAULT_NAME="$(basename "${RUNTIME_DEPS}")"
readonly DEFAULT_MANIFEST="${DEFAULT_NAME%.*}.manifest"

MANIFEST=()
while read file; do
  path="bin/$(basename $file)"
  MANIFEST+=("${path}=${file}")
done <${RUNTIME_DEPS}

(IFS=$'\n'; echo "${MANIFEST[*]}" >${2:-$DEFAULT_MANIFEST})
