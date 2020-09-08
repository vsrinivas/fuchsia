#!/bin/bash
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Tests that femu is able to correctly interact with the fx emu command and
# its dependencies like fvm and aemu. These tests do not actually start up
# the emulator, but check the arguments are as expected.

set -e

# Specify a simulated CIPD instance id for prebuilts
AEMU_VERSION="git_revision:unknown"
AEMU_LABEL="$(echo "${AEMU_VERSION}" | tr ':/' '_')"
GRPCWEBPROXY_VERSION="git_revision:unknown"
GRPCWEBPROXY_LABEL="$(echo "${GRPCWEBPROXY_VERSION}" | tr ':/' '_')"

function run_femu_wrapper() {
  # femu.sh will run "fvm decompress" to convert the given fvm image format into
  # an intermediate raw image suffixed by ".decompress". The image is then used for
  # extension. Since the fvm tool is faked and does nothing in the test, we need
  # to fake the intermediate decompressed image.
  cp "${FUCHSIA_WORK_DIR}/image/storage-full.blk" \
    "${FUCHSIA_WORK_DIR}/image/storage-full.blk.decompressed"
  gn-test-run-bash-script "${BT_TEMP_DIR}/scripts/sdk/gn/base/bin/femu.sh" "$@"
}

# Verifies that the correct emulator command is run by femu, do not activate the network interface
TEST_femu_standalone() {

  PATH_DIR_FOR_TEST="${BT_TEMP_DIR}/_isolated_path_for"
  export PATH="${PATH_DIR_FOR_TEST}:${PATH}"

  # Create fake ZIP file download so femu.sh doesn't try to download it, and
  # later on provide a mocked emulator script so it doesn't try to unzip it.
  touch "${FUCHSIA_WORK_DIR}/emulator/aemu-${PLATFORM}-${AEMU_LABEL}.zip"

  # Need to configure a DISPLAY so that we can get past the graphics error checks
  export DISPLAY="fakedisplay"

  # Run command.
  BT_EXPECT run_femu_wrapper

  # Verify that the image first goes through a decompress process by fvm
  source "${MOCKED_FVM}.mock_state.1"
  gn-test-check-mock-args _ANY_ _ANY_ decompress --default _ANY_

  # Verify that the image will be extended to double the size
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
  gn-test-check-mock-args _ANY_ -o _ANY_ "${FUCHSIA_WORK_DIR}/image/zircon-a.zbi" --entry "data/ssh/authorized_keys=${HOME}/.ssh/fuchsia_authorized_keys"

  # Verify some of the arguments passed to the emulator binary
  # shellcheck disable=SC1090
  source "${FUCHSIA_WORK_DIR}/emulator/aemu-${PLATFORM}-${AEMU_LABEL}/emulator.mock_state"
  gn-test-check-mock-partial -fuchsia
}

TEST_femu_fallback_to_fvm_fastboot_if_raw_image_not_exist() {
  PATH_DIR_FOR_TEST="${BT_TEMP_DIR}/_isolated_path_for"
  export PATH="${PATH_DIR_FOR_TEST}:${PATH}"

  # Create fake ZIP file download so femu.sh doesn't try to download it, and
  # later on provide a mocked emulator script so it doesn't try to unzip it.
  touch "${FUCHSIA_WORK_DIR}/emulator/aemu-${PLATFORM}-${AEMU_LABEL}.zip"

  # Need to configure a DISPLAY so that we can get past the graphics error checks
  export DISPLAY="fakedisplay"

  # Renamed the fvm image
  src="${FUCHSIA_WORK_DIR}/image/storage-full.blk"
  dst="${FUCHSIA_WORK_DIR}/image/storage-fastboot.blk"
  mv "${src}" "${dst}"
  cp "${dst}" "${dst}.decompressed"
  export IMAGE_FVM_FASTBOOT="storage-fastboot.blk"

  # Run command.
  BT_EXPECT gn-test-run-bash-script "${BT_TEMP_DIR}/scripts/sdk/gn/base/bin/femu.sh"

  # Verify that the image first goes through a decompress process by fvm
  source "${MOCKED_FVM}.mock_state.1"
  gn-test-check-mock-args _ANY_ _ANY_ decompress --default "${dst}"

  # Verify that the image will be extended to double the size
  source "${MOCKED_FVM}.mock_state.2"
  gn-test-check-mock-args _ANY_ _ANY_ extend --length 2048 --length-is-lowerbound
}

