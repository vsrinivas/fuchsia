#!/bin/bash
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -eu

SCRIPT_SRC_DIR="$(cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd)"

# Fuchsia command common functions.
# shellcheck disable=SC1090
source "${SCRIPT_SRC_DIR}/fuchsia-common.sh" || exit $?

FUCHSIA_SDK_PATH="$(get-fuchsia-sdk-dir)"
FUCHSIA_IMAGE_WORK_DIR="$(get-fuchsia-sdk-data-dir)"
FUCHSIA_BUCKET="$(get-fuchsia-property bucket)"
if [[ "${FUCHSIA_BUCKET}" == "" ]]; then
  FUCHSIA_BUCKET="${DEFAULT_FUCHSIA_BUCKET}"
fi

FUCHSIA_SERVER_PORT="8083"

IMAGE_NAME="$(get-fuchsia-property image)"
if [[ "${IMAGE_NAME}" == "" ]]; then
  IMAGE_NAME="generic-x64"
fi

usage () {
  echo "Usage: $0"
  echo "  [--work-dir <directory to store image assets>]"
  echo "    Defaults to ${FUCHSIA_IMAGE_WORK_DIR}"
  echo "  [--bucket <fuchsia gsutil bucket>]"
  echo "    Defaults to ${FUCHSIA_BUCKET}"
  echo "  [--image <image name>]"
  echo "    Defaults to ${IMAGE_NAME}. Use --image list to list all available images."
  echo "  [--private-key <identity file>]"
  echo "    Uses additional private key when using ssh to access the device."
  echo "  [--server-port <port>]"
  echo "    Port number to use when serving the packages.  Defaults to ${FUCHSIA_SERVER_PORT}."
  echo "  [--device-name <device hostname>]"
  echo "    Only serves packages to a device with the given device hostname. Cannot be used with --device-ip."
  echo "  [--device-ip <device ip>]"
  echo "    Only serves packages to a device with the given device ip address. Cannot be used with --device-name."
  echo "  [--kill]"
  echo "    Kills any existing package manager server"
  echo "  [--prepare]"
  echo "    Downloads any dependencies but does not start the package server"
  echo "  [-x] Enable debug."
}

PRIVATE_KEY_FILE=""
PREPARE_ONLY=""
DEVICE_NAME_FILTER="$(get-fuchsia-property device-name)"
DEVICE_IP_ADDR="$(get-fuchsia-property device-ip)"

# Parse command line
while (( "$#" )); do
case $1 in
    --work-dir)
    shift
    FUCHSIA_IMAGE_WORK_DIR="${1:-.}"
    ;;
    --bucket)
    shift
    FUCHSIA_BUCKET="${1}"
    ;;
    --image)
    shift
    IMAGE_NAME="${1}"
    ;;
    --private-key)
    shift
    PRIVATE_KEY_FILE="${1}"
    ;;
    --server-port)
    shift
    FUCHSIA_SERVER_PORT="${1}"
    ;;
    --device-name)
    shift
    DEVICE_NAME_FILTER="${1}"
    ;;
    --device-ip)
    shift
    DEVICE_IP_ADDR="${1}"
    ;;
    --kill)
    kill-running-pm
    exit 0
    ;;
    --prepare)
    PREPARE_ONLY="yes"
    ;;
    -x)
    set -x
    ;;
    *)
    # unknown option
    usage
    exit 1
    ;;
esac
shift
done

if [[ "${DEVICE_IP_ADDR}" != "" && "${DEVICE_NAME_FILTER}" != "" ]]; then
  fx-error "Cannot use both --device-name and --device-ip".
  exit 1
fi

SDK_ID="$(get-sdk-version)"

# The package tarball.  We add the SDK ID to the filename to make them
# unique.
#
# Consider cleaning up old tarballs when getting a new one?
#
if [[ "$IMAGE_NAME" == "" ]]; then
  IMAGES=("$(get-available-images "${SDK_ID}" "${FUCHSIA_BUCKET}")")
  fx-error "IMAGE_NAME not set. Valid images for this SDK version are:" "${IMAGES[*]}."
  exit 1
fi

FUCHSIA_TARGET_PACKAGES=$(get-package-src-path "${SDK_ID}" "${IMAGE_NAME}")
IMAGE_FILENAME="${SDK_ID}_${IMAGE_NAME}.tar.gz"

# Validate the image is found
if [[ ! -f "${FUCHSIA_IMAGE_WORK_DIR}/${IMAGE_FILENAME}" ]] ; then
  if ! run-gsutil ls "${FUCHSIA_TARGET_PACKAGES}"; then
    echo "Packages for ${IMAGE_NAME} not found. Valid images for this SDK version are:"
    IMAGES=("$(get-available-images "${SDK_ID}" "${FUCHSIA_BUCKET}")")
    echo "${IMAGES[*]}."
    exit 2
  fi

  if ! run-gsutil cp "${FUCHSIA_TARGET_PACKAGES}" "${FUCHSIA_IMAGE_WORK_DIR}/${IMAGE_FILENAME}"; then
    fx-error "Could not copy image from ${FUCHSIA_TARGET_PACKAGES} to ${FUCHSIA_IMAGE_WORK_DIR}/${IMAGE_FILENAME}."
    exit 2
  fi
