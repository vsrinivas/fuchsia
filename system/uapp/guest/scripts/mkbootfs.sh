#!/usr/bin/env bash

# Copyright 2017 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

usage() {
    echo "usage: ${0} target [options]"
    echo ""
    echo "    -f user.bootfs            Fuchsia bootfs"
    echo "    -z zircon.bin             Zircon kernel"
    echo "    -b bootdata.bin           Zircon bootfs"
    echo "    -g zircon.gpt             Zircon GPT disk image"
    echo "    -l bzImage                Linux kernel"
    echo "    -i initrd                 Linux initrd"
    echo "    -r rootfs.ext2            Linux EXT2 root filesystem image"
    echo ""
    exit 1
}

if [[ "$1" != zircon-* ]]; then
    usage
fi

GUEST_SCRIPTS_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
ZIRCON_DIR="$GUEST_SCRIPTS_DIR/../../../.."
BUILD_DIR="$ZIRCON_DIR/build-$1"
shift

declare ZIRCON="$BUILD_DIR/zircon.bin"
declare BOOTDATA="$BUILD_DIR/bootdata.bin"
declare ZIRCON_DISK="$BUILD_DIR/zircon.gpt"
declare BZIMAGE="/tmp/linux/arch/x86/boot/bzImage"
declare INITRD="/tmp/toybox/initrd.gz"
declare ROOTFS="/tmp/toybox/rootfs.ext2"
declare HOST_BOOTFS="$BUILD_DIR/bootdata.bin"

while getopts "f:z:b:l:i:r:g:" opt; do
  case "${opt}" in
    f) HOST_BOOTFS="${OPTARG}" ;;
    z) ZIRCON="${OPTARG}" ;;
    b) BOOTDATA="${OPTARG}" ;;
    g) ZIRCON_DISK="${OPTARG}" ;;
    l) BZIMAGE="${OPTARG}" ;;
    i) INITRD="${OPTARG}" ;;
    r) ROOTFS="${OPTARG}" ;;
    *) usage ;;
  esac
done

readonly ZIRCON BOOTDATA ZIRCON_DISK BZIMAGE INITRD ROOTFS HOST_BOOTFS

echo "
data/dsdt.aml=$ZIRCON_DIR/system/ulib/hypervisor/acpi/dsdt.aml
data/madt.aml=$ZIRCON_DIR/system/ulib/hypervisor/acpi/madt.aml
data/mcfg.aml=$ZIRCON_DIR/system/ulib/hypervisor/acpi/mcfg.aml
data/zircon.bin=$ZIRCON
data/bootdata.bin=$BOOTDATA" > /tmp/guest.manifest

if [ -f "$ZIRCON_DISK" ]; then
    echo "data/zircon.gpt=$ZIRCON_DISK" >> /tmp/guest.manifest
fi

if [ -f "$BZIMAGE" ]; then
    echo "data/bzImage=$BZIMAGE" >> /tmp/guest.manifest
fi

if [ -f "$INITRD" ]; then
    echo "data/initrd=$INITRD" >> /tmp/guest.manifest
fi

if [ -f "$ROOTFS" ]; then
    echo "data/rootfs.ext2=$ROOTFS" >> /tmp/guest.manifest
fi

$BUILD_DIR/tools/mkbootfs \
    --target=boot \
    -o $BUILD_DIR/bootdata-with-guest.bin \
    "${HOST_BOOTFS}" \
    /tmp/guest.manifest
