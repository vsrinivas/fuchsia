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

  # Create fake ZIP file download so femu.sh doesn't try to download it, and
  # later on provide a mocked emulator script so it doesn't try to unzip it.
  touch "${FUCHSIA_WORK_DIR}/emulator/aemu-${PLATFORM}-${AEMU_LABEL}.zip"

  # Need to configure a DISPLAY so that we can get past the graphics error checks
  export DISPLAY="fakedisplay"

  # Run command.
  BT_EXPECT run_femu_wrapper

  # Verify that the image first goes through a decompress process by fvm.
  # shellcheck disable=SC1090
  source "${MOCKED_FVM}.mock_state.1"
  gn-test-check-mock-args _ANY_ _ANY_ decompress --default _ANY_

  # Verify that the image will be extended to double the size.
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
  gn-test-check-mock-args _ANY_ -o _ANY_ "${FUCHSIA_WORK_DIR}/image/zircon-a.zbi" --entry "data/ssh/authorized_keys=${HOME}/.ssh/fuchsia_authorized_keys"

  # Verify some of the arguments passed to the emulator binary
  # shellcheck disable=SC1090
  source "${FUCHSIA_WORK_DIR}/emulator/aemu-${PLATFORM}-${AEMU_LABEL}/emulator.mock_state"
  gn-test-check-mock-partial -fuchsia
}

TEST_femu_fallback_to_fvm_fastboot_if_raw_image_not_exist() {

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

  # Verify that the image first goes through a decompress process by fvm.
  # shellcheck disable=SC1090
  source "${MOCKED_FVM}.mock_state.1"
  gn-test-check-mock-args _ANY_ _ANY_ decompress --default "${dst}"

  # Verify that the image will be extended to double the size.
  # shellcheck disable=SC1090
  source "${MOCKED_FVM}.mock_state.2"
  gn-test-check-mock-args _ANY_ _ANY_ extend --length 2048 --length-is-lowerbound
}

# Verifies that the --experiment-arm64 option selects arm64 support
TEST_femu_arm64() {

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

  # Verify that the image first goes through a decompress process by fvm.
  # shellcheck disable=SC1090
  source "${MOCKED_FVM}.mock_state.1"
  gn-test-check-mock-args _ANY_ _ANY_ decompress --default _ANY_

  # Verify that the image will be extended to double the size.
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
  if is-mac; then
    if [[ -c /dev/tap0 && -w /dev/tap0 ]]; then
      gn-test-check-mock-partial -netdev type=tap,ifname=tap0,id=net0,script="${BT_TEMP_DIR}/scripts/sdk/gn/base/bin/devshell/lib/emu-ifup-macos.sh"
      gn-test-check-mock-partial -device virtio-net-pci,vectors=8,netdev=net0,mac=52:54:00:4d:27:96
    else
      gn-test-check-mock-partial -netdev type=tap,ifname=fakenetwork,id=net0,script="${BT_TEMP_DIR}/scripts/sdk/gn/base/bin/devshell/lib/emu-ifup-macos.sh"
      gn-test-check-mock-partial -device virtio-net-pci,vectors=8,netdev=net0,mac=52:54:00:95:03:66
    fi
  else
    gn-test-check-mock-partial -netdev type=tap,ifname=qemu,id=net0,script=no
    gn-test-check-mock-partial -device virtio-net-pci,vectors=8,netdev=net0,mac=52:54:00:63:5e:7a
  fi
  gn-test-check-mock-partial --unknown-arg1-to-qemu
  gn-test-check-mock-partial --unknown-arg2-to-qemu
}

