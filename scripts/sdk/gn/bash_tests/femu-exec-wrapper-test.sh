#!/bin/bash
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Tests that femu is able to correctly interact with the fx emu command and
# its dependencies like fvm and aemu. These tests do not actually start up
# the emulator, but check the arguments are as expected.

set -e
SCRIPT_SRC_DIR="$(cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd)"
# shellcheck disable=SC1090
source "${SCRIPT_SRC_DIR}/gn-bash-test-lib.sh"

TEST_femu_exec_wrapper() {
  # Create femu.sh that fails with output to stderr to check redirection
  cat>"${BT_TEMP_DIR}/scripts/sdk/gn/base/bin/femu.sh.mock_side_effects" <<INPUT
echo "Cannot start emulator" > /dev/stderr
INPUT

  # Run command, which is expected to fail
  BT_EXPECT_FAIL  "${BT_TEMP_DIR}/scripts/sdk/gn/base/bin/femu-exec-wrapper.sh" \
    --femu-log "${BT_TEMP_DIR}/femu.log" > /dev/null 2>&1

  # Check that femu.sh was called
  # shellcheck disable=SC1090
  source "${BT_TEMP_DIR}/scripts/sdk/gn/base/bin/femu.sh.mock_state"
  if ! is-mac; then
    gn-test-check-mock-args _ANY_ --image qemu-x64 -N
  else
    gn-test-check-mock-args _ANY_ --image qemu-x64
  fi
  # Check that the stderr from femu.sh was correctly written to the log file
  BT_EXPECT_FILE_CONTAINS_SUBSTRING "${BT_TEMP_DIR}/femu.log" "Cannot start emulator"
}

# Test initialization. Note that we copy various tools/devshell files and need to replicate the
# behavior of generate.py by copying these files into scripts/sdk/gn/base/bin/devshell
# shellcheck disable=SC2034
BT_FILE_DEPS=(
  scripts/sdk/gn/base/bin/femu-exec-wrapper.sh
  scripts/sdk/gn/base/bin/fuchsia-common.sh
  scripts/sdk/gn/bash_tests/gn-bash-test-lib.sh
)
# shellcheck disable=SC2034
BT_MOCKED_TOOLS=(
  scripts/sdk/gn/base/bin/femu.sh
)

BT_RUN_TESTS "$@"
