#!/usr/bin/env bash

# Copyright 2018 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

set -eo pipefail

SCRIPTS_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
ZIRCON_DIR="${SCRIPTS_DIR}/.."

BOARD=
ZIRCON_BUILD_DIR=
ROOT_BUILD_DIR=
CMDLINE=
MKBOOTIMG_CMDLINE=
MEXEC=
DTB_PATH=
DTB_DEST=
USE_GZIP=
USE_LZ4=
KERNEL_OFFSET=0x00080000
MEMBASE=0x00000000
BOOT_IMG=
VBMETA_IMG=
ZIRCON_BOOTIMAGE=
MKBOOTIMG_ARGS=
RAMDISK_TYPE="dummy"

function HELP {
    echo "help:"
    echo "-b <board>                        : board name"
    echo "-B <build-dir>                    : path to zircon build directory"
    echo "-R <build-dir>                    : path to Fuchsia build directory"
    echo "-c <cmd line>                     : Extra command line options for the ZBI"
    echo "-C <cmd line>                     : Extra command line options for mkbootimg"
    echo "-d <dtb-path>                     : path to device tree binary"
    echo "-D (append | kdtb | mkbootimg)    : destination for device tree binary"
    echo "-g                                : gzip compress the image"
    echo "-l                                : lz4 compress the image"
    echo "-h                                : print help"
    echo "-K                                : kernel offset for mkbootimg (default ${KERNEL_OFFSET})"
    echo "-m                                : Add mexec option to command line"
    echo "-M                                : membase for mkbootimg (default ${MEMBASE})"
    echo "-o                                : output boot.img file (defaults to <build-dir>/<board>-boot.img)"
    echo "-r (none | dummy | zbi)           : choose the type of ramdisk to add to the image, defaults to dummy"
    echo "-v                                : output vbmeta.img file (defaults to <build-dir>/<board>-vbmeta.img)"
    echo "-z                                : input zircon ZBI file (defaults to <build-dir>/<board>-boot.img)"
    exit 1
}

# These values seem to work with all our boards, so we haven't parameterized them.
DTB_OFFSET=0x03000000
BOOT_PARTITION_SIZE=33554432

while getopts "ab:B:c:C::d:D:ghK:lmM:o:r:v:z:" FLAG; do
    case $FLAG in
        b) BOARD="${OPTARG}";;
        B) ZIRCON_BUILD_DIR="${OPTARG}";;
        R) ROOT_BUILD_DIR="${OPTARG}";;
        c) CMDLINE+="${OPTARG}";;
        C) MKBOOTIMG_CMDLINE+="${OPTARG}";;
        d) DTB_PATH="${OPTARG}";;
        D) DTB_DEST="${OPTARG}";;
        g) USE_GZIP=true;;
        K) KERNEL_OFFSET="${OPTARG}";;
        l) USE_LZ4=true;;
        h) HELP;;
        m) MEXEC=true;;
        M) MEMBASE="${OPTARG}";;
        o) BOOT_IMG="${OPTARG}";;
        r) RAMDISK_TYPE="${OPTARG}";;
        v) VBMETA_IMG="${OPTARG}";;
        z) ZIRCON_BOOTIMAGE="${OPTARG}";;
        \?)
            echo unrecognized option
            HELP
            ;;
    esac
done
shift $((OPTIND-1))

if [[ -z "${BOARD}" ]]; then
    echo must specify a board to flash
    HELP
fi

if [[ -z "${ZIRCON_BUILD_DIR}" ]]; then
    echo must specify a Zircon build directory
    HELP
fi

if [[ -z "${ROOT_BUILD_DIR}" ]]; then
    ROOT_BUILD_DIR="${ZIRCON_BUILD_DIR%%.zircon}"
fi

if [[ -n "${DTB_PATH}" ]] &&
   [[ "${DTB_DEST}" != "append" ]] &&
   [[ "${DTB_DEST}" != "kdtb" ]] &&
   [[ "${DTB_DEST}" != "mkbootimg" ]]; then
    echo Invalid dtb destination ${DTB_DEST}
    HELP
fi

if [[ "${RAMDISK_TYPE}" != "none" ]] &&
   [[ "${RAMDISK_TYPE}" != "dummy" ]] &&
   [[ "${RAMDISK_TYPE}" != "zbi" ]]; then
    echo invalid ramdisk type ${RAMDISK_TYPE}
    HELP
fi

# Some tools we use
LZ4="${ZIRCON_BUILD_DIR}/tools/lz4"
MKBOOTIMG="${ZIRCON_DIR}/third_party/tools/android/mkbootimg"
ZBI="${ZIRCON_BUILD_DIR}/tools/zbi"

