#!/bin/bash
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
#### CATEGORY=Device management
### copy a file to/from a target device

set -eu

# Source common functions
SCRIPT_SRC_DIR="$(cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd)"

# Fuchsia command common functions.
# shellcheck disable=SC1090
source "${SCRIPT_SRC_DIR}/fuchsia-common.sh" || exit $?

FUCHSIA_SDK_PATH="$(get-fuchsia-sdk-dir)"

function usage {
  cat << EOF
usage: fcp.sh [(--device-name <device hostname> | --device-ip <device ip-addr>)]  [--private-key <identity file>] [(--to-target|--to-host)] SRC... DST
    Copies a file from the host to the target device, or vice versa.

  --device-name <device hostname>
      Connects to a device by looking up the given device hostname.
  --device-ip <device ipaddr>
      Connects to a device by using the provided IP address, cannot use with --device-name.
      If neither --device-name nor --device-ip are given, the connection is attempted to the
      first device detected.
  --private-key <identity file>
      Uses additional private key when using ssh to access the device.
  --to-target
      Copy file SRC from host to DST on the target. This is the default.
  --to-host
      Copy file SRC from target to DST on the host.
EOF
}

PRIVATE_KEY_FILE=""
DEVICE_NAME_FILTER=""
DEVICE_IP=""
TO_TARGET=true

# Process all the args except the last two.
while (( "$#" > 2 )); do
  case "$1" in
  --to-target)
    TO_TARGET=true
    ;;
  --to-host)
    TO_TARGET=false
    ;;
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
  --help)
    usage
    exit 1
  esac
shift
done

readonly DEVICE_NAME_FILTER
readonly DEVICE_IP
readonly PRIVATE_KEY_FILE
readonly TO_TARGET

args=( "$@" )
nargs=${#args[@]}
dst=${args[$nargs-1]}
srcs=( "${args[@]:0:$nargs-1}" )
target_addr="${DEVICE_IP}"

if [[ $# -ne 2 ]]; then
  usage
  exit 1
fi

# Check for core SDK being present
if [[ ! -d "${FUCHSIA_SDK_PATH}" ]]; then
  fx-error "Fuchsia Core SDK not found at ${FUCHSIA_SDK_PATH}."
  exit 2
fi

# Get the device IP address if not specified.
if [[ "${target_addr}" == "" ]]; then
  # explicitly pass the sdk here since the name filter arg must be $2.
  target_addr=$(get-device-ip-by-name "${FUCHSIA_SDK_PATH}" "${DEVICE_NAME_FILTER}")
  if [[ ! "$?" || -z "${target_addr}" ]]; then
    fx-error "Error finding device"
    exit 2
  fi
else
  if [[ "${DEVICE_NAME_FILTER}" != "" ]]; then
    fx-error "Cannot specify both --device-name and --device-ip"
    exit 3
  fi
fi

# Build the command line
sftp_cmd=( "sftp" "-F" "${SCRIPT_SRC_DIR}/sshconfig" )
if [[ "${PRIVATE_KEY_FILE}" != "" ]]; then
  sftp_cmd+=( "-i" "${PRIVATE_KEY_FILE}" )
fi
# Pass in commands in batch mode from stdin
sftp_cmd+=( "-b" "-" )
# sftp needs the [] around the ip address, which is different than ssh.
sftp_cmd+=( "[${target_addr}]" )

if [[ "${TO_TARGET}" = "true" ]]; then
  (
  for src in "${srcs[@]}"; do
    echo "put \"${src}\" \"${dst}\""
  done
  ) | "${sftp_cmd[@]}" > /dev/null
else
  (
  for src in "${srcs[@]}"; do
    echo "get \"${src}\" \"${dst}\""
  done
  ) |  "${sftp_cmd[@]}" > /dev/null
fi
