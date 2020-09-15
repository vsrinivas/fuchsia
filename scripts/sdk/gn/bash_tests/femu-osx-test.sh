#!/bin/bash
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Tests as much of femu as possible for OSX but running on a Linux machine, using
# fake uname output to test these code paths. We do not actually run the binaries,
# but just make sure that the arguments and dependencies are as correct as possible.

set -e

# Initialize variables that need to be set before BT_SET_UP()

# Specify a simulated CIPD instance id for prebuilts
AEMU_VERSION="git_revision:unknown"
AEMU_LABEL="$(echo "${AEMU_VERSION}" | tr ':/' '_')"
GRPCWEBPROXY_VERSION="git_revision:unknown"
# Force mac-amd64 to test OSX on Linux
PLATFORM="mac-amd64"

# Create fake "uname -s" to pretend we are on OSX
set_up_uname() {
  cat >"${PATH_DIR_FOR_TEST}/uname.mock_side_effects" <<"INPUT"
if [[ "$1" == "-m" ]]; then
  echo "x86_64"
elif [[ "$1" == "-s" ]]; then
  echo "Darwin"
elif [[ "$1" == "" ]]; then
  echo "Darwin"
else
  echo "Unexpected uname option: $1"
  exit 1
fi
INPUT
}

# The tools/devshell/lib/fvm.sh script adds -x for Darwin, so we need to
# remove it here so it runs on Linux. Create a fake "stat" command that
# ignores the -x, and then calls the local Linux stat command. Make sure
# this continues to work on OSX as well with IS_MAC.
set_up_stat() {
  cat >"${PATH_DIR_FOR_TEST}/stat.mock_side_effects" <<"INPUT"
if (( ! IS_MAC )); then
  if [[ "$1" != "-x" ]]; then
    echo "Error: Expected -x argument to ignore"
    exit 1
  fi
  shift
fi
"${STAT_PATH}" $@
INPUT
}

# Create fake ZIP file download so femu.sh doesn't try to download it, and
# later on provide a mocked emulator script so it doesn't try to unzip it.
set_up_cipd() {
  touch "${FUCHSIA_WORK_DIR}/emulator/aemu-${PLATFORM}-${AEMU_LABEL}.zip"
}

function run_femu_wrapper() {
  # femu.sh will run "fvm decompress" to convert the given fvm image format into
  # an intermediate raw image suffixed by ".decompress". The image is then used for
  # extension. Since the fvm tool is faked and does nothing in the test, we need
  # to fake the intermediate decompressed image.
  cp "${FUCHSIA_WORK_DIR}/image/storage-full.blk" \
    "${FUCHSIA_WORK_DIR}/image/storage-full.blk.decompressed"
  gn-test-run-bash-script "${BT_TEMP_DIR}/scripts/sdk/gn/base/bin/femu.sh" "$@"
}


TEST_femu_osx_networking() {
  PATH_DIR_FOR_TEST="${BT_TEMP_DIR}/_isolated_path_for"
  export PATH="${PATH_DIR_FOR_TEST}:${PATH}"

  set_up_uname
  set_up_stat
  set_up_cipd

  # Run command.
  BT_EXPECT run_femu_wrapper \
    -N \
    -I fakenetwork \
    --unknown-arg1-to-qemu \
    --authorized-keys "${BT_TEMP_DIR}/scripts/sdk/gn/base/testdata/authorized_keys" \
    --unknown-arg2-to-qemu

  # Verify that the image first goes through a decompress process by fvm
  # shellcheck disable=SC1090
  source "${MOCKED_FVM}.mock_state.1"
  gn-test-check-mock-args _ANY_ _ANY_ decompress --default _ANY_

  # Verify that the image will be extended to double the size
  # shellcheck disable=SC1090
  source "${MOCKED_FVM}.mock_state.2"
  gn-test-check-mock-args _ANY_ _ANY_ extend --length 2048 --length-is-lowerbound

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
  source "${MOCKED_ZBI}.mock_state"
  gn-test-check-mock-args _ANY_ -o _ANY_ "${FUCHSIA_WORK_DIR}/image/zircon-a.zbi" --entry "data/ssh/authorized_keys=${BT_TEMP_DIR}/scripts/sdk/gn/base/testdata/authorized_keys"

  # Verify some of the arguments passed to the emulator binary
  # shellcheck disable=SC1090
  source "${FUCHSIA_WORK_DIR}/emulator/aemu-${PLATFORM}-${AEMU_LABEL}/emulator.mock_state"
  # The mac address is computed with a hash function in fx emu based on the device name.
  # We test the generated mac address since other scripts hard code this to SSH into the device.
  gn-test-check-mock-partial -fuchsia
  gn-test-check-mock-partial -netdev type=tap,ifname=fakenetwork,id=net0,script="${BT_TEMP_DIR}/scripts/sdk/gn/base/bin/devshell/lib/emu-ifup-macos.sh"
  gn-test-check-mock-partial -device e1000,netdev=net0,mac=52:54:00:95:03:66
  gn-test-check-mock-partial --unknown-arg1-to-qemu
  gn-test-check-mock-partial --unknown-arg2-to-qemu

  # Check that the default OSX ifup script actually exists
  BT_EXPECT_FILE_EXISTS "${BT_TEMP_DIR}/scripts/sdk/gn/base/bin/devshell/lib/emu-ifup-macos.sh"
}