# Verifies that the --experiment-arm64 option selects arm64 support
TEST_femu_arm64() {

  PATH_DIR_FOR_TEST="${BT_TEMP_DIR}/_isolated_path_for"
  export PATH="${PATH_DIR_FOR_TEST}:${PATH}"

  # Create fake ZIP file download so femu.sh doesn't try to download it, and
  # later on provide a mocked emulator script so it doesn't try to unzip it.
  touch "${FUCHSIA_WORK_DIR}/emulator/aemu-${PLATFORM}-${AEMU_LABEL}.zip"

  # Need to configure a DISPLAY so that we can get past the graphics error checks
  export DISPLAY="fakedisplay"

  # Run command.
  BT_EXPECT run_femu_wrapper \
    --experiment-arm64 \
    --image qemu-arm64 \
    --software-gpu

  # Check that fpave.sh was called to download the needed system images
  # shellcheck disable=SC1090
  source "${BT_TEMP_DIR}/scripts/sdk/gn/base/bin/fpave.sh.mock_state"
  gn-test-check-mock-args _ANY_ --prepare --image qemu-arm64 --bucket fuchsia --work-dir "${FUCHSIA_WORK_DIR}"

  # Check that fserve.sh was called to download the needed system images
  # shellcheck disable=SC1090
  source "${BT_TEMP_DIR}/scripts/sdk/gn/base/bin/fserve.sh.mock_state"
  gn-test-check-mock-args _ANY_ --prepare --image qemu-arm64 --bucket fuchsia --work-dir "${FUCHSIA_WORK_DIR}"

  # Verify some of the arguments passed to the emulator binary
  # shellcheck disable=SC1090
  source "${FUCHSIA_WORK_DIR}/emulator/aemu-${PLATFORM}-${AEMU_LABEL}/emulator.mock_state"
  gn-test-check-mock-partial -fuchsia
  gn-test-check-mock-partial -avd-arch arm64
  gn-test-check-mock-partial -cpu cortex-a53
  gn-test-check-mock-partial -gpu swiftshader_indirect
}

