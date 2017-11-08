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
ZIRCON_SCRIPTS_DIR="${ZIRCONDIR}/scripts"
BUILDDIR="${ZIRCONDIR}/build-zircon-pc-x86-64"

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

echo -n "Building zircon..."
${ZIRCON_SCRIPTS_DIR}/build-zircon-x86-64
${GUEST_SCRIPTS_DIR}/mkbootfs.sh zircon-pc-x86-64
echo " SUCCESS"

echo -n "Building zircon-guest kernel..."
$GUEST_SCRIPTS_DIR/mklinux.sh
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

echo "[format_usb] Creating 2GB Linux Home Partition"
sudo sgdisk -n 3:0:+2G -c 3:"Home" "$DEVICE_PATH"
HOME_PARTITION_PATH="${DEVICE_PATH}3"
sudo mkfs.ext4 "$HOME_PARTITION_PATH"

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
HOME_MOUNT_PATH="${MOUNT_PATH}/home"
sudo mount "${ROOT_PARTITION_PATH}" "${MOUNT_PATH}"
sudo mkdir -p "${EFI_MOUNT_PATH}"
sudo mkdir -p "${HOME_MOUNT_PATH}"
sudo mkdir -p "${MOUNT_PATH}/dev"
sudo mkdir -p "${MOUNT_PATH}/sys"
sudo mkdir -p "${MOUNT_PATH}/proc"

sudo mount "${EFI_PARTITION_PATH}" "${EFI_MOUNT_PATH}"
sudo mount "${HOME_PARTITION_PATH}" "${HOME_MOUNT_PATH}"
sudo mount --bind /sys "${MOUNT_PATH}/sys"
sudo mount --bind /proc "${MOUNT_PATH}/proc"

unmount_all() {
    umount_retry "${EFI_MOUNT_PATH}"
    umount_retry "${HOME_MOUNT_PATH}"
    umount_retry "${MOUNT_PATH}/sys"
    umount_retry "${MOUNT_PATH}/proc"
    umount_retry "${MOUNT_PATH}"

    rm -rf "${MOUNT_PATH}"
    echo "Unmounted successfully"

}
trap "unmount_all" INT TERM EXIT

echo -n "Installing Debian base system ..."
sudo "${GUEST_SCRIPTS_DIR}/bootstrap-debian.sh" "${MOUNT_PATH}"
echo " SUCCESS"

sudo mkdir -p "${EFI_MOUNT_PATH}/EFI/BOOT"
echo -n "Copying Bootloader..."
sudo cp "${BUILDDIR}/bootloader/bootx64.efi" "${EFI_MOUNT_PATH}/EFI/BOOT/gigaboot.efi"
echo " SUCCESS"

echo -n "Copying zircon.bin..."
sudo cp "${BUILDDIR}/zircon.bin" "${EFI_MOUNT_PATH}/zircon.bin"
sudo cp "${BUILDDIR}/bootdata-with-guest.bin" "${EFI_MOUNT_PATH}/ramdisk.bin"
echo " SUCCESS"

echo -n "Copying fstab..."
ROOT_UUID=$(sudo blkid -s UUID -o value "${ROOT_PARTITION_PATH}")
EFI_UUID=$(sudo blkid -s UUID -o value "${EFI_PARTITION_PATH}")
HOME_UUID=$(sudo blkid -s UUID -o value "${HOME_PARTITION_PATH}")
FSTAB=$(mktemp)
echo "" > "${FSTAB}"
echo "UUID=${EFI_UUID} /boot vfat defaults,iversion,nofail 0 1" > "${FSTAB}"
sudo cp "${FSTAB}" "${MOUNT_PATH}/etc/fstab"
rm "${FSTAB}"
echo "SUCCESS"

echo -n "Copying run-qemu.sh..."
sudo cp /tmp/linux/arch/x86/boot/bzImage "${MOUNT_PATH}/opt/bzImage"
RUN_QEMU=$(mktemp)

###############################################################################
# Emit run-qemu.sh.
###############################################################################
cat > "${RUN_QEMU}" << EOF
#!/bin/sh

KERNEL_CMDLINE="root=/dev/vda ro lockfs console=ttyS0 io_delay=none"
CPU_SPEC="host,migratable=no"
KVM_DISABLE_PARAVIRT_FEATURES="-kvmclock,-kvm-nopiodelay,-kvm-asyncpf,-kvm-steal-time,-kvm-pv-eoi,-kvmclock-stable-bit,-kvm-pv-unhalt"

usage() {
    echo "usage: run-qemu.sh [options]"
    echo ""
    echo "  -p    Disable KVM paravirt features."
}

while getopts "p" opt; do
  case "\${opt}" in
    p) CPU_SPEC="\${CPU_SPEC},\${KVM_DISABLE_PARAVIRT_FEATURES}" ;;
    *) usage ;;
  esac
done

qemu-system-x86_64 \\
    -nographic \\
    -drive file=/dev/disk/by-uuid/${ROOT_UUID},readonly=on,format=raw,if=none,cache=none,id=root \\
    -device virtio-blk-pci,drive=root \\
    -drive file=/dev/disk/by-uuid/${HOME_UUID},format=raw,if=none,cache=none,id=home \\
    -device virtio-blk-pci,drive=home \\
    -device virtio-serial-pci \\
    -net none \\
    -machine q35 \\
    -m 1G \\
    -smp 1 \\
    -enable-kvm \\
    -cpu "\${CPU_SPEC}" \\
    -initrd /initrd.img \\
    -kernel /opt/bzImage \\
    -append "\${KERNEL_CMDLINE}"
EOF
###############################################################################
# End run-qemu.sh
###############################################################################
sudo cp "${RUN_QEMU}" "${MOUNT_PATH}/opt/run-qemu.sh"
sudo chmod +x "${MOUNT_PATH}/opt/run-qemu.sh"
rm "${RUN_QEMU}"
echo " SUCCESS"

# Filesystems will be unmounted at exit by the trap set above.
