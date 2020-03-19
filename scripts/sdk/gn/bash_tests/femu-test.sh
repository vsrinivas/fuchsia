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


# Specify a simulated CIPD instance id for aemu.version
AEMU_VERSION="git_revision:unknown"
AEMU_LABEL="$(echo "${AEMU_VERSION}" | tr ':/' '_')"

# Helpers.

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
  BT_EXPECT false "Could not find ${expected[*]} in ${BT_MOCK_ARGS[*]}"
  return 1
}

# Verifies that the correct emulator command is run by femu when no arguments are provided on the command line.
TEST_femu_noargs() {

  PATH_DIR_FOR_TEST="${BT_TEMP_DIR}/_isolated_path_for"
  export PATH="${PATH_DIR_FOR_TEST}:${PATH}"

  # Mock authkeys
  echo ssh-ed25519 00000000000000000000000000000000000000000000000000000000000000000000 \
    >"${BT_TEMP_DIR}/scripts/sdk/gn/base/authkeys.txt"


  # Create fake "curl" command to that creates a simulated zip file based on the mocked emulator script
  # femu.sh calls curl like this, so use $4 to write output: curl -L "${CIPD_URL}" -o "${CIPD_FILE}" -#
  cat >"${PATH_DIR_FOR_TEST}/curl.mock_side_effects" <<INPUT
zip "\$4" "${FUCHSIA_WORK_DIR}/emulator/aemu-linux-amd64-${AEMU_LABEL}/emulator"
INPUT

  # Run command.
  BT_EXPECT gn-test-run-bash-script "${BT_TEMP_DIR}/scripts/sdk/gn/base/bin/femu.sh"

  # Verify that fvm resized the disk file by 2x from the input 1024 to 2048.
  # shellcheck disable=SC1090
  source "${BT_TEMP_DIR}/scripts/sdk/gn/base/tools/fvm.mock_state"
  gn-test-check-mock-args _ANY_ _ANY_ extend --length 2048

  # Check that fpave.sh was called to download the needed system images
  # shellcheck disable=SC1090
  source "${BT_TEMP_DIR}/scripts/sdk/gn/base/bin/fpave.sh.mock_state"
  gn-test-check-mock-args _ANY_ --prepare --image qemu-x64 --bucket fuchsia --work-dir "${FUCHSIA_WORK_DIR}"

  # Check that fserve.sh was called to download the needed system images
  # shellcheck disable=SC1090
  source "${BT_TEMP_DIR}/scripts/sdk/gn/base/bin/fserve.sh.mock_state"
  gn-test-check-mock-args _ANY_ --prepare --image qemu-x64 --bucket fuchsia --work-dir "${FUCHSIA_WORK_DIR}"

  # Verify that zbi was called to add the authorized_keys
  # shellcheck disable=SC1090
  source "${BT_TEMP_DIR}/scripts/sdk/gn/base/tools/zbi.mock_state"
  gn-test-check-mock-args _ANY_ -o _ANY_ "${FUCHSIA_WORK_DIR}/image/zircon-a.zbi" --entry "data/ssh/authorized_keys=${BT_TEMP_DIR}/scripts/sdk/gn/base/authkeys.txt"

  # Verify some of the arguments passed to the emulator binary
  # shellcheck disable=SC1090
  source "${FUCHSIA_WORK_DIR}/emulator/aemu-linux-amd64-${AEMU_LABEL}/emulator.mock_state"
  check_mock_has_args -fuchsia

}

