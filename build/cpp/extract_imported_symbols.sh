#!/usr/bin/env bash

# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -euo pipefail

function usage() {
  echo "Usage: $0 [NM] [LIBRARY] [SYMBOLS]"
  echo "Dump the list of imported symbols from a prebuilt library"
  echo "[NM]:      path to the llvm-nm binary"
  echo "[LIBRARY]: path to the prebuilt library"
  echo "[SYMBOLS]: path to the output symbols file"
  exit 1
}

if [[ $# -ne 3 ]]
then
    usage
fi

readonly NM=$1
readonly LIBRARY=$2
readonly SYMBOLS=$3

if ! [[ -e "$NM" ]]; then
  echo "Error: $NM not found" >&2
  usage
fi

if ! [[ -e "$LIBRARY" ]]; then
  echo "Error: $LIBRARY not found" >&2
  usage
fi

mkdir -p $(dirname "${SYMBOLS}")

if [[ ${LIBRARY} == *.a ]]
then
  echo "Error: $LIBRARY, support for static libraries is not ready yet."
elif ! [[ ( ${LIBRARY} == *.so ) || ( ${LIBRARY} == *.so.debug ) ]]
then
  echo "Error: cannot handle $LIBRARY" >&2
  exit 1
fi

${NM} --dynamic --undefined-only --no-weak ${LIBRARY} \
  | awk '{ print $2; }' \
  | env LC_COLLATE=C sort \
  > ${SYMBOLS}
