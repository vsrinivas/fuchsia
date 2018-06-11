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
	Name       string
	Features   uint32
	Netmask    tcpip.AddressMask
	Routes     []tcpip.Route
	DNSServers []tcpip.Address
	Ipv6addrs  []tcpip.Address
	Mac        [6]byte
}

func IsAny(a tcpip.Address) bool {
	for i := 0; i < len(a); i++ {
		if a[i] != 0 {
			return false
		}
	}
	return true
}

func hasGateway(r *tcpip.Route) bool {
	return r.Gateway != ""
}

func prefixLength(mask tcpip.Address) (len int) {
	for _, b := range []byte(mask) {
		for i := uint(0); i < 8; i++ {
			if b&(1<<i) != 0 {
				len++
			} else {
				return len
			}
		}
	}
	return len
}

// Less is the sorting function used for routes.
func Less(ri, rj *tcpip.Route, nics map[tcpip.NICID]*NIC) bool {
	ni, nj := nics[ri.NIC], nics[rj.NIC]
	// If only one of them has a route for specific address (i.e. not a route
	// for Any destination), that element should sort before the other.
	if IsAny(ri.Destination) != IsAny(rj.Destination) {
		return !IsAny(ri.Destination)
	}
	// If only one of them has a gateway, that element should sort before
	// the other.
	if hasGateway(ri) != hasGateway(rj) {
		return hasGateway(ri)
	}
	// If both have gateways and only one is for a specific address, that
	// element should sort before the other.
	if IsAny(ri.Gateway) != IsAny(rj.Gateway) {
		return !IsAny(ri.Gateway)
	}
	// If only one them has a NIC with an IP address, that element should sort
	// before the other.
	if (ni.Addr != "") != (nj.Addr != "") {
		return ni.Addr != ""
	}
	// If one has a more specific netmask (longer prefix len), that element
	// should sort before the other.
	li, lj := prefixLength(ri.Mask), prefixLength(rj.Mask)
	if len(ri.Mask) == len(rj.Mask) && li != lj {
		return li > lj
	}
	// Otherwise, sort them by the IDs of their NICs.
	return ri.NIC < rj.NIC
}
