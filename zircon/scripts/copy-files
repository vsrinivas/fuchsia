#!/bin/bash
#
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -e

if [ $# -ne 3 ]; then
  echo >&2 "Usage: $0 OUTDIR DEPFILE LISTFILE"
fi

OUTDIR="$1"
DEPFILE="$2"
LISTFILE="$3"

FILES=()
while read file; do
  FILES+=("$file")
done < "$LISTFILE"

echo "$DEPFILE: ${FILES[*]}" > "$DEPFILE"
mkdir -p "$OUTDIR"
ln -f "${FILES[@]}" "$OUTDIR"
