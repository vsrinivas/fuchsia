#!/usr/bin/env bash
#
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -euxo pipefail

declare -r IPV4_ADDR="192.168.1.1"
declare -r IPV4_SUBNET_LENGTH="24"
declare -r DHCP_CONFIG_FILE="/etc/default/isc-dhcp-server"

# The ethernet interface's name varies depending on the number of block devices
# that are added when starting the guest.  Discover the interface name prior to
# configuration.
iface_name=""

for word in $(netstat -i); do
  if [[ "${word}" =~ "enp0s" ]]; then
    iface_name="${word}"
    break
  fi
done

if [[ -z "${iface_name}" ]]; then
  echo "Unable to find an ethernet interface"
  exit 1
fi

# Write the config to tell isc-dhcp-server what interfaces to serve DHCP on.
echo "INTERFACESv4=\"${iface_name}\"" > "${DHCP_CONFIG_FILE}"

# The ethernet interface needs IP addresses or else isc-dhcp-server will not
# start.
ifconfig "${iface_name}" "${IPV4_ADDR}/${IPV4_SUBNET_LENGTH}" up

# netemul will put all of the dhcpd configs in place as a pre-test step.
service isc-dhcp-server start