# Verifies that the correct emulator command is run by femu, along with the image setup.
# This tests the -N option for networking.
TEST_femu_networking() {

  PATH_DIR_FOR_TEST="${BT_TEMP_DIR}/_isolated_path_for"
  export PATH="${PATH_DIR_FOR_TEST}:${PATH}"

  # Create fake "ip tuntap show" command to let fx emu know the network is configured with some mocked output
  cat >"${PATH_DIR_FOR_TEST}/ip.mock_side_effects" <<INPUT
echo "qemu: tap persist user 238107"
INPUT

  # Create fake ZIP file download so femu.sh doesn't try to download it, and
  # later on provide a mocked emulator script so it doesn't try to unzip it.
  touch "${FUCHSIA_WORK_DIR}/emulator/aemu-${PLATFORM}-${AEMU_LABEL}.zip"

  # Need to configure a DISPLAY so that we can get past the graphics error checks
  export DISPLAY="fakedisplay"

  # OSX may not have the tun/tap driver installed, and you cannot bypass the
  # network checks, so need to work around this for the test. Linux does not
  # need a fake network because we use a fake ip command.
  if is-mac && [[ ! -c /dev/tap0 || ! -w /dev/tap0 ]]; then
    NETWORK_ARGS=( -N -I fakenetwork )
  else
    NETWORK_ARGS=( -N )
  fi

  # Run command.
  BT_EXPECT run_femu_wrapper \
    "${NETWORK_ARGS[*]}" \
    --unknown-arg1-to-qemu \
    --authorized-keys "${BT_TEMP_DIR}/scripts/sdk/gn/base/testdata/authorized_keys" \
    --unknown-arg2-to-qemu

  # Verify that the image first goes through a decompress process by fvm
  source "${MOCKED_FVM}.mock_state.1"
  gn-test-check-mock-args _ANY_ _ANY_ decompress --default _ANY_

  # Verify that the image will be extended to double the size
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
  if is-mac; then
    if [[ -c /dev/tap0 && -w /dev/tap0 ]]; then
      gn-test-check-mock-partial -netdev type=tap,ifname=tap0,id=net0,script="${BT_TEMP_DIR}/scripts/sdk/gn/base/bin/devshell/lib/emu-ifup-macos.sh"
      gn-test-check-mock-partial -device e1000,netdev=net0,mac=52:54:00:4d:27:96
    else
      gn-test-check-mock-partial -netdev type=tap,ifname=fakenetwork,id=net0,script="${BT_TEMP_DIR}/scripts/sdk/gn/base/bin/devshell/lib/emu-ifup-macos.sh"
      gn-test-check-mock-partial -device e1000,netdev=net0,mac=52:54:00:95:03:66
    fi
  else
    gn-test-check-mock-partial -netdev type=tap,ifname=qemu,id=net0,script=no
    gn-test-check-mock-partial -device e1000,netdev=net0,mac=52:54:00:63:5e:7a
  fi
  gn-test-check-mock-partial --unknown-arg1-to-qemu
  gn-test-check-mock-partial --unknown-arg2-to-qemu
}

# Verifies that fx emu starts up grpcwebproxy correctly
TEST_femu_grpcwebproxy() {

  PATH_DIR_FOR_TEST="${BT_TEMP_DIR}/_isolated_path_for"
  export PATH="${PATH_DIR_FOR_TEST}:${PATH}"

  # Create fake "kill" command so when emu tries to kill grpcwebproxy, it doesn't fail
  # this test. Need to embed "enable -n kill" into fx-image-common.sh to disable the
  # bash builtin kill so we can intercept it.
  cat >"${PATH_DIR_FOR_TEST}/kill.mock_side_effects" <<INPUT
echo "$@"
INPUT
  echo "enable -n kill" >> "${BT_TEMP_DIR}/scripts/sdk/gn/base/bin/fx-image-common.sh"

  # Create fake ZIP file download so femu.sh doesn't try to download it, and
  # later on provide a mocked emulator script so it doesn't try to unzip it.
  touch "${FUCHSIA_WORK_DIR}/emulator/aemu-${PLATFORM}-${AEMU_LABEL}.zip"
  touch "${FUCHSIA_WORK_DIR}/emulator/grpcwebproxy-${PLATFORM}-${AEMU_LABEL}.zip"

  # Need to configure a DISPLAY so that we can get past the graphics error checks
  export DISPLAY="fakedisplay"

  if is-mac; then
    # grpcwebproxy does not work on OSX, so check there is an error message
    BT_EXPECT_FAIL run_femu_wrapper \
      -x 1234 \
      > femu_error_output.txt 2>&1
  else
    # Run command with the default grpcwebproxy.
    BT_EXPECT run_femu_wrapper \
      -x 1234

    # Verify that the default grpcwebproxy binary is called correctly
    # shellcheck disable=SC1090
    source "${FUCHSIA_WORK_DIR}/emulator/grpcwebproxy-${PLATFORM}-${GRPCWEBPROXY_LABEL}/grpcwebproxy.mock_state"
    gn-test-check-mock-partial --backend_addr localhost:5556
    gn-test-check-mock-partial --server_http_debug_port 1234

    # Run command and test the -X for a manually provided grpcwebproxy.
    BT_EXPECT run_femu_wrapper \
      -x 1234 -X "${BT_TEMP_DIR}/mocked/grpcwebproxy-dir"

    # Verify that the grpcwebproxy binary is called correctly
    # shellcheck disable=SC1090
    source "${BT_TEMP_DIR}/mocked/grpcwebproxy-dir/grpcwebproxy.mock_state"
    gn-test-check-mock-partial --backend_addr localhost:5556
    gn-test-check-mock-partial --server_http_debug_port 1234
  fi
}

