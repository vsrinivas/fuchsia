// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package netiface

import (
	"sort"
	"strings"
	"testing"

	"github.com/google/netstack/tcpip"
)

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

	sort.Slice(nics, func(i, j int) bool {
		return Less(nics[i], nics[j])
	})

	// NIC 2 should be before NIC 1.
	if !(nics[0].ID == 2 && nics[1].ID == 1) {
		t.Fatal("Loopback-if should be before any-dest-if")
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

	sort.Slice(nics, func(i, j int) bool {
		return Less(nics[i], nics[j])
	})

	// NIC 2 should be before NIC 1.
	if !(nics[0].ID == 2 && nics[1].ID == 1) {
		t.Fatal("netif with gateway should be before netif with addr")
	}
}

func TestAddr(t *testing.T) {
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

	sort.Slice(nics, func(i, j int) bool {
		return Less(nics[i], nics[j])
	})

	// NIC 2 should be before NIC 1.
	if !(nics[0].ID == 2 && nics[1].ID == 1) {
		t.Fatal("netif with gateway should be before netif with addr")
	}
}

func TestID(t *testing.T) {
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

	sort.Slice(nics, func(i, j int) bool {
		return Less(nics[i], nics[j])
	})

	// NIC 1 should be before NIC 2.
	if !(nics[0].ID == 1 && nics[1].ID == 2) {
		t.Fatal("netif with smaller ID should be before the other")
	}
}
