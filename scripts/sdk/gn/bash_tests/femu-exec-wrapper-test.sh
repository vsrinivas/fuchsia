#!/bin/bash
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Tests that femu is able to correctly interact with the fx emu command and
# its dependencies like fvm and aemu. These tests do not actually start up
# the emulator, but check the arguments are as expected.

set -e

TEST_femu_exec_wrapper() {
  # Create femu.sh that fails with output to stderr to check redirection
  cat>"${BT_TEMP_DIR}/scripts/sdk/gn/base/bin/femu.sh.mock_side_effects" <<INPUT
echo "Cannot start emulator" > /dev/stderr
return 3
INPUT

  # Make fssh never connect, because the emulator did not start
  cat>"${MOCKED_FSSH}.mock_side_effects" <<INPUT
  echo "Cannot connect" > /dev/stderr
  # ssh returns 255 when there is a failure to connect to the host.
  return 255
INPUT

  # Run command, which is expected to fail
  BT_EXPECT_FAIL  "${BT_TEMP_DIR}/scripts/sdk/gn/base/bin/femu-exec-wrapper.sh" \
  --femu-log "${BT_TEMP_DIR}/femu.log"  > "${BT_TEMP_DIR}/TEST_femu_exec_wrapper_out.txt" 2>&1

  # Check that femu.sh was called
  # shellcheck disable=SC1090
  source "${BT_TEMP_DIR}/scripts/sdk/gn/base/bin/femu.sh.mock_state"

  gn-test-check-mock-args _ANY_ --image qemu-x64 -N

  # Check that the stderr from femu.sh was correctly written to the log file
  BT_EXPECT_FILE_CONTAINS_SUBSTRING "${BT_TEMP_DIR}/femu.log" "Cannot start emulator"
}


TEST_femu_exec_wrapper_args() {
  cat>"${BT_TEMP_DIR}/script.sh.mock_side_effects" <<"INPUT"
  # Make sure $1 looks like an ipv6 address
  if [[ "${1}" =~ :.*% ]]; then
    exit 0
  else
    exit 4
  fi
INPUT

  BT_EXPECT "${BT_TEMP_DIR}/scripts/sdk/gn/base/bin/femu-exec-wrapper.sh" --exec "${BT_TEMP_DIR}/script.sh" \
  --femu-log "${BT_TEMP_DIR}/femu_exec_wrapper_args.log" > "${BT_TEMP_DIR}/TEST_femu_exec_wrapper_args_out.txt" 2>&1
}

