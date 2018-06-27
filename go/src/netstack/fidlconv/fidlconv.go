// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fidlconv

import (
	"bytes"
	"encoding/binary"
	"fmt"
	"math/big"

	"fidl/fuchsia/net"

	"github.com/google/netstack/tcpip"
)

func ToTCPIPAddress(addr net.IpAddress) tcpip.Address {
	out := tcpip.Address("")
	switch addr.Which() {
	case net.IpAddressIpv4:
		out = tcpip.Address(addr.Ipv4.Addr[:])
	case net.IpAddressIpv6:
		out = tcpip.Address(addr.Ipv6.Addr[:])
	}
	return out
}

func ToNetIpAddress(addr tcpip.Address) net.IpAddress {
	var out net.IpAddress
	switch len(addr) {
	case 4:
		out.SetIpv4(net.IPv4Address{Addr: [4]uint8{}})
		copy(out.Ipv4.Addr[:], addr[:])
	case 16:
		out.SetIpv6(net.IPv6Address{Addr: [16]uint8{}})
		copy(out.Ipv6.Addr[:], addr[:])
	default:
		panic("invalid tcpip.Address length")
	}
	return out
}

func ToNetSubnets(addrs []tcpip.Address) ([]net.Subnet, error) {
	out := make([]net.Subnet, len(addrs))
	total := 0
	for i := range addrs {
		if len(addrs[i]) != 4 && len(addrs[i]) != 16 {
			return nil, fmt.Errorf("Invalid subnet: %v", addrs[i])
		}
		out[total] = net.Subnet{Addr: ToNetIpAddress(addrs[i]), PrefixLen: GetPrefixLen(addrs[i])}
		total += 1
	}
	return out[:total], nil
}

func GetPrefixLen(mask tcpip.Address) uint8 {
	prefixLen := uint8(0)
	switch len(mask) {
	case 4:
		var x uint32
		if err := binary.Read(bytes.NewReader([]byte(mask)), binary.BigEndian, &x); err != nil {
			return 0
		}
		if x == 0 {
			return 0
		}
		for x&1 == 0 {
			prefixLen += 1
			x >>= 1
		}
		return 32 - prefixLen
	case 16:
		var x big.Int
		zero := big.NewInt(0)
		one := big.NewInt(1)
		x.SetBytes([]byte(mask))

		if x.Cmp(zero) == 0 {
			return 0
		}
		var tmp big.Int
		for tmp.And(&x, one).Cmp(zero) == 0 {
			prefixLen += 1
			x.Rsh(&x, 1)
		}
		return 128 - prefixLen
	default:
		panic("invalid tcpip.Address length")
	}
}
