#!/bin/bash
#
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# This script is used by manifest.gni which see.
#   Usage: manifest-cat.sh LISTFILE OUTPUT DEPFILE
# It's just cat with a response file and a dep file.

set -e

readonly LISTFILE="$1"
readonly OUTPUT="$2"
readonly DEPFILE="$3"

readonly FILES=($(<"$LISTFILE"))

cleanup() {
  rm -f "$OUTPUT" "$DEPFILE"
}

trap cleanup ERR HUP INT TERM
cleanup

echo "$OUTPUT: $LISTFILE ${FILES[*]}" > "$DEPFILE"

cat "${FILES[@]}" > "$OUTPUT"
