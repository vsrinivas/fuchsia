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

function usage {
  echo "Usage: $(basename "$0") [args]"
  echo "  [--help]"
  echo "    This message."
  echo "  [--device-name <device hostname>]"
  echo "    Connects to the device with the given device hostname. Cannot be used with --device-ip."
  echo "    Defaults to the value from \`fconfig.sh get device-name\`."
  echo "  [--device-ip <device ip>]"
  echo "    Connects to the device with the given device ip address. Cannot be used with --device-name."
  echo "    Defaults to the value from \`fconfig.sh get device-ip\`."
  echo "    Note: If defaults are configured for both device-name and device-ip, then device-ip is used."
  echo "          If the device is specified at all, then the first device discovered is used."
  echo "  [--private-key <identity file>]"
  echo "    Uses additional private key when using ssh to access the device."
  echo "  [--sshconfig <sshconfig file>]"
  echo "    Use the specified sshconfig file instead of fssh's version."
  echo
  echo "All positional arguments are passed through to SSH to be executed on the device."
}

PRIVATE_KEY_FILE=""
DEVICE_NAME_FILTER=""
DEVICE_IP_ADDR=""
POSITIONAL=()
SSHCONFIG_FILE=""

# Parse command line
while (( "$#" )); do
case $1 in
    --device-name)
      shift
      DEVICE_NAME_FILTER="${1}"
    ;;
    --device-ip)
      shift
      DEVICE_IP_ADDR="${1}"
    ;;
    --private-key)
      shift
      PRIVATE_KEY_FILE="${1}"
    ;;
    --sshconfig)
      shift
      SSHCONFIG_FILE="${1}"
    ;;
    --help)
      usage
      exit 1
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

if [[ "${DEVICE_IP_ADDR}" == "" && "${DEVICE_NAME_FILTER}" == "" ]]; then
  # No device specified on the command line, so use the default IP, then the
  # default name if configured.
  DEVICE_IP_ADDR="$(get-fuchsia-property device-ip)"
  if [[ -z "${DEVICE_IP_ADDR}" ]]; then
    DEVICE_NAME_FILTER="$(get-fuchsia-property device-name)"
    if [[ -n "${DEVICE_NAME_FILTER}" ]]; then
      echo "Using device name ${DEVICE_NAME_FILTER}. Use --device-name or fconfig.sh to use another device."
    fi
  else
    echo "Using device address ${DEVICE_IP_ADDR}. Use --device-ip or fconfig.sh to use another device."
  fi
elif [[ "${DEVICE_IP_ADDR}" != "" && "${DEVICE_NAME_FILTER}" != "" ]]; then
  fx-error "Cannot use both --device-name and --device-ip".
  exit 1
fi

readonly PRIVATE_KEY_FILE
readonly DEVICE_NAME_FILTER
readonly DEVICE_IP_ADDR
readonly POSITIONAL

target_device_ip="${DEVICE_IP_ADDR}"

# Get the device IP address.
if [[ "${target_device_ip}" == "" ]]; then
  target_device_ip=$(get-device-ip-by-name "${DEVICE_NAME_FILTER}")
  if [[ ! "$?" || -z "$target_device_ip" ]]; then
    fx-error "Error finding device"
    exit 2
  fi
fi

ssh_args=()
# Build the command line
if [[ "${SSHCONFIG_FILE}" != "" ]]; then
  ssh_args+=("-F" "${SSHCONFIG_FILE}")
fi
if [[ "${PRIVATE_KEY_FILE}" != "" ]]; then
  ssh_args+=( "-i" "${PRIVATE_KEY_FILE}")
fi

ssh_args+=( "${target_device_ip}" )
if [[ "${#POSITIONAL[@]}" -ne 0 ]]; then
  ssh_args+=( "${POSITIONAL[@]}" )
fi

ssh-cmd "${ssh_args[@]}"
