#!/bin/bash
set -eu

PACKAGE_REFS_DIR="$1"
PACKAGE_NAME="$2"
SDK_FIDL_REF_FILE="$PACKAGE_REFS_DIR/fuchsia-sdk-all-fidl-refs.txt"
PACKAGE_REFS_FILE="${PACKAGE_REFS_DIR}/${PACKAGE_NAME}_all_fidl_refs.txt"

if [[ ! -f "${PACKAGE_REFS_FILE}" ]]; then
  echo "Missing fidl refs file for ${PACKAGE_NAME}. Expected at ${PACKAGE_REFS_FILE}."
  exit 1
fi

#check for rot13.fidl
if ! grep rot13 "${PACKAGE_REFS_FILE}"; then
  echo "Expected rot13 in  ${PACKAGE_REFS_FILE}."
  exit 1
fi

# check for non-empty sdk fidl ref.
if [[ -s "${SDK_FIDL_REF_FILE}" ]]; then
  echo "Expected ${SDK_FIDL_REF_FILE} to be non-empty."
  exit 1
fi
