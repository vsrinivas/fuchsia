#!/bin/bash
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Tests that we can start up the dependencies needed for Fuchsia DevTools.
# These tests do not actually start up Fuchsia DevTools, just that the
# set up steps happen as expected.

set -e

# Specify a simulated CIPD instance id for devtools.version
DEVTOOLS_VERSION="git_revision:unknown"
DEVTOOLS_LABEL="$(echo "${DEVTOOLS_VERSION}" | tr ':/' '_')"

# Verifies that the correct commands are run before starting Fuchsia DevTools
TEST_fdevtools_with_authkeys_fuchsia_devtools_binary() {
  btf::make_mock "${BT_TEMP_DIR}/test-home/.fuchsia/fuchsia_devtools-${DEVTOOLS_LABEL}/system_monitor/linux/fuchsia_devtools"

  # Run command.
  BT_EXPECT "${BT_TEMP_DIR}/scripts/sdk/gn/base/bin/fdevtools.sh" \
    --extra-fdt-arg-1 --extra-fdt-arg-2 \
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
  if is-mac; then
    source "${PATH_DIR_FOR_TEST}/open.mock_state"
    gn-test-check-mock-args "${PATH_DIR_FOR_TEST}/open" "${FUCHSIA_WORK_DIR}/fuchsia_devtools-${DEVTOOLS_LABEL}/fuchsia_devtools/macos-extracted/Fuchsia DevTools.app" "--args" "--extra-fdt-arg-1" "--extra-fdt-arg-2"
  else
    source "${FUCHSIA_WORK_DIR}/fuchsia_devtools-${DEVTOOLS_LABEL}/system_monitor/linux/fuchsia_devtools.mock_state"
    gn-test-check-mock-args "${FUCHSIA_WORK_DIR}/fuchsia_devtools-${DEVTOOLS_LABEL}/system_monitor/linux/fuchsia_devtools" "--extra-fdt-arg-1" "--extra-fdt-arg-2"
  fi
}

TEST_fdevtools_with_authkeys_system_monitor_binary() {
  btf::make_mock "${BT_TEMP_DIR}/test-home/.fuchsia/fuchsia_devtools-${DEVTOOLS_LABEL}/system_monitor/linux/system_monitor"

  # Run command.
  BT_EXPECT "${BT_TEMP_DIR}/scripts/sdk/gn/base/bin/fdevtools.sh" \
    --extra-fdt-arg-1 --extra-fdt-arg-2 \
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
  if is-mac; then
    source "${PATH_DIR_FOR_TEST}/open.mock_state"
    gn-test-check-mock-args "${PATH_DIR_FOR_TEST}/open" "${FUCHSIA_WORK_DIR}/fuchsia_devtools-${DEVTOOLS_LABEL}/fuchsia_devtools/macos-extracted/Fuchsia DevTools.app" "--args" "--extra-fdt-arg-1" "--extra-fdt-arg-2"
  else
    source "${FUCHSIA_WORK_DIR}/fuchsia_devtools-${DEVTOOLS_LABEL}/system_monitor/linux/system_monitor.mock_state"
    gn-test-check-mock-args "${FUCHSIA_WORK_DIR}/fuchsia_devtools-${DEVTOOLS_LABEL}/system_monitor/linux/system_monitor" "--extra-fdt-arg-1" "--extra-fdt-arg-2"
  fi
}

TEST_fdevtools_noargs_fuchsia_devtools_binary() {
  btf::make_mock "${BT_TEMP_DIR}/test-home/.fuchsia/fuchsia_devtools-${DEVTOOLS_LABEL}/system_monitor/linux/fuchsia_devtools"

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
  if is-mac; then
    source "${PATH_DIR_FOR_TEST}/open.mock_state"
    gn-test-check-mock-args "${PATH_DIR_FOR_TEST}/open" "${FUCHSIA_WORK_DIR}/fuchsia_devtools-${DEVTOOLS_LABEL}/fuchsia_devtools/macos-extracted/Fuchsia DevTools.app" "--args"
  else
    source "${FUCHSIA_WORK_DIR}/fuchsia_devtools-${DEVTOOLS_LABEL}/system_monitor/linux/fuchsia_devtools.mock_state"
    gn-test-check-mock-args "${FUCHSIA_WORK_DIR}/fuchsia_devtools-${DEVTOOLS_LABEL}/system_monitor/linux/fuchsia_devtools"
  fi
}

TEST_fdevtools_noargs_system_monitor_binary() {
  btf::make_mock "${BT_TEMP_DIR}/test-home/.fuchsia/fuchsia_devtools-${DEVTOOLS_LABEL}/system_monitor/linux/system_monitor"

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
  if is-mac; then
    source "${PATH_DIR_FOR_TEST}/open.mock_state"
    gn-test-check-mock-args "${PATH_DIR_FOR_TEST}/open" "${FUCHSIA_WORK_DIR}/fuchsia_devtools-${DEVTOOLS_LABEL}/fuchsia_devtools/macos-extracted/Fuchsia DevTools.app" "--args"
  else
    source "${FUCHSIA_WORK_DIR}/fuchsia_devtools-${DEVTOOLS_LABEL}/system_monitor/linux/system_monitor.mock_state"
    gn-test-check-mock-args "${FUCHSIA_WORK_DIR}/fuchsia_devtools-${DEVTOOLS_LABEL}/system_monitor/linux/system_monitor"
  fi
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
  test-home/.fuchsia/fuchsia_devtools-"${DEVTOOLS_LABEL}"/fuchsia_devtools/macos-extracted/"Fuchsia DevTools.app"
  scripts/sdk/gn/base/bin/cipd
  _isolated_path_for/open
)

BT_SET_UP() {
  # shellcheck disable=SC1090
  source "${BT_TEMP_DIR}/scripts/sdk/gn/bash_tests/gn-bash-test-lib.sh"
  
  # Make "home" directory in the test dir so the paths are stable."
  mkdir -p "${BT_TEMP_DIR}/test-home"
  export HOME="${BT_TEMP_DIR}/test-home"
  FUCHSIA_WORK_DIR="${HOME}/.fuchsia"

  PATH_DIR_FOR_TEST="${BT_TEMP_DIR}/_isolated_path_for"
  export PATH="${PATH_DIR_FOR_TEST}:${PATH}"
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
