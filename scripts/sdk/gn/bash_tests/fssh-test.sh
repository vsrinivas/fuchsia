#!/bin/bash
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Test that verifies that fssh correctly discovers and connects to a Fuchsia
# device.

set -e

BT_INIT_TEMP_DIR() {
  # This authorized_keys file must not be empty, but its contents aren't used.
  mkdir -p "${BT_TEMP_DIR}/scripts/sdk/gn/base/testdata"
  echo ssh-ed25519 00000000000000000000000000000000000000000000000000000000000000000000 \
    >"${BT_TEMP_DIR}/scripts/sdk/gn/base/testdata/authorized_keys"
}

BT_SET_UP() {
  # shellcheck disable=SC1090
  source "${BT_TEMP_DIR}/scripts/sdk/gn/bash_tests/gn-bash-test-lib.sh"

  # Make "home" directory in the test dir so the paths are stable."
  mkdir -p "${BT_TEMP_DIR}/test-home"
  export HOME="${BT_TEMP_DIR}/test-home"
  FUCHSIA_WORK_DIR="${HOME}/.fuchsia"

  MOCKED_DEVICE_FINDER="${BT_TEMP_DIR}/scripts/sdk/gn/base/$(gn-test-tools-subdir)/device-finder"
  FCONFIG_CMD="${BT_TEMP_DIR}/scripts/sdk/gn/base/bin/fconfig.sh"

  # Add the ssh mock to the path so fssh uses it vs. the real ssh.
  SSH_MOCK_PATH="${BT_TEMP_DIR}/isolated_path_for"
  export PATH="${SSH_MOCK_PATH}:${PATH}"
}

# Sets up a device-finder mock. The implemented mock aims to produce minimal
# output that parses correctly but is otherwise uninteresting.
set_up_device_finder() {
  cat >"${MOCKED_DEVICE_FINDER}.mock_side_effects" <<"SETVAR"
while (("$#")); do
  case "$1" in
  --local)
    # Emit a different address than the default so the device and the host can
    # have different IP addresses.
    echo fe80::1234%coffee
    exit
    ;;
  --full)
    echo fe80::c0ff:eec0:ffee%coffee coffee-coffee-coffee-coffee
    exit
    ;;
  esac
  shift
done

echo fe80::c0ff:eec0:ffee%coffee
SETVAR
}

