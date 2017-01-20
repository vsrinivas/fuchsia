#!/bin/bash
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -eo pipefail; [[ "${TRACE}" ]] && set -x

if [[ `uname -s` -ne Darwin ]]
then
  echo '$0 only supports macOS right now.'
  exit 1
fi

INTERFACE=$1
SUBNET_PREFIX=${SUBNET_PREFIX:-192.168.3}

if [[ -z "$INTERFACE" ]]
then
  echo "Missing interface name."
  exit 1
fi

if [[ ! "$SUBNET_PREFIX" =~ ^[0-9]+\.[0-9]+\.[0-9]$ ]]
then
  echo "Invalid SUBNET_PREFIX '$SUBNET_PREFIX'. Must be in the form '1.2.3'"
  exit 1
fi

# Check if dnsmasq is running.
DNSMASQ_PID=`ps -Ac -o pid,comm | awk '/^ *[0-9]+ dnsmasq$/ {print $1}'`

# Calculate the link-local IPv6 address for $INTERFACE
function calc_link_local() {
    local mac
    IFS=':' read -ra mac <<< "$1"
    mac[0]=$(printf "%x\n" "$((0x${mac[0]} ^ 0x2))")
    echo fe80::${mac[0]}${mac[1]}:${mac[2]}ff:fe${mac[3]}:${mac[4]}${mac[5]}
}
MAC=`ifconfig $INTERFACE | awk '/ether/ {print $2}'`
LINK_LOCAL=`calc_link_local $MAC`

# Bring up the network.
echo "Bringing up the network interface: $INTERFACE"
sudo ifconfig $INTERFACE inet ${SUBNET_PREFIX}.1
sudo ifconfig $INTERFACE inet6 $LINK_LOCAL
if [[ -n "$DNSMASQ_PID" ]]
then
  echo "Killing the old dnsmasq (pid: $DNSMASQ_PID)..."
  sudo kill $DNSMASQ_PID
fi

# Look for dnsmasq in the path and then homebrew.
DNSMASQ_PATH=$(which dnsmasq) || \
  DNSMASQ_PATH=$(brew --prefix)/sbin/dnsmasq && test -x $DNSMASQ_PATH || \
  (
    echo dnsmasq not found. Install it from homebrew.
    exit 1
  )
echo Starting dnsmasq...
sudo $DNSMASQ_PATH --interface=$INTERFACE \
  --dhcp-range=$INTERFACE,${SUBNET_PREFIX}.50,${SUBNET_PREFIX}.150,24h \
  --dhcp-leasefile=/tmp/fuchsia-dhcp-leases.$INTERFACE \
  --listen-address=${SUBNET_PREFIX}.1

echo "Enable IP forwarding..."
DEFAULT_INTERFACE=`route -n get default | grep interface | awk '{print $2}'` || true
if [[ -z "$DEFAULT_INTERFACE" ]]
then
  echo "No default route, not enabling forwarding."
else
  sudo sysctl -q net.inet.ip.forwarding=1
  echo "
  nat on $DEFAULT_INTERFACE from ${SUBNET_PREFIX}.0/24 to any -> ($DEFAULT_INTERFACE)
  pass out on $DEFAULT_INTERFACE inet from ${SUBNET_PREFIX}.0/24 to any
  " | sudo pfctl -q -ef - >& /dev/null || true
fi
