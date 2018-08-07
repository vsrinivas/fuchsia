// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package connectivity

import (
	"fidl/fuchsia/netstack"
	"testing"
)

var nextid uint32 = 0

func newV4Address(a, b, c, d uint8) netstack.NetAddress {
	addr := [4]uint8{a, b, c, d}
	return netstack.NetAddress{Family: netstack.NetAddressFamilyIpv4, Ipv4: &netstack.Ipv4Address{Addr: addr}}
}

func interfaceDHCPEnabledNoAddress() netstack.NetInterface {
	res := netstack.NetInterface{
		Id:        nextid,
		Flags:     netstack.NetInterfaceFlagDhcp | netstack.NetInterfaceFlagUp,
		Addr:      newV4Address(0, 0, 0, 0),
		Netmask:   newV4Address(0, 0, 0, 0),
		Broadaddr: newV4Address(0, 0, 0, 0),
		Ipv6addrs: []netstack.Subnet{},
		Hwaddr:    []uint8{0, 1, 2, 3, 4, 5},
	}
	nextid++
	return res
}

func interfaceStaticAddress() netstack.NetInterface {
	res := netstack.NetInterface{
		Id:        nextid,
		Flags:     netstack.NetInterfaceFlagUp,
		Addr:      newV4Address(192, 168, 42, 10),
		Netmask:   newV4Address(255, 255, 255, 0),
		Broadaddr: newV4Address(255, 255, 255, 0),
		Ipv6addrs: []netstack.Subnet{},
		Hwaddr:    []uint8{1, 2, 3, 4, 5, 6},
	}
	nextid = nextid + 1
	return res
}

func interfaceDHCPEnabledWithAddress() netstack.NetInterface {
	res := netstack.NetInterface{
		Id:        nextid,
		Flags:     netstack.NetInterfaceFlagDhcp | netstack.NetInterfaceFlagUp,
		Addr:      newV4Address(10, 0, 0, 1),
		Netmask:   newV4Address(255, 255, 255, 0),
		Broadaddr: newV4Address(255, 255, 255, 0),
		Ipv6addrs: []netstack.Subnet{},
		Hwaddr:    []uint8{1, 2, 3, 4, 5, 6},
	}
	nextid = nextid + 1
	return res
}

var tt = []struct {
	iface netstack.NetInterface
	res   bool
}{
	{
		iface: interfaceDHCPEnabledNoAddress(),
		res:   false,
	},
	{
		iface: interfaceStaticAddress(),
		res:   false,
	},
	{
		iface: interfaceDHCPEnabledWithAddress(),
		res:   true,
	},
}

func TestHasDHCPAddress(t *testing.T) {
	for _, in := range tt {
		if got := hasDHCPAddress(in.iface); got != in.res {
			t.Errorf("got %t, want %t; input: %+v", got, in.res, in.iface)
		}
	}
}
