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
	"gvisor.dev/gvisor/pkg/tcpip/header"
	"gvisor.dev/gvisor/pkg/tcpip/network/ipv4"
	"gvisor.dev/gvisor/pkg/tcpip/network/ipv6"
)

// isLinkLocal determines if the given IPv6 address is link-local. This is the
// case when it has the fe80::/10 prefix. This check is used to determine when
// the NICID is relevant for a given IPv6 address.
func isLinkLocal(addr fidlnet.Ipv6Address) bool {
	return addr.Addr[0] == 0xfe && addr.Addr[1]&0xc0 == 0x80
}

// toNetSocketAddress converts a tcpip.FullAddress into a fidlnet.SocketAddress
// taking the protocol into consideration. If addr is unspecified, the
// unspecified address for the provided protocol is returned.
//
// Panics if protocol is neither IPv4 nor IPv6.
func toNetSocketAddress(protocol tcpip.NetworkProtocolNumber, addr tcpip.FullAddress) fidlnet.SocketAddress {
	switch protocol {
	case ipv4.ProtocolNumber:
		out := fidlnet.Ipv4SocketAddress{
			Port: addr.Port,
		}
		copy(out.Address.Addr[:], addr.Addr)
		return fidlnet.SocketAddressWithIpv4(out)
	case ipv6.ProtocolNumber:
		out := fidlnet.Ipv6SocketAddress{
			Port: addr.Port,
		}
		if len(addr.Addr) == header.IPv4AddressSize {
			// Copy address in v4-mapped format.
			copy(out.Address.Addr[header.IPv6AddressSize-header.IPv4AddressSize:], addr.Addr)
			out.Address.Addr[header.IPv6AddressSize-header.IPv4AddressSize-1] = 0xff
			out.Address.Addr[header.IPv6AddressSize-header.IPv4AddressSize-2] = 0xff
		} else {
			copy(out.Address.Addr[:], addr.Addr)
			if isLinkLocal(out.Address) {
				out.ZoneIndex = uint64(addr.NIC)
			}
		}
		return fidlnet.SocketAddressWithIpv6(out)
	default:
		panic(fmt.Sprintf("invalid protocol for conversion: %d", protocol))
	}
}

func bytesToAddressDroppingUnspecified(b []uint8) tcpip.Address {
	for _, e := range b {
		if e != 0 {
			return tcpip.Address(b)
		}
	}
	return ""
}

func toTCPIPFullAddress(addr fidlnet.SocketAddress) (tcpip.FullAddress, error) {
	switch w := addr.Which(); w {
	case fidlnet.SocketAddressIpv4:
		return tcpip.FullAddress{
			NIC:  0,
			Addr: bytesToAddressDroppingUnspecified(addr.Ipv4.Address.Addr[:]),
			Port: addr.Ipv4.Port,
		}, nil
	case fidlnet.SocketAddressIpv6:
		return tcpip.FullAddress{
			NIC:  tcpip.NICID(addr.Ipv6.ZoneIndex),
			Addr: bytesToAddressDroppingUnspecified(addr.Ipv6.Address.Addr[:]),
			Port: addr.Ipv6.Port,
		}, nil
	default:
		return tcpip.FullAddress{}, fmt.Errorf("invalid fuchsia.net/SocketAddress variant: %d", w)
	}
}

func toTcpIpAddressDroppingUnspecifiedv4(fidl fidlnet.Ipv4Address) tcpip.Address {
	return bytesToAddressDroppingUnspecified(fidl.Addr[:])
}

func toTcpIpAddressDroppingUnspecifiedv6(fidl fidlnet.Ipv6Address) tcpip.Address {
	return bytesToAddressDroppingUnspecified(fidl.Addr[:])
}
