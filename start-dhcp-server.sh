#!/bin/bash
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# This script takes a network interface (eg: tap0) as its only argument. It sets
# up that interface for running a Fuchsia device. It runs dnsmasq to provide
# DHCP and DNS to the Fuchsia device. It configures NAT. It will do its best to
# kill old instances of dnsmasq from previous runs of the script for this
# interface.
#
# It can be passed with -u as a start-up script to run-zircon-* or frun to
# bring up a network for a new qemu instance
#
# If the environment variable FUCHSIA_IP is set it will give that IP to the
# Fuchsia device, otherwise, for historical reasons it will allocate
# 192.168.3.53.

set -eo pipefail; [[ "$TRACE" ]] && set -x

INTERFACE=$1
LEASE_FILE=/tmp/fuchsia-dhcp-$INTERFACE.leases
PID_FILE=/tmp/fuchsia-dhcp-$INTERFACE.pid
LOG_FILE=/tmp/fuchsia-dhcp-$INTERFACE.log

if [[ -z "$INTERFACE" ]]
then
  echo "Missing interface name."
  exit 1
fi

FUCHSIA_IP=${FUCHSIA_IP:-192.168.3.53} # is this a good default?
if [[ ! $FUCHSIA_IP =~ (^[0-9]+\.[0-9]+\.[0-9]+)\.[0-9]+$ ]]
then
  echo "Invalid FUCHSIA_IP '$FUCHSIA_IP'. Must be a valid IPv4 address."
  exit 1
fi

SUBNET_PREFIX=${BASH_REMATCH[1]}
FUCHSIA_NETWORK=${SUBNET_PREFIX}.0/24
HOST_IP=${SUBNET_PREFIX}.1

DARWIN=false
if [[ $(uname -s) == Darwin ]]
then
  DARWIN=true
fi

# Find the dnsmasq binary.
DNSMASQ=$(which dnsmasq) || DNSMASQ=$(brew --prefix)/sbin/dnsmasq
if [[ ! -x "$DNSMASQ" ]]
then
  echo "dnsmasq not found."
  if $DARWIN
  then
    echo " brew install dnsmasq"
  else
    echo " apt-get install dnsmasq"
  fi
  exit 1
fi

if [[ $DARWIN == false && $(pidof NetworkManager) ]]
then
  nmstat=$(nmcli d status | awk "/$INTERFACE/ { print \$3 }")
  if [[ -n $nmstat && $nmstat != unmanaged ]]; then
    echo "$INTERFACE is managed by NetworkManager so can't be configured by this script."
    echo ""
    echo "If you DON'T want this, create a file /etc/network/interfaces.d/$INTERFACE containing:"
    echo "iface $INTERFACE inet manual"
    echo ""
    echo "Then restart Network manager with: sudo killall NetworkManager"
    exit 1
  fi
fi

# Check if dnsmasq is running.
if [[ -r $PID_FILE ]]
then
  # Read the PID file.
  DNSMASQ_PID=$(<$PID_FILE)

  # Check that the PID file actually refers to a dnsmasq process.
  if $DARWIN
  then
    DNSMASQ_PID_NAME=$( (ps -A -o comm $DNSMASQ_PID || true) | tail +2)
    if [[ "$DNSMASQ_PID_NAME" != "$DNSMASQ" ]]
    then
      # There's a PID file but the process name isn't right.
      unset DNSMASQ_PID
    fi
  else
    if [[ /proc/$DNSMASQ_PID/exe -ef $DNSMASQ ]]
    then
      # There's a PID file but the process name isn't right.
      unset DNSMASQ_PID
    fi
  fi

  if [[ -n "$DNSMASQ_PID" ]]
  then
    echo "Killing the old dnsmasq (pid: $DNSMASQ_PID)..."
    sudo kill $DNSMASQ_PID || true
    sudo rm -f $PID_FILE
  fi
fi

if [[ -f "$LEASE_FILE" ]]
then
  echo "Removing the old dnsmasq lease file $LEASE_FILE ..."
  sudo rm $LEASE_FILE
fi

# Bring up the network.
echo "Bringing up the network interface: $INTERFACE"
sudo ifconfig $INTERFACE inet $HOST_IP

if $DARWIN
then
  LOOPBACK=lo0
else
  LOOPBACK=lo
fi

echo Starting dnsmasq...
# TODO: can we use --dhcp-host instead of --dhcp-range
sudo $DNSMASQ \
  --conf-file=/dev/null \
  --bind-interfaces \
  --interface=$INTERFACE \
  --except-interface=$LOOPBACK \
  --dhcp-range=$INTERFACE,$FUCHSIA_IP,$FUCHSIA_IP,24h \
  --dhcp-leasefile=$LEASE_FILE \
  --pid-file=$PID_FILE \
  --log-facility=$LOG_FILE \
  --listen-address=$HOST_IP

if $DARWIN
then
  # OSX will not bring up ipv6 until an ipv6 address is assigned, but as soon
  # as an address is assigned, it will also assign a link-local address. Here
  # we assign the same address as used by the zircon ifup script, and let OSX
  # assign the link-local address. Previously we computed and assigned a
  # link-local address, but this resulted in duplicate addresses assigned to
  # the interface, and TAP just duplicated that traffic to applications.
  # This is configured after dnsmasq is started, as dnsmasq has no need to
  # listen on ipv6, and fails to bind fc00.
  sudo ifconfig $INTERFACE inet6 fc00::/7 up

  DEFAULT_INTERFACE=$(route -n get default | awk '/interface:/ { print $2 }') || true
else
  DEFAULT_INTERFACE=$(ip route get 8.8.8.8 | awk '/^8.8.8.8/ { print $5 }')
fi
if [[ -z "$DEFAULT_INTERFACE" ]]
then
  echo "No default route, not enabling forwarding."
else
  echo "Enable IP forwarding..."
  if $DARWIN
  then
    sudo sysctl -q net.inet.ip.forwarding=1
    echo "
    nat on $DEFAULT_INTERFACE from $FUCHSIA_NETWORK to any -> ($DEFAULT_INTERFACE)
    pass out on $DEFAULT_INTERFACE inet from $FUCHSIA_NETWORK to any
    " | sudo pfctl -q -ef - >& /dev/null || true
  else
    sudo /bin/bash -c "echo 1 > /proc/sys/net/ipv4/ip_forward"
    sudo iptables -t nat -A POSTROUTING -o $DEFAULT_INTERFACE -j MASQUERADE
    sudo iptables -A FORWARD -i $DEFAULT_INTERFACE -o $INTERFACE -m state --state RELATED,ESTABLISHED -j ACCEPT
    sudo iptables -A FORWARD -i $INTERFACE -o $DEFAULT_INTERFACE -j ACCEPT
  fi
fi
