// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package netiface_test

import (
	"reflect"
	"sort"
	"strings"
	"testing"

	"netstack/fidlconv"
	"netstack/netiface"

	"fidl/fuchsia/netstack"

	"github.com/google/netstack/tcpip"
)

func indexedByID(nics []netiface.NIC) map[tcpip.NICID]*netiface.NIC {
	res := make(map[tcpip.NICID]*netiface.NIC)
	for i, v := range nics {
		res[v.ID] = &nics[i]
	}
	return res
}

func setID(n *netiface.NIC, id tcpip.NICID) {
	n.ID = id
	for i := range n.Routes {
		n.Routes[i].NIC = id
	}
}

func setGateway(n *netiface.NIC, gateway tcpip.Address) {
	for i := range n.Routes {
		n.Routes[i].Gateway = gateway
	}
}

func NewLoopback() netiface.NIC {
	return netiface.NIC{
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

func NewAnyDest() netiface.NIC {
	return netiface.NIC{
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
	var nics []netiface.NIC

	// Create NIC 1 for any destination.
	n := NewAnyDest()
	setID(&n, 1)
	nics = append(nics, n)

	// Create NIC 2 for loopback.
	n = NewLoopback()
	setID(&n, 2)
	nics = append(nics, n)

	var routes []tcpip.Route
	for _, nic := range nics {
		routes = append(routes, nic.Routes...)
	}
	indexedByID := indexedByID(nics)
	sort.Slice(routes, func(i, j int) bool {
		return netiface.Less(&routes[i], &routes[j], indexedByID)
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
	var nics []netiface.NIC

	// Create NIC 1 for any destination with an address.
	n := NewAnyDest()
	setID(&n, 1)
	n.Addr = tcpip.Parse("1.2.3.4")
	nics = append(nics, n)

	// Create NIC 2 for any destination with a gateway.
	n = NewAnyDest()
	setID(&n, 2)
	setGateway(&n, tcpip.Parse("1.1.1.1"))
	nics = append(nics, n)

	var routes []tcpip.Route
	for _, nic := range nics {
		routes = append(routes, nic.Routes...)
	}
	indexedByID := indexedByID(nics)
	sort.Slice(routes, func(i, j int) bool {
		return netiface.Less(&routes[i], &routes[j], indexedByID)
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
	var nics []netiface.NIC

	// Create NIC 1 for any destination.
	n := NewAnyDest()
	setID(&n, 1)
	nics = append(nics, n)

	// Create NIC 2 for any destination with an address.
	n = NewAnyDest()
	setID(&n, 2)
	n.Addr = tcpip.Parse("1.2.3.4")
	nics = append(nics, n)

	var routes []tcpip.Route
	for _, nic := range nics {
		routes = append(routes, nic.Routes...)
	}
	indexedByID := indexedByID(nics)
	sort.Slice(routes, func(i, j int) bool {
		return netiface.Less(&routes[i], &routes[j], indexedByID)
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
	var nics []netiface.NIC

	// Create NIC 2 for any destination with an address.
	n := NewAnyDest()
	setID(&n, 2)
	n.Addr = tcpip.Parse("1.2.3.4")
	nics = append(nics, n)

	// Create NIC 1 for any destination with an address.
	n = NewAnyDest()
	setID(&n, 1)
	n.Addr = tcpip.Parse("::1")
	nics = append(nics, n)

	var routes []tcpip.Route
	for _, nic := range nics {
		routes = append(routes, nic.Routes...)
	}
	indexedByID := indexedByID(nics)
	sort.Slice(routes, func(i, j int) bool {
		return netiface.Less(&routes[i], &routes[j], indexedByID)
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
	nics := []netiface.NIC{
		{
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
		{
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

	var routes []tcpip.Route
	for _, nic := range nics {
		routes = append(routes, nic.Routes...)
	}
	indexedByID := indexedByID(nics)
	sort.Slice(routes, func(i, j int) bool {
		return netiface.Less(&routes[i], &routes[j], indexedByID)
	})

	if !reflect.DeepEqual(expected, routes) {
		t.Fatalf("Expected:\n  %v\nActual:\n  %v", expected, routes)
	}
}

func TestSpecificMaskFirst(t *testing.T) {
	nics := []netiface.NIC{
		{
			ID: 1,
			Routes: []tcpip.Route{
				{
					Gateway: tcpip.Parse("192.168.0.1"),
					Mask:    tcpip.Parse("255.255.0.0"),
					NIC:     1,
				},
			},
		},
		{
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

	var routes []tcpip.Route
	for _, nic := range nics {
		routes = append(routes, nic.Routes...)
	}
	indexedByID := indexedByID(nics)
	sort.Slice(routes, func(i, j int) bool {
		return netiface.Less(&routes[i], &routes[j], indexedByID)
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
		if res := netiface.IsAny(fidlconv.NetAddressToTCPIPAddress(tst.addr)); res != tst.res {
			t.Errorf("expected netiface.IsAny(%+v) to be %v, got %v", tst.addr, tst.res, res)
		}
	}
}
