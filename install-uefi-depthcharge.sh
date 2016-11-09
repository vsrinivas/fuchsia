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

SCRIPT_DIR=$( cd $( dirname "${BASH_SOURCE[0]}" ) && pwd)
source "${SCRIPT_DIR}"/build-utils.sh

function usage() {
  echo "$0 BLOCK_DEVICE [type]"
  echo "\"type\" is optional and can be \"dev\" or \"net\""
}

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
if ! is_usb "${BLOCK_DEVICE}"; then
  die "${BLOCK_DEVICE} is not a usb drive"
fi

make -C "${VB_DIR}" cgpt futility

CGPT="${VB_DIR}/build/cgpt/cgpt"
FUTILITY="${VB_DIR}/build/futility"

PATH="${PATH}:${FUTILITY}" \
    BUILD_IMAGE_PATH="${VB_DIR}/tests/devkeys" \
    VB_SOURCE="${VB_DIR}" make -j8 -C "${DC_DIR}" uefi


# Figure out what partition on the device is the EFI system partition.

EFI_SYS_PART=`sudo "${CGPT}" show -q /dev/sdb |
              grep 'EFI System Partition' |
              awk '{ print $3 }'`
EFI_SYS="${BLOCK_DEVICE}${EFI_SYS_PART}"


# Copy depthcharge to the EFI partition (and just let the trap unmount us).
MOUNT_POINT="$(mktemp -d)"
trap "umount_retry \"${MOUNT_POINT}\" && rm -rf \"${MOUNT_POINT}\"" INT TERM EXIT
sudo mount "${EFI_SYS}" "${MOUNT_POINT}"
sudo mkdir -p "${MOUNT_POINT}"/efi/boot
sudo mkdir -p "${MOUNT_POINT}"/depthcharge

echo "Installing the \"${DC_TYPE}\" version of depthcharge..."
case "${DC_TYPE}" in
"normal")
  sudo cp "${DC_DIR}"/build/uefi/image/uefi.efi \
      "${MOUNT_POINT}"/efi/boot/bootx64.efi
  sudo cp "${DC_DIR}"/build/uefi/image/uefi.rwa.bin \
      "${MOUNT_POINT}"/depthcharge/rwa
  sudo cp "${DC_DIR}"/build/uefi/image/uefi.rwb.bin \
      "${MOUNT_POINT}"/depthcharge/rwb
  ;;
"dev")
  sudo cp "${DC_DIR}"/build/uefi/image/uefi_dev.efi \
      "${MOUNT_POINT}"/efi/boot/bootx64.efi
  sudo cp "${DC_DIR}"/build/uefi/image/uefi_dev.rwa.bin \
      "${MOUNT_POINT}"/depthcharge/rwa
  sudo cp "${DC_DIR}"/build/uefi/image/uefi_dev.rwb.bin \
      "${MOUNT_POINT}"/depthcharge/rwb
  ;;
"net")
  sudo cp "${DC_DIR}"/build/uefi/image/uefi_net.efi \
      "${MOUNT_POINT}"/efi/boot/bootx64.efi
  sudo touch "${MOUNT_POINT}"/depthcharge/netboot_params
  ;;
*)
  die "Unrecognized image type."
  ;;
esac
