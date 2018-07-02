#!/bin/bash
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Halt on use of undeclared variables.
set -o nounset

# Set up the environment
readonly TEST_DIR="$(mktemp -d)"
if [[ ! -d "${TEST_DIR}" ]]; then
  echo >&2 "Failed to create temporary directory"
  exit 1
fi

readonly SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly FUCHSIA_DIR="$(dirname "$(dirname "$(dirname "${SCRIPT_DIR}")")")"
readonly BUILD_DIR="foo"
readonly TEST_BUILD_DIR="${TEST_DIR}/${BUILD_DIR}"

readonly ENV="FUCHSIA_DIR=${TEST_DIR}"

ln -s \
  "${FUCHSIA_DIR}/scripts" \
  "${TEST_DIR}"

cat <<END >> "${TEST_DIR}/.config"
FUCHSIA_BUILD_DIR='${BUILD_DIR}'
FUCHSIA_ARCH='x64'
END

mkdir -p "${TEST_BUILD_DIR}"
cat <<END >> "${TEST_BUILD_DIR}/args.gn"
target_cpu = "x64"
use_goma = false
END

ln -s \
  "${FUCHSIA_DIR}/buildtools" \
  "${TEST_DIR}"

cat <<END >> "${TEST_BUILD_DIR}/build.ninja"
rule touch
  command = touch \$out
build foo.o: touch
END

# Invoke `fx build`.
env -i "${ENV}" "${FUCHSIA_DIR}/scripts/fx" build

declare RETURN_CODE

if [[ -f "${TEST_BUILD_DIR}/foo.o" ]]; then
  echo "SUCCESS"
  RETURN_CODE=0
else
  echo "FAILURE"
  RETURN_CODE=1
fi

# Clean up
rm -rf -- "${TEST_DIR}"

exit "${RETURN_CODE}"
