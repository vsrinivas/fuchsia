#!/usr/bin/env bash

# Copyright 2017 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

GUEST_SCRIPTS_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
MAGENTADIR="${MAGENTA_DIR:-${GUEST_SCRIPTS_DIR}/../../../..}"
FUCHSIADIR="${FUCHSIA_DIR:-${MAGENTADIR}/..}"
BUILDDIR="${MAGENTA_BUILD_DIR:-$MAGENTADIR/build-magenta-pc-x86-64}"

usage() {
    echo "usage: ${0} [options]"
    echo ""
    echo "    -f                        Build a Fuchsia host image."
    echo "    -m magenta.bin            Magenta kernel."
    echo "    -b bootdata.bin           Magenta bootdata."
    echo "    -g magenta.gpt            Magenta GPT disk image."
    echo "    -l bzImage                Linux kernel."
    echo "    -i initrd                 Linux initrd."
    echo "    -r rootfs.ext2            Linux EXT2 root filesystem image."
    echo ""
    exit 1
}

declare MAGENTA="$BUILDDIR/magenta.bin"
declare BOOTDATA="$BUILDDIR/bootdata.bin"
declare MAGENTA_DISK="$BUILDDIR/magenta.gpt"
declare BZIMAGE="/tmp/linux/arch/x86/boot/bzImage"
declare INITRD="/tmp/toybox/initrd.gz"
declare ROOTFS="/tmp/toybox/rootfs.ext2"
declare HOST_BOOTFS="$BUILDDIR/bootdata.bin"

while getopts "m:b:l:i:r:g:f" opt; do
  case "${opt}" in
    m) MAGENTA="${OPTARG}" ;;
    b) BOOTDATA="${OPTARG}" ;;
    g) MAGENTA_DISK="${OPTARG}" ;;
    l) BZIMAGE="${OPTARG}" ;;
    i) INITRD="${OPTARG}" ;;
    r) ROOTFS="${OPTARG}" ;;
    f) HOST_BOOTFS="${FUCHSIA_BUILD_DIR}/user.bootfs" ;;
    *) usage ;;
  esac
done

readonly MAGENTA BOOTDATA MAGENTA_DISK BZIMAGE INITRD ROOTFS HOST_BOOTFS

echo "
data/dsdt.aml=$MAGENTADIR/system/ulib/hypervisor/acpi/dsdt.aml
data/madt.aml=$MAGENTADIR/system/ulib/hypervisor/acpi/madt.aml
data/mcfg.aml=$MAGENTADIR/system/ulib/hypervisor/acpi/mcfg.aml
data/magenta.bin=$MAGENTA
data/bootdata.bin=$BOOTDATA" > /tmp/guest.manifest

if [ -f "$MAGENTA_DISK" ]; then
    echo "data/magenta.gpt=$MAGENTA_DISK" >> /tmp/guest.manifest
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

$BUILDDIR/tools/mkbootfs \
    --target=boot \
    -o $BUILDDIR/bootdata-with-guest.bin \
    "${HOST_BOOTFS}" \
    /tmp/guest.manifest
