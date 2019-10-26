#!/usr/bin/env bash

# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -euo pipefail

function usage() {
  echo "Usage: $0 [CXXFILT] [LIBRARY_NAME] [CURRENT_SYMBOLS] [ALLOWLIST] [STAMP] [--warn]"
  echo "Verifies the list of imported symbols against an allowlist."
  echo "Additionally, checks that none of the symbols is a C++ symbol."
  echo "[CXXFILT]:      path to the llvm-cxxfilt binary"
  echo "[LIBRARY_NAME]: a human-readable name for the library"
  echo "[CURRENT]:      path to the generated symbols file from the extract_imported_symbols script"
  echo "[ALLOWLIST]:    path to the checked in symbol allowlist"
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
readonly ALLOWLIST=$4
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
  echo "Error: Cannot import C++ symbols"
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

if ! [[ -e "$ALLOWLIST" ]]; then
  echo "Error: $ALLOWLIST not found" >&2
  usage
fi

VIOLATIONS=$(comm -23 <(cat ${CURRENT} | sort) <(cat ${ALLOWLIST} | sort))
if ! [[ -z "${VIOLATIONS}" ]]; then
  echo
  echo "Error: Library $LIBRARY_NAME contains symbols not on the allowlist"
  echo

  echo "NOTE: the following symbols were not on the allowlist:"
  echo "${VIOLATIONS}"
  echo
  echo "The allowlist is located at: ${ALLOWLIST}"

  if [[ "$WARN_ON_CHANGES" -eq 0 ]]; then
    exit 1
  fi
fi

touch ${STAMP}
