#!/usr/bin/env bash

# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -euo pipefail

function usage() {
  echo "Usage: $0 [TARGET] [LIBRARIES_USED] [LIBRARIES_ALLOWLIST] [OUT_FILE]"
  echo "Check the libraries that a target uses against an allowlist."
  echo "Output the libraries that the target uses"
  echo "[TARGET]: the name of the target we are checking (used for error printing)"
  echo "[LIBRARIES_USED]: path to a file that lists the libraries being used"
  echo "[LIBRARIES_ALLOWLIST]: path to a file that lists the allowlist of accepted libraries"
  echo "[OUT_FILE]: path to the output file that is generated"
  exit 1
}

if [[ $# -ne 4 ]]
then
    usage
fi

readonly TARGET=$1
readonly LIBRARIES_USED=$2
readonly LIBRARIES_ALLOWLIST=$3
readonly OUT_FILE=$4


if ! [[ -e "$LIBRARIES_USED" ]]; then
  echo "Error: $LIBRARIES_USED not found" >&2
  usage
fi

 if ! [[ -e "$LIBRARIES_ALLOWLIST" ]]; then
   echo "Error: Allowlist: $LIBRARIES_ALLOWLIST not found" >&2
   usage
 fi

# Remove any beginning paths and lines that aren't actually .so libraries.
LIBRARIES_USED_STRIPPED=$(sed -n 's/^.*\/\(.*\.so\)$/\1/p' "${LIBRARIES_USED}")

VIOLATIONS=$(comm -23 <(echo "${LIBRARIES_USED_STRIPPED}" | sort) <(sort "${LIBRARIES_ALLOWLIST}"))
if [[ -n "${VIOLATIONS}" ]]; then
  echo "Error: Target $TARGET contains shared libraries not on the allowlist"
  echo
  echo "NOTE: the following shared libraries were not on the allowlist:"
  echo "${VIOLATIONS}"
  echo
  echo "The allowlist contains:"
  cat "${LIBRARIES_ALLOWLIST}"

  exit 1
fi

echo "$LIBRARIES_USED_STRIPPED"  > "${OUT_FILE}"