else
  echo "Skipping download, packages tarball exists"
fi

# The checksum file contains the output from `md5`. This is used to detect content
# changes in the packages file.
CHECKSUM_FILE="${FUCHSIA_IMAGE_WORK_DIR}/packages/packages.md5"

# check that any existing contents of the image directory match the intended target device
if [[ -f "${CHECKSUM_FILE}" ]]; then
  if [[ "$(run-md5 "${FUCHSIA_IMAGE_WORK_DIR}/${IMAGE_FILENAME}")" != "$(cat "${CHECKSUM_FILE}")" ]]; then
    fx-warn "Removing old package files."
    if ! rm -f "$(cut -d ' '  -f3 "${CHECKSUM_FILE}")"; then
      fx-error "Could not clean up old image archive."
      exit 2
    fi
    if ! rm -rf "${FUCHSIA_IMAGE_WORK_DIR}/packages"; then
      fx-error "Could not clean up old image."
      exit 2
    fi
  fi
else
  # if the checksum file does not exist, something is inconsistent.
  # so delete the entire directory to make sure we're starting clean.
  # This also happens on a clean run, where the packages directory does not
  # exist.
  if ! rm -rf "${FUCHSIA_IMAGE_WORK_DIR}/packages"; then
    fx-error "Could not clean up old packages."
    exit 2
  fi
fi

if ! mkdir -p "${FUCHSIA_IMAGE_WORK_DIR}/packages"; then
  fx-error "Could not create packages directory."
  exit 2
fi

# if the tarball is not untarred, do it.
if [[ ! -d "${FUCHSIA_IMAGE_WORK_DIR}/packages/amber-files" ]]; then
  if ! tar xzf "${FUCHSIA_IMAGE_WORK_DIR}/${IMAGE_FILENAME}" --directory "${FUCHSIA_IMAGE_WORK_DIR}/packages"; then
    fx-error "Could not extract image from ${FUCHSIA_IMAGE_WORK_DIR}/${IMAGE_FILENAME}."
    exit 1
  fi
  run-md5 "${FUCHSIA_IMAGE_WORK_DIR}/${IMAGE_FILENAME}" > "${CHECKSUM_FILE}"
fi

# Exit out if we only need to prepare the downloads
if [[ "${PREPARE_ONLY}" == "yes" ]]; then
  exit 0
fi

if [[ "${DEVICE_IP_ADDR}" != "" ]]; then
  DEVICE_IP="${DEVICE_IP_ADDR}"
else
  DEVICE_IP=$(get-device-ip-by-name "$FUCHSIA_SDK_PATH" "$DEVICE_NAME_FILTER")
fi

if [[ ! "$?" || -z "$DEVICE_IP" ]]; then
  fx-error "Could not get device IP address"
  exit 2
fi

# get the host address as seen by the device.
ssh_args=("${DEVICE_IP}" echo "\$SSH_CONNECTION")
if ! connection_str="$(ssh-cmd "${ssh_args[@]}")"; then
  fx-error "unable to determine host address as seen from the target.  Is the target up?"
  exit 1
fi
HOST_IP="$(echo "$connection_str" | cut -d ' ' -f 1)"

if [[ ! "$?" || -z "$HOST_IP" ]]; then
  fx-error "Could not get Host IP address"
  exit 2
fi

# A simple heuristic for "is an ipv6 address", URL encase escape
# the address.
if [[ "${HOST_IP}" =~ : ]]; then
  HOST_IP="${HOST_IP//%/%25}"
  HOST_IP="[${HOST_IP}]"
fi

# kill existing pm if present
kill-running-pm

# Start the package server
echo "** Starting package server in the background**"
# `:port` syntax is valid for Go programs that intend to serve on every
# interface on a given port. For example, if $FUCHSIA_SERVER_PORT is 54321,
# this is similar to serving on [::]:54321 or 0.0.0.0:54321.
"${FUCHSIA_SDK_PATH}/tools/pm" serve -repo "${FUCHSIA_IMAGE_WORK_DIR}/packages/amber-files" -l ":${FUCHSIA_SERVER_PORT}"&

SSH_ARGS=()
if [[ "${PRIVATE_KEY_FILE}" != "" ]]; then
  SSH_ARGS+=( "-i" "${PRIVATE_KEY_FILE}")
fi
SSH_ARGS+=( "${DEVICE_IP}" amber_ctl add_src -f "http://${HOST_IP}:${FUCHSIA_SERVER_PORT}/config.json" )

# Update the device to point to the server.
# Because the URL to config.json contains an IPv6 address, the address needs
# to be escaped in square brackets. This is not necessary for the ssh target,
# since that's just an address and not a full URL.
if ! ssh-cmd "${SSH_ARGS[@]}" ; then
  fx-error "Error: could not update device"
  exit 1
fi