# Verifies that fx emu starts up grpcwebproxy correctly
TEST_femu_grpcwebproxy() {

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

TEST_femu_help() {
  BT_EXPECT "${BT_TEMP_DIR}/scripts/sdk/gn/base/bin/femu.sh" --help > "${BT_TEMP_DIR}/usage.txt"

readonly EXPECTED_HELP="Usage: femu.sh
  [--work-dir <directory to store image assets>]
    Defaults to ${BT_TEMP_DIR}/test-home/.fuchsia
  [--bucket <fuchsia gsutil bucket>]
    Default is read using \`fconfig.sh get emu-bucket\` if set. Otherwise defaults to fuchsia.
  [--image <image name>]
     Default is read using \`fconfig.sh get emu-image\` if set. Otherwise defaults to qemu-x64.
  [--authorized-keys <file>]
    The authorized public key file for securing the device.  Defaults to 
    ${BT_TEMP_DIR}/test-home/.ssh/fuchsia_authorized_keys, which is generated if needed.
  [--version <version>]
    Specify the CIPD version of AEMU to download.
    Defaults to aemu.version file with git_revision:unknown
  [--experiment-arm64]
    Override FUCHSIA_ARCH to arm64, instead of the default x64.
    This is required for *-arm64 system images, and is not auto detected.
  [--setup-ufw]
    Set up ufw firewall rules needed for Fuchsia device discovery
    and package serving, then exit. Only works on Linux with ufw
    firewall, and requires sudo.
  [--help] [-h]
    Show command line options for femu.sh and emu subscript

Remaining arguments are passed to emu wrapper and emulator:
  -a <mode> acceleration mode (auto, off, kvm, hvf, hax), default is auto
  -c <text> append item to kernel command line
  -ds <size> extends the fvm image size to <size> bytes. Default is twice the original size
  -N run with emulated nic via tun/tap
  -I <ifname> uses the tun/tap interface named ifname
  -u <path> execute emu if-up script, default: linux: no script, macos: tap ifup script.
  -e <directory> location of emulator, defaults to looking in prebuilt/third_party/aemu/PLATFORM
  -g <port> enable gRPC service on port to control the emulator, default is 5556 when WebRTC service is enabled
  -r <fps> webrtc frame rate when using gRPC service, default is 30
  -t <cmd> execute the given command to obtain turn configuration
  -x <port> enable WebRTC HTTP service on port
  -X <directory> location of grpcwebproxy, defaults to looking in prebuilt/third_party/grpcwebproxy
  -w <size> window size, default is 1280x800
  -s <cpus> number of cpus, 1 for uniprocessor
  -m <mb> total memory, in MB
  -k <authorized_keys_file> SSH authorized keys file, otherwise defaults to ~/.ssh/fuchsia_authorized_keys
  -K <kernel_image> path to image to use as kernel, defaults to kernel generated by the current build.
  -z <zbi_image> path to image to use as ZBI, defaults to zircon-a generated by the current build.
  -f <fvm_image> path to full FVM image, defaults to image generated by the current build.
  -A <arch> architecture of images (x64, arm64), default is the current build.
  -H|--headless run in headless mode
  --audio run with audio hardware added to the virtual machine
  --host-gpu run with host GPU acceleration
  --software-gpu run without host GPU acceleration
  --debugger pause on launch and wait for a debugger process to attach before resuming

Invalid argument names are not flagged as errors, and are passed on to emulator"

  BT_EXPECT_FILE_CONTAINS "${BT_TEMP_DIR}/usage.txt" "${EXPECTED_HELP}"
}

TEST_femu_with_props() {

 BT_EXPECT "${FCONFIG_CMD}" set emu-bucket "test-bucket"
 BT_EXPECT "${FCONFIG_CMD}" set emu-image "test-image"

  # Create fake ZIP file download so femu.sh doesn't try to download it, and
  # later on provide a mocked emulator script so it doesn't try to unzip it.
  touch "${FUCHSIA_WORK_DIR}/emulator/aemu-${PLATFORM}-${AEMU_LABEL}.zip"

  # Need to configure a DISPLAY so that we can get past the graphics error checks
  export DISPLAY="fakedisplay"

  # Run command.
  BT_EXPECT run_femu_wrapper

  # Check that fpave.sh was called to download the needed system images
  # shellcheck disable=SC1090
  source "${BT_TEMP_DIR}/scripts/sdk/gn/base/bin/fpave.sh.mock_state"
  gn-test-check-mock-args _ANY_ --prepare --image test-image --bucket test-bucket --work-dir "${FUCHSIA_WORK_DIR}"

}


# Test initialization. Note that we copy various tools/devshell files and need to replicate the
# behavior of generate.py by copying these files into scripts/sdk/gn/base/bin/devshell
# shellcheck disable=SC2034
BT_FILE_DEPS=(
  scripts/sdk/gn/base/bin/fconfig.sh
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
  FCONFIG_CMD="${BT_TEMP_DIR}/scripts/sdk/gn/base/bin/fconfig.sh"

  PATH_DIR_FOR_TEST="${BT_TEMP_DIR}/_isolated_path_for"
  export PATH="${PATH_DIR_FOR_TEST}:${PATH}"


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
