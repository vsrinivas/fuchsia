#!/bin/bash
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Test that verifies that fssh correctly discovers and connects to a Fuchsia
# device.

set -e

# Sets up an ssh mock binary on the $PATH of any subshells and creates a stub
# authorized_keys.
set_up_ssh() {
  PATH_DIR_FOR_TEST="$(mktemp -d)"
  cp "${BT_TEMP_DIR}/ssh" "${PATH_DIR_FOR_TEST}"
  export PATH="${PATH_DIR_FOR_TEST}:${PATH}"

  # This authorized_keys file must not be empty, but its contents aren't used.
  mkdir -p "${BT_TEMP_DIR}/scripts/sdk/gn/base/testdata"
  echo ssh-ed25519 00000000000000000000000000000000000000000000000000000000000000000000 \
    >"${BT_TEMP_DIR}/scripts/sdk/gn/base/testdata/authorized_keys"
}

# Sets up a device-finder mock. The implemented mock aims to produce minimal
# output that parses correctly but is otherwise uninteresting.
set_up_device_finder() {
  cat >"${BT_TEMP_DIR}/scripts/sdk/gn/base/tools/device-finder.mock_side_effects" <<"SETVAR"
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

  if ! output=$(bash "${shell_flags[@]}" "$@" 2>&1); then
    echo "${output}"
  fi
}

# Verifies that the given arguments appear in the command line invocation of the
# most previously sourced mock state. Any arguments passed to this function will
# be searched for in the actual arguments. This succeeds if the arguments are
# found in adjacent positions in the correct order.
#
# This function only checks for presence. As a result, it will NOT verify any of
# the following:
#
# * The arguments only appear once.
# * The arguments don't appear with conflicting arguments.
# * Any given argument --foo isn't overridden, say with a --no-foo flag later.
#
# Args: any number of arguments to check.
# Returns: 0 if found; 1 if not found.
check_mock_has_args() {
  local expected=("$@")
  for j in "${!BT_MOCK_ARGS[@]}"; do
    local window=("${BT_MOCK_ARGS[@]:$j:${#expected}}")
    local found=true
    for k in "${!expected[@]}"; do
      if [[ "${expected[$k]}" != "${window[$k]}" ]]; then
        found=false
        break
      fi
    done
    if [[ "${found}" == "true" ]]; then
      return 0
    fi
  done
  return 1
}

# Verifies that the correct ssh command is run by fssh.
TEST_fssh() {
  set_up_ssh
  set_up_device_finder

  # Run command.
  BT_EXPECT run_bash_script "${BT_TEMP_DIR}/scripts/sdk/gn/base/bin/fssh.sh"

  # Verify that ssh was run correctly.
  source "${PATH_DIR_FOR_TEST}/ssh.mock_state"

  BT_EXPECT_EQ 4 "${#BT_MOCK_ARGS[@]}"
  check_mock_has_args -F "${BT_TEMP_DIR}/scripts/sdk/gn/base/bin/sshconfig"
  check_mock_has_args fe80::c0ff:eec0:ffee%coffee
  BT_EXPECT_FILE_DOES_NOT_EXIST "${BT_TEMP_DIR}/scripts/sdk/gn/base/bin/ssh.mock_state.1"
}

# Test initialization.
BT_FILE_DEPS=(
  scripts/sdk/gn/base/bin/fssh.sh
  scripts/sdk/gn/base/bin/fuchsia-common.sh
)
BT_MOCKED_TOOLS=(
  scripts/sdk/gn/base/tools/device-finder
  ssh
)

BT_INIT_TEMP_DIR() {
  mkdir -p "${BT_TEMP_DIR}/scripts/sdk/gn/base/sdk/meta"
}

BT_RUN_TESTS "$@"
