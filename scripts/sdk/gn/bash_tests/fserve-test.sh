#!/bin/bash
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Test that verifies that fserve starts up a package server. This test also
# verifies that fserve connects to a connected Fuchsia device and registers the
# package server with the device.

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

# Sets up a gsutil mock. The implemented mock creates an empty gzipped tarball
# at the destination of the cp command.
set_up_gsutil() {
  cat >"${BT_TEMP_DIR}/scripts/sdk/gn/base/bin/gsutil.mock_side_effects" <<"SETVAR"
set -x

if [[ "$1" != "cp" ]]; then
  # Ignore any invocations other than cp.
  exit 0
fi

outpath="$3"

mkdir -p "$(dirname "${outpath}")"
touch foo
tar czf "${outpath}" foo
SETVAR
}

# Creates a stub Core SDK hashes file. The filename is based on the SDK version
# in manifest.json.
set_up_sdk_stubs() {
  # The filename is constructed from the Core SDK version ("id") in the
  # manifest. See //scripts/sdk/gn/testdata/meta/manifest.json.
  mkdir -p "${BT_TEMP_DIR}/scripts/sdk/gn/base/images/image"
  local tarball="${BT_TEMP_DIR}/scripts/sdk/gn/base/images/8890373976687374912_generic-x64.tgz"

  touch foo
  tar czf "${tarball}" foo

  local hash
  hash=$(md5sum "${tarball}" | cut -d ' ' -f 1)
  echo "${hash}  ${tarball}" >"${BT_TEMP_DIR}/scripts/sdk/gn/base/images/image/image.md5"
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
  local status
  if [[ $- == *x* ]]; then
    shell_flags+=(-x)
    # Run command with inherited stdout/stderr.
    bash "${shell_flags[@]}" "$@"
    status=$?
  else
    # Run command, capturing stdout/stderr. Only output those if the test fails.
    local output
    output="$(bash "${shell_flags[@]}" "$@" 2>&1)"
    status=$?
    if [[ $status != 0 ]]; then
      echo "${output}"
    fi
  fi
  return $status
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

# Verify that pm serve was run correctly.
TEST_fserve_starts_package_server() {
  set_up_ssh
  set_up_device_finder
  set_up_gsutil
  set_up_sdk_stubs

  # Run command.
  BT_EXPECT run_bash_script "${BT_TEMP_DIR}/scripts/sdk/gn/base/bin/fserve.sh"

  source "${BT_TEMP_DIR}/scripts/sdk/gn/base/tools/pm.mock_state"

  BT_EXPECT_EQ 6 ${#BT_MOCK_ARGS[@]}
  check_mock_has_args serve
  check_mock_has_args -repo "${BT_TEMP_DIR}/scripts/sdk/gn/base/images/packages/amber-files"
  check_mock_has_args -l :8083

  # Verify that pm was only run once.
  BT_EXPECT_FILE_DOES_NOT_EXIST "${BT_TEMP_DIR}/scripts/sdk/gn/base/tools/pm.mock_state.1"
}

# Verifies that the package server has been correctly registered with the
# Fuchsia device.
TEST_fserve_registers_package_server() {
  set_up_ssh
  set_up_device_finder
  set_up_gsutil
  set_up_sdk_stubs

  # Run command.
  BT_EXPECT run_bash_script "${BT_TEMP_DIR}/scripts/sdk/gn/base/bin/fserve.sh"

  source "${PATH_DIR_FOR_TEST}/ssh.mock_state"

  BT_EXPECT_EQ 8 "${#BT_MOCK_ARGS[@]}"
  check_mock_has_args -F "${BT_TEMP_DIR}/scripts/sdk/gn/base/bin/sshconfig"
  check_mock_has_args fe80::c0ff:eec0:ffee%coffee
  check_mock_has_args amber_ctl add_src -f "http://[fe80::c0ff:eec0:ffee]:8083/config.json"

  # Verify that ssh was only run once.
  BT_EXPECT_FILE_DOES_NOT_EXIST "${PATH_DIR_FOR_TEST}/ssh.mock_state.1"
}

# Test initialization.
BT_FILE_DEPS=(
  scripts/sdk/gn/base/bin/fserve.sh
  scripts/sdk/gn/base/bin/fuchsia-common.sh
)
BT_MOCKED_TOOLS=(
  scripts/sdk/gn/base/bin/gsutil
  scripts/sdk/gn/base/tools/device-finder
  scripts/sdk/gn/base/tools/pm
  ssh
)

BT_INIT_TEMP_DIR() {
  mkdir -p "${BT_TEMP_DIR}/scripts/sdk/gn/base/meta"

  # Create a stub SDK manifest.
  cp "${BT_DEPS_ROOT}/scripts/sdk/gn/testdata/meta/manifest.json" \
    "${BT_TEMP_DIR}/scripts/sdk/gn/base/meta/manifest.json"
}

BT_RUN_TESTS "$@"
