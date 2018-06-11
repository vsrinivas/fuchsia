// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package netiface

import (
	"fidl/fuchsia/netstack"
	"netstack/fidlconv"
	"reflect"
	"sort"
	"strings"
	"testing"

	"github.com/google/netstack/tcpip"
)

func indexedByID(nics []*NIC) map[tcpip.NICID]*NIC {
	res := make(map[tcpip.NICID]*NIC)
	for _, v := range nics {
		res[v.ID] = v
	}

	return res
}

func (n *NIC) setID(id tcpip.NICID) {
	n.ID = id
	for i, _ := range n.Routes {
		n.Routes[i].NIC = id
	}
}

func (n *NIC) setAddr(addr tcpip.Address) {
	n.Addr = addr
}

func (n *NIC) setGateway(gateway tcpip.Address) {
	for i, _ := range n.Routes {
		n.Routes[i].Gateway = gateway
	}
}

func NewLoopbackIf() *NIC {
	return &NIC{
		Addr: tcpip.Parse("127.0.0.1"),
		Routes: []tcpip.Route{
			{
				Destination: tcpip.Parse("127.0.0.1"),
				Mask:        tcpip.Parse("255.255.255.255"),
			},
			{
				Destination: tcpip.Parse("::1"),
				Mask:        tcpip.Address(strings.Repeat("\xff", 16)),
			},
		},
	}
}

func NewAnyDestIf() *NIC {
	return &NIC{
		Routes: []tcpip.Route{
			{
				Destination: tcpip.Address(strings.Repeat("\x00", 4)),
				Mask:        tcpip.Address(strings.Repeat("\x00", 4)),
			},
			{
				Destination: tcpip.Parse("::"),
				Mask:        tcpip.Parse("::"),
			},
		},
	}
}

func TestLoopbackBeforeAny(t *testing.T) {
	nics := []*NIC{}

	// Create NIC 1 for any destination.
	n := NewAnyDestIf()
	n.setID(1)
	nics = append(nics, n)

	// Create NIC 2 for loopback.
	n = NewLoopbackIf()
	n.setID(2)
	nics = append(nics, n)

	routes := make([]tcpip.Route, 0)
	for i := 0; i < len(nics); i++ {
		routes = append(routes, nics[i].Routes...)
	}
	sort.Slice(routes, func(i, j int) bool {
		return Less(&routes[i], &routes[j], indexedByID(nics))
	})

	expected := []tcpip.Route{
		{
			Destination: tcpip.Parse("127.0.0.1"),
			Mask:        tcpip.Parse("255.255.255.255"),
			NIC:         2,
		},
		{
			Destination: tcpip.Parse("::1"),
			Mask:        tcpip.Address(strings.Repeat("\xff", 16)),
			NIC:         2,
		},
		{
			Destination: tcpip.Address(strings.Repeat("\x00", 4)),
			Mask:        tcpip.Address(strings.Repeat("\x00", 4)),
			NIC:         1,
		},
		{
			Destination: tcpip.Parse("::"),
			Mask:        tcpip.Parse("::"),
			NIC:         1,
		},
	}

	if !reflect.DeepEqual(expected, routes) {
		t.Fatalf("Expected:\n  %v\nActual:\n  %v", expected, routes)
	}
}

