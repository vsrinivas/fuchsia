#!/bin/bash
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Tests that we can start up the dependencies needed for Fuchsia DevTools.
# These tests do not actually start up Fuchsia DevTools, just that the
# set up steps happen as expected.

set -e
SCRIPT_SRC_DIR="$(cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd)"
# shellcheck disable=SC1090
source "${SCRIPT_SRC_DIR}/gn-bash-test-lib.sh"

# Specify a simulated CIPD instance id for devtools.version
DEVTOOLS_VERSION="git_revision:unknown"
DEVTOOLS_LABEL="$(echo "${DEVTOOLS_VERSION}" | tr ':/' '_')"

# Verifies that the correct commands are run before starting Fuchsia DevTools
TEST_fdevtools_with_authkeys() {
  # Run command.
  BT_EXPECT "${BT_TEMP_DIR}/scripts/sdk/gn/base/bin/fdevtools.sh" \
    --version "${DEVTOOLS_VERSION}" \
    --private-key "${BT_TEMP_DIR}/scripts/sdk/gn/base/testdata/private_key" > "${BT_TEMP_DIR}/launch_devtools.txt"

  BT_EXPECT_FILE_CONTAINS_SUBSTRING "${BT_TEMP_DIR}/launch_devtools.txt" "${FUCHSIA_WORK_DIR}/sshconfig"
  BT_EXPECT_FILE_CONTAINS_SUBSTRING "${BT_TEMP_DIR}/launch_devtools.txt" "FDT_SSH_KEY"
  BT_EXPECT_FILE_CONTAINS_SUBSTRING "${BT_TEMP_DIR}/launch_devtools.txt" "${BT_TEMP_DIR}/scripts/sdk/gn/base/testdata/private_key"

  # Verify that cipd was called to download the correct path
  # shellcheck disable=SC1090
  source "${BT_TEMP_DIR}/scripts/sdk/gn/base/bin/cipd.mock_state"
  gn-test-check-mock-args _ANY_ ensure -ensure-file _ANY_ -root "${FUCHSIA_WORK_DIR}/fuchsia_devtools-${DEVTOOLS_LABEL}"

  # Verify that the executable is called, no arguments are passed
  # shellcheck disable=SC1090
  source "${FUCHSIA_WORK_DIR}/fuchsia_devtools-${DEVTOOLS_LABEL}/system_monitor/linux/system_monitor.mock_state"
  gn-test-check-mock-args "${FUCHSIA_WORK_DIR}/fuchsia_devtools-${DEVTOOLS_LABEL}/system_monitor/linux/system_monitor"
}

TEST_fdevtools_noargs() {

  # Set the version file to match the mock
  echo "git_revision_unknown" > "${BT_TEMP_DIR}/scripts/sdk/gn/base/bin/devtools.version"

  # Run command.
  BT_EXPECT "${BT_TEMP_DIR}/scripts/sdk/gn/base/bin/fdevtools.sh" > "${BT_TEMP_DIR}/launch_devtools.txt"

  BT_EXPECT_FILE_CONTAINS_SUBSTRING "${BT_TEMP_DIR}/launch_devtools.txt" "${FUCHSIA_WORK_DIR}/sshconfig"


  # Verify that cipd was called to download the correct path
  # shellcheck disable=SC1090
  source "${BT_TEMP_DIR}/scripts/sdk/gn/base/bin/cipd.mock_state"
  gn-test-check-mock-args _ANY_ ensure -ensure-file _ANY_ -root "${FUCHSIA_WORK_DIR}/fuchsia_devtools-${DEVTOOLS_LABEL}"

  # Verify that the executable is called, no arguments are passed
  # shellcheck disable=SC1090
  source "${FUCHSIA_WORK_DIR}/fuchsia_devtools-${DEVTOOLS_LABEL}/system_monitor/linux/system_monitor.mock_state"
  gn-test-check-mock-args "${FUCHSIA_WORK_DIR}/fuchsia_devtools-${DEVTOOLS_LABEL}/system_monitor/linux/system_monitor"
}

# Test initialization.
# shellcheck disable=SC2034
BT_FILE_DEPS=(
  scripts/sdk/gn/base/bin/fdevtools.sh
  scripts/sdk/gn/base/bin/fuchsia-common.sh
  scripts/sdk/gn/bash_tests/gn-bash-test-lib.sh
)
# shellcheck disable=SC2034
BT_MOCKED_TOOLS=(
  scripts/sdk/gn/base/images/fuchsia_devtools-"${DEVTOOLS_LABEL}"/system_monitor/linux/system_monitor
  scripts/sdk/gn/base/bin/cipd
)

BT_SET_UP() {
  FUCHSIA_WORK_DIR="${BT_TEMP_DIR}/scripts/sdk/gn/base/images"
}

BT_INIT_TEMP_DIR() {
  # Generate an invalid devtools.version that we will never see since --version overrides this
  echo "unused_version_string" > "${BT_TEMP_DIR}/scripts/sdk/gn/base/bin/devtools.version"

  # Create empty private_key which SSH would expect to make the connection
  mkdir -p "${BT_TEMP_DIR}/scripts/sdk/gn/base/testdata"
  echo "-----BEGIN TEST-----\
00000000000000000000000000000000000000000000000000000000000000000000 \
-----END TEST-----\
  " >"${BT_TEMP_DIR}/scripts/sdk/gn/base/testdata/private_key"
}

BT_RUN_TESTS "$@"
