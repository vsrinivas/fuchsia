#!/usr/bin/env bash

# Copyright 2017 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

GUEST_SCRIPTS_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
MAGENTADIR="${MAGENTA_DIR:-${GUEST_SCRIPTS_DIR}/../../../..}"
BUILDDIR="${MAGENTA_BUILD_DIR:-$MAGENTADIR/build-magenta-pc-x86-64}"

usage() {
    echo "usage: ${0} [options]"
    echo ""
    echo "    -k kernel.bin             Magenta kernel."
    echo "    -b bootdata.bin           Magenta bootdata."
    echo "    -l bzImage                Linux kernel."
    echo "    -i initrd                 Linux initrd."
    echo "    -r rootfs.ext2            Linux EXT2 root filesystem image."
    echo ""
    exit 1
}

declare KERNEL="$BUILDDIR/magenta.bin"
declare BOOTDATA="$BUILDDIR/bootdata.bin"
declare BZIMAGE="/tmp/linux/arch/x86/boot/bzImage"
declare INITRD="$BUILDDIR/toybox-x86/initrd.gz"
declare ROOTFS="$BUILDDIR/toybox-x86/rootfs.ext2"

while getopts "k:b:l:i:r:" opt; do
  case "${opt}" in
    k) KERNEL="${OPTARG}" ;;
    b) BOOTDATA="${OPTARG}" ;;
    l) BZIMAGE="${OPTARG}" ;;
    i) INITRD="${OPTARG}" ;;
    r) ROOTFS="${OPTARG}" ;;
    *) usage ;;
  esac
done

readonly KERNEL BOOTDATA BZIMAGE INITRD ROOTFS

echo "
data/dsdt.aml=$MAGENTADIR/system/ulib/hypervisor/acpi/dsdt.aml
data/madt.aml=$MAGENTADIR/system/ulib/hypervisor/acpi/madt.aml
data/mcfg.aml=$MAGENTADIR/system/ulib/hypervisor/acpi/mcfg.aml
data/kernel.bin=$KERNEL
data/bootdata.bin=$BOOTDATA" > /tmp/guest.manifest

if [ -f "$BZIMAGE" ]; then
    echo "data/bzImage=$BZIMAGE" >> /tmp/guest.manifest
fi

if [ -f "$INITRD" ]; then
    echo "data/initrd=$INITRD" >> /tmp/guest.manifest
fi

if [ -f "$ROOTFS" ]; then
    echo "data/rootfs.ext2=$ROOTFS" >> /tmp/guest.manifest
fi

$BUILDDIR/tools/mkbootfs \
    --target=boot \
    -o $BUILDDIR/bootdata-with-kernel.bin \
    $BUILDDIR/bootdata.bin \
    /tmp/guest.manifest
