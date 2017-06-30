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
MAGENTA_DIR="$SCRIPT_DIR/.."

echo $MAGENTA_DIR


# Ensure Magenta has been built prior to formatting USB
pushd "$MAGENTA_DIR" > /dev/null
./scripts/make-parallel magenta-odroidc2-arm64
popd > /dev/null

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
echo "[format_sdcard] ARE YOU 100% SURE? This will hurt if you get it wrong."
get_confirmation

# Create three new partitions on the device, 1GB for root and divide the
# remaining space into 2 pieces for data and bootfs

# Determine how many bytes we have available on this disk.
BLK_DEV_SIZE=`sudo blockdev --getsize64 $DEVICE_PATH`
BLK_DEV_SIZE_MB=$((BLK_DEV_SIZE/1048576))

if [ "$BLK_DEV_SIZE_MB" -lt 3000 ]; then
    echo " FAILED"
    echo "[format_sdcard] ERROR: SD Card must be at least 4GB"
    exit 1
fi


sudo dd if=/dev/zero of="$DEVICE_PATH" bs=512 count=1




DATA_PTN_START=4259840
DATA_PTN_END=4521983
DATA_PTN_SIZE=$((DATA_PTN_END-DATA_PTN_START+1))
DATA_PTN_SIZE_B=$(((DATA_PTN_END - DATA_PTN_START + 1)*512))

echo "Using Data Partition Size = ${DATA_PTN_SIZE_B} bytes"

sudo sfdisk -uS "$DEVICE_PATH" << EOF
${DATA_PTN_START} ${DATA_PTN_SIZE} 0x0b -

EOF

DATA_PTN_PATH="${DEVICE_PATH}1"

sudo mkfs.fat ${DEVICE_PATH}1



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
sudo mount "$DATA_PTN_PATH" "$MOUNT_PATH"
trap "umount_retry \"${MOUNT_PATH}\" && rm -rf \"${MOUNT_PATH}\" && echo \"Unmounted succesfully\"" INT TERM EXIT

# Copy the kernel to the boot partition.
sudo cp "$MAGENTA_DIR/build-magenta-odroidc2-arm64/magenta.bin" \
        "${MOUNT_PATH}/"

# Copy the ramdisk
sudo cp "$MAGENTA_DIR/build-magenta-odroidc2-arm64/bootdata.bin" \
        "${MOUNT_PATH}/"

sudo cp "$MAGENTA_DIR/kernel/target/odroidc2/boot.ini" \
        "${MOUNT_PATH}/"


# Make sure all writes are committed to disk.
pushd "$MOUNT_PATH" > /dev/null
sync
popd > /dev/null

curl -L http://dn.odroid.com/S905/BootLoader/ODROID-C2/c2_boot_release.tar.gz >/tmp/sd.tgz
tar -C /tmp -xzf /tmp/sd.tgz
cd /tmp/sd_fuse
sudo sh sd_fusing.sh $DEVICE_PATH
rm -rf /tmp/sd_fuse
rm -f /tmp/sd.tgz


echo " SUCCESS"
