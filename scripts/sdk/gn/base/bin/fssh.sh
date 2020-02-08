#!/bin/bash
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Command to SSH to a Fuchsia device.
set -eu

# Source common functions
SCRIPT_SRC_DIR="$(cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd)"

# Fuchsia command common functions.
source "${SCRIPT_SRC_DIR}/fuchsia-common.sh" || exit $?

FUCHSIA_SDK_PATH="$(realpath "${SCRIPT_SRC_DIR}/..")"

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
DEVICE_NAME_FILTER=""
DEVICE_IP=""
declare -a POSITIONAL

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

# Check for core SDK being present
if [[ ! -d "${FUCHSIA_SDK_PATH}" ]]; then
  fx-error "Fuchsia Core SDK not found at ${FUCHSIA_SDK_PATH}."
  exit 2
fi

# Get the device IP address.
if [[ "${DEVICE_IP}" == "" ]]; then
  DEVICE_IP=$(get-device-ip-by-name "$FUCHSIA_SDK_PATH" "$DEVICE_NAME_FILTER")
  if [[ ! "$?" || -z "$DEVICE_IP" ]]; then
    fx-error "Error finding device"
    exit 2
  fi
else
  if [[ "${DEVICE_NAME_FILTER}" != "" ]]; then
    fx-error "Cannot specify both --device-name and --device-ip"
    exit 3
  fi
fi

SSH_ARGS=()
if [[ "${PRIVATE_KEY_FILE}" != "" ]]; then
  SSH_ARGS+=( "-i" "${PRIVATE_KEY_FILE}" )
fi

SSH_ARGS+=( "${DEVICE_IP}" )
SSH_ARGS+=( "${POSITIONAL[@]}" )

ssh-cmd "${SSH_ARGS[@]}"
