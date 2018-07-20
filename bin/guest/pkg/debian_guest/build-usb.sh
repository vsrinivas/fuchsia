#!/usr/bin/env bash
#
# Copyright 2018 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

set -e

DEBIAN_GUEST_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

usage() {
  echo "usage: ${0} {arm64, x64}"
  echo
  exit 1
}

declare -r ARCH=$1

case "${ARCH}" in
arm64)
  ROOT_GUID=B921B045-1DF0-41C3-AF44-4C6F280D3FAE;;
x64)
  ROOT_GUID=4F68BCE3-E8CD-4DB1-96E7-FBCAF984B709;;
*)
  usage;;
esac

get_confirmation() {
  echo -n "Press 'y' to confirm: "
  read CONFIRM
  if [[ "$CONFIRM" != "y" ]]; then
    echo "[format_usb] Aborted due to invalid confirmation"
    exit 1
  fi
}

command -v sgdisk > /dev/null 2>&1 || {
  echo "[format_usb] Requires the sgdisk command"
  echo "sudo apt-get install gdisk"
  exit 1
}

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
READLINK_USB=$(readlink -f "/sys/class/block/$DEVICE/device" | { grep -i "usb" || true; })
if [[ -z "$READLINK_USB" ]]; then
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
sudo -v -p "[sudo] Enter password to confirm information about device: "
sudo sgdisk -p "$DEVICE_PATH"
echo "[format_usb] ABOUT TO COMPLETELY WIPE / FORMAT: $DEVICE_PATH"
get_confirmation
echo "[format_usb] ARE YOU 100% SURE?"
get_confirmation

echo "[format_usb] Deleting all partition info on USB, creating new GPT"
sudo sgdisk -og "$DEVICE_PATH"

SECTOR_SIZE=`cat "/sys/block/$DEVICE/queue/hw_sector_size"`

echo "[format_usb] Creating Linux Root Partition"
sudo sgdisk -n 1:0:0 -c 1:"Root" -t 1:$ROOT_GUID "$DEVICE_PATH"
ROOT_PARTITION_PATH="${DEVICE_PATH}1"

$DEBIAN_GUEST_DIR/build-image.sh -d "${ROOT_PARTITION_PATH}" "${ARCH}"
