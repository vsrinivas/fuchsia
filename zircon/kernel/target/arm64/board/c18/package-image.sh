#!/usr/bin/env bash

# Copyright 2019 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

set -eo pipefail

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
ZIRCON_DIR="${DIR}/../../../../.."
PROJECT_ROOT="${ZIRCON_DIR}/.."

# arguments
BOARD=c18
ZIRCON_BUILD_DIR=
ROOT_BUILD_DIR=
BOOT_IMG=
ZIRCON_BOOTIMAGE=

function HELP {
    echo "help:"
    echo "-B <build-dir>  : path to zircon build directory"
    echo "-R <build-dir>  : path to root build directory"
    echo "-o              : output boot.img file (defaults to <build-dir>/<board>-boot.img)"
    echo "-z              : input zircon ZBI file (defaults to <build-dir>/<board>-boot.img)"
    exit 1
}

while getopts "B:o:z:" FLAG; do
    case $FLAG in
        B) ZIRCON_BUILD_DIR="${OPTARG}";;
        R) ROOT_BUILD_DIR="${OPTARG}";;
        o) BOOT_IMG="${OPTARG}";;
        z) ZIRCON_BOOTIMAGE="${OPTARG}";;
        \?)
            echo unrecognized option
            HELP
            ;;
    esac
done

if [[ -z "${ZIRCON_BUILD_DIR}" ]]; then
    echo must specify a Zircon build directory
    HELP
fi

if [[ -z "${ROOT_BUILD_DIR}" ]]; then
  ROOT_BUILD_DIR="${ZIRCON_BUILD_DIR%.zircon}"
  if [[ ! -d "${ROOT_BUILD_DIR}" ]]; then
    echo >&2 "Cannot find ROOT_BUILD_DIR (${ROOT_BUILD_DIR}), please use -R <build-dir>."
    exit 1
  fi
fi

# zircon image built by the Zircon build system
if [[ -z "${ZIRCON_BOOTIMAGE}" ]]; then
    ZIRCON_BOOTIMAGE="${ZIRCON_BUILD_DIR}/arm64.zbi"
fi

# Final packaged ChromeOS style boot image
if [[ -z "${BOOT_IMG}" ]]; then
    BOOT_IMG="${ZIRCON_BUILD_DIR}/${BOARD}-boot.img"
fi

case "$(uname -s)-$(uname -m)" in
  Darwin-x86_64)
    readonly HOST_PLATFORM="mac-x64"
    ;;
  Linux-x86_64)
    readonly HOST_PLATFORM="linux-x64"
    ;;
  Linux-aarch64)
    readonly HOST_PLATFORM="linux-arm64"
    ;;
  *)
    echo >&2 "Unknown operating system."
    exit 1
    ;;
esac

# internal use
FUTILITY="${PROJECT_ROOT}/prebuilt/tools/futility/${HOST_PLATFORM}/futility"
KEYBLOCK="${PROJECT_ROOT}/third_party/vboot_reference/tests/devkeys/kernel.keyblock"
PRIVATEKEY="${PROJECT_ROOT}/third_party/vboot_reference/tests/devkeys/kernel_data_key.vbprivk"
ITSSCRIPT_TEMPLATE="${DIR}/its_script"
DUMMY_DEVICE_TREE="${ZIRCON_DIR}/kernel/target/arm64/dtb/dummy-device-tree.dtb"
LZ4="${ROOT_BUILD_DIR}/host_x64/lz4"

# boot shim for our board
BOOT_SHIM="${ROOT_BUILD_DIR}/${BOARD}-boot-shim.bin"

# outputs
SHIMMED_ZIRCON_BOOTIMAGE="${ZIRCON_BOOTIMAGE}.shim"
COMPRESSED_BOOTIMAGE="${ZIRCON_BOOTIMAGE}.lz4"
FIT_BOOTIMAGE="${ZIRCON_BOOTIMAGE}.fit"

# shim and compress the ZBI
cat "${BOOT_SHIM}" "${ZIRCON_BOOTIMAGE}" > "${SHIMMED_ZIRCON_BOOTIMAGE}"
"${LZ4}" -q -B4 -f "${SHIMMED_ZIRCON_BOOTIMAGE}" "${COMPRESSED_BOOTIMAGE}"

# wrap everything up the way depthcharge expects it to be
mkimage -D "-q -I dts -O dtb"                                                \
        -p 2048                                                              \
        -f <(sed -e s@COMPRESSED_BOOTIMAGE@"${PWD}/${COMPRESSED_BOOTIMAGE}"@ \
                 -e s@DUMMY_DEVICE_TREE@"${DUMMY_DEVICE_TREE}"@              \
                 "${ITSSCRIPT_TEMPLATE}")                                    \
        "${FIT_BOOTIMAGE}" >/dev/null

# sign it
"${FUTILITY}" vbutil_kernel                      \
              --pack "${BOOT_IMG}"               \
              --keyblock "${KEYBLOCK}"           \
              --signprivate "${PRIVATEKEY}"      \
              --bootloader "${ZIRCON_BOOTIMAGE}" \
              --vmlinuz "${FIT_BOOTIMAGE}"       \
              --version 1                        \
              --arch arm