func TestGatewayBeforeAddr(t *testing.T) {
	nics := []*NIC{}

	// Create NIC 1 for any destination with an address.
	n := NewAnyDestIf()
	n.setID(1)
	n.setAddr(tcpip.Parse("1.2.3.4"))
	nics = append(nics, n)

	// Create NIC 2 for any destination with a gateway.
	n = NewAnyDestIf()
	n.setID(2)
	n.setGateway(tcpip.Parse("1.1.1.1"))
	nics = append(nics, n)

	routes := make([]tcpip.Route, 0)
	for i := 0; i < len(nics); i++ {
		routes = append(routes, nics[i].Routes...)
	}

	sort.Slice(routes, func(i, j int) bool {
		return Less(&routes[i], &routes[j], indexedByID(nics))
	})

	expected := []tcpip.Route{
		{
			Destination: tcpip.Address(strings.Repeat("\x00", 4)),
			Mask:        tcpip.Address(strings.Repeat("\x00", 4)),
			Gateway:     tcpip.Parse("1.1.1.1"),
			NIC:         2,
		},
		{
			Destination: tcpip.Parse("::"),
			Mask:        tcpip.Parse("::"),
			Gateway:     tcpip.Parse("1.1.1.1"),
			NIC:         2,
		},
		{
			Destination: tcpip.Address(strings.Repeat("\x00", 4)),
			Mask:        tcpip.Address(strings.Repeat("\x00", 4)),
			NIC:         1,
		},
		{
			Destination: tcpip.Parse("::"),
			Mask:        tcpip.Parse("::"),
			NIC:         1,
		},
	}

	if !reflect.DeepEqual(expected, routes) {
		t.Fatalf("Expected:\n  %v\nActual:\n  %v", expected, routes)
	}
}

func TestAddrBeforeAny(t *testing.T) {
	nics := []*NIC{}

	// Create NIC 1 for any destination.
	n := NewAnyDestIf()
	n.setID(1)
	nics = append(nics, n)

	// Create NIC 2 for any destination with an address.
	n = NewAnyDestIf()
	n.setID(2)
	n.setAddr(tcpip.Parse("1.2.3.4"))
	nics = append(nics, n)

	routes := make([]tcpip.Route, 0)
	for i := 0; i < len(nics); i++ {
		routes = append(routes, nics[i].Routes...)
	}

	sort.Slice(routes, func(i, j int) bool {
		return Less(&routes[i], &routes[j], indexedByID(nics))
	})

	expected := []tcpip.Route{
		{
			Destination: tcpip.Address(strings.Repeat("\x00", 4)),
			Mask:        tcpip.Address(strings.Repeat("\x00", 4)),
			NIC:         2,
		},
		{
			Destination: tcpip.Parse("::"),
			Mask:        tcpip.Parse("::"),
			NIC:         2,
		},
		{
			Destination: tcpip.Address(strings.Repeat("\x00", 4)),
			Mask:        tcpip.Address(strings.Repeat("\x00", 4)),
			NIC:         1,
		},
		{
			Destination: tcpip.Parse("::"),
			Mask:        tcpip.Parse("::"),
			NIC:         1,
		},
	}

	if !reflect.DeepEqual(expected, routes) {
		t.Fatalf("Expected:\n  %v\nActual:\n  %v", expected, routes)
	}
}

func TestSortByIDAsFallback(t *testing.T) {
	nics := []*NIC{}

	// Create NIC 2 for any destination with an address.
	n := NewAnyDestIf()
	n.setID(2)
	n.setAddr(tcpip.Parse("1.2.3.4"))
	nics = append(nics, n)

	// Create NIC 1 for any destination with an address.
	n = NewAnyDestIf()
	n.setID(1)
	n.setAddr(tcpip.Parse("::1"))
	nics = append(nics, n)

	routes := make([]tcpip.Route, 0)
	for i := 0; i < len(nics); i++ {
		routes = append(routes, nics[i].Routes...)
	}

	sort.Slice(routes, func(i, j int) bool {
		return Less(&routes[i], &routes[j], indexedByID(nics))
	})

	expected := []tcpip.Route{
		{
			Destination: tcpip.Address(strings.Repeat("\x00", 4)),
			Mask:        tcpip.Address(strings.Repeat("\x00", 4)),
			NIC:         1,
		},
		{
			Destination: tcpip.Parse("::"),
			Mask:        tcpip.Parse("::"),
			NIC:         1,
		},
		{
			Destination: tcpip.Address(strings.Repeat("\x00", 4)),
			Mask:        tcpip.Address(strings.Repeat("\x00", 4)),
			NIC:         2,
		},
		{
			Destination: tcpip.Parse("::"),
			Mask:        tcpip.Parse("::"),
			NIC:         2,
		},
	}

	if !reflect.DeepEqual(expected, routes) {
		t.Fatalf("Expected:\n  %v\nActual:\n  %v", expected, routes)
	}
}

