// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// +build !build_with_native_toolchain

package fidlconv

import (
	"fmt"

	"fidl/fuchsia/net"
	"fidl/fuchsia/net/stack"

	"gvisor.dev/gvisor/pkg/tcpip"
	"gvisor.dev/gvisor/pkg/tcpip/header"
	"gvisor.dev/gvisor/pkg/tcpip/network/ipv4"
	"gvisor.dev/gvisor/pkg/tcpip/network/ipv6"
)

func ToTCPIPAddressAndProtocolNumber(addr net.IpAddress) (tcpip.Address, tcpip.NetworkProtocolNumber) {
	switch tag := addr.Which(); tag {
	case net.IpAddressIpv4:
		return tcpip.Address(addr.Ipv4.Addr[:]), ipv4.ProtocolNumber
	case net.IpAddressIpv6:
		return tcpip.Address(addr.Ipv6.Addr[:]), ipv6.ProtocolNumber
	default:
		panic(fmt.Sprintf("invalid fuchsia.net/IpAddress tag %d", tag))
	}
}

func ToTCPIPAddress(addr net.IpAddress) tcpip.Address {
	a, _ := ToTCPIPAddressAndProtocolNumber(addr)
	return a
}

func ToNetIpAddress(addr tcpip.Address) net.IpAddress {
	switch l := len(addr); l {
	case header.IPv4AddressSize:
		var v4 net.Ipv4Address
		copy(v4.Addr[:], addr)
		return net.IpAddressWithIpv4(v4)
	case header.IPv6AddressSize:
		var v6 net.Ipv6Address
		copy(v6.Addr[:], addr)
		return net.IpAddressWithIpv6(v6)
	default:
		panic(fmt.Sprintf("invalid IP address length = %d: %x", l, addr))
	}
}

func ToNetMacAddress(addr tcpip.LinkAddress) net.MacAddress {
	if len(addr) != header.EthernetAddressSize {
		panic(fmt.Sprintf("invalid link address length = %d: %x", len(addr), addr))
	}
	var mac net.MacAddress
	copy(mac.Octets[:], addr)
	return mac
}

func ToTCPIPLinkAddress(mac net.MacAddress) tcpip.LinkAddress {
	return tcpip.LinkAddress(mac.Octets[:])
}

func ToNetSocketAddress(addr tcpip.FullAddress) net.SocketAddress {
	var out net.SocketAddress
	switch l := len(addr.Addr); l {
	case header.IPv4AddressSize:
		var v4 net.Ipv4Address
		copy(v4.Addr[:], addr.Addr)
		out.SetIpv4(net.Ipv4SocketAddress{
			Address: v4,
			Port:    addr.Port,
		})
	case header.IPv6AddressSize:
		var v6 net.Ipv6Address
		copy(v6.Addr[:], addr.Addr)

		// Zone information should only be included for non-global addresses as the same
		// address may be used across different zones. Note, there is only a single globally
		// scoped zone where global addresses may only be used once so zone information is not
		// needed for global addresses. See RFC 4007 section 6 for more details.
		var zoneIdx uint64
		if header.IsV6LinkLocalAddress(addr.Addr) || header.IsV6LinkLocalMulticastAddress(addr.Addr) {
			zoneIdx = uint64(addr.NIC)
		}
		out.SetIpv6(net.Ipv6SocketAddress{
			Address:   v6,
			Port:      addr.Port,
			ZoneIndex: zoneIdx,
		})
	default:
		panic(fmt.Sprintf("invalid IP address length = %d: %x", l, addr.Addr))
	}
	return out
}

func ToTCPIPSubnet(sn net.Subnet) tcpip.Subnet {
	a := ToTCPIPAddress(sn.Addr)
	return tcpip.AddressWithPrefix{
		Address:   a,
		PrefixLen: int(sn.PrefixLen),
	}.Subnet()
}

func TcpipRouteToForwardingEntry(route tcpip.Route) stack.ForwardingEntry {
	forwardingEntry := stack.ForwardingEntry{
		Subnet: net.Subnet{
			Addr:      ToNetIpAddress(route.Destination.ID()),
			PrefixLen: uint8(route.Destination.Prefix()),
		},
	}
	// There are two types of destinations: link-local and next-hop.
	//   If a route has a gateway, use that as the next-hop, and ignore the NIC.
	//   Otherwise, it is considered link-local, and use the NIC.
	if len(route.Gateway) == 0 {
		forwardingEntry.Destination.SetDeviceId(uint64(route.NIC))
	} else {
		forwardingEntry.Destination.SetNextHop(ToNetIpAddress(route.Gateway))
	}
	return forwardingEntry
}

func ForwardingEntryToTcpipRoute(forwardingEntry stack.ForwardingEntry) tcpip.Route {
	route := tcpip.Route{
		Destination: ToTCPIPSubnet(forwardingEntry.Subnet),
	}
	switch forwardingEntry.Destination.Which() {
	case stack.ForwardingDestinationDeviceId:
		route.NIC = tcpip.NICID(forwardingEntry.Destination.DeviceId)
	case stack.ForwardingDestinationNextHop:
		route.Gateway = ToTCPIPAddress(forwardingEntry.Destination.NextHop)
	}
	return route
}
