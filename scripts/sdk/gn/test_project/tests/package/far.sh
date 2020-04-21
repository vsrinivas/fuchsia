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
  echo
  echo "Usage: $0 far_tool"
  echo "* far_tool: Path to the far tool."
  echo
  echo "Usage: $0 far_file"
  echo "* far_file: Fuchsia archive to be tested."
  echo
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

echo "==== Testing FAR file ===="
echo "${FAR_FILE}"

echo
echo "==== Scanning FAR files ===="

echo
echo "Testing ${FAR_FILE}"
FAR_EXTRACTED_DIR=$(dirname "${FAR_FILE}")/$EXTRACTED_FAR_DIR_NAME/$(basename "${FAR_FILE}")/extracted
mkdir -p "$FAR_EXTRACTED_DIR"
$FAR_BIN extract --archive="${FAR_FILE}" --output="$FAR_EXTRACTED_DIR"
$FAR_BIN extract --archive="$FAR_EXTRACTED_DIR"/meta.far --output="$FAR_EXTRACTED_DIR"
FAR_EXTRACTED_META_DIR=$FAR_EXTRACTED_DIR/meta
FAR_EXTRACTED_META_DIR_CONTENTS=$FAR_EXTRACTED_DIR/meta/contents
sed 's/^/  /' "$FAR_EXTRACTED_META_DIR_CONTENTS"

# Check for shared libraries required to run a component
SHARED_LIB_ERR=0
if ! grep -q "ld.so.1" "$FAR_EXTRACTED_META_DIR_CONTENTS"; then
  echo "**** Failed to find ld.so.1 mentioned in ${FAR_FILE} ****"
  SHARED_LIB_ERR=1
fi
if ! grep -q "libc++.so" "$FAR_EXTRACTED_META_DIR_CONTENTS"; then
  echo "**** Failed to find libc++.so mentioned in ${FAR_FILE} ****"
  SHARED_LIB_ERR=1
fi
if ! grep -q "libfdio.so" "$FAR_EXTRACTED_META_DIR_CONTENTS"; then
  echo "**** Failed to find libfdio.so mentioned in ${FAR_FILE} ****"
  SHARED_LIB_ERR=1
fi

# Check for component manifest
COMPONENT_MANIFEST_ERR=0
NUM_CM_FILES=$(find "$FAR_EXTRACTED_META_DIR" -name "*.cm" | wc -l)
NUM_CMX_FILES=$(find "$FAR_EXTRACTED_META_DIR" -name "*.cmx" | wc -l)
if [ "$NUM_CM_FILES" -eq 0 ] && [ "$NUM_CMX_FILES" -eq 0 ]; then
  echo "**** Failed to find component manifest in ${FAR_FILE} ****"
  COMPONENT_MANIFEST_ERR=1
fi

cleanup "${FAR_FILE}"

if [ $SHARED_LIB_ERR -gt 0 ]; then
  exit 1;
fi

if [ $COMPONENT_MANIFEST_ERR -gt 0 ]; then
  exit 1;
fi

echo "Test for ${FAR_FILE} passed."
