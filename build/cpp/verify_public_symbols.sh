#!/usr/bin/env bash

# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -euo pipefail

function usage() {
  echo "Usage: $0 [CXXFILT] [LIBRARY_NAME] [CURRENT_SYMBOLS] [REFERENCE_SYMBOLS] [STAMP] [--warn]"
  echo "Verifies the list of public symbols against a golden file."
  echo "Additionally, checks that none of the symbols is a C++ symbol."
  echo "[CXXFILT]:      path to the llvm-cxxfilt binary"
  echo "[LIBRARY_NAME]: a human-readable name for the library"
  echo "[CURRENT]:      path to the generated symbols file from the extract_public_symbols script"
  echo "[REFERENCE]:    path to the checked in reference symbol file"
  echo "[STAMP]:        touch this file when the script succeeds"
  echo "[--warn]:       (optional) if is specified, only output warnings and do not fail the build"
  exit 1
}

WARN_ON_CHANGES=0

if [[ $# -ne 5 ]]
then
  if [[ $# -ne 6 ]]
  then
    usage
  else
    if ! [[ $6 == "--warn" ]]; then
      usage
    else
      WARN_ON_CHANGES=1
    fi
  fi
fi

readonly CXXFILT=$1
readonly LIBRARY_NAME=$2
readonly CURRENT=$3
readonly REFERENCE=$4
readonly STAMP=$5

if ! [[ -e "$CXXFILT" ]]; then
  echo "Error: $CXXFILT not found" >&2
  usage
fi

if ! [[ -e "$CURRENT" ]]; then
  echo "Error: $CURRENT not found" >&2
  usage
fi

# Detect presence of C++ symbols
ORIGINAL_SYMBOLS=$(cat ${CURRENT})
DEMANGLED_SYMBOLS=$(${CXXFILT} < ${CURRENT})

if ! [[ "$ORIGINAL_SYMBOLS" == "$DEMANGLED_SYMBOLS" ]]; then
  echo
  echo "Error: Prebuilt libraries exported to the SDK should not have C++ symbols"
  echo "In library $LIBRARY_NAME"
  echo "NOTE: the following functions are exported with C++ linkage:"
  while read original <&3 && read demangled <&4; do
    if ! [[ "$original" == "$demangled" ]]; then
      echo "$demangled"
    fi
  done 3<<<"$ORIGINAL_SYMBOLS" 4<<<"$DEMANGLED_SYMBOLS"
  echo

  if [[ "$WARN_ON_CHANGES" -eq 0 ]]; then
    exit 1
  fi
fi

if ! [[ -e "$REFERENCE" ]]; then
  echo "Error: $REFERENCE not found" >&2
  usage
fi

if ! diff ${CURRENT} ${REFERENCE} >/dev/null; then
  echo
  echo "Error: ABI has changed! In library $LIBRARY_NAME"
  echo

  ONLY_IN_CURRENT=$(env LC_ALL=C comm -23 <(cat ${CURRENT}) <(cat ${REFERENCE}))
  ONLY_IN_REFERENCE=$(env LC_ALL=C comm -13 <(cat ${CURRENT}) <(cat ${REFERENCE}))

  if ! [[ -z ${ONLY_IN_CURRENT} ]]; then
    echo "NOTE: the following symbols were added:"
    echo ${ONLY_IN_CURRENT}
    echo
  fi

  if ! [[ -z ${ONLY_IN_REFERENCE} ]]; then
    echo "NOTE: the following symbols were removed:"
    echo ${ONLY_IN_REFERENCE}
    echo
  fi

  echo "Please acknowledge this change by running:"
  echo "cp $CURRENT $REFERENCE"
  echo

  if [[ "$WARN_ON_CHANGES" -eq 0 ]]; then
    exit 1
  fi
fi

touch ${STAMP}
