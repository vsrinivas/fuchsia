#!/usr/bin/env bash

# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -euo pipefail

function usage() {
  echo "Usage: $0 [LIBRARY_NAME] [CURRENT_IFS] [REFERENCE_IFS] [STAMP] [--warn]"
  echo "Verifies the list of public symbols against a golden file."
  echo "Additionally, checks that none of the symbols is a C++ symbol."
  echo "[LIBRARY_NAME]: a human-readable name for the library"
  echo "[CURRENT]:      path to the ifs file from this library"
  echo "[REFERENCE]:    path to the checked in ifs file"
  echo "[STAMP]:        touch this file when the script succeeds"
  echo "[--warn]:       (optional) if is specified, only output warnings and do not fail the build"
  exit 1
}

function realpath() {
  echo $(cd $(dirname "$1"); pwd)/$(basename "$1")
}

readonly LIBRARY_NAME=$1
readonly CURRENT=$(realpath "$2")
readonly REFERENCE=$(realpath "$3")
readonly STAMP=$4

WARN_ON_CHANGES=0
if [[ ($# -eq 5) && ($5 == "--warn") ]]; then
  WARN_ON_CHANGES=1
fi

for file in $CURRENT $REFERENCE
do
    if ! [[ -e "$file" ]]; then
        echo "Error: $file not found" >&2
        usage
    fi
done

# TODO(fxbug.dev/101666): This is no longer needed, this whole file can be replaced
# with `golden_file` or similar.
if [[ $LIBRARY_NAME != "libc" ]]; then
  # Detect presence of C++ symbols
  if grep -q "Name: _Z" $REFERENCE; then
    echo
    echo "Error: Prebuilt libraries exported to the SDK should not have C++ symbols"
    echo "In library $LIBRARY_NAME"
    echo "NOTE: the following functions are exported with C++ linkage:"
    grep "Name: _Z" $REFERENCE

    if [[ "$WARN_ON_CHANGES" -eq 0 ]]; then
      exit 1
    fi
  fi
fi

if ! diff -U0 $REFERENCE $CURRENT; then
  echo
  echo "Error: ABI has changed! In library $LIBRARY_NAME"
  echo

  echo -e "Please acknowledge this change by running:\ncp $CURRENT $REFERENCE\n"

  if [[ "$WARN_ON_CHANGES" -eq 0 ]]; then
    exit 1
  fi
fi

touch ${STAMP}
