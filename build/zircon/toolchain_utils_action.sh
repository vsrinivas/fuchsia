#!/bin/bash
#
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# This script is used by c_utils.gni, which see.
#
# Usage: toolchain_utils_action.sh OUTPUT DEPFILE PROGRAM ARG...
#
# This runs PROGRAM ARG... with some special handling.
# It always writes DEPFILE with OUTPUT as its target.
#
# For each ARG that is "@RSPFILE", RSPFILE should contain nothing
# but file names, one per line.  Each such file will be added as
# an input in the depfile.
#
# Most toolchain utilities handle "@RSPFILE" syntax themselves, so we let them
# do so after scanning RSPFILE to collect dependencies.  However, for certain
# named tools known to be lacking the support, this script will splice the
# file contents into the argument list directly.

set -e

OUTPUT="$1"
DEPFILE="$2"
PROGRAM="$3"
shift 3

trap 'rm -f "$OUTPUT" "$DEPFILE"' ERR HUP INT TERM

if [ "${PROGRAM##*/}" = llvm-objcopy ]; then
  expand_rspfile=true
else
  expand_rspfile=false
fi

INPUTS=()
ARGS=()

read_rspfile() {
  local file
  while read file; do
    INPUTS+=("$file")
    if $expand_rspfile; then
      ARGS+=("$file")
    fi
  done
}

for arg; do
  if [[ "$arg" == @* ]]; then
    rspfile="${arg#@}"
    INPUTS+=("$rspfile")
    read_rspfile < "$rspfile"
    if $expand_rspfile; then
      # Don't add $arg since its contents were already added.
      continue
    fi
  fi
  ARGS+=("$arg")
done

echo "$OUTPUT: ${FILES[*]}" > "$DEPFILE"

exec "$PROGRAM" "${ARGS[@]}"
