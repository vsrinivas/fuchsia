#!/usr/bin/env bash

# Copyright 2017 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

set -e

usage() {
    echo "usage: ${0} build-dir [options]"
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

GUEST_SCRIPTS_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
GARNET_DIR="$GUEST_SCRIPTS_DIR/../../.."

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
shift $((OPTIND-1))

if [ ! -d "$1" ]; then
    echo "Build directory '$1' does not exit."
    usage
fi

declare -r HOST_BOOTFS=${HOST_BOOTFS:-$1/bootdata.bin}
declare -r ZIRCON=${ZIRCON:-$1/zircon.bin}
declare -r BOOTDATA=${BOOTDATA:-$1/bootdata.bin}
declare -r ZIRCON_DISK=${ZIRCON_DISK:-$1/zircon.gpt}
declare -r BZIMAGE=${BZIMAGE:-/tmp/linux/arch/x86/boot/bzImage}
declare -r INITRD=${INITRD:-/tmp/toybox/initrd.gz}
declare -r ROOTFS=${ROOTFS:-/tmp/toybox/rootfs.ext2}

echo "
data/dsdt.aml=$GARNET_DIR/lib/machina/arch/x86/acpi/dsdt.aml
data/madt.aml=$GARNET_DIR/lib/machina/arch/x86/acpi/madt.aml
data/mcfg.aml=$GARNET_DIR/lib/machina/arch/x86/acpi/mcfg.aml
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

$1/tools/mkbootfs \
    --target=boot \
    -o bootdata-with-guest.bin \
    "${HOST_BOOTFS}" \
    /tmp/guest.manifest
