#!/bin/bash
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
set -eu

SCRIPT_SRC_DIR="$(cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd)"

# Fuchsia command common functions.
# shellcheck disable=SC1090
source "${SCRIPT_SRC_DIR}/fuchsia-common.sh" || exit $?

usage() {
  cat << EOF
usage: fserve-remote.sh [--no-serve] [--device-name <device hostname>]  HOSTNAME REMOTE-PATH
    Uses SSH port forwarding to connect to a remote server and forward package serving and other connections to a local device.

  --device-name <device hostname>
      Connects to a device by looking up the given device hostname.
  --image <image name> 
      Name of prebuilt image packages to serve.
  --bucket <bucket name>
      Name of GCS bucket containing the image archive.
  --no-serve
      Only tunnel, do not start a package server.

  HOSTNAME
      The hostname of the workstation you want to serve from
  REMOTE-PATH
      The path to the Fuchsia GN SDK bin directory on  "HOSTNAME"
EOF
}

START_SERVE=1
REMOTE_HOST=""
REMOTE_DIR=""
DEVICE_NAME="$(get-fuchsia-property device-name)"
FUCHSIA_SDK_PATH="$(get-fuchsia-sdk-dir)"
BUCKET="$(get-fuchsia-property bucket)"
IMAGE="$(get-fuchsia-property image)"

while [[ $# -ne 0 ]]; do
  case "$1" in
  --help|-h)
      usage
      exit 0
      ;;
  --no-serve)
    START_SERVE=0
    ;;
  --device-name)
    shift
    DEVICE_NAME="${1}"
    ;;
  --bucket)
    shift
    BUCKET="${1}"
    ;;
  --image)
    shift
    IMAGE="${1}"
    ;;
  -*)
    fx-error "Unknown flag: $1"
    usage
    exit 1
    ;;
  *)
    if [[ -z "${REMOTE_HOST}" ]]; then
      REMOTE_HOST="$1"
    elif [[ -z "${REMOTE_DIR}" ]]; then
      REMOTE_DIR="$1"
    else
      fx-error "unexpected argument: '$1'"
      usage
    fi
    ;;
  esac
  shift
done

if [[ -z "${REMOTE_HOST}" ]]; then
  fx-error "HOSTNAME must be specified"
  usage
  exit 1
fi

if ((START_SERVE)); then
  if [[ -z "${REMOTE_DIR}" ]]; then
      fx-error "REMOTE-DIR must be specified"
      usage
      exit 1
  fi
fi

if [[ "${DEVICE_NAME}" == "" ]]; then
    DEVICE_NAME="$(get-device-name)"
fi
# Determine the local device name/address to use.
if ! DEVICE_IP=$(get-device-ip-by-name "$FUCHSIA_SDK_PATH" "${DEVICE_NAME}"); then
  fx-error "unable to discover device. Is the target up?"
  exit 1
fi

if [[  -z "${DEVICE_IP}" ]]; then
  fx-error "unable to discover device. Is the target up?"
  exit 1
fi

echo "Using remote ${REMOTE_HOST}:${REMOTE_DIR}"
echo "Using target device ${DEVICE_NAME}"

# First we need to check if we already have a control master for the
# host, if we do, we might already have the forwards and so we don't
# need to worry about tearing down:
if ! ssh -O check "${REMOTE_HOST}" > /dev/null 2>&1; then
  # If we didn't have a control master, and the device already has 8022
  # bound, then there's a good chance there's a stale sshd instance
  # running from another device or another session that will block the
  # forward, so we'll check for that and speculatively attempt to clean
  # it up. Unfortunately this means authing twice, but it's likely the
  # best we can do for now.
  if ssh "${REMOTE_HOST}" 'ss -ln | grep :8022' > /dev/null; then
    ssh "${REMOTE_HOST}" "pkill -u \$USER sshd"
    ssh "${REMOTE_HOST}" -O exit > /dev/null 2>&1
  fi
fi

args=(
  -6 # We want ipv6 binds for the port forwards
  -L "\*:8083:localhost:8083" # requests to the package server address locally go to the workstation
  -R "8022:[${DEVICE_IP}]:22" # requests from the workstation to ssh to localhost:8022 will make it to the target
  -o "ExitOnForwardFailure=yes"
  "${REMOTE_HOST}"
)

# If the user requested serving, then we'll check to see if there's a
# server already running and kill it, this prevents most cases where
# signal propagation seems to sometimes not make it to "pm".
if ((START_SERVE)) && ssh "${args[@]}" 'ss -ln | grep :8083' > /dev/null; then
  ssh "${args[@]}" "pkill -u \$USER pm"
fi

if ((START_SERVE)); then
  # Starts a package server
  args+=(cd "${REMOTE_DIR}" "&&" ./bin/fconfig.sh set device-ip 127.0.0.1 "&&"  ./bin/fserve.sh)
  if [[ "${BUCKET}" != "" ]]; then
    args+=(--bucket "${BUCKET}")
  fi
  if [[ "${IMAGE}" ]]; then
    args+=(--image "${IMAGE}")
  fi
else
  # Starts nothing, just goes to sleep
  args+=("-nNT")
fi

ssh "${args[@]}"
