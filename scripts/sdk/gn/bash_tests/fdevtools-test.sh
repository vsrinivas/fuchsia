#!/bin/bash
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Tests that we can start up the dependencies needed for Fuchsia DevTools.
# These tests do not actually start up Fuchsia DevTools, just that the
# set up steps happen as expected.

set -e

# Helpers.

# Runs a bash script. The function provides these conveniences over calling the
# script directly:
#
# * Rather than calling the bash script directly, this command explicitly
#   invokes Bash and propagates some option flags.
# * Rather than showing the bash output, this command only outputs output if a
#   test fails.
#
# Args: the script to run and all args to pass.
run_bash_script() {
  local shell_flags
  # propagate certain bash flags if present
  shell_flags=()
  if [[ $- == *x* ]]; then
    shell_flags+=(-x)
  fi
  local output

  output=$(bash "${shell_flags[@]}" "$@" 2>&1)
  status=$?
  if [[ ${status} != 0 ]]; then
    echo "${output}"
  fi

  return ${status}
}

# Verifies that the correct commands are run before starting Fuchsia DevTools
TEST_fdevtools() {
  # Run command.
  BT_EXPECT run_bash_script "${BT_TEMP_DIR}/scripts/sdk/gn/base/bin/fdevtools.sh" \
    --authorized-keys "${BT_TEMP_DIR}/scripts/sdk/gn/base/testdata/authorized_keys"

  # Verify that cipd was called to download the correct path
  source "${BT_TEMP_DIR}/scripts/sdk/gn/base/bin/cipd.mock_state"
  local CIPD_ARGS=("${BT_MOCK_ARGS[@]:1}")
  local EXPECTED_CIPD_ARGS=(
    ensure
    -ensure-file NOCHECK_TEMP_FILE
    -root "${BT_TEMP_DIR}/scripts/sdk/gn/base/images/fuchsia_devtools"
  )
  BT_EXPECT_EQ ${#EXPECTED_CIPD_ARGS[@]} ${#CIPD_ARGS[@]}
  for i in "${!EXPECTED_CIPD_ARGS[@]}"; do
    # The zbi tools creates an internal mktemp file that we don't know, so do not match NOCHECK_TEMP_FILE
    if [[ "${EXPECTED_CIPD_ARGS[$i]}" != "NOCHECK_TEMP_FILE" ]]; then
      BT_EXPECT_EQ "${EXPECTED_CIPD_ARGS[$i]}" "${CIPD_ARGS[$i]}"
    fi
  done

  # Verify that the executable is called, no arguments are passed
  source "${BT_TEMP_DIR}/scripts/sdk/gn/base/images/fuchsia_devtools/system_monitor/linux/system_monitor.mock_state"
  local MONITOR_ARGS=("${BT_MOCK_ARGS[@]:1}")
  local EXPECTED_MONITOR_ARGS=()
  BT_EXPECT_EQ ${#EXPECTED_MONITOR_ARGS[@]} ${#MONITOR_ARGS[@]}
}

# Test initialization. Note that we copy various tools/devshell files and need to replicate the
# behavior of generate.py by copying these files into scripts/sdk/gn/base/bin/devshell
BT_FILE_DEPS=(
  scripts/sdk/gn/base/bin/fdevtools.sh
  scripts/sdk/gn/base/bin/fuchsia-common.sh
)
BT_MOCKED_TOOLS=(
  scripts/sdk/gn/base/images/fuchsia_devtools/system_monitor/linux/system_monitor
  scripts/sdk/gn/base/bin/cipd
)

BT_INIT_TEMP_DIR() {
  # Create empty authorized_keys file to add to the system image, but the contents are not used.
  mkdir -p "${BT_TEMP_DIR}/scripts/sdk/gn/base/testdata"
  echo ssh-ed25519 00000000000000000000000000000000000000000000000000000000000000000000 \
    >"${BT_TEMP_DIR}/scripts/sdk/gn/base/testdata/authorized_keys"
}

BT_RUN_TESTS "$@"