# Verifies that the correct emulator command is run by femu, along with the image setup.
# This tests the -N option. On mac os, this is expected to fail.
TEST_femu_networking() {

  PATH_DIR_FOR_TEST="${BT_TEMP_DIR}/_isolated_path_for"
  export PATH="${PATH_DIR_FOR_TEST}:${PATH}"

  # Create fake "ip tuntap show" command to let fx emu know the network is configured with some mocked output
  cat >"${PATH_DIR_FOR_TEST}/ip.mock_side_effects" <<INPUT
echo "qemu: tap persist user 238107"
INPUT

  # Create fake "curl" command to that creates a simulated zip file based on the mocked emulator script
  # femu.sh calls curl like this, so use $4 to write output: curl -L "${CIPD_URL}" -o "${CIPD_FILE}" -#
  cat >"${PATH_DIR_FOR_TEST}/curl.mock_side_effects" <<INPUT
zip "\$4" "${FUCHSIA_WORK_DIR}/emulator/aemu-linux-amd64-${AEMU_LABEL}/emulator"
INPUT

  if is-mac; then
    expected_result=BT_EXPECT_FAIL
  else
    expected_result=BT_EXPECT
  fi

  # Run command.
  ${expected_result} gn-test-run-bash-script "${BT_TEMP_DIR}/scripts/sdk/gn/base/bin/femu.sh" \
    -N \
    --unknown-arg1-to-qemu \
    --authorized-keys "${BT_TEMP_DIR}/scripts/sdk/gn/base/testdata/authorized_keys" \
    --unknown-arg2-to-qemu

  #The remaining validation steps are disabled on OSX since the test failed earlier due to -N not being supported yet".
  if is-mac; then
    return 0
  fi

  # Verify that fvm resized the disk file by 2x from the input 1024 to 2048.
  # shellcheck disable=SC1090
  source "${BT_TEMP_DIR}/scripts/sdk/gn/base/tools/fvm.mock_state"
  gn-test-check-mock-args _ANY_ _ANY_ extend --length 2048

  # Check that fpave.sh was called to download the needed system images
  # shellcheck disable=SC1090
  source "${BT_TEMP_DIR}/scripts/sdk/gn/base/bin/fpave.sh.mock_state"
  gn-test-check-mock-args _ANY_ --prepare --image qemu-x64 --bucket fuchsia --work-dir "${FUCHSIA_WORK_DIR}"

  # Check that fserve.sh was called to download the needed system images
  # shellcheck disable=SC1090
  source "${BT_TEMP_DIR}/scripts/sdk/gn/base/bin/fserve.sh.mock_state"
  gn-test-check-mock-args _ANY_ --prepare --image qemu-x64 --bucket fuchsia --work-dir "${FUCHSIA_WORK_DIR}"

  # Verify that zbi was called to add the authorized_keys
  # shellcheck disable=SC1090
  source "${BT_TEMP_DIR}/scripts/sdk/gn/base/tools/zbi.mock_state"
  gn-test-check-mock-args _ANY_ -o _ANY_ "${FUCHSIA_WORK_DIR}/image/zircon-a.zbi" --entry "data/ssh/authorized_keys=${BT_TEMP_DIR}/scripts/sdk/gn/base/testdata/authorized_keys"

  # Verify some of the arguments passed to the emulator binary
  # shellcheck disable=SC1090
  source "${FUCHSIA_WORK_DIR}/emulator/aemu-linux-amd64-${AEMU_LABEL}/emulator.mock_state"
  # The mac address is computed with a hash function in fx emu but will not change if the device
  # is named qemu. We test the generated mac address here since our scripts use the mac for a
  # hard coded address to SSH into the device.
  check_mock_has_args -fuchsia
  check_mock_has_args -netdev type=tap,ifname=qemu,script=no,downscript=no,id=net0
  check_mock_has_args -device e1000,netdev=net0,mac=52:54:00:63:5e:7a
  check_mock_has_args --unknown-arg1-to-qemu
  check_mock_has_args --unknown-arg2-to-qemu
}
# Test initialization. Note that we copy various tools/devshell files and need to replicate the
# behavior of generate.py by copying these files into scripts/sdk/gn/base/bin/devshell
# shellcheck disable=SC2034
BT_FILE_DEPS=(
  scripts/sdk/gn/base/bin/femu.sh
  scripts/sdk/gn/base/bin/devshell/lib/image_build_vars.sh
  scripts/sdk/gn/base/bin/fuchsia-common.sh
  scripts/sdk/gn/base/bin/fx-image-common.sh
  scripts/sdk/gn/bash_tests/gn-bash-test-lib.sh
  tools/devshell/emu
  tools/devshell/lib/fvm.sh
)
# shellcheck disable=SC2034
BT_MOCKED_TOOLS=(
  scripts/sdk/gn/base/images/emulator/aemu-linux-amd64-"${AEMU_LABEL}"/emulator
  scripts/sdk/gn/base/bin/fpave.sh
  scripts/sdk/gn/base/bin/fserve.sh
  scripts/sdk/gn/base/tools/zbi
  scripts/sdk/gn/base/tools/fvm
  _isolated_path_for/ip
  _isolated_path_for/curl
  # Create fake "stty sane" command so that fx emu test succeeds when < /dev/null is being used
  _isolated_path_for/stty
)

BT_SET_UP() {
  FUCHSIA_WORK_DIR="${BT_TEMP_DIR}/scripts/sdk/gn/base/images"

  # Create a small disk image to avoid downloading, and test if it is doubled in size as expected
  mkdir -p "${FUCHSIA_WORK_DIR}/image"
  dd if=/dev/zero of="${FUCHSIA_WORK_DIR}/image/storage-full.blk" bs=1024 count=1  > /dev/null 2>/dev/null
}

BT_INIT_TEMP_DIR() {

  # Generate the aemu.version file based on the simulated version string
  echo "${AEMU_VERSION}" > "${BT_TEMP_DIR}/scripts/sdk/gn/base/bin/aemu.version"


  # Create empty authorized_keys file to add to the system image, but the contents are not used.
  mkdir -p "${BT_TEMP_DIR}/scripts/sdk/gn/base/testdata"
  echo ssh-ed25519 00000000000000000000000000000000000000000000000000000000000000000000 \
    >"${BT_TEMP_DIR}/scripts/sdk/gn/base/testdata/authorized_keys"

  # Stage the files we copy from the fx emu implementation, replicating behavior of generate.py
  cp "${BT_TEMP_DIR}/tools/devshell/emu" "${BT_TEMP_DIR}/scripts/sdk/gn/base/bin/devshell/emu"
  cp "${BT_TEMP_DIR}/tools/devshell/lib/fvm.sh" "${BT_TEMP_DIR}/scripts/sdk/gn/base/bin/devshell/lib/fvm.sh"
}

BT_RUN_TESTS "$@"
