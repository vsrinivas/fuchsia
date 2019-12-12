#!/bin/bash
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -euxo pipefail

declare -r LINUX_LINK_MAC="02:1a:11:00:01:00"
declare -r LINUX_LINK_IPV4_ADDR="10.0.0.1"
declare -r FUCHSIA_LINK_MAC="02:1a:11:00:01:01"
declare -r FUCHSIA_LINK_IPV4_ADDR="192.168.0.1"
declare -r IPV4_SUBNET_LENGTH="24"

# The ethernet interface's name varies depending on the number of block devices
# that are added when starting the guest.  Discover the interface name prior to
# configuration.
linux_iface_set=false
fuchsia_iface_set=false

for iface in $(ls -1 /sys/class/net); do
  iface_mac=$(cat "/sys/class/net/${iface}/address")
  if [[ "${iface_mac}" == "${LINUX_LINK_MAC}" ]]; then
    ifconfig "${iface}" "${LINUX_LINK_IPV4_ADDR}/${IPV4_SUBNET_LENGTH}" up
    linux_iface_set=true
  elif [[ "${iface_mac}" == "${FUCHSIA_LINK_MAC}" ]]; then
    ifconfig "${iface}" "${FUCHSIA_LINK_IPV4_ADDR}/${IPV4_SUBNET_LENGTH}" up
    fuchsia_iface_set=true
  fi
done

if [[ "${linux_iface_set}" != true ]] || [[ "${fuchsia_iface_set}" != true ]]; then
  echo "Linux interface configured: ${linux_iface_set}"
  echo "Fuchsia interface configured: ${fuchsia_iface_set}"
  exit 1
fi

# Enable IPv4 forwarding.
sysctl -w net.ipv4.ip_forward=1