# Verifies that the --setup-ufw option runs ufw
TEST_femu_ufw() {

  PATH_DIR_FOR_TEST="${BT_TEMP_DIR}/_isolated_path_for"
  export PATH="${PATH_DIR_FOR_TEST}:${PATH}"

  if is-mac; then
    BT_EXPECT_FAIL run_femu_wrapper \
      --setup-ufw > femu_ufw_error.txt 2>&1
  else
    BT_EXPECT run_femu_wrapper \
      --setup-ufw

    # Check that ufw was called via sudo
    # shellcheck disable=SC1090
    source "${BT_TEMP_DIR}/_isolated_path_for/sudo.mock_state.1"
    gn-test-check-mock-args _ANY_ ufw allow proto _ANY_ from _ANY_ to any port _ANY_ comment _ANY_
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
  # mock both mac and linux so the test runs successfully on both.
  test-home/.fuchsia/emulator/aemu-linux-amd64-"${AEMU_LABEL}"/emulator
  test-home/.fuchsia/emulator/aemu-mac-amd64-"${AEMU_LABEL}"/emulator
  test-home/.fuchsia/emulator/grpcwebproxy-mac-amd64-"${GRPCWEBPROXY_LABEL}"/grpcwebproxy
  test-home/.fuchsia/emulator/grpcwebproxy-linux-amd64-"${GRPCWEBPROXY_LABEL}"/grpcwebproxy
  scripts/sdk/gn/base/bin/fpave.sh
  scripts/sdk/gn/base/bin/fserve.sh
  scripts/sdk/gn/base/tools/x64/zbi
  scripts/sdk/gn/base/tools/arm64/zbi
  scripts/sdk/gn/base/tools/x64/fvm
  scripts/sdk/gn/base/tools/arm64/fvm
  mocked/grpcwebproxy-dir/grpcwebproxy
  _isolated_path_for/ip
  _isolated_path_for/kill
  _isolated_path_for/sudo
  # Create fake "stty sane" command so that fx emu test succeeds when < /dev/null is being used
  _isolated_path_for/stty
)

BT_SET_UP() {
  # shellcheck disable=SC1090
  source "${BT_TEMP_DIR}/scripts/sdk/gn/bash_tests/gn-bash-test-lib.sh"

  # Make "home" directory in the test dir so the paths are stable."
  mkdir -p "${BT_TEMP_DIR}/test-home"
  export HOME="${BT_TEMP_DIR}/test-home"
  FUCHSIA_WORK_DIR="${HOME}/.fuchsia"

  if is-mac; then
    PLATFORM="mac-amd64"
  else
    PLATFORM="linux-amd64"
  fi

  MOCKED_FVM="${BT_TEMP_DIR}/scripts/sdk/gn/base/$(gn-test-tools-subdir)/fvm"
  MOCKED_ZBI="${BT_TEMP_DIR}/scripts/sdk/gn/base/$(gn-test-tools-subdir)/zbi"

  # Create a small disk image to avoid downloading, and test if it is doubled in size as expected
  mkdir -p "${FUCHSIA_WORK_DIR}/image"
  dd if=/dev/zero of="${FUCHSIA_WORK_DIR}/image/storage-full.blk" bs=1024 count=1  > /dev/null 2>/dev/null
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
