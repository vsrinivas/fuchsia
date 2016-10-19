#!/usr/bin/env bash
# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -eu

get_confirmation() {
  echo -n "Press 'y' to confirm: "
  read CONFIRM
  if [[ $CONFIRM != "y" ]]; then
    echo "[format_usb] Aborted due to invalid confirmation"
    exit 1
  fi
}

if [[ $OSTYPE != "linux-gnu" ]]; then
  echo "[format_usb] Script is currently Linux-exclusive"
  exit 1
fi

SCRIPT_DIR=$( cd $( dirname "${BASH_SOURCE[0]}" ) && pwd)
FUCHSIA_DIR=$SCRIPT_DIR/..

# Ensure the bootloader and magenta have been built prior to formatting USB
pushd $FUCHSIA_DIR/magenta/bootloader > /dev/null
make
popd > /dev/null
pushd $FUCHSIA_DIR/magenta > /dev/null
./scripts/build-magenta-x86-64
popd > /dev/null

lsblk
echo "Enter the name of a block device to format: "
echo "     This will probably be of the form 'sd[letter]', like 'sdc'"
echo -n ">  "
read DEVICE

# Ensure that device exists
echo -n "[format_usb] Checking that device exists: $DEVICE ..."
DEVICE_PATH="/dev/$DEVICE"
if [[ ! -e "$DEVICE_PATH" ]]; then
  echo " FAILED"
  echo "[format_usb] ERROR: This device does not exist: $DEVICE_PATH"
  exit 1
fi
echo " SUCCESS"

# Ensure that the device is a real block device
echo -n "[format_usb] Checking that device is a known block device..."
if [[ ! -e "/sys/block/$DEVICE" ]]; then
  echo " FAILED"
  echo "[format_usb] ERROR: /sys/block/$DEVICE does not exist."
  echo "            Does $DEVICE refer to a partition?"
  exit 1
fi
echo " SUCCESS"

# Try to check that the device is a USB stick
echo -n "[format_usb] Checking if device is USB: $DEVICE ..."
READLINK_USB=$(readlink -f /sys/class/block/$DEVICE/device | { grep -i "usb" || true; })
if [[ -z $READLINK_USB ]]; then
  echo " FAILED"
  echo "[format_usb] ERROR: Cannot confirm that device is a USB stick"
  echo "[format_usb] ERROR: Please insert USB stick and retry"
  exit 1
fi
echo " SUCCESS"

# Ensure the device is not mounted
echo -n "[format_usb] Checking that device is not mounted: $DEVICE ..."
if [[ -n $(df -Hl | grep "$DEVICE") ]]; then
  echo " FAILED"
  echo "[format_usb] ERROR: Your device appears to be mounted: "
  echo "..."
  df -Hl | grep "$DEVICE"
  echo "..."
  echo "[format_usb] ERROR: Please unmount your device and retry"
  exit 1
fi
echo " SUCCESS"

# Confirm that the user knows what they are doing
echo "[format_usb] (sudo) Getting info about: $DEVICE_PATH"
sudo sgdisk -p $DEVICE_PATH
echo "[format_usb] ABOUT TO COMPLETELY WIPE / FORMAT: $DEVICE_PATH"
get_confirmation
echo "[format_usb] ARE YOU 100% SURE?"
get_confirmation

echo "[format_usb] Deleting all partition info on USB, creating new GPT"
sudo sgdisk -og $DEVICE_PATH

SECTOR_SIZE=`cat /sys/block/$DEVICE/queue/hw_sector_size`
echo "[format_usb] Creating 1GB EFI System Partition"
sudo sgdisk -n 1:0:1GB -c 1:"EFI System Partition" -t 1:ef00 $DEVICE_PATH
EFI_PARTITION_PATH=${DEVICE_PATH}1
sudo mkfs.vfat $EFI_PARTITION_PATH
echo "[format_usb] Creating FAT data partition"
sudo sgdisk -n 2:0:0 -c 2:"FAT Partition" -t2:0700 $DEVICE_PATH
DATA_PARTITION_PATH=${DEVICE_PATH}2
sudo mkfs.vfat $DATA_PARTITION_PATH

MOUNT_PATH=`mktemp -d`
sudo mount $EFI_PARTITION_PATH $MOUNT_PATH
sudo mkdir -p ${MOUNT_PATH}/EFI/BOOT
echo -n "Copying Bootloader..."
sudo cp $FUCHSIA_DIR/magenta/bootloader/out/osboot.efi ${MOUNT_PATH}/EFI/BOOT/BOOTX64.EFI
echo " SUCCESS"
echo -n "Copying magenta.bin..."
sudo cp $FUCHSIA_DIR/magenta/build-magenta-pc-x86-64/magenta.bin ${MOUNT_PATH}/magenta.bin
echo " SUCCESS"
echo -n "Copying user.bootfs..."
sudo cp $FUCHSIA_DIR/out/debug-x86-64/user.bootfs ${MOUNT_PATH}/ramdisk.bin
echo " SUCCESS"
echo -n "Syncing / Unmounting EFI partition (this may take a minute)..."
pushd $MOUNT_PATH > /dev/null
sync
popd > /dev/null
sudo umount -l $MOUNT_PATH
echo " SUCCESS"
rmdir $MOUNT_PATH

echo "USB Formatted Successfully. Ready to boot!"
