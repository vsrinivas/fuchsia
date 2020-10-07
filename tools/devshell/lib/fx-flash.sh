#!/bin/bash
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# WARNING: This is not supposed to be directly executed by users.

set -o errexit

function fx-flash {
  local serial="$1"
  local device="$2"
  # Process devices in gigaboot.
  gb_device_ip=
  num_gb_devices=0
  while read line; do
    # Split line into IP and nodename.
    elements=($line)
    if [[ ! -z "${device}" ]]; then
      if [[ "${elements[1]}" == "${device}" ]]; then
        gb_device_ip=${elements[0]}
        num_gb_devices=1
        break
      fi
    else
      let num_gb_devices=$num_gb_devices+1
      gb_device_ip=${elements[0]}
    fi
  done < <(fx-device-finder list -netboot -full)

  if [[ $num_gb_devices > 1 ]]; then
    echo "More than one device detected, please provide -device <device>"
    return 1
  fi

  flash_args=("--ssh-key=$(get-ssh-authkeys)") || {
    fx-warn "Cannot find a valid authorized keys file. Recovery will be flashed."
    flash_args=("--recovery")
  }

  if [[ ! -z "${gb_device_ip}" ]]; then
    "./flash.sh" "${flash_args[@]}" "-s" "udp:${gb_device_ip}"
  else
    # Process traditional fastboot over USB.
    fastboot="$(fx-command-run list-build-artifacts --build --expect-one --name fastboot tools)"
    num_devices=$("${fastboot}" devices | wc -l)
    if [[ "${num_devices}" -lt 1 ]]; then
      fx-error "Please place device into fastboot mode!"
      return 1
    elif [[ "${num_devices}" -gt 1 ]] && [[ -z "${serial}" ]]; then
      fx-error "More than one device detected, please provide -s <serial>!"
      return 1
    fi

    fastboot_args=()
    if [[ ! -z "${serial}" ]]; then
      fastboot_args=("-s" "${serial}")
    fi

    "./flash.sh" "${flash_args[@]}" "${fastboot_args[@]}"
  fi
}
