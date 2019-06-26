#!/bin/bash
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

fx-config-read
source "${FUCHSIA_DIR}/buildtools/vars.sh"

if ! which socat > /dev/null 2>&1; then
  fx-error "The command \`socat\` was not found!"
  if [[ "$(uname)" == "Linux" ]]; then
    fx-error "  maybe \`apt install socat\`"
  else
    fx-error "  maybe \`brew install socat\`"
  fi
  exit 1
fi

# Arguments: None.
#
#   None.
#
# Returns:
#
#   Sets the DEVICE variable with the found device. Exits otherwise.
get_serial_device() {
  # We get the list of devices
  devices=()
  for device in /dev/ttyUSB*; do
    devices+=("${device}")
  done

  # If there is only one device, we set that one as the default.
  if [[ "${#devices[@]}" == "1" ]]; then
    echo "Found unique serial device in ${devices[0]}"
    DEVICE="${devices[0]}"
  else
    # There are many options, we make the user select.
    if [[ -z "$DEVICE" ]]; then
      echo >&2 "Select a serial device from the following list:"
      select device in "${devices[@]}"; do
        DEVICE="$device"
        break
      done
    fi
  fi

  # Verify that the device found is a valid one.
  if [[ ! -e "$DEVICE" ]]; then
    fx-error "device not found"
    exit 1
  fi

  if [[ ! -r "$DEVICE" ]]; then
    if [[ "$(uname)" == "Linux" ]]; then
      owninggroup=$(stat "$DEVICE" --printf="%G")
      if [[ ! -r "$DEVICE" ]]; then
        fx-error "$DEVICE is not readable by $USER"
        fx-error " fix: sudo usermod -a -G "$owninggroup" $USER"
        fx-error "You need to start a new login session for a group change to take effect"
        exit 1
      fi
    else
      fx-warn "$DEVICE is not readable by $USER"
      fx-warn "Fix the permissions on $DEVICE or group membership of $USER"
    fi
  fi
}

# Arguments:
#
#   $1: device to connect to.
#
# Returns:
#
#   0 if the device is unavailable. 1 otherwise.
serial_port_unavailable() {
  lsof "${device}" > /dev/null 2>&1
  if [[ "$?" == "1" ]]; then
    return 1
  else
    return 0
  fi
}

# Arguments:
#
#   $1: Device to connect to.
#   $2: The file to pipe it to.
#   $@: (Rest) the arguments to pipe into.
#
# Example:
#
#   send_serial_command /dev/ttyUSB0 /tmp/result_file "run bugreport.cmx"
#
# Returns:
#
#   None. Check error with command result ($?).
send_serial_command() {
  local device="$1"
  local result_file="$2"
  local cmd="${@:3}"

  echo "${cmd}" | socat - file:${device},b115200,ospeed=115200,ispeed=115200,cs8,clocal=0,cstopb=0,parenb=0,nonblock=1,raw,echo=0 | pv > "${result_file}"
}
