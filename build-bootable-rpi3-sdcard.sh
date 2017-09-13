#!/usr/bin/env bash
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -eu

get_confirmation() {
  echo -n "Press 'y' to confirm: "
  read CONFIRM
  if [[ "$CONFIRM" != "y" ]]; then
    echo "[format_sdcard] Aborted due to invalid confirmation"
    exit 1
  fi
}

if [[ $OSTYPE != "linux-gnu" ]]; then
  echo "[format_sdcard] Script is currently Linux-exclusive"
  exit 1
fi

SCRIPT_DIR=$( cd $( dirname "${BASH_SOURCE[0]}" ) && pwd)
FUCHSIA_DIR="$SCRIPT_DIR/.."
MINFS="$FUCHSIA_DIR/out/build-zircon/tools/minfs"
MANIFEST_BUILDER="$SCRIPT_DIR/installer/manifest-builder.py"

echo $FUCHSIA_DIR


# Ensure Zircon has been built prior to formatting USB
pushd "$FUCHSIA_DIR/zircon" > /dev/null
./scripts/make-parallel zircon-rpi3-arm64
popd > /dev/null

if [[ ! -e "$MINFS" ]]; then
  echo "minfs not found, please build fuchsia and try again."
  exit 1
fi

lsblk
echo "Enter the name of a block device to format: "
echo "     This will probably be of the form 'sd[letter]', like 'sdc'"
echo -n ">  "
read DEVICE

# Ensure that device exists
echo -n "[format_sdcard] Checking that device exists: $DEVICE ..."
DEVICE_PATH="/dev/$DEVICE"
if [[ ! -e "$DEVICE_PATH" ]]; then
  echo " FAILED"
  echo "[format_sdcard] ERROR: This device does not exist: $DEVICE_PATH"
  exit 1
fi
echo " SUCCESS"

# Ensure that the device is a real block device
echo -n "[format_sdcard] Checking that device is a known block device..."
if [[ ! -e "/sys/block/$DEVICE" ]]; then
  echo " FAILED"
  echo "[format_sdcard] ERROR: /sys/block/$DEVICE does not exist."
  echo "            Does $DEVICE refer to a partition?"
  exit 1
fi
echo " SUCCESS"

# Try to check that the device is a USB stick
echo -n "[format_sdcard] Checking if device is USB: $DEVICE ..."
READLINK_USB=$(readlink -f "/sys/class/block/$DEVICE/device" | { grep -i "usb" || true; })
if [[ -z "$READLINK_USB" ]]; then
  echo " FAILED"
  echo "[format_sdcard] ERROR: Cannot confirm that device is a USB stick"
  echo "[format_sdcard] ERROR: Please insert USB stick and retry"
  exit 1
fi
echo " SUCCESS"

# Ensure the device is not mounted
echo -n "[format_sdcard] Checking that device is not mounted: $DEVICE ..."
if [[ -n $(df -Hl | grep "$DEVICE") ]]; then
  echo " FAILED"
  echo "[format_sdcard] ERROR: Your device appears to be mounted: "
  echo "..."
  df -Hl | grep "$DEVICE"
  echo "..."
  echo "[format_sdcard] ERROR: Please unmount your device and retry"
  exit 1
fi
echo " SUCCESS"

# Confirm that the user knows what they are doing
sudo -v -p "[sudo] Enter password to confirm information about device: "
sudo sfdisk -l "$DEVICE_PATH"
echo "[format_sdcard] ABOUT TO COMPLETELY WIPE / FORMAT: $DEVICE_PATH"
get_confirmation
echo "[format_sdcard] ARE YOU 100% SURE?"
get_confirmation

# Create three new partitions on the device, 1GB for root and divide the
# remaining space into 2 pieces for data and bootfs

# Determine how many bytes we have available on this disk.
BLK_DEV_SIZE=`sudo blockdev --getsize64 $DEVICE_PATH`
BLK_DEV_SIZE_MB=$((BLK_DEV_SIZE/1048576))

if [ "$BLK_DEV_SIZE_MB" -lt 8000 ]; then
    echo " FAILED"
    echo "[format_sdcard] ERROR: SD Card must be at least 8GB"
    exit 1
fi

# Stomp the existing MBR if one exists.
sudo dd if=/dev/zero of="$DEVICE_PATH" bs=512 count=1

BOOT_PTN_OF=0
BOOT_PTN_SZ=1024

ROOT_PTN_OF=$((BOOT_PTN_SZ + 4))
ROOT_PTN_SZ=4096
ROOT_PTN_SZ_B=$((ROOT_PTN_SZ*1048576))

