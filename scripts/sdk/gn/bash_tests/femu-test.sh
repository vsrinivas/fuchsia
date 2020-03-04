#!/bin/bash
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Tests that femu is able to correctly interact with the fx emu command and
# its dependencies like fvm and aemu. These tests do not actually start up
# the emulator, but check the arguments are as expected.

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

# Verifies that the correct emulator command is run by femu, along with the image setup
TEST_femu() {
  # Create fake "ip tuntap show" command to let fx emu know the network is configured with some mocked output
  PATH_DIR_FOR_TEST="$(mktemp -d)"
  cat >"${PATH_DIR_FOR_TEST}/ip" <<"SETVAR"
#!/bin/bash
if [[ "$1" != "tuntap" ]]; then
  echo "Arg 1 is \"$1\" and not \"tuntap\""
  exit 1
fi
if [[ "$2" != "show" ]]; then
  echo "Arg 2 is \"$2\" and not \"show\""
  exit 1
fi
echo "qemu: tap persist user 238107"
SETVAR
  chmod ugo+x "${PATH_DIR_FOR_TEST}/ip"

  # Create fake "stty sane" command so that fx emu succeeds when < /dev/null is being used
  cat >"${PATH_DIR_FOR_TEST}/stty" <<"SETVAR"
#!/bin/bash
SETVAR
  chmod ugo+x "${PATH_DIR_FOR_TEST}/stty"

  export PATH="${PATH_DIR_FOR_TEST}:${PATH}"

  # Run command.
  BT_EXPECT run_bash_script "${BT_TEMP_DIR}/scripts/sdk/gn/base/bin/femu.sh" \
    -N \
    --unknown-arg1-to-qemu \
    --authorized-keys "${BT_TEMP_DIR}/scripts/sdk/gn/base/testdata/authorized_keys" \
    --unknown-arg2-to-qemu

  # Verify that fvm resized the disk file by 2x from the input 1024 to 2048.
  # This is an internal operation in fvm.sh with mktemp, so we cannot check
  # the input image path name, and so skip it with :2 when creating FVM_ARGS.
  source "${BT_TEMP_DIR}/scripts/sdk/gn/base/tools/fvm.mock_state"
  local FVM_ARGS=("${BT_MOCK_ARGS[@]:2}")
  local EXPECTED_FVM_ARGS=(
    extend
    --length 2048
  )
  BT_EXPECT_EQ ${#EXPECTED_FVM_ARGS[@]} ${#FVM_ARGS[@]}
  for i in "${!EXPECTED_FVM_ARGS[@]}"; do
    BT_EXPECT_EQ "${EXPECTED_FVM_ARGS[$i]}" "${FVM_ARGS[$i]}"
  done

  # Check that fpave.sh was called to download the needed system images
  source "${BT_TEMP_DIR}/scripts/sdk/gn/base/bin/fpave.sh.mock_state"
  local FPAVE_ARGS=("${BT_MOCK_ARGS[@]:1}")
  local EXPECTED_FPAVE_ARGS=(
    --prepare
    --image qemu-x64
    --bucket fuchsia
    --work-dir "${BT_TEMP_DIR}/scripts/sdk/gn/base/images"
  )
  BT_EXPECT_EQ ${#EXPECTED_FPAVE_ARGS[@]} ${#FPAVE_ARGS[@]}
  for i in "${!EXPECTED_FPAVE_ARGS[@]}"; do
    BT_EXPECT_EQ "${EXPECTED_FPAVE_ARGS[$i]}" "${FPAVE_ARGS[$i]}"
  done

  # Check that fserve.sh was called to download the needed system images
  source "${BT_TEMP_DIR}/scripts/sdk/gn/base/bin/fserve.sh.mock_state"
  local FSERVE_ARGS=("${BT_MOCK_ARGS[@]:1}")
  local EXPECTED_FSERVE_ARGS=(
    --prepare
    --image qemu-x64
    --bucket fuchsia
    --work-dir "${BT_TEMP_DIR}/scripts/sdk/gn/base/images"
  )
  BT_EXPECT_EQ ${#EXPECTED_FSERVE_ARGS[@]} ${#FSERVE_ARGS[@]}
  for i in "${!EXPECTED_FSERVE_ARGS[@]}"; do
    BT_EXPECT_EQ "${EXPECTED_FSERVE_ARGS[$i]}" "${FSERVE_ARGS[$i]}"
  done

  # Verify that zbi was called to add the authorized_keys
  source "${BT_TEMP_DIR}/scripts/sdk/gn/base/tools/zbi.mock_state"
  local ZBI_ARGS=("${BT_MOCK_ARGS[@]:1}")
  local EXPECTED_ZBI_ARGS=(
    -o NOCHECK_ZBI_FILE
    "${BT_TEMP_DIR}/scripts/sdk/gn/base/images/image/zircon-a.zbi"
    --entry "data/ssh/authorized_keys=${BT_TEMP_DIR}/scripts/sdk/gn/base/testdata/authorized_keys"
  )
  BT_EXPECT_EQ ${#EXPECTED_ZBI_ARGS[@]} ${#ZBI_ARGS[@]}
  for i in "${!EXPECTED_ZBI_ARGS[@]}"; do
    # The zbi tools creates an internal mktemp file that we don't know, so do not match NOCHECK_ZBI_FILE
    if [[ "${EXPECTED_ZBI_ARGS[$i]}" != "NOCHECK_ZBI_FILE" ]]; then
      BT_EXPECT_EQ "${EXPECTED_ZBI_ARGS[$i]}" "${ZBI_ARGS[$i]}"
    fi
  done

  # Verify some of the arguments passed to the emulator binary
  source "${BT_TEMP_DIR}/scripts/sdk/gn/base/images/emulator/aemu-linux-amd64/emulator.mock_state"
  local EMULATOR_ARGS=("${BT_MOCK_ARGS[@]:1}")
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
BT_FILE_DEPS=(
  scripts/sdk/gn/base/bin/femu.sh
  scripts/sdk/gn/base/bin/devshell/lib/image_build_vars.sh
  scripts/sdk/gn/base/bin/fuchsia-common.sh
  scripts/sdk/gn/base/bin/fx-image-common.sh
  tools/devshell/emu
  tools/devshell/lib/fvm.sh
)
BT_MOCKED_TOOLS=(
  scripts/sdk/gn/base/images/emulator/aemu-linux-amd64/emulator
  scripts/sdk/gn/base/bin/fpave.sh
  scripts/sdk/gn/base/bin/fserve.sh
  scripts/sdk/gn/base/tools/zbi
  scripts/sdk/gn/base/tools/fvm
)

BT_INIT_TEMP_DIR() {
  # Do not download aemu, set up necessary files to skip this
  touch "${BT_TEMP_DIR}/scripts/sdk/gn/base/images/emulator/aemu-linux-amd64-latest.zip"
  mkdir -p "${BT_TEMP_DIR}/scripts/sdk/gn/base/images/aemu-linux-amd64"

  # Create a small disk image to avoid downloading, and test if it is doubled in size as expected
  mkdir -p "${BT_TEMP_DIR}/scripts/sdk/gn/base/images/image"
  dd if=/dev/zero of="${BT_TEMP_DIR}/scripts/sdk/gn/base/images/image/storage-full.blk" bs=1024 count=1 status=none

  # Create empty authorized_keys file to add to the system image, but the contents are not used.
  mkdir -p "${BT_TEMP_DIR}/scripts/sdk/gn/base/testdata"
  echo ssh-ed25519 00000000000000000000000000000000000000000000000000000000000000000000 \
    >"${BT_TEMP_DIR}/scripts/sdk/gn/base/testdata/authorized_keys"

  # Stage the files we copy from the fx emu implementation, replicating behavior of generate.py
  cp "${BT_TEMP_DIR}/tools/devshell/emu" "${BT_TEMP_DIR}/scripts/sdk/gn/base/bin/devshell/emu"
  cp "${BT_TEMP_DIR}/tools/devshell/lib/fvm.sh" "${BT_TEMP_DIR}/scripts/sdk/gn/base/bin/devshell/lib/fvm.sh"
}

BT_RUN_TESTS "$@"
