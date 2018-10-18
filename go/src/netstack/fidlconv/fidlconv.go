// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fidlconv

import (
	"bytes"
	"encoding/binary"
	"math/big"

	"netstack/util"

	"fidl/fuchsia/net"
	"fidl/fuchsia/net/stack"
	"fidl/fuchsia/netstack"

	"github.com/google/netstack/tcpip"
)

func ToTCPIPAddress(addr net.IpAddress) tcpip.Address {
	out := tcpip.Address("")
	switch addr.Which() {
	case net.IpAddressIpv4:
		out = tcpip.Address(addr.Ipv4.Addr[:])
	case net.IpAddressIpv6:
		out = tcpip.Address(addr.Ipv6.Addr[:])
	}
	return out
}

func ToNetIpAddress(addr tcpip.Address) net.IpAddress {
	var out net.IpAddress
	switch len(addr) {
	case 4:
		out.SetIpv4(net.IPv4Address{Addr: [4]uint8{}})
		copy(out.Ipv4.Addr[:], addr[:])
	case 16:
		out.SetIpv6(net.IPv6Address{Addr: [16]uint8{}})
		copy(out.Ipv6.Addr[:], addr[:])
	default:
		panic("invalid tcpip.Address length")
	}
	return out
}

// TODO: remove when we port from fidl/fuchsia/netstack to fidl/fuchsia/net
// NetAddress type is restated in the name to prevent a name collision with `ToTCPIPAddress(net.IpAddress)`
func NetAddressToTCPIPAddress(addr netstack.NetAddress) tcpip.Address {
	out := tcpip.Address("")
	switch addr.Family {
	case netstack.NetAddressFamilyIpv4:
		out = tcpip.Address(addr.Ipv4.Addr[:])
	case netstack.NetAddressFamilyIpv6:
		out = tcpip.Address(addr.Ipv6.Addr[:])
	}
	return out
}

// TODO: remove when we port from fidl/fuchsia/netstack to fidl/fuchsia/net
func ToNetAddress(addr tcpip.Address) netstack.NetAddress {
	out := netstack.NetAddress{Family: netstack.NetAddressFamilyUnspecified}
	switch len(addr) {
	case 4:
		out.Family = netstack.NetAddressFamilyIpv4
		out.Ipv4 = &netstack.Ipv4Address{Addr: [4]uint8{}}
		copy(out.Ipv4.Addr[:], addr[:])
	case 16:
		out.Family = netstack.NetAddressFamilyIpv6
		out.Ipv6 = &netstack.Ipv6Address{Addr: [16]uint8{}}
		copy(out.Ipv6.Addr[:], addr[:])
	case 0: // assume ipv4 for now; perhaps ipv6-encoded-ipv4 later
		out.Family = netstack.NetAddressFamilyIpv4
		out.Ipv4 = &netstack.Ipv4Address{Addr: [4]uint8{0, 0, 0, 0}}
	}
	return out
}

func ToTCPIPSubnet(sn net.Subnet) (tcpip.Subnet, error) {
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
		Subnet: net.Subnet{
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
