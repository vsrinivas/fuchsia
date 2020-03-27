#!/bin/bash
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Command to SSH to a Fuchsia device.
set -eu

# Source common functions
readonly MY_SCRIPT_SRC_DIR="$(cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd)"

# Fuchsia command common functions.
# shellcheck disable=SC1090
source "${MY_SCRIPT_SRC_DIR}/fuchsia-common.sh" || exit $?

readonly FUCHSIA_SDK_PATH="$(get-fuchsia-sdk-dir)"

function usage {
  echo "Usage: $0"
  echo "  [--device-name <device hostname>]"
  echo "    Connects to a device by looking up the given device hostname."
  echo "  [--device-ip <device ipaddr>]"
  echo "    Connects to a device by using the provided IP address, cannot use with --device-name"
  echo "  [--private-key <identity file>]"
  echo "    Uses additional private key when using ssh to access the device."
}

PRIVATE_KEY_FILE=""
DEVICE_NAME_FILTER="$(get-fuchsia-property device-name)"
DEVICE_IP="$(get-fuchsia-property device-ip)"
POSITIONAL=()


# Parse command line
while (( "$#" )); do
case $1 in
    --device-name)
      shift
      DEVICE_NAME_FILTER="${1}"
    ;;
    --device-ip)
      shift
      DEVICE_IP="${1}"
    ;;
    --private-key)
    shift
    PRIVATE_KEY_FILE="${1}"
    ;;
    -*)
    if [[ "${#POSITIONAL[@]}" -eq 0 ]]; then
      echo "Unknown option ${1}"
      usage
      exit 1
    else
      POSITIONAL+=("${1}")
    fi
    ;;
    *)
      POSITIONAL+=("${1}")
    ;;
esac
shift
done

readonly PRIVATE_KEY_FILE
readonly DEVICE_NAME_FILTER
readonly DEVICE_IP
readonly POSITIONAL

target_device_ip="${DEVICE_IP}"

# Get the device IP address.
if [[ "${DEVICE_IP}" == "" ]]; then
  # explicitly pass the sdk dir here.
  target_device_ip=$(get-device-ip-by-name "$FUCHSIA_SDK_PATH" "$DEVICE_NAME_FILTER")
  if [[ ! "$?" || -z "$target_device_ip" ]]; then
    fx-error "Error finding device"
    exit 2
  fi
else
  if [[ "${DEVICE_NAME_FILTER}" != "" ]]; then
    fx-error "Cannot specify both --device-name and --device-ip"
    exit 3
  fi
fi

ssh_args=()
# Build the command line
if [[ "${PRIVATE_KEY_FILE}" != "" ]]; then
  ssh_args+=( "-i" "${PRIVATE_KEY_FILE}")
fi

ssh_args+=( "${target_device_ip}" )
if [[ "${#POSITIONAL[@]}" -ne 0 ]]; then
  ssh_args+=( "${POSITIONAL[@]}" )
fi

ssh-cmd "${ssh_args[@]}"
