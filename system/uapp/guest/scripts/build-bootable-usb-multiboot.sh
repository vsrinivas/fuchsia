#!/usr/bin/env bash
#
# Copyright 2017 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

set -eu

GUEST_SCRIPTS_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
ZIRCONDIR="${ZIRCON_DIR:-${GUEST_SCRIPTS_DIR}/../../../..}"
BUILDDIR="${ZIRCON_BUILD_DIR:-$ZIRCONDIR/build-zircon-pc-x86-64}"

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

usage() {
  echo "build-bootable-usb-multiboot.sh"
  echo ""
  echo "Formats a USB drive with a GRUB bootloader that can either boot"
  echo "Debian or Zircon (via gigaboot)."
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
echo "[format_usb] Creating 200MB EFI System Partition"
sudo sgdisk -n 1:0:+200M -c 1:"EFI System Partition" -t 1:ef00 "$DEVICE_PATH"
EFI_PARTITION_PATH="${DEVICE_PATH}1"
sudo mkfs.vfat "$EFI_PARTITION_PATH"

echo "[format_usb] Creating 5GB Linux Root Partition"
sudo sgdisk -n 2:0:+5G -c 2:"Root" "$DEVICE_PATH"
ROOT_PARTITION_PATH="${DEVICE_PATH}2"
sudo mkfs.ext4 "$ROOT_PARTITION_PATH"

# Function to attempt unmounting a mount point up to three times, sleeping
# a couple of seconds between attempts.
function umount_retry() {
  set +e
  TRIES=0
  while (! sudo umount $1); do
    ((TRIES++))
    if [[ ${TRIES} > 2 ]]; then
      echo "[format_usb] Unable to umount $0"
      exit 1
    fi
    sleep 2
  done
  set -e
}

MOUNT_PATH=`mktemp -d`
EFI_MOUNT_PATH="${MOUNT_PATH}/boot"
sudo mount "${ROOT_PARTITION_PATH}" "${MOUNT_PATH}"
sudo mkdir -p "${EFI_MOUNT_PATH}"
sudo mount "${EFI_PARTITION_PATH}" "${EFI_MOUNT_PATH}"
trap "umount_retry \"${EFI_MOUNT_PATH}\" && umount_retry \"${MOUNT_PATH}\" && rm -rf \"${MOUNT_PATH}\" && echo \"Unmounted successfully\"" INT TERM EXIT

echo -n "Installing Debian base system ..."
sudo "${GUEST_SCRIPTS_DIR}/bootstrap-debian.sh" "${MOUNT_PATH}"
echo " SUCCESS"

echo -n "Installing GRUB2..."
sudo grub-install \
    --target x86_64-efi \
    --efi-directory="${EFI_MOUNT_PATH}" \
    --boot-directory="${EFI_MOUNT_PATH}" \
    --removable
echo " SUCCESS"

sudo mkdir -p "${EFI_MOUNT_PATH}/EFI/BOOT"
echo -n "Copying Bootloader..."
sudo cp "${BUILDDIR}/bootloader/bootx64.efi" "${EFI_MOUNT_PATH}/EFI/BOOT/gigaboot.efi"
echo " SUCCESS"

echo -n "Copying zircon.bin..."
sudo cp "${BUILDDIR}/zircon.bin" "${EFI_MOUNT_PATH}/zircon.bin"
sudo cp "${BUILDDIR}/bootdata.bin" "${EFI_MOUNT_PATH}/ramdisk.bin"
echo " SUCCESS"

echo -n "Copying grub.cfg..."
# Debian creates symlinks to the kernel/initrd. Read these links to find the
# specific filenames.
LINUX_PATH="/$(basename $(readlink ${MOUNT_PATH}/vmlinuz))"
INITRD_PATH="/$(basename $(readlink ${MOUNT_PATH}/initrd.img))"
ROOT_UUID=$(sudo blkid -s UUID -o value "${ROOT_PARTITION_PATH}")
GRUB_CONF=$(mktemp)
cat > "${GRUB_CONF}" << EOF
set timeout=5

menuentry "Zircon" {
  insmod chain
  echo "Loading gigaboot..."
  chainloader /EFI/BOOT/gigaboot.efi
}

menuentry "Debian" {
  insmod efi_gop
  insmod efi_uga

  if loadfont /grub/fonts/unicode.pf2
  then
    insmod gfxterm
    set gfxmode=auto
    set gfxpayload=keep
    terminal_output gfxterm
  fi

  echo "Loading linux..."
  linux ${LINUX_PATH} root=/dev/disk/by-uuid/${ROOT_UUID} ro rootwait lockfs
  echo "Loading initrd..."
  initrd ${INITRD_PATH}
}
EOF
sudo cp "${GRUB_CONF}" "${EFI_MOUNT_PATH}/grub/grub.cfg"
rm "${GRUB_CONF}"
echo " SUCCESS"

echo -n "Copying fstab..."
EFI_UUID=$(sudo blkid -s UUID -o value "${EFI_PARTITION_PATH}")
FSTAB=$(mktemp)
echo "" > "${FSTAB}"
echo "UUID=${EFI_UUID} /boot vfat defaults,iversion,nofail 0 1" > "${FSTAB}"
sudo cp "${FSTAB}" "${MOUNT_PATH}/etc/fstab"
rm "${FSTAB}"
echo "SUCCESS"

echo -n "Copying run-qemu.sh..."
RUN_QEMU=$(mktemp)
cat > "${RUN_QEMU}" << EOF
#!/bin/sh

qemu-system-x86_64 \
    -nographic \
    -drive file=/dev/disk/by-uuid/${ROOT_UUID},readonly=on,format=raw,if=none,id=root \
    -device virtio-blk-pci,drive=root \
    -device virtio-serial-pci \
    -net none \
    -machine q35 \
    -enable-kvm \
    -cpu host,migratable=no \
    -initrd /initrd.img \
    -kernel /vmlinuz \
    -append "root=/dev/disk/by-uuid/${ROOT_UUID} ro lockfs console=ttyS0"
EOF
sudo cp "${RUN_QEMU}" "${MOUNT_PATH}/opt/run-qemu.sh"
sudo chmod +x "${MOUNT_PATH}/opt/run-qemu.sh"
rm "${RUN_QEMU}"
echo " SUCCESS"

# Filesystems will be unmounted at exit by the trap set above.