TEST_femu_exec_wrapper_multi_pid() {
  # Tests cleaning up the emulator when the emu process creates child processes.
  # mac uses pgrep and linux uses ps
  export PATH="${BT_TEMP_DIR}/isolated:$PATH"
 cat>"${BT_TEMP_DIR}/isolated/pgrep.mock_side_effects"<<"INPUT"
 if (( "${2}" == "10" )); then
  echo "11"
  echo "12"
  echo "13"
  return 0
 elif (( "${2}" == "11" )); then
  echo "20"
  return 0
 elif (( "$2" > 20 )); then
  echo 10
  return 0
 fi
 return 1
INPUT

 cat>"${BT_TEMP_DIR}/isolated/ps.mock_side_effects"<<"INPUT"
 if (( "${4}" == "10" )); then
  echo "11"
  echo "12"
  echo "13"
  return 0
 elif (( "${4}" == "11" )); then
  echo "20"
  return 0
 elif (( "$4" > 20 )); then
  echo 10
  return 0
 fi
 return 1
INPUT

  # Mock the builtin kill
  kill() {
    "${BT_TEMP_DIR}/mocked/kill" "$@"
  }
  export -f kill

  BT_EXPECT "${BT_TEMP_DIR}/scripts/sdk/gn/base/bin/femu-exec-wrapper.sh"  \
  --femu-log "${BT_TEMP_DIR}/femu_exec_wrapper_multi_pid.log"  > "${BT_TEMP_DIR}/TEST_femu_exec_wrapper_multi_pid_out.txt" 2>&1

  if ! is-mac; then
    # shellcheck disable=SC1090
    source "${BT_TEMP_DIR}/isolated/ps.mock_state.1"
    gn-test-check-mock-args _ANY_ "-o" "pid:1=" "--ppid" _ANY_
    # shellcheck disable=SC1090
    source "${BT_TEMP_DIR}/isolated/ps.mock_state.2"
    # shellcheck disable=SC1090
    gn-test-check-mock-args _ANY_ "-o" "pid:1=" "--ppid" "10"
    # shellcheck disable=SC1090
    source "${BT_TEMP_DIR}/isolated/ps.mock_state.3"
    gn-test-check-mock-args _ANY_ "-o" "pid:1=" "--ppid" "11"
    # shellcheck disable=SC1090
    source "${BT_TEMP_DIR}/isolated/ps.mock_state.4"
    gn-test-check-mock-args _ANY_ "-o" "pid:1=" "--ppid" "20"
    # shellcheck disable=SC1090
    source "${BT_TEMP_DIR}/isolated/ps.mock_state.5"
    gn-test-check-mock-args _ANY_ "-o" "pid:1=" "--ppid" "12"
    # shellcheck disable=SC1090
    source "${BT_TEMP_DIR}/isolated/ps.mock_state.6"
    gn-test-check-mock-args _ANY_ "-o" "pid:1=" "--ppid" "13"
  fi

  # read the second mock state since the first kill call is checking to see if the emulator is up and running.
  BT_ASSERT_FILE_EXISTS  "${BT_TEMP_DIR}/mocked/kill.mock_state.2"
  # shellcheck disable=SC1090
  source "${BT_TEMP_DIR}/mocked/kill.mock_state.2"
  # Make sure the call to kill is with multiple args, one per pid and not a string of pids separated by spaces.
  # $1 is the pid of the emu process, so it changes each time.
  gn-test-check-mock-args _ANY_  "10" "11" "12" "13" "20"

}

TEST_femu_exec_wrapper_with_default_ip_set() {
  # This tests making sure the package server is serving to the emulator, and not the default ip address
  # set via fconfig.

    cat >"${MOCKED_FCONFIG}.mock_side_effects" <<"EOF"

  if [[ "$1" == "get" ]]; then
    if [[ "${2}" == "device-ip" ]]; then
      echo "192.1.1.2"
      return 0
    fi
    echo ""
  fi
EOF

    BT_EXPECT "${BT_TEMP_DIR}/scripts/sdk/gn/base/bin/femu-exec-wrapper.sh"  > "${BT_TEMP_DIR}/TEST_femu_exec_wrapper_with_default_ip_set.txt" 2>&1

  # Check that femu.sh was called
  # shellcheck disable=SC1090
  source "${MOCKED_FSERVE}.mock_state.1"

  gn-test-check-mock-args _ANY_ --device-ip "fe80::5054:ff:fe63:5e7a%qemu" --image qemu-x64

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
  scripts/sdk/gn/base/tools/x64/fserve
  scripts/sdk/gn/base/tools/arm64/fserve
  scripts/sdk/gn/base/tools/x64/fconfig
  scripts/sdk/gn/base/tools/arm64/fconfig
  scripts/sdk/gn/base/tools/x64/fssh
  scripts/sdk/gn/base/tools/arm64/fssh
  scripts/sdk/gn/base/bin/femu.sh
  script.sh
  isolated/ps
  isolated/pgrep
  mocked/kill
)

BT_SET_UP() {
  # shellcheck disable=SC1090
  source "${BT_TEMP_DIR}/scripts/sdk/gn/bash_tests/gn-bash-test-lib.sh"

    # Make "home" directory in the test dir so the paths are stable."
  mkdir -p "${BT_TEMP_DIR}/test-home"
  export HOME="${BT_TEMP_DIR}/test-home"

  MOCKED_FSERVE="${BT_TEMP_DIR}/scripts/sdk/gn/base/$(gn-test-tools-subdir)/fserve"
  MOCKED_FCONFIG="${BT_TEMP_DIR}/scripts/sdk/gn/base/$(gn-test-tools-subdir)/fconfig"
  MOCKED_FSSH="${BT_TEMP_DIR}/scripts/sdk/gn/base/$(gn-test-tools-subdir)/fssh"
}

BT_RUN_TESTS "$@"
