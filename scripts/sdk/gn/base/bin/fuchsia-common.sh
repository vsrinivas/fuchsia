#!/bin/bash
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Helper functions, no environment specific functions should be included below
# this line.

SCRIPT_SRC_DIR="$(cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd)"
DEFAULT_FUCHSIA_BUCKET="fuchsia"
SSH_BIN="$(command -v ssh)"

function is-mac {
  [[ "$(uname -s)" == "Darwin" ]] && return 0
  return 1
}

# Add Mac specific support
if is-mac; then
  # Fuchsia mac functions.

  realpath() {
      [[ $1 = /* ]] && echo "$1" || echo "$PWD/${1#./}"
  }
fi
# Returns the fuchsia sdk root dir. Assuming this script is in ${FUCHSIA_SDK}/bin.
function get-fuchsia-sdk-dir {
  dirname "${SCRIPT_SRC_DIR}"
}

# Returns the data directory for the fuchsia sdk.
# This directory is expected to be per-developer, not checked into source code revision systems,
# and is used to store device images and packages and related data items.
function get-fuchsia-sdk-data-dir {
  local data_dir
  data_dir="$(get-fuchsia-sdk-dir)/images"
  if [[ -d "${data_dir}" ]]; then
    mkdir -p "${data_dir}"
  fi
  echo "${data_dir}" 
}

# fx-warn prints a line to stderr with a yellow WARNING: prefix.
function fx-warn {
  if [[ -t 2 ]]; then
    echo -e >&2 "\033[1;33mWARNING:\033[0m $*"
  else
    echo -e >&2 "WARNING: $*"
  fi
}

# fx-error prints a line to stderr with a red ERROR: prefix.
function fx-error {
  if [[ -t 2 ]]; then
    echo -e >&2 "\033[1;31mERROR:\033[0m $*"
  else
    echo -e >&2 "ERROR: $*"
  fi
}

function ssh-cmd {
  "${SSH_BIN}" -F "${SCRIPT_SRC_DIR}/sshconfig" "$@"
}

function get-device-ip {
  # $1 is the SDK_PATH (optional. defaults to get-fuchsia-sdk-dir)
  # -ipv4 false: Disable IPv4. Fuchsia devices are IPv6-compatible, so
  #   forcing IPv6 allows for easier manipulation of the result.
  "${1-$(get-fuchsia-sdk-dir)}/tools/device-finder" list -netboot -device-limit 1 -ipv4=false
}

function get-device-name {
  # $1 is the SDK_PATH (optional. defaults to get-fuchsia-sdk-dir)
  "${1-$(get-fuchsia-sdk-dir)}/tools/device-finder" list -netboot -device-limit 1 -full | cut -d\  -f2
}

function get-device-ip-by-name {
  # Writes the IP address of the device with the given name.
  # If no such device is found, this function returns with a non-zero status
  # code.

  # $1 is the SDK_PATH, if specified else get-fuchsia-sdk-dir value is used.
  # $2 is the hostname of the Fuchsia device. If $2 is empty, this function
  # returns the IP address of an arbitrarily selected Fuchsia device.

  if [[ -n "$2" ]]; then
    # There should typically only be one device that matches the nodename
    # but we add a device-limit to speed up resolution by exiting when the first
    # candidate is found.
    "${1-$(get-fuchsia-sdk-dir)}/tools/device-finder" resolve -device-limit 1 -ipv4=false -netboot "${2}"
  else
    if [[ -n "$1" ]]; then
      get-device-ip "$1"
    else
      get-device-ip
    fi
  fi
}

function get-host-ip {
  # $1 is the SDK_PATH, if specified else get-fuchsia-sdk-dir value is used.
  # $2 is the hostname of the Fuchsia device. If $2 is empty, this function
  # returns the IP address of an arbitrarily selected Fuchsia device.
  local DEVICE_NAME
  if [[ "${2}" != "" ]]; then
    DEVICE_NAME="${2}"
  else
    DEVICE_NAME="$(get-device-name "${1-$(get-fuchsia-sdk-dir)}")"
  fi
  # -ipv4 false: Disable IPv4. Fuchsia devices are IPv6-compatible, so
  #   forcing IPv6 allows for easier manipulation of the result.
  # cut: Remove the IPv6 scope, if present. For link-local addresses, the scope
  #   effectively describes which interface a device is connected on. Since
  #   this information is device-specific (i.e. the Fuchsia device refers to
  #   the development host with a different scope than vice versa), we can
  #   strip this from the IPv6 result. This is reliable as long as the Fuchsia
  #   device only needs link-local networking on one interface.
  "${1-$(get-fuchsia-sdk-dir)}/tools/device-finder" resolve -local -ipv4=false "${DEVICE_NAME}" | head -1 | cut -d '%' -f1
}

function get-sdk-version {
# Get the Fuchsia SDK id
 # $1 is the SDK_PATH, if specified else get-fuchsia-sdk-dir value is used.
 local FUCHSIA_SDK_METADATA="${1-$(get-fuchsia-sdk-dir)}/meta/manifest.json"
  grep \"id\": "${FUCHSIA_SDK_METADATA}" | cut -d\" -f4
}

function get-package-src-path {
  # $1 is the SDK ID.  See #get-sdk-version.
  # $2 is the image name.
  echo "gs://${FUCHSIA_BUCKET}/development/${1}/packages/${2}.tar.gz"
}

function get-image-src-path {
  # $1 is the SDK ID.  See #get-sdk-version.
  # $2 is the image name.
  echo "gs://${FUCHSIA_BUCKET}/development/${1}/images/${2}.tgz"
}

# Run gsutil from the directory of this script if it exists, otherwise
# use the path.
function run-gsutil {
  GSUTIL_BIN="${SCRIPT_SRC_DIR}/gsutil"
  if [[ ! -e "${GSUTIL_BIN}" ]]; then
    GSUTIL_BIN="$(command -v gsutil)"
  fi
  if [[ ! -e "${GSUTIL_BIN}" ]]; then
    GSUTIL_BIN="$(command -v gsutil.py)"
  fi


  if [[ "${GSUTIL_BIN}" == "" ]]; then
    fx-error "Cannot find gsutil."
    exit 2
  fi
  "${GSUTIL_BIN}" "$@"
}

# Run cipd from the directory of this script if it exists, otherwise
# use the path.
function run-cipd {
  CIPD_BIN="${SCRIPT_SRC_DIR}/cipd"
  if [[ ! -e "${CIPD_BIN}" ]]; then
    CIPD_BIN="$(command -v cipd)"
  fi

  if [[ "${CIPD_BIN}" == "" ]]; then
    fx-error "Cannot find cipd."
    exit 2
  fi
  "${CIPD_BIN}" "$@"
}

# Runs md5sum or equivalent on mac.
function run-md5 {
  if is-mac; then
    MD5_CMD=("/sbin/md5"  "-r")
  else
    MD5_CMD=("md5sum")
  fi

  MD5_CMD+=("$@")

  "${MD5_CMD[@]}"
}

function get-available-images {
  # $1 is the SDK ID.
  # $2 is the bucket, or uses the default.
  local IMAGES=()
  local BUCKET=""

  BUCKET="${2:-${DEFAULT_FUCHSIA_BUCKET}}"

  for f in $(run-gsutil "ls" "gs://${BUCKET}/development/${1}/images" | cut -d/ -f7 | tr '\n' ' ')
  do
    IMAGES+=("${f%.*}")
  done
  if [[ "${BUCKET}" != "${DEFAULT_FUCHSIA_BUCKET}" ]]; then
      for f in $(run-gsutil "ls" "gs://${DEFAULT_FUCHSIA_BUCKET}/development/${1}/images" | cut -d/ -f7 | tr '\n' ' ')
      do
        IMAGES+=("${f%.*}")
      done
  fi
  echo "${IMAGES[@]}"
}

function kill-running-pm {
  local PM_PROCESS=()
  IFS=" " read -r -a PM_PROCESS <<< "$(pgrep -ax pm)"
  if [[  ${#PM_PROCESS[@]} -gt 0 && -n "${PM_PROCESS[*]}" ]]; then
    # mac only provides the pid, not the name
    if is-mac; then
      fx-warn "Killing existing pm process"
      kill "${PM_PROCESS[0]}"
      return $?
    elif [[ ${#PM_PROCESS[@]} -gt 1 &&  "${PM_PROCESS[1]}" == *"tools/pm" ]]; then
      fx-warn "Killing existing pm process"
      kill "${PM_PROCESS[0]}"
      return $?
    fi
  else
    fx-warn "existing pm process not found"
  fi
  return 0
}
