#!/bin/bash
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Helper functions, no environment specific functions should be included below
# this line.

# Force all pipes to return any non-zero error code instead of just the last
set -e -o pipefail

SCRIPT_SRC_DIR="$(cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd)"


DEFAULT_FUCHSIA_BUCKET="fuchsia"
SSH_BIN="$(command -v ssh)"
FUCHSIA_PROPERTY_NAMES=(
  "bucket" # Used as the default for --bucket
  "device-ip" # Used as the default for --device-ip
  "device-name" # Used as the default for --device-name
  "image" # Used as the default for image
  "emu-image" # Used as the default for image when running the emulator.
  "emu-bucket" # Used as the default for bucket when running the emulator.
)

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
# The default is $HOME/.fuchsia. This can be overridden by setting the environment variable
# FUCHSIA_SDK_DATA_DIR.
function get-fuchsia-sdk-data-dir {
  local data_dir="${FUCHSIA_SDK_DATA_DIR:-}"
  if [[ -z "${data_dir}" ]]; then
    data_dir="${HOME}/.fuchsia"
    if [[ ! -d "${data_dir}" ]]; then
      mkdir -p "${data_dir}"
    fi
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

function get-fuchsia-property-names {
  echo "${FUCHSIA_PROPERTY_NAMES[@]}"
}

function is-valid-fuchsia-property {
  [[ "${FUCHSIA_PROPERTY_NAMES[*]}" =~ $1 ]]
}

function set-fuchsia-property {
  local prop_path
  prop_path="$(get-fuchsia-sdk-data-dir)/.properties/$1.txt"
  if ! mkdir -p "$(dirname "${prop_path}")"; then
    fx-error "Cannot write property to $prop_path"
    exit 1
  fi
  echo "$2" > "${prop_path}"
}

function get-fuchsia-property {
  local prop_path
  prop_path="$(get-fuchsia-sdk-data-dir)/.properties/$1.txt"
  if [[ -e "${prop_path}" ]]; then
    cat "${prop_path}"
  else
    echo ""
  fi
}

function ssh-cmd {
  check-fuchsia-ssh-config
  # If the command line passed starts with the -F option,
  # use that config file.
  if [[ "$*" =~ ^-F* ]]; then
    "${SSH_BIN}" "$@"
  else
    "${SSH_BIN}" -F "$(get-fuchsia-sshconfig-file)" "$@"
  fi
}

function get-device-ip {
  # -ipv4 false: Disable IPv4. Fuchsia devices are IPv6-compatible, so
  #   forcing IPv6 allows for easier manipulation of the result.
  local device_addr
  device_addr="$(get-fuchsia-property device-addr)"
  if [[ "${device_addr}" != "" ]]; then
    echo "${device_addr}"
    return 0
  else
    "$(get-fuchsia-sdk-tools-dir)/device-finder" list -device-limit 1 -ipv4=false
  fi
}

function get-device-name {
  # Check for a device name being configured.
  local device_name
  if ! device_name="$(get-fuchsia-property device-name)"; then
    return $?
  fi
  if [[ "${device_name}" != "" ]]; then
    echo "${device_name}"
    return 0
  else
    if device_name="$("$(get-fuchsia-sdk-tools-dir)/device-finder" list -device-limit 1 -full)"; then
      echo "${device_name}"  | cut -d' '  -f2
    fi
  fi
}

function get-device-ip-by-name {
  # Writes the IP address of the device with the given name.
  # If no such device is found, this function returns with a non-zero status
  # code.

  # $1 is the hostname of the Fuchsia device. If $1 is empty, this function
  # returns the IP address of an arbitrarily selected Fuchsia device.

  if [[ "${#}" -eq 1 &&  -n "$1" ]]; then
    # There should typically only be one device that matches the nodename
    # but we add a device-limit to speed up resolution by exiting when the first
    # candidate is found.
    "$(get-fuchsia-sdk-tools-dir)/device-finder" resolve -device-limit 1 -ipv4=false "${1}"
  else
    #shellcheck disable=SC2119
    get-device-ip
  fi
}

function get-host-ip {
  # $1 is the hostname of the Fuchsia device. If $1 is empty, this function
  # returns the IP address of an arbitrarily selected Fuchsia device.
  local DEVICE_NAME
  if [[ "${#}" -eq 1 &&  "${1}" != "" ]]; then
    DEVICE_NAME="${1}"
  else
    DEVICE_NAME="$(get-device-name)"
  fi
  # -ipv4 false: Disable IPv4. Fuchsia devices are IPv6-compatible, so
  #   forcing IPv6 allows for easier manipulation of the result.
  # cut: Remove the IPv6 scope, if present. For link-local addresses, the scope
  #   effectively describes which interface a device is connected on. Since
  #   this information is device-specific (i.e. the Fuchsia device refers to
  #   the development host with a different scope than vice versa), we can
  #   strip this from the IPv6 result. This is reliable as long as the Fuchsia
  #   device only needs link-local networking on one interface.
  "$(get-fuchsia-sdk-tools-dir)/device-finder" resolve -local -ipv4=false "${DEVICE_NAME}" | head -1 | cut -d '%' -f1
}

function get-sdk-version {
  # Get the Fuchsia SDK id
  # $1 is the SDK_PATH, if specified else get-fuchsia-sdk-dir value is used.
  local FUCHSIA_SDK_METADATA="${1-$(get-fuchsia-sdk-dir)}/meta/manifest.json"
  if ! SDK_VERSION="$(grep \"id\": "${FUCHSIA_SDK_METADATA}" | cut -d\" -f4)"; then
    return 1
  fi
  echo "${SDK_VERSION}"
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
    return 1
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
    return 1
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
  GSURL="gs://${BUCKET}/development/${1}/images"
  if ! RESULTS=$(run-gsutil "ls" "${GSURL}" | cut -d/ -f7 | tr '\n' ' '); then
    return 1
  fi
  if [[ "${RESULTS}" == "" ]]; then
    return 2
  fi
  for f in ${RESULTS}; do
    IMAGES+=("${f%.*}")
  done
  if [[ "${BUCKET}" != "${DEFAULT_FUCHSIA_BUCKET}" ]]; then
    echo -n "${IMAGES[*]} "
    get-available-images "${1}" "${DEFAULT_FUCHSIA_BUCKET}"
  else
    echo "${IMAGES[*]}"
  fi
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

function _move_legacy_key {
  # Check for legacy GN SDK key and copy it to the new location.
  local legacy_ssh_dir
  legacy_ssh_dir="$(get-fuchsia-sdk-data-dir)/.ssh"
  local legacy_keyfile="${legacy_ssh_dir}/pkey"
  local legacy_authfile="${legacy_ssh_dir}/authorized_keys"
  local ssh_dir="${HOME}/.ssh"
  local keyfile="${ssh_dir}/fuchsia_ed25519"
  local authfile="${ssh_dir}/fuchsia_authorized_keys"

  if [[ -f "${legacy_keyfile}" ]]; then
    fx-warn "Migrating existing key to ${keyfile}"
    mv "${legacy_keyfile}" "${keyfile}"
    # only move authorized_keys if the private key exists.
    if [[ -f "${legacy_authfile}" ]]; then
      mv "${legacy_authfile}" "${authfile}"
    fi
    rm -f  "${legacy_keyfile}.pub"
  fi
  return 0
}


function check-fuchsia-ssh-config {
  # This function creates the ssh keys needed to
  # work with devices running Fuchsia. There are two parts, the keys and the config.
  #
  # There is a key for Fuchsia that is placed in a well-known location so that applications
  # which need to access the Fuchsia device can all use the same key. This is stored in
  # ${HOME}/.ssh/fuchsia_ed25519.
  #
  # The authorized key file used for paving is in ${HOME}/.ssh/fuchsia_authorized_keys.
  # The private key used when ssh'ing to the device is in ${HOME}/.ssh/fuchsia_ed25519.
  #
  #
  # The second part of is the sshconfig file used by the SDK when using SSH.
  # This is stored in the Fuchsia SDK data directory named sshconfig.
  # This script checks for the private key file being referenced in the sshconfig and
  # the matching version tag. If they are not present, the sshconfig file is regenerated.
  # The ssh configuration should not be modified.
  local SSHCONFIG_TAG="Fuchsia SDK config version 5 tag"

  if [[ ! -d "${HOME}" ]]; then
    fx-error "\$HOME must be set to use these commands."
    return 2
  fi

  local ssh_dir="${HOME}/.ssh"
  local authfile="${ssh_dir}/fuchsia_authorized_keys"
  local keyfile="${ssh_dir}/fuchsia_ed25519"
  local sshconfig_file
  sshconfig_file="$(get-fuchsia-sdk-data-dir)/sshconfig"

  # If the public and private key pair exist, and the sshconfig
  # file is up to date, then our work here is done, return success.
  if [[ -e "${authfile}" && -e "${keyfile}" ]]; then
    if grep "${keyfile}" "${sshconfig_file}" > /dev/null 2>&1; then
      if grep "${SSHCONFIG_TAG}" "${sshconfig_file}" > /dev/null 2>&1; then
        return 0
      fi
    fi
  fi

  mkdir -p "${ssh_dir}"

  # Check to migrate keys from old location
  if [[ ! -f "${authfile}" ||  ! -f "${keyfile}" ]]; then
      if ! _move_legacy_key; then
        return 1
      fi
  fi

  if [[ ! -f "${authfile}" ]]; then
    if [[ ! -f "${keyfile}" ]]; then
      # If ${USER} or hostname -f is not available, provide nouser or nohostname defaults
      hostname="$(hostname -f 2> /dev/null || echo "nohostname")"
      if ! result="$(ssh-keygen -P "" -t ed25519 -f "${keyfile}" -C "${USER-nouser}@${hostname} Generated by Fuchsia GN SDK" 2>&1)"; then
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
# ${SSHCONFIG_TAG}
# Configure port 8022 for connecting to a device with the local address.
# This makes it possible to forward 8022 to a device connected remotely.
# The fuchsia private key is used for the identity.
Host 127.0.0.1
  Port 8022

Host ::1
  Port 8022

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

# When expanded, the ControlPath below cannot have more than 90 characters
# (total of 108 minus 18 used by a random suffix added by ssh).
# '%C' expands to 40 chars and there are 9 fixed chars, so '~' can expand to
# up to 41 chars, which is a reasonable limit for a user's home in most
# situations. If '~' expands to more than 41 chars, the ssh connection
# will fail with an error like:
#     unix_listener: path "..." too long for Unix domain socket
# A possible solution is to use /tmp instead of ~, but it has
# its own security concerns.
ControlPath=~/.ssh/fx-%C

# Connect with user, use the identity specified.
User fuchsia
IdentitiesOnly yes
IdentityFile "${keyfile}"
GSSAPIDelegateCredentials no

EOF

  return 0
}

function get-fuchsia-auth-keys-file {
  check-fuchsia-ssh-config
  echo "${HOME}/.ssh/fuchsia_authorized_keys"
}

function get-fuchsia-sshconfig-file {
  check-fuchsia-ssh-config
  echo "$(get-fuchsia-sdk-data-dir)/sshconfig"
}

function get-fuchsia-sdk-tools-dir {
  local machine
  machine="$(uname -m)"
  local dir
  case "${machine}" in
  x86_64)
    dir="$(get-fuchsia-sdk-dir)/tools/x64"
    ;;
  aarch64*)
    dir="$(get-fuchsia-sdk-dir)/tools/arm64"
    ;;
  armv8*)
    dir="$(get-fuchsia-sdk-dir)/tools/arm64"
    ;;
  *)
    dir="$(get-fuchsia-sdk-dir)/tools/${machine}"
    ;;
  esac

  echo "${dir}"
}