# zircon image built by the Zircon build system
if [[ -z "${ZIRCON_BOOTIMAGE}" ]]; then
    ZIRCON_BOOTIMAGE="${ZIRCON_BUILD_DIR}/arm64.zbi"
fi

# boot shim for our board
BOOT_SHIM="${ZIRCON_BUILD_DIR}/${BOARD}-boot-shim.bin"

# zircon ZBI image with prepended boot shim
SHIMMED_ZIRCON_BOOTIMAGE="${ZIRCON_BOOTIMAGE}.shim"

# Final packaged Android style boot.img
if [[ -z "${BOOT_IMG}" ]]; then
    BOOT_IMG="${ZIRCON_BUILD_DIR}/${BOARD}-boot.img"
fi

# PACKAGING STEPS BEGIN HERE

if [[ ${MEXEC} == true ]]; then
    CMDLINE+=" netsvc.netboot=true"
    MKBOOTIMG_CMDLINE+=" netsvc.netboot=true"
fi

# Append extra command line items
if [[ -n "${CMDLINE}" ]]; then
    CMDLINE_FILE="${ZIRCON_BOOTIMAGE}-cmdline.txt"
    CMDLINE_BOOTIMAGE="${ZIRCON_BOOTIMAGE}.cmdline"

    echo ${CMDLINE} > "${CMDLINE_FILE}"
    "${ZBI}" -o "${CMDLINE_BOOTIMAGE}" "${ZIRCON_BOOTIMAGE}" -T cmdline "${CMDLINE_FILE}"
else
    CMDLINE_BOOTIMAGE="${ZIRCON_BOOTIMAGE}"
fi

# Prepend boot shim
cat "${BOOT_SHIM}" "${CMDLINE_BOOTIMAGE}" > "${SHIMMED_ZIRCON_BOOTIMAGE}"

# Optionally compress the shimmed image
if [[ ${USE_GZIP} == true ]]; then
    COMPRESSED_BOOTIMAGE="${ZIRCON_BOOTIMAGE}.gz"
    gzip -c "${SHIMMED_ZIRCON_BOOTIMAGE}" > "${COMPRESSED_BOOTIMAGE}"
elif [[ ${USE_LZ4} == true ]]; then
    COMPRESSED_BOOTIMAGE="${ZIRCON_BOOTIMAGE}.lz4"
    "${LZ4}" -B4 -c "${SHIMMED_ZIRCON_BOOTIMAGE}" > "${COMPRESSED_BOOTIMAGE}"
else
    COMPRESSED_BOOTIMAGE="${SHIMMED_ZIRCON_BOOTIMAGE}"
fi

# Handle options for packaging dtb
if [[ -n "${DTB_PATH}" ]] && [[ "${DTB_DEST}" == "append" ]]; then
    COMPRESSED_BOOTIMAGE_DTB="${COMPRESSED_BOOTIMAGE}.dtb"
    cat "${COMPRESSED_BOOTIMAGE}" "${DTB_PATH}" > "${COMPRESSED_BOOTIMAGE_DTB}"
elif [[ -n "${DTB_PATH}" ]] && [[ "${DTB_DEST}" == "mkbootimg" ]]; then
    COMPRESSED_BOOTIMAGE_DTB="${COMPRESSED_BOOTIMAGE}"
    MKBOOTIMG_ARGS+=" --second ${DTB_PATH} --second_offset ${DTB_OFFSET}"
else
    COMPRESSED_BOOTIMAGE_DTB="${COMPRESSED_BOOTIMAGE}"
fi

# some bootloaders insist on having a ramdisk
if [[ "${RAMDISK_TYPE}" == "zbi" ]]; then
    RAMDISK_OPTION="--ramdisk ${ZIRCON_BOOTIMAGE}"
elif [[ "${RAMDISK_TYPE}" == "none" ]]; then
    RAMDISK_OPTION=""
else
    RAMDISK="${ZIRCON_BUILD_DIR}/dummy-ramdisk"
    echo "foo" > "${RAMDISK}"
    RAMDISK_OPTION="--ramdisk ${RAMDISK}"
fi

# create our boot.img
"${MKBOOTIMG}" \
    --kernel "${COMPRESSED_BOOTIMAGE_DTB}" \
    --kernel_offset ${KERNEL_OFFSET} \
    ${RAMDISK_OPTION} \
    --base ${MEMBASE} \
    --tags_offset 0xE000000 \
    --cmdline "${MKBOOTIMG_CMDLINE}" \
    ${MKBOOTIMG_ARGS} \
    -o "${BOOT_IMG}"
