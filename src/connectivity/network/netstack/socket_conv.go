// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//go:build !build_with_native_toolchain
// +build !build_with_native_toolchain

package netstack

import (
	"fmt"

	fidlnet "fidl/fuchsia/net"

	"gvisor.dev/gvisor/pkg/tcpip"
)

func bytesToAddressDroppingUnspecified(b []uint8) tcpip.Address {
	for _, e := range b {
		if e != 0 {
			return tcpip.Address(b)
		}
	}
	return ""
}

func toTCPIPFullAddress(addr fidlnet.SocketAddress) tcpip.FullAddress {
	switch w := addr.Which(); w {
	case fidlnet.SocketAddressIpv4:
		return tcpip.FullAddress{
			NIC:  0,
			Addr: bytesToAddressDroppingUnspecified(addr.Ipv4.Address.Addr[:]),
			Port: addr.Ipv4.Port,
		}
	case fidlnet.SocketAddressIpv6:
		return tcpip.FullAddress{
			NIC:  tcpip.NICID(addr.Ipv6.ZoneIndex),
			Addr: bytesToAddressDroppingUnspecified(addr.Ipv6.Address.Addr[:]),
			Port: addr.Ipv6.Port,
		}
	default:
		panic(fmt.Sprintf("invalid fuchsia.net/SocketAddress variant: %d", w))
	}
}

func toTcpIpAddressDroppingUnspecifiedv4(fidl fidlnet.Ipv4Address) tcpip.Address {
	return bytesToAddressDroppingUnspecified(fidl.Addr[:])
}

func toTcpIpAddressDroppingUnspecifiedv6(fidl fidlnet.Ipv6Address) tcpip.Address {
	return bytesToAddressDroppingUnspecified(fidl.Addr[:])
}
