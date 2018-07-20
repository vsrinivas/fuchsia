#!/usr/bin/env bash

# Copyright 2018 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

DEBIAN_GUEST_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

pushd "${DEBIAN_GUEST_DIR}"

usage() {
  echo "usage: ${0} [OPTIONS] {arm64, x64}"
  echo
  echo "OPTIONS:"
  echo "  -d DEVICE     Build the image on the given block device."
  echo
  exit 1
}

check_dep() {
  local bin="${1}"
  local package="${2:-${bin}}"
  type -P "${bin}" &>/dev/null && return 0

  echo "Required package ${package} is not installed. (sudo apt install ${package})"
  exit 1
}

check_dep multistrap
check_dep qemu-img qemu-utils

while getopts "d:" FLAG; do
  case "${FLAG}" in
  d) DEVICE="${OPTARG}";;
  *) usage;;
  esac
done
shift $((OPTIND - 1))

case "${1}" in
arm64)
  MULTISTRAP_CONFIG=multistrap/arm64.cfg;
  ARCH=arm64;

  # We use qemu user mode emulation to configure the guest system for
  # foreign architectures.
  check_dep qemu-aarch64-static qemu-user-static;
  HOST_QEMU_BIN=qemu-aarch64-static;;
x64)
  MULTISTRAP_CONFIG=multistrap/x86.cfg;
  ARCH="x86";;
*)
  usage;;
esac

OUTDIR=./debian-$ARCH
mkdir -p $OUTDIR

RAW_IMAGE_PATH=$OUTDIR/rootfs.img
QCOW_IMAGE_PATH=$OUTDIR/rootfs.qcow2
KERNEL_PATH=$OUTDIR/vmlinuz
INITRD_PATH=$OUTDIR/initrd.img
MOUNTPOINT=`mktemp -d`

if [[ -z "${DEVICE}" ]]; then
  truncate -s 2G "${RAW_IMAGE_PATH}"
  mkfs.ext4 -F "${RAW_IMAGE_PATH}"
  sudo -v -p "[sudo] Enter password to mount image file"
  sudo mount -o loop "${RAW_IMAGE_PATH}" "${MOUNTPOINT}"
else
  sudo -v -p "[sudo] Enter password to format device ${DEVICE}"
  sudo mkfs.ext4 -F "${DEVICE}"
  sudo mount "${DEVICE}" "${MOUNTPOINT}"
fi

sudo chown "${USER}" "${MOUNTPOINT}"
sudo multistrap -f "${MULTISTRAP_CONFIG}" -d "${MOUNTPOINT}"

if [[ -n "${HOST_QEMU_BIN}" ]]; then
  HOST_QEMU_PATH=`which "${HOST_QEMU_BIN}"`
  TARGET_QEMU_PATH="${MOUNTPOINT}/usr/bin"
  sudo cp "${HOST_QEMU_PATH}" "${TARGET_QEMU_PATH}"
fi

sudo -v -p "[sudo] Enter password to chroot into guest system"
sudo mount --bind /dev "${MOUNTPOINT}/dev"
sudo mount proc -t proc "${MOUNTPOINT}/proc"
sudo chroot "${MOUNTPOINT}" /configscript.sh

TARGET_KERNEL_PATH="${MOUNTPOINT}/vmlinuz"
if [[ -e "${TARGET_KERNEL_PATH}" ]]; then
  cp "${TARGET_KERNEL_PATH}" "${KERNEL_PATH}"
else
  echo "WARNING: Unable to locate kernel image"
fi

TARGET_INITRD_PATH="${MOUNTPOINT}/initrd.img"
if [[ -e "${TARGET_INITRD_PATH}" ]]; then
  cp "${TARGET_INITRD_PATH}" "${INITRD_PATH}"
else
  echo "WARNING: Unable to locate initrd image"
fi

# Remove any deleted file data. This allows the sparse image files to be more
# efficiently compressed.
sudo fstrim -v "${MOUNTPOINT}"

sudo -v -p "[sudo] Enter password to unmount filesystems"
sudo umount "${MOUNTPOINT}/proc"
sudo umount "${MOUNTPOINT}/dev"
sudo umount "${MOUNTPOINT}/etc/machine-id"
sudo umount "${MOUNTPOINT}"

if [[ -z "${DEVICE}" ]]; then
  qemu-img convert -f raw -O qcow2 "${RAW_IMAGE_PATH}" "${QCOW_IMAGE_PATH}"
fi
