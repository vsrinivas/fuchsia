#!/bin/bash
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -euxo pipefail

declare -r IFACE_MAC="02:1a:11:00:01:02"
declare -r IPV4_ADDR="10.0.0.2"
declare -r DEFAULT_GATEWAY="10.0.0.1"
declare -r IPV4_SUBNET_LENGTH="24"
declare -r DHCP_CONFIG_FILE="/etc/default/isc-dhcp-server"

# The ethernet interface's name varies depending on the number of block devices
# that are added when starting the guest.  Discover the interface name prior to
# configuration.
iface_name=""

for iface in $(ls -1 /sys/class/net); do
  iface_mac=$(cat "/sys/class/net/${iface}/address")
  if [[ "${iface_mac}" == "${iface_mac}" ]]; then
    iface_name="${iface}"
    break
  fi
done

if [[ -z "${iface_name}" ]]; then
  echo "Unable to find an ethernet interface"
  exit 1
fi

# Set the IP address and bring the interface up.
ifconfig "${iface_name}" "${IPV4_ADDR}/${IPV4_SUBNET_LENGTH}" up
route add default gw "${DEFAULT_GATEWAY}"

