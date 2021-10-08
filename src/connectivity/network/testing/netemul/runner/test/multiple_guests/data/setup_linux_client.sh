#!/usr/bin/env bash
#
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -euxo pipefail

declare -r IFACE_MAC="02:1a:11:00:01:02"
declare -r IPV4_ADDR="10.0.0.2"
declare -r DEFAULT_GATEWAY="10.0.0.1"
declare -r IPV4_SUBNET_LENGTH="24"

# The ethernet interface's name varies depending on the number of block devices
# that are added when starting the guest.  Discover the interface name prior to
# configuration.
iface_set=false

for dir in /sys/class/net/*; do
  iface_mac=$(cat "${dir}/address")
  iface=$(basename "${dir}")
  if [[ "${iface_mac}" == "${IFACE_MAC}" ]]; then
    ifconfig "${iface}" "${IPV4_ADDR}/${IPV4_SUBNET_LENGTH}"
    ifconfig "${iface}" up
    route add default gw "${DEFAULT_GATEWAY}"
    iface_set=true
    break
  fi
done

if [[ "${iface_set}" != true ]]; then
  echo "Unable to find an ethernet interface"
  exit 1
fi