DATA_PTN_OF=$((ROOT_PTN_OF + ROOT_PTN_SZ + 4))
DATA_PTN_SZ=$((BLK_DEV_SIZE_MB - DATA_PTN_OF))
DATA_PTN_SZ_B=$((DATA_PTN_SZ*1048576))

echo "Using Boot Partition Size = ${BOOT_PTN_SZ}MB"
echo "Using Root Partition Size = ${ROOT_PTN_SZ}MB"
echo "Using Data Partition Size = ${DATA_PTN_SZ}MB"

sudo sfdisk -uM "$DEVICE_PATH" << EOF
${BOOT_PTN_OF} ${BOOT_PTN_SZ} 0xc *
${ROOT_PTN_OF} ${ROOT_PTN_SZ} 0xea -
${DATA_PTN_OF} ${DATA_PTN_SZ} 0xe9 -
EOF

BOOT_PTN_PATH="${DEVICE_PATH}1"
ROOT_PTN_PATH="${DEVICE_PATH}2"
DATA_PTN_PATH="${DEVICE_PATH}3"

# Format the boot partition as FAT so that the pi bootloader can
# read it
sudo mkfs.vfat "$BOOT_PTN_PATH"

# Format the root and data partitions as MinFS so that zircon can
# read them
sudo $MINFS "$ROOT_PTN_PATH@$ROOT_PTN_SZ_B" create
sudo $MINFS "$DATA_PTN_PATH@$DATA_PTN_SZ_B" create

# Copy the necessary files to the boot partition
sudo $MANIFEST_BUILDER \
    --disk_path $ROOT_PTN_PATH \
    --minfs_path $MINFS \
    --file_manifest "$FUCHSIA_DIR/out/debug-aarch64/gen/packages/gn/system.bootfs.manifest"

# Function to attempt unmounting a mount point up to three times, sleeping
# a couple of seconds between attempts.
function umount_retry() {
  set +e
  TRIES=0
  while (! sudo umount $1); do
    ((TRIES++))
    if [[ ${TRIES} > 2 ]]; then
      echo "[format_sdcard] Unable to umount $0"
      exit 1
    fi
    sleep 2
  done
  set -e
}

# Copy the necessary files to the root partition
MOUNT_PATH=`mktemp -d`
sudo mount "$BOOT_PTN_PATH" "$MOUNT_PATH"
trap "umount_retry \"${MOUNT_PATH}\" && rm -rf \"${MOUNT_PATH}\" && echo \"Unmounted succesfully\"" INT TERM EXIT

# Copy the kernel to the boot partition.
sudo cp "$FUCHSIA_DIR/zircon/build-zircon-rpi3-arm64/zircon.bin" \
        "${MOUNT_PATH}/kernel8.img"

# Copy the rpi configuration
sudo cp "$FUCHSIA_DIR/zircon/kernel/target/rpi3/cmdline.txt" \
        "${MOUNT_PATH}/"
sudo cp "$FUCHSIA_DIR/zircon/kernel/target/rpi3/config.txt" \
        "${MOUNT_PATH}/"
sudo cp "$FUCHSIA_DIR/zircon/kernel/target/rpi3/bcm2710-rpi-3-b.dtb" \
        "${MOUNT_PATH}/"

# Copy the zircon boot image to the disk as well. Note that the fuchsia build
# also generates a fuchsia boot image that contains the entire boot image, we
# abstain from using this because it relies on the whole image being loaded into
# memory as a ramfs. Building the fuchsia system in a /system partition directly
# to the SD card reduces memory presure and alleviates the need for the RPi3's
# bootloader to load the whole fuchsia system from the SD card to RAM upon boot.
sudo cp "$FUCHSIA_DIR/zircon/build-zircon-rpi3-arm64/bootdata.bin" \
        "${MOUNT_PATH}/"

curl -L "https://github.com/raspberrypi/firmware/raw/390f53ed0fd79df274bdcc81d99e09fa262f03ab/boot/start.elf" > \
      /tmp/start.elf
sudo cp /tmp/start.elf "${MOUNT_PATH}/start.elf"
rm /tmp/start.elf

curl -L https://github.com/raspberrypi/firmware/raw/7fcb39cb5b5543ca7485cd1ae9e6d908f31e40c6/boot/bootcode.bin > \
     /tmp/bootcode.bin
sudo cp /tmp/bootcode.bin "${MOUNT_PATH}/bootcode.bin"
rm /tmp/bootcode.bin

# Make sure all writes are committed to disk.
pushd "$MOUNT_PATH" > /dev/null
sync
popd > /dev/null
echo " SUCCESS"