func TestSpecificGatewayBeforeAnyGateway(t *testing.T) {
	nics := []*NIC{
		&NIC{
			ID: 1,
			Routes: []tcpip.Route{
				{
					Gateway: tcpip.Parse("192.168.42.1"),
					NIC:     1,
				},
				{
					Gateway: tcpip.Parse("::"),
					NIC:     1,
				},
			},
		},
		&NIC{
			ID: 2,
			Routes: []tcpip.Route{
				{
					Gateway: tcpip.Parse("10.0.1.1"),
					NIC:     2,
				},
				{
					Gateway: tcpip.Parse("::"),
					NIC:     2,
				},
			},
		},
	}

	expected := []tcpip.Route{
		{
			Gateway: tcpip.Parse("192.168.42.1"),
			NIC:     1,
		},
		{
			Gateway: tcpip.Parse("10.0.1.1"),
			NIC:     2,
		},
		{
			Gateway: tcpip.Parse("::"),
			NIC:     1,
		},
		{
			Gateway: tcpip.Parse("::"),
			NIC:     2,
		},
	}
	routes := make([]tcpip.Route, 0)
	for i := 0; i < len(nics); i++ {
		routes = append(routes, nics[i].Routes...)
	}

	sort.Slice(routes, func(i, j int) bool {
		r := Less(&routes[i], &routes[j], indexedByID(nics))
		t.Logf("Comparing:\n\t%+v\n\t%+v\n\tLess:%v", &routes[i], &routes[j], r)
		return r
	})

	if !reflect.DeepEqual(expected, routes) {
		t.Fatalf("Expected:\n  %v\nActual:\n  %v", expected, routes)
	}
}

func TestSpecificMaskFirst(t *testing.T) {
	nics := []*NIC{
		&NIC{
			ID: 1,
			Routes: []tcpip.Route{
				{
					Gateway: tcpip.Parse("192.168.0.1"),
					Mask:    tcpip.Parse("255.255.0.0"),
					NIC:     1,
				},
			},
		},
		&NIC{
			ID: 1,
			Routes: []tcpip.Route{
				{
					Gateway: tcpip.Parse("192.168.42.1"),
					Mask:    tcpip.Parse("255.255.255.0"),
					NIC:     1,
				},
			},
		},
	}

	expected := []tcpip.Route{
		{
			Gateway: tcpip.Parse("192.168.42.1"),
			Mask:    tcpip.Parse("255.255.255.0"),
			NIC:     1,
		},
		{
			Gateway: tcpip.Parse("192.168.0.1"),
			Mask:    tcpip.Parse("255.255.0.0"),
			NIC:     1,
		},
	}
	routes := make([]tcpip.Route, 0)
	for i := 0; i < len(nics); i++ {
		routes = append(routes, nics[i].Routes...)
	}

	sort.Slice(routes, func(i, j int) bool {
		r := Less(&routes[i], &routes[j], indexedByID(nics))
		t.Logf("Comparing:\n\t%+v\n\t%+v\n\tLess:%v", &routes[i], &routes[j], r)
		return r
	})

	if !reflect.DeepEqual(expected, routes) {
		t.Fatalf("Expected:\n  %v\nActual:\n  %v", expected, routes)
	}
}

func NewV4Address(b [4]uint8) netstack.NetAddress {
	return netstack.NetAddress{Family: netstack.NetAddressFamilyIpv4, Ipv4: &netstack.Ipv4Address{Addr: b}}
}

var isAnyTests = []struct {
	addr netstack.NetAddress
	res  bool
}{
	{
		addr: NewV4Address([4]uint8{0, 0, 0, 0}),
		res:  true,
	},
	{
		addr: NewV4Address([4]uint8{127, 0, 0, 1}),
		res:  false,
	},
}

func TestIsAny(t *testing.T) {
	for _, tst := range isAnyTests {
		if res := IsAny(fidlconv.NetAddressToTCPIPAddress(tst.addr)); res != tst.res {
			t.Errorf("expected netiface.IsAny(%+v) to be %v, got %v", tst.addr, tst.res, res)
		}
	}
}
