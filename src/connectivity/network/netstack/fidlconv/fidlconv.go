// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fidlconv

import (
	"fmt"
	"net"

	netfidl "fidl/fuchsia/net"
	"fidl/fuchsia/net/stack"

	"gvisor.dev/gvisor/pkg/tcpip"
)

func toNet(addr netfidl.IpAddress) net.IP {
	switch tag := addr.Which(); tag {
	case netfidl.IpAddressIpv4:
		return addr.Ipv4.Addr[:]
	case netfidl.IpAddressIpv6:
		return addr.Ipv6.Addr[:]
	default:
		panic(fmt.Sprintf("invalid IP address tag = %d", tag))
	}
}

func ToTCPIPAddress(addr netfidl.IpAddress) tcpip.Address {
	return tcpip.Address(toNet(addr))
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

func ToTCPIPSubnet(sn netfidl.Subnet) tcpip.Subnet {
	a := toNet(sn.Addr)
	m := net.CIDRMask(int(sn.PrefixLen), len(a)*8)
	subnet, err := tcpip.NewSubnet(tcpip.Address(a.Mask(m)), tcpip.AddressMask(m))
	if err != nil {
		panic(err)
	}
	return subnet
}

func TcpipRouteToForwardingEntry(route tcpip.Route) stack.ForwardingEntry {
	forwardingEntry := stack.ForwardingEntry{
		Subnet: netfidl.Subnet{
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