TEST_fssh_help() {
  BT_EXPECT_FAIL  "${BT_TEMP_DIR}/scripts/sdk/gn/base/bin/fssh.sh" --help > "${BT_TEMP_DIR}/usage.txt"

readonly EXPECTED_HELP="Usage: fssh.sh [args]
  [--help]
    This message.
  [--device-name <device hostname>]
    Connects to the device with the given device hostname. Cannot be used with --device-ip.
    Defaults to the value from \`fconfig.sh get device-name\`.
  [--device-ip <device ip>]
    Connects to the device with the given device ip address. Cannot be used with --device-name.
    Defaults to the value from \`fconfig.sh get device-ip\`.
    Note: If defaults are configured for both device-name and device-ip, then device-ip is used.
          If the device is specified at all, then the first device discovered is used.
  [--private-key <identity file>]
    Uses additional private key when using ssh to access the device.

All positional arguments are passed through to SSH to be executed on the device."

  BT_EXPECT_FILE_CONTAINS "${BT_TEMP_DIR}/usage.txt" "${EXPECTED_HELP}"
}

# Verifies that the correct ssh command is run by fssh.
TEST_fssh() {
  set_up_device_finder

  # Run command.
  BT_EXPECT "${BT_TEMP_DIR}/scripts/sdk/gn/base/bin/fssh.sh"

  # Verify that ssh was run correctly.
  # shellcheck disable=SC1090
  source "${SSH_MOCK_PATH}/ssh.mock_state"

  gn-test-check-mock-args _ANY_ -F "${FUCHSIA_WORK_DIR}/sshconfig" fe80::c0ff:eec0:ffee%coffee

  BT_EXPECT_FILE_DOES_NOT_EXIST "${BT_TEMP_DIR}/scripts/sdk/gn/base/bin/ssh.mock_state"
}

TEST_fssh_by_ip() {
   set_up_device_finder

  # Run command.
  BT_EXPECT "${BT_TEMP_DIR}/scripts/sdk/gn/base/bin/fssh.sh" --device-ip fe80::d098:513f:9cfb:eb53%hardcoded

  # Verify that ssh was run correctly.
  # shellcheck disable=SC1090
  source "${SSH_MOCK_PATH}/ssh.mock_state"

  gn-test-check-mock-args _ANY_ -F "${FUCHSIA_WORK_DIR}/sshconfig" fe80::d098:513f:9cfb:eb53%hardcoded
}

TEST_fssh_by_name() {
   set_up_device_finder

  # Run command.
  BT_EXPECT "${BT_TEMP_DIR}/scripts/sdk/gn/base/bin/fssh.sh" --device-name coffee-coffee-coffee-coffee

  # Verify that ssh was run correctly.
  # shellcheck disable=SC1090
  source "${SSH_MOCK_PATH}/ssh.mock_state"

  gn-test-check-mock-args _ANY_ -F "${FUCHSIA_WORK_DIR}/sshconfig" fe80::c0ff:eec0:ffee%coffee
}

TEST_fssh_name_not_found() {
  echo 2 > "${MOCKED_DEVICE_FINDER}.mock_status"
  echo "2020/02/25 07:42:59 no devices with domain matching 'name-not-found'" >  "${MOCKED_DEVICE_FINDER}.stderr"

  # Run command.
  BT_EXPECT_FAIL  "${BT_TEMP_DIR}/scripts/sdk/gn/base/bin/fssh.sh" --device-name name-not-found
}


TEST_fssh_with_ip_prop() {
  set_up_device_finder

  BT_EXPECT "${FCONFIG_CMD}" set device-ip "192.1.1.2"

  BT_EXPECT "${BT_TEMP_DIR}/scripts/sdk/gn/base/bin/fssh.sh"

  # shellcheck disable=SC1090
  source "${SSH_MOCK_PATH}/ssh.mock_state"
  expected_args=("${SSH_MOCK_PATH}/ssh" -F "${FUCHSIA_WORK_DIR}/sshconfig" 192.1.1.2)
  gn-test-check-mock-args "${expected_args[@]}"
}

TEST_fssh_with_name_prop() {
  set_up_device_finder

  BT_EXPECT "${FCONFIG_CMD}" set device-name "coffee-coffee-coffee-coffee"

  BT_EXPECT "${BT_TEMP_DIR}/scripts/sdk/gn/base/bin/fssh.sh"

  # shellcheck disable=SC1090
  source "${SSH_MOCK_PATH}/ssh.mock_state"
  expected_args=("${SSH_MOCK_PATH}/ssh" -F "${FUCHSIA_WORK_DIR}/sshconfig" "fe80::c0ff:eec0:ffee%coffee")
  gn-test-check-mock-args "${expected_args[@]}"
}

TEST_fssh_with_all_props() {
  set_up_device_finder

  BT_EXPECT "${FCONFIG_CMD}" set device-ip "192.1.1.2"
  BT_EXPECT "${FCONFIG_CMD}" set device-name "coffee-coffee-coffee-coffee"

  BT_EXPECT "${BT_TEMP_DIR}/scripts/sdk/gn/base/bin/fssh.sh"

  # shellcheck disable=SC1090
  source "${SSH_MOCK_PATH}/ssh.mock_state"
  expected_args=("${SSH_MOCK_PATH}/ssh" -F "${FUCHSIA_WORK_DIR}/sshconfig" 192.1.1.2)
  gn-test-check-mock-args "${expected_args[@]}"
}


# Test initialization.
# shellcheck disable=SC2034
BT_FILE_DEPS=(
  scripts/sdk/gn/base/bin/fconfig.sh
  scripts/sdk/gn/base/bin/fssh.sh
  scripts/sdk/gn/base/bin/fuchsia-common.sh
  scripts/sdk/gn/bash_tests/gn-bash-test-lib.sh
)
# shellcheck disable=SC2034
BT_MOCKED_TOOLS=(
  scripts/sdk/gn/base/tools/x64/device-finder
  scripts/sdk/gn/base/tools/arm64/device-finder
  isolated_path_for/ssh
)

BT_RUN_TESTS "$@"