TEST_femu_osx_fail_tuntap() {
  PATH_DIR_FOR_TEST="${BT_TEMP_DIR}/_isolated_path_for"
  export PATH="${PATH_DIR_FOR_TEST}:${PATH}"

  set_up_uname
  set_up_stat
  set_up_cipd

  if [[ -c /dev/tap0 && -w /dev/tap0 ]]; then
    # Run command, which should work because the tun/tap driver is installed and writable by the user
    BT_EXPECT run_femu_wrapper \
      -N

    # Verify some of the arguments passed to the emulator binary
    # shellcheck disable=SC1090
    source "${FUCHSIA_WORK_DIR}/emulator/aemu-${PLATFORM}-${AEMU_LABEL}/emulator.mock_state"
    # The mac address is computed with a hash function in fx emu based on the device name.
    # We test the generated mac address since other scripts hard code this to SSH into the device.
    gn-test-check-mock-partial -fuchsia
    gn-test-check-mock-partial -netdev type=tap,ifname=tap0,id=net0,script="${BT_TEMP_DIR}/scripts/sdk/gn/base/bin/devshell/lib/emu-ifup-macos.sh"
    gn-test-check-mock-partial -device e1000,netdev=net0,mac=52:54:00:4d:27:96
  else
    # The tun/tap driver is not installed, so test if the execution fails the way we expect
    BT_EXPECT_FAIL run_femu_wrapper \
      -N \
      > femu_error_output.txt 2>&1

    if [[ ! -c /dev/tap0 ]]; then
      BT_EXPECT_FILE_CONTAINS_SUBSTRING femu_error_output.txt "To use emu with networking on macOS, install the tun/tap driver"
    else
      BT_EXPECT_FILE_CONTAINS_SUBSTRING femu_error_output.txt "For networking /dev/tap0 must be owned by ${USER}. Please run:"
    fi
  fi
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
  tools/devshell/lib/emu-ifup-macos.sh
)
# shellcheck disable=SC2034
BT_MOCKED_TOOLS=(
  test-home/.fuchsia/emulator/aemu-linux-amd64-"${AEMU_LABEL}"/emulator
  test-home/.fuchsia/emulator/aemu-mac-amd64-"${AEMU_LABEL}"/emulator
  scripts/sdk/gn/base/bin/fpave.sh
  scripts/sdk/gn/base/bin/fserve.sh
  scripts/sdk/gn/base/tools/x64/zbi
  scripts/sdk/gn/base/tools/x64/fvm
  scripts/sdk/gn/base/tools/arm64/zbi
  scripts/sdk/gn/base/tools/arm64/fvm
  _isolated_path_for/ip
  # Create fake "stty sane" command so that fx emu test succeeds when < /dev/null is being used
  _isolated_path_for/stty
  # Create fake "uname" command to pretend we are on OSX
  _isolated_path_for/uname
  # Create fake "stat" command that eats up the -x argument for OSX which doesn't work on Linux
  _isolated_path_for/stat
)

BT_SET_UP() {

  # shellcheck disable=SC1090
  source "${BT_TEMP_DIR}/scripts/sdk/gn/bash_tests/gn-bash-test-lib.sh"

  # Make "home" directory in the test dir so the paths are stable."
  mkdir -p "${BT_TEMP_DIR}/test-home"
  export HOME="${BT_TEMP_DIR}/test-home"
  FUCHSIA_WORK_DIR="${HOME}/.fuchsia"

  # We change the PATH to override the stat command, but need a reference to it
  if ! STAT_PATH="$(type -p stat)"; then
    echo "Error: Could not type -p stat"
    exit 1
  fi
  export STAT_PATH

  # Detect if we are on a Mac and store it, because once we override uname
  # then is-mac will always return 1
  if is-mac; then
    export IS_MAC=1
  else
    export IS_MAC=0
  fi

  # Create a small disk image to avoid downloading, and test if it is doubled in size as expected
  mkdir -p "${FUCHSIA_WORK_DIR}/image"
  dd if=/dev/zero of="${FUCHSIA_WORK_DIR}/image/storage-full.blk" bs=1024 count=1  > /dev/null 2>/dev/null

  MOCKED_FVM="${BT_TEMP_DIR}/scripts/sdk/gn/base/$(gn-test-tools-subdir)/fvm"
  MOCKED_ZBI="${BT_TEMP_DIR}/scripts/sdk/gn/base/$(gn-test-tools-subdir)/zbi"

}

BT_INIT_TEMP_DIR() {

  # Generate the prebuilt version file based on the simulated version string
  echo "${AEMU_VERSION}" > "${BT_TEMP_DIR}/scripts/sdk/gn/base/bin/aemu.version"
  echo "${GRPCWEBPROXY_VERSION}" > "${BT_TEMP_DIR}/scripts/sdk/gn/base/bin/grpcwebproxy.version"

  # Create empty authorized_keys file to add to the system image, but the contents are not used.
  mkdir -p "${BT_TEMP_DIR}/scripts/sdk/gn/base/testdata"
  echo ssh-ed25519 00000000000000000000000000000000000000000000000000000000000000000000 \
    >"${BT_TEMP_DIR}/scripts/sdk/gn/base/testdata/authorized_keys"

  # Stage the files we copy from the fx emu implementation, replicating behavior of generate.py
  cp -r "${BT_TEMP_DIR}/tools/devshell" "${BT_TEMP_DIR}/scripts/sdk/gn/base/bin/"
}

BT_RUN_TESTS "$@"
