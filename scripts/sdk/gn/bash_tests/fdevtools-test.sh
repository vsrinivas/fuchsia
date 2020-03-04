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

# Retrieve the expected version of DevTools with the CIPD instance id
DEVTOOLS_VERSION=$(cat "${SCRIPT_SRC_DIR}/../base/bin/devtools.version")

# Verifies that the correct commands are run before starting Fuchsia DevTools
TEST_fdevtools() {
  # Run command.
  BT_EXPECT gn-test-run-bash-script "${BT_TEMP_DIR}/scripts/sdk/gn/base/bin/fdevtools.sh" \
    --authorized-keys "${BT_TEMP_DIR}/scripts/sdk/gn/base/testdata/authorized_keys"

  # Verify that cipd was called to download the correct path
  # shellcheck disable=SC1090
  source "${BT_TEMP_DIR}/scripts/sdk/gn/base/bin/cipd.mock_state"
  gn-test-check-mock-args _ANY_ ensure -ensure-file _ANY_ -root "${BT_TEMP_DIR}/scripts/sdk/gn/base/images/fuchsia_devtools-${DEVTOOLS_VERSION}"

  # Verify that the executable is called, no arguments are passed
  # shellcheck disable=SC1090
  source "${BT_TEMP_DIR}/scripts/sdk/gn/base/images/fuchsia_devtools-${DEVTOOLS_VERSION}/system_monitor/linux/system_monitor.mock_state"
  gn-test-check-mock-args "${BT_TEMP_DIR}/scripts/sdk/gn/base/images/fuchsia_devtools-${DEVTOOLS_VERSION}/system_monitor/linux/system_monitor"
}

# Test initialization.
# shellcheck disable=SC2034
BT_FILE_DEPS=(
  scripts/sdk/gn/base/bin/devtools.version
  scripts/sdk/gn/base/bin/fdevtools.sh
  scripts/sdk/gn/base/bin/fuchsia-common.sh
  scripts/sdk/gn/bash_tests/gn-bash-test-lib.sh
)
# shellcheck disable=SC2034
BT_MOCKED_TOOLS=(
  scripts/sdk/gn/base/images/fuchsia_devtools-"${DEVTOOLS_VERSION}"/system_monitor/linux/system_monitor
  scripts/sdk/gn/base/bin/cipd
)

BT_INIT_TEMP_DIR() {
  # Create empty authorized_keys file to add to the system image, but the contents are not used.
  mkdir -p "${BT_TEMP_DIR}/scripts/sdk/gn/base/testdata"
  echo ssh-ed25519 00000000000000000000000000000000000000000000000000000000000000000000 \
    >"${BT_TEMP_DIR}/scripts/sdk/gn/base/testdata/authorized_keys"
}

BT_RUN_TESTS "$@"
