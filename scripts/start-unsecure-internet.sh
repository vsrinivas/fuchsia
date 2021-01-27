#!/bin/bash
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# This is an upscript to use with qemu. It creates a bridge device that
# connects to the qemu network interfaces and sets up network forwarding.
# The emulator is exposed to the internet, and there is no security
# implemented, so should be used with care.
#
# Example usage with the Fuchsia emulator:
# The -I can be any device name, this script creates br${INTERFACE}.
# fx emu --headless -N -I qemu -u ./start-unsecure-internet.sh
#
# This script is designed to be invoked from qemu with an interface argument.
# It can also be run directly for testing as:
# scripts/start-unsecure-internet.sh <interface>
#
# If a --cleanup argument is provided instead of <interface>, this
# script will remove the previously created bridge device.

usage() {
  echo "Usage: $(basename "$0") <interface>"
  echo "  [--cleanup]"
  echo "    Remove previously created bridge device and stop"
  echo "  <interface>"
  echo "    Interface name provided to fx emu -I, typically 'qemu'"
}


# Check that we do not run this script in unsupported environments
if [[ "$(lsb_release --release --short)" == "rodete" ]]; then
  echo "This script cannot be run on rodete"
  exit 1
fi
if [[ "$(hostname --fqdn)" == *".google.com" ]]; then
  echo "This script cannot be run on *.google.com"
  exit 1
fi


CLEANUP=0
if [[ "$1" == "--cleanup" ]]; then
  CLEANUP=1
  shift
fi
if [[ $1 == -* ]]; then
  echo "Unknown argument provided"
  usage
  exit 1
fi
if [[ "$2" != "" ]]; then
  echo "Unexpected arguments provided"
  echo
  usage
  exit 1
fi

INTERFACE="$1"
if [[ "${INTERFACE}" == "" ]]; then
  echo "No network interface name provided by qemu -u"
  echo
  usage
  exit 1
fi
BRIDGE="br${INTERFACE}"

# Enable error checking for all commands
err_print() {
  echo "Error at $1"
  stty sane
}
trap 'err_print $0:$LINENO' ERR
set -eu

# Show every command being executed
set -x

# qemu has already brought up ${INTERFACE}, but delete any previous ${BRIDGE} to start from a known state.
# If any other interface is using the 172.16.243.1 address, everything will fail silently.
sudo ip link delete "${BRIDGE}" || echo "No existing bridge ${BRIDGE} to delete"
sudo iptables -D POSTROUTING -t nat -s 172.16.243.0/24 ! -d 172.16.243.0/24 -j MASQUERADE || echo "No existing POSTROUTING nat rule to delete"
for delbridge in $(ip --oneline address show to "172.16.243.1" | awk '{ print $2 }'); do
  sudo ip link delete "${delbridge}"
done

# Exit out if the purpose was just to clean up
if (( CLEANUP )); then
  exit 0
fi

# Enable packet forwarding
sudo sysctl -w net.ipv4.ip_forward=1
CHECK="$(cat /proc/sys/net/ipv4/ip_forward)"
if [[ "${CHECK}" != "1" ]]; then
  echo "ERROR: Could not enable ip_forward"
  exit 1
fi

# Create bridge with DHCP and default gateway.
sudo ip link add name "${BRIDGE}" type bridge
sudo ip addr add 172.16.243.1/24 dev "${BRIDGE}"

# UFW blocks DHCP/BOOTP and bridge traffic, so open this up if installed
if (command -v ufw && grep -q "^ENABLED=yes" /etc/ufw/ufw.conf) >/dev/null 2>&1; then
  sudo ufw allow proto udp from 172.16.243.0/24 to 172.16.243.0/24 port 67,68 comment 'Fuchsia qemu DHCP'
  sudo ufw allow dns comment 'Fuchsia qemu DNS'
  sudo ufw allow proto udp from 0.0.0.0 to 255.255.255.255 port 67 comment 'Fuchsia qemu DHCP'
  sudo ufw allow proto udp from 172.16.243.1 to 255.255.255.255 port 68 comment 'Fuchsia qemu DHCP'
  sudo ufw route allow in on "${BRIDGE}" comment 'Fuchsia qemu bridge in'
  sudo ufw route allow out on "${BRIDGE}" comment 'Fuchsia qemu bridge out'
fi

# Remove any previous dnsmasq attached to the IP range we use
# Searching for the ${BRIDGE} is another alternative, but it may be different
DNSMASQ_PID=$(ps -axww | grep dnsmasq | grep "\-\-dhcp-range=172\.16\.243\.2," | awk '{ print $1 }')
if [[ "${DNSMASQ_PID}" != "" ]]; then
  sudo kill "${DNSMASQ_PID}"
fi

# I've noticed that --listen-interface= is needed in some situations previously, but doesn't seem to be needed here
sudo dnsmasq --interface="${BRIDGE}" --bind-interfaces --dhcp-range=172.16.243.2,172.16.243.254 --except-interface=lo || { echo "Failed to start dnsmasq"; exit 1; }
sudo ip link set "${BRIDGE}" up

# Remove pre-existing addresses from the interface as they'll no longer be routable
sudo ip addr flush "${INTERFACE}"

# Add qemu to the bridge
sudo ip link set "${INTERFACE}" master "${BRIDGE}"

# NAT packets arriving to the bridge
sudo iptables -A POSTROUTING -t nat -s 172.16.243.0/24 ! -d 172.16.243.0/24 -j MASQUERADE
