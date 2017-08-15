// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package netiface

import (
	"github.com/google/netstack/tcpip"
)

type NIC struct {
	ID         tcpip.NICID
	Addr       tcpip.Address
	Netmask    tcpip.AddressMask
	Routes     []tcpip.Route
	DNSServers []tcpip.Address
}

func isAny(a tcpip.Address) bool {
	for i := 0; i < len(a); i++ {
		if a[i] != 0 {
			return false
		}
	}
	return true
}

func hasGateway(n *NIC) bool {
	for _, r := range n.Routes {
		if r.Gateway != "" {
			return true
		}
	}
	return false
}

func Less(ni, nj *NIC) bool {
	// If only one of them has a route for specific address (i.e. not a route
	// for Any destination), that element should sort before the other.
	// TODO: should also check routes[1:]?
	ri, rj := ni.Routes, nj.Routes
	if len(ri) > 0 && len(rj) > 0 {
		if isAny(ri[0].Destination) != isAny(rj[0].Destination) {
			return !isAny(ri[0].Destination)
		}
	}
	// If only one of them has a gateway, that element should sort before
	// the other.
	if hasGateway(ni) != hasGateway(nj) {
		return hasGateway(ni)
	}
	// If only one them has an IP address, that element should sort before
	// the other.
	if (ni.Addr != "") != (nj.Addr != "") {
		return ni.Addr != ""
	}
	// Otherwise, the element with a small nicid should sort before the other.
	return ni.ID < nj.ID
}
