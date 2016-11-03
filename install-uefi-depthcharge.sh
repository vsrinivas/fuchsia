#!/bin/bash

# Copyright 2015 Google Inc. All Rights Reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# This script produces a bootable USB drive, designed to boot using verified
# boot.

# Because this script does things like sgdisk, mount, etc. you must be root,
# so don't get the wrong command line parameters! <- super serious

set -e
shopt -s extglob

# Convenience function for ending the script with some output.
trap "exit 99" USR2
TOP_PID=$$

function die() {
  echo "$*" >& 2
  kill -USR2 $TOP_PID
}

# Function to attempt unmounting a mount point up to three times, sleeping
# a couple of seconds between attempts.
function umount_retry() {
  set +e
  TRIES=0
  while (! umount $1); do
    ((TRIES++))
    [[ ${TRIES} > 2 ]] && die "Unable to umount $0"
    sleep 2
  done
  set -e
}

function usage() {
  echo "$0 BLOCK_DEVICE [type]"
  echo "\"type\" is optional and can be \"dev\" or \"net\""
}

is_usb() {
  if [ -n "$(type -path udevadm)" ]; then
    udevadm info --query=all --name="${BLOCK_DEVICE}" | grep -q ID_BUS=usb
   else
    # For a usb device on the pixel2 we expect to see something like:
    # /sys/devices/pci0000:00/0000:00:14.0/usb2/2-2/2-2:1.0/host5/target5:0:0/5:0:0:0
    (cd /sys/block/$(basename "${BLOCK_DEVICE}")/device 2>/dev/null && pwd -P) | grep -q usb
  fi
}


# Absolute path of where this script is stored.
SCRIPT_DIR=$( cd $( dirname "${BASH_SOURCE[0]}" ) && pwd)
PATH="${SCRIPT_DIR}/../buildtools/toolchain/:${PATH}"
VB_DIR="${SCRIPT_DIR}/../third_party/vboot_reference"
DC_DIR="${SCRIPT_DIR}/../third_party/depthcharge"

# Grab some arguments, do some basic validation.
[[ $# -eq 2 ]] || [[ $# -eq 1 ]] || (usage; die)

[[ -b "$1" ]] || (usage; die "$1 is not a block device")
BLOCK_DEVICE=$1

# By default, the "normal" version of depthcharge is installed.
DC_TYPE="normal"
if [[ $# -eq 2 ]]; then
  case "$2" in
  "dev")
    DC_TYPE="dev"
    ;;
  "net")
    DC_TYPE="net"
    ;;
  *)
    usage; die
    ;;
  esac
fi


# Do a sanity check on the block device: help prevent users from accidentally
# destroying their workstations by checking whether this is a usb drive.
if ! is_usb; then
  die "${BLOCK_DEVICE} is not a usb drive"
fi

make -C "${VB_DIR}" cgpt futility

CGPT="${VB_DIR}/build/cgpt/cgpt"
FUTILITY="${VB_DIR}/build/futility"

PATH="${PATH}:${FUTILITY}" \
    BUILD_IMAGE_PATH="${VB_DIR}/tests/devkeys" \
    VB_SOURCE="${VB_DIR}" make -j8 -C "${DC_DIR}" uefi


# Figure out what partition on the device is the EFI system partition.

EFI_SYS_PART=`"${CGPT}" show -q /dev/sdb |
              grep 'EFI System Partition' |
              awk '{ print $3 }'`
EFI_SYS="${BLOCK_DEVICE}${EFI_SYS_PART}"


# Copy depthcharge to the EFI partition (and just let the trap unmount us).
MOUNT_POINT="$(mktemp -d)"
trap "umount_retry \"${MOUNT_POINT}\" && rm -rf \"${MOUNT_POINT}\"" INT TERM EXIT
mount "${EFI_SYS}" "${MOUNT_POINT}"
mkdir -p "${MOUNT_POINT}"/efi/boot
mkdir -p "${MOUNT_POINT}"/depthcharge

echo "Installing the \"${DC_TYPE}\" version of depthcharge..."
case "${DC_TYPE}" in
"normal")
  cp "${DC_DIR}"/build/uefi/image/uefi.efi \
      "${MOUNT_POINT}"/efi/boot/bootx64.efi
  cp "${DC_DIR}"/build/uefi/image/uefi.rwa.bin \
      "${MOUNT_POINT}"/depthcharge/rwa
  cp "${DC_DIR}"/build/uefi/image/uefi.rwb.bin \
      "${MOUNT_POINT}"/depthcharge/rwb
  ;;
"dev")
  cp "${DC_DIR}"/build/uefi/image/uefi_dev.efi \
      "${MOUNT_POINT}"/efi/boot/bootx64.efi
  cp "${DC_DIR}"/build/uefi/image/uefi_dev.rwa.bin \
      "${MOUNT_POINT}"/depthcharge/rwa
  cp "${DC_DIR}"/build/uefi/image/uefi_dev.rwb.bin \
      "${MOUNT_POINT}"/depthcharge/rwb
  ;;
"net")
  cp "${DC_DIR}"/build/uefi/image/uefi_net.efi \
      "${MOUNT_POINT}"/efi/boot/bootx64.efi
  touch "${MOUNT_POINT}"/depthcharge/netboot_params
  ;;
*)
  die "Unrecognized image type."
  ;;
esac
