#!/bin/bash
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# WARNING: This is not supposed to be directly executed by users.

set -o errexit

# fx-mkzedboot USB_DEVICE IMAGE_TYPE IMAGE FORCE_DEVICE
#    IMAGE_TYPE    Can be "efi" for x64 bootable disks
#                      or "vboot" for chromebook
#    IMAGE         Path to image
#    FORCE_DEVICE  true/false
function fx-mkzedboot {
  local usb_device="$1"
  local type="$2"
  local image="$3"
  local force="$4"

  #  EFI boot constants
  readonly esp_size=$(((63*1024*1024)/512))
  readonly esp_offset=2048

  # vboot constants
  readonly reserved_size=2048
  readonly reserved_offset=2048
  readonly vboot_size=$(((64*1024*1024)/512))
  readonly vboot_offset=$(($reserved_offset + $reserved_size))

  CGPT="${PREBUILT_CGPT_DIR}/cgpt"

  is_usb() {
    if ! ${force}; then
      fx-command-run list-usb-disks | grep "$1"
    fi
  }

  if ! is_usb "${usb_device}"; then
    fx-error >&2 "${usb_device} does not look like a USB device, use -f to force, or pick from below"
    fx-info "USB disks:"
    fx-command-run list-usb-disks
    exit 1
  fi

  if ! [[ -w "${usb_device}" ]] ; then
    fx-info "Changing ownership of ${usb_device} to ${USER}"
    sudo chown "${USER}" "${usb_device}"
  fi

  fx-info "Opening device..."
  # We open the device and hold onto an fd for the duration of our modifications.
  # This prevents automounting solutions from observing a final close and
  # rescanning the partition table until we're all done making changes -
  # particularly important on macOS where users would otherwise receive
  # EAGAIN/EBUSY and so on.
  open_device() {
    case "${HOST_OS}" in
      mac)
        if ! diskutil quiet unmountDisk "${usb_device}"; then
          fx-error "Failed to unmount ${usb_device}, cannot continue"
          exit 1
        fi
        ;;
    esac
    exec 3>>"${usb_device}"
  }
  close_device() {
    fx-info "Closing device."
    exec 3>&-
  }
  open_device

  # Destroy any existing GPT/MBR on the device and re-create
  fx-info "Create new GPT partition table... "
  "${CGPT}" create "${usb_device}"
  "${CGPT}" boot -p "${usb_device}"
  fx-info "done"

  fx-info "Create new partitions... "
  # ESP needs to be a FAT compatible size
  if [[ "${type}" == "efi" ]]; then
    "${CGPT}" add -s "${esp_size}" -t efi -b "${esp_offset}" -l esp "${usb_device}"
  elif [[ "${type}" == "vboot" ]]; then
    "${CGPT}" add -s "${reserved_size}" -t reserved -b "${reserved_offset}" -l reserved "${usb_device}"
    "${CGPT}" add -s "${vboot_size}" -t kernel -b "${vboot_offset}" -l zedboot "${usb_device}"
    "${CGPT}" add -i 2 -T 1 -S 1 -P 2 "${usb_device}"
  else
    fx-error "Unknown image type: ${type}. Please check the build target used in \"fx set\"."
    exit 1
  fi

  fx-info "done"

  if [[ is_esp ]]; then
    fx-info "Writing zedboot for EFI"
    dd if="${image}" of="${usb_device}" seek=${esp_offset}
  else
    fx-info "Writing zedboot for Cros"
    dd if="${image}" of="${usb_device}" seek=${vboot_offset}
  fi
  fx-info "done"

  close_device

  case "${HOST_OS}" in
    linux)
      eject "${usb_device}"
      ;;
    mac)
      diskutil eject "${usb_device}"
      ;;
  esac

}
