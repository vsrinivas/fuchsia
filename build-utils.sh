#!/bin/bash

# Copyright 2016 Google Inc. All Rights Reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# This script produces a bootable USB drive, designed to boot using verified
# boot.

set -e
shopt -s extglob

# Convenience function for ending the script with some output.
trap "exit 99" USR2
TOP_PID=$$

function die() {
  echo "$*" >& 2
  kill -USR2 $TOP_PID
}

# Function to attempt unmounting a mount point up to three times, sleeping
# a couple of seconds between attempts.
function umount_retry() {
  set +e
  TRIES=0
  while (! sudo umount $1); do
    ((TRIES++))
    [[ ${TRIES} > 2 ]] && die "Unable to umount $0"
    sleep 2
  done
  set -e
}

is_usb() {
  if [ -n "$(type -path udevadm)" ]; then
    udevadm info --query=all --name="$1" | grep -q ID_BUS=usb
   else
    # For a usb device on the pixel2 we expect to see something like:
    # /sys/devices/pci0000:00/0000:00:14.0/usb2/2-2/2-2:1.0/host5/target5:0:0/5:0:0:0
    (cd /sys/block/$(basename "$1")/device 2>/dev/null && pwd -P) | grep -q usb
  fi
}


if [[ -z "${SCRIPT_DIR}" ]]; then
  die "SCRIPT_DIR variable not set or empty"
fi

# Absolute path of where this script is stored.
PATH="${SCRIPT_DIR}/../buildtools/toolchain/:${PATH}"
VB_DIR="${SCRIPT_DIR}/../third_party/vboot_reference"
DC_DIR="${SCRIPT_DIR}/../third_party/depthcharge"
MX_DIR="${SCRIPT_DIR}/../magenta"
