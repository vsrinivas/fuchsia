// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fidlconv

import (
	"bytes"
	"encoding/binary"
	"fmt"
	"math/big"
	"net"

	"netstack/util"

	netfidl "fidl/fuchsia/net"
	"fidl/fuchsia/net/stack"

	"github.com/google/netstack/tcpip"
)

func ToTCPIPAddress(addr netfidl.IpAddress) tcpip.Address {
	switch tag := addr.Which(); tag {
	case netfidl.IpAddressIpv4:
		return tcpip.Address(addr.Ipv4.Addr[:])
	case netfidl.IpAddressIpv6:
		return tcpip.Address(addr.Ipv6.Addr[:])
	default:
		panic(fmt.Sprintf("invalid IP address tag = %d", tag))
	}
}

func ToNetIpAddress(addr tcpip.Address) netfidl.IpAddress {
	var out netfidl.IpAddress
	switch l := len(addr); l {
	case net.IPv4len:
		var ipv4 netfidl.Ipv4Address
		copy(ipv4.Addr[:], addr)
		out.SetIpv4(ipv4)
	case net.IPv6len:
		var ipv6 netfidl.Ipv6Address
		copy(ipv6.Addr[:], addr)
		out.SetIpv6(ipv6)
	default:
		panic(fmt.Sprintf("invalid IP address length = %d: %x", l, addr))
	}
	return out
}

func ToTCPIPSubnet(sn netfidl.Subnet) (tcpip.Subnet, error) {
	// Use ToTCPIPAddress to abstract the IPv4 vs IPv6 behavior.
	a := []byte(ToTCPIPAddress(sn.Addr))
	m := util.CIDRMask(int(sn.PrefixLen), int(len(a)*8))
	for i := range a {
		a[i] = a[i] & m[i]
	}
	return tcpip.NewSubnet(tcpip.Address(a), m)
}

func GetPrefixLen(mask tcpip.AddressMask) uint8 {
	prefixLen := uint8(0)
	switch len(mask) {
	case 4:
		var x uint32
		if err := binary.Read(bytes.NewReader([]byte(mask)), binary.BigEndian, &x); err != nil {
			return 0
		}
		if x == 0 {
			return 0
		}
		for x&1 == 0 {
			prefixLen += 1
			x >>= 1
		}
		return 32 - prefixLen
	case 16:
		var x big.Int
		zero := big.NewInt(0)
		one := big.NewInt(1)
		x.SetBytes([]byte(mask))

		if x.Cmp(zero) == 0 {
			return 0
		}
		var tmp big.Int
		for tmp.And(&x, one).Cmp(zero) == 0 {
			prefixLen += 1
			x.Rsh(&x, 1)
		}
		return 128 - prefixLen
	default:
		panic("invalid tcpip.Address length")
	}
}

func TcpipRouteToForwardingEntry(tcpipRoute tcpip.Route) stack.ForwardingEntry {
	var dest stack.ForwardingDestination
	// There are two types of destinations: link-local and next-hop.
	//   If a route has a gateway, use that as the next-hop, and ignore the NIC.
	//   Otherwise, it is considered link-local, and use the NIC.
	if tcpipRoute.Gateway == tcpip.Address("") {
		dest.SetDeviceId(uint64(tcpipRoute.NIC))
	} else {
		dest.SetNextHop(ToNetIpAddress(tcpipRoute.Gateway))
	}
	return stack.ForwardingEntry{
		Subnet: netfidl.Subnet{
			Addr:      ToNetIpAddress(tcpipRoute.Destination),
			PrefixLen: GetPrefixLen(tcpipRoute.Mask),
		},
		Destination: dest,
	}
}

func ForwardingEntryToTcpipRoute(forwardingEntry stack.ForwardingEntry) tcpip.Route {
	dest := ToTCPIPAddress(forwardingEntry.Subnet.Addr)
	tcpipRoute := tcpip.Route{
		Destination: dest,
		Mask:        util.CIDRMask(int(forwardingEntry.Subnet.PrefixLen), len(dest)*8),
	}
	switch forwardingEntry.Destination.Which() {
	case stack.ForwardingDestinationDeviceId:
		tcpipRoute.NIC = tcpip.NICID(forwardingEntry.Destination.DeviceId)
	case stack.ForwardingDestinationNextHop:
		tcpipRoute.Gateway = ToTCPIPAddress(forwardingEntry.Destination.NextHop)
	}
	return tcpipRoute
}
