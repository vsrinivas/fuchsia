#!/bin/bash
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# WARNING: This is not supposed to be directly executed by users.

set -o errexit

function fx-flash {
  local enable_ipv4="${FX_ENABLE_IPV4:-false}"
  local serial="$1"
  local device="$2"
  local flash_manifest="$3"


  if [[ $num_gb_devices > 1 ]]; then
    echo "More than one device detected, please provide -device <device>"
    return 1
  fi

  ffx_flash_args=("fuchsia" "--ssh-key" "$(get-ssh-authkeys)") || {
    fx-warn "Cannot find a valid authorized keys file. Recovery will be flashed."
    ffx_flash_args=("recovery")
  }

  flash_args=("--ssh-key=$(get-ssh-authkeys)") || {
    fx-warn "Cannot find a valid authorized keys file. Recovery will be flashed."
    flash_args=("--recovery")
  }

  # Process traditional fastboot over USB.
  fastboot_args=()
  ffx_args=()
  if [[ -n "${serial}" ]]; then
    fastboot_args=("-s" "${serial}")
    ffx_args=("-t" "${serial}")
  fi

  if is_feature_enabled "legacy_fastboot"; then
    fx-warn "Using legacy flash method via 'fastboot'"
    fx-warn "To use the current ffx-based flash method, don't use the flag '--enable=legacy_fastboot' and unset the 'FUCHSIA_DISABLED_legacy_fastboot' environment variable."

    if [[ -z "${serial}" ]]; then
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
      done < <(fx-command-run host-tool --check-firewall device-finder list -ipv4="${enable_ipv4}" -netboot -full)

      if [[ ! -z "${gb_device_ip}" ]]; then
        fastboot_args+=("-s" "udp:${gb_device_ip}")
      else
        # If the user didn't specify a device with -s, see if there's exactly 1.
        num_devices=$(fx-command-run host-tool fastboot devices | awk 'NF' | wc -l)
        if [[ "${num_devices}" -lt 1 ]]; then
          fx-error "No device detected, boot into fastboot mode or provide -s <serial>!"
          return 1
        elif [[ "${num_devices}" -gt 1 ]]; then
          fx-error "More than one device detected, please provide -s <serial>!"
          return 1
        fi
      fi
    fi

    "./flash.sh" "${flash_args[@]}" "${fastboot_args[@]}"
  fi

  if [[ ! -f "${flash_manifest}" ]]; then
    fx-error "Flash manifest: '${flash_manifest}' not found"
    return 1
  fi

  fx-info "Running fx ffx ${ffx_args[@]} target flash ${flash_manifest} ${ffx_flash_args[@]}"
  fx-command-run host-tool --check-firewall ffx "${ffx_args[@]}" target flash "${flash_manifest}" "${ffx_flash_args[@]}"
}
