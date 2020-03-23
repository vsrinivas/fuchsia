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
  check-ssh-config
  "${SSH_BIN}" -F "$(get-fuchsia-sshconfig-file)" "$@"
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

  if [[ "${#}" -ge 2 &&  -n "$2" ]]; then
    # There should typically only be one device that matches the nodename
    # but we add a device-limit to speed up resolution by exiting when the first
    # candidate is found.
    "${1-$(get-fuchsia-sdk-dir)}/tools/device-finder" resolve -device-limit 1 -ipv4=false -netboot "${2}"
  else
    if [[ "${#}" -ge 1 && -n "$1" ]]; then
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
  if [[ "${#}" -ge 2 &&  "${2}" != "" ]]; then
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

  # Prevent gsutil prompting for updates by making stdin not a TTY
  "${GSUTIL_BIN}" "$@" < /dev/null
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

function check-ssh-config {
  # This function creates the ssh keys needed to
  # work with devices running Fuchsia. There are two parts, the keys and the config.
  #
  # The keys are stored in the Fuchsia SDK data directory in a directory named .ssh.
  # This is the same structure as used "in-tree" for Fuchsia development. You can copy the
  # keys from the other directory to make the keys identical, allowing SSH access using both
  # SDK commands and in-tree "fx" commands.
  #
  # The authorized key file used for paving is in .ssh/authorized_keys.
  # The private key used when ssh'ing to the device is in .ssh/pkey.
  #
  #
  # The second part of is the sshconfig file used by the SDK when using SSH.
  # This is stored in the Fuchsia SDK data directory named sshconfig.
  # This script checks for the private key file being referenced in the sshconfig, if it
  # is not present, the sshconfig file is regenerated.
  # You can customize the sshconfig and it will not be overwritten unless the full path to the
  # private key is not present. If you want to remove it as part of customization, you can comment out
  # the IdentityFile line with the `#` character.

  local ssh_dir
  ssh_dir="$(get-fuchsia-sdk-data-dir)/.ssh"
  local authfile="${ssh_dir}/authorized_keys"
  local keyfile="${ssh_dir}/pkey"
  local sshconfig_file
  sshconfig_file="$(get-fuchsia-sshconfig-file)"

  if [[ -e "${authfile}" && -e "${keyfile}" ]]; then
   if grep "${keyfile}" "${sshconfig_file}" > /dev/null 2>&1; then
    return 0
   fi
  fi

  mkdir -p "${ssh_dir}"
  if [[ ! -f "${authfile}" ]]; then
    if [[ ! -f "${keyfile}" ]]; then
      if ! result="$(ssh-keygen -P "" -t ed25519 -f "${keyfile}" -C "${USER}@$(hostname -f)" 2>&1)"; then
        fx-error "${result}"
        return 1
      fi
    fi
    if ! result="$(ssh-keygen -y -f "${keyfile}" > "${authfile}" 2>&1)"; then
      fx-error "${result}"
      return 1
    fi
  fi

  cat >"${sshconfig_file}" <<EOF
Host *

# Turn off refusing to connect to hosts whose key has changed
StrictHostKeyChecking no
CheckHostIP no

# Disable recording the known hosts
UserKnownHostsFile=/dev/null

# Do not forward auth agent connection to remote, no X11
ForwardAgent no
ForwardX11 no

# Connection timeout in seconds
ConnectTimeout=10

# Check for server alive in seconds, max count before disconnecting
ServerAliveInterval 1
ServerAliveCountMax 10

# Try to keep the master connection open to speed reconnecting.
ControlMaster auto
ControlPersist yes
ControlPath=/tmp/fuchsia--%r@%h:%p

# Connect with user, use the identity specified.
User fuchsia
IdentityFile "${keyfile}"
EOF

  return 0
}

function get-fuchsia-auth-keys-file {
  echo "$(get-fuchsia-sdk-data-dir)/.ssh/authorized_keys"
}

function get-fuchsia-sshconfig-file {
  echo "$(get-fuchsia-sdk-data-dir)/sshconfig"
}
