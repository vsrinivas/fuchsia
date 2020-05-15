#!/bin/bash
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -eu # Error checking
err_print() {
  cleanup "${FAR_FILE}"
  echo "Error on line $1"
}
trap 'err_print $LINENO' ERR
DEBUG_LINE() {
    "$@"
}
function usage {
  echo "Usage: $0 far_tool far_file expected_manifest"
  echo "extracts manifest file from given package archive"
}

EXTRACTED_FAR_DIR_NAME=extracted_far_data

cleanup() {
  EXTRACTED_FAR_DIR="$(dirname "$1")"/"$EXTRACTED_FAR_DIR_NAME"
  rm -rf "${EXTRACTED_FAR_DIR:?}"
}

# Ensure far tool is present
FAR_BIN="$1"
if [ ! -x "$FAR_BIN" ]; then
  echo "Error: Could not find file far tool in $FAR_BIN"
  exit 1;
fi

# Path to far archive to test
FAR_FILE="$2"
if [ ! -f "$FAR_FILE" ]; then
  echo "Error: Could not find far file in $FAR_FILE"
  exit 1;
fi

EXPECTED_MANIFEST="$3"




echo "Testing ${FAR_FILE}"
FAR_EXTRACTED_DIR=$(dirname "${FAR_FILE}")/$EXTRACTED_FAR_DIR_NAME/$(basename "${FAR_FILE}")/extracted
mkdir -p "$FAR_EXTRACTED_DIR"
$FAR_BIN extract --archive="${FAR_FILE}" --output="$FAR_EXTRACTED_DIR"
$FAR_BIN extract --archive="$FAR_EXTRACTED_DIR"/meta.far --output="$FAR_EXTRACTED_DIR"
FAR_EXTRACTED_META_DIR=$FAR_EXTRACTED_DIR/meta
FAR_EXTRACTED_META_DIR_CONTENTS=$FAR_EXTRACTED_DIR/meta/contents

# Print the contents, stripping leading spaces to make it look better.
sed 's/^/  /' "$FAR_EXTRACTED_META_DIR_CONTENTS"

manifest_file="$(find "$FAR_EXTRACTED_META_DIR" -name "${EXPECTED_MANIFEST}")"

if [[ ! -e "$manifest_file" ]]; then
  if ! grep "$EXPECTED_MANIFEST" "$FAR_EXTRACTED_META_DIR_CONTENTS"; then
    echo "ERROR: Cannot file manifest $EXPECTED_MANIFEST in $FAR_EXTRACTED_META_DIR"
    exit 2
  fi
  if (( $# == 4 )); then
    if ! grep "$4" "$FAR_EXTRACTED_META_DIR_CONTENTS"; then
      echo "ERROR: Cannot file  $4 in $FAR_EXTRACTED_META_DIR_CONTENTS"
      exit 2
    fi
  fi
fi

exit 0
