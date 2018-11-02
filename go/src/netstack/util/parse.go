// Copyright 2018 The Fuchsia Authors. All rights reserved.

// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package util

import (
	"net"

	"errors"
	"strconv"
	"strings"

	"github.com/google/netstack/tcpip"
)

func ApplyMask(addr tcpip.Address, mask tcpip.AddressMask) tcpip.Address {
	if len(addr) != len(mask) {
		return ""
	}
	subnet := []byte(addr)
	for i := 0; i < len(subnet); i++ {
		subnet[i] &= mask[i]
	}
	return tcpip.Address(subnet)
}

// Copied from pkg net to avoid taking a dependency.
func CIDRMask(ones, bits int) tcpip.AddressMask {
	// header.IPv4AddressSize, header.IPv6AddressSize
	if bits != 8*4 && bits != 8*16 {
		return tcpip.AddressMask("")
	}
	if ones < 0 || ones > bits {
		return tcpip.AddressMask("")
	}
	l := bits / 8
	m := make([]byte, l)
	n := uint(ones)
	for i := 0; i < l; i++ {
		if n >= 8 {
			m[i] = 0xff
			n -= 8
			continue
		}
		m[i] = ^byte(0xff >> n)
		n = 0
	}
	return tcpip.AddressMask(m)
}

var errInvalidCIDRNotation = errors.New("CIDR notation invalid")

func ParseCIDR(subnet string) (tcpip.Address, tcpip.Subnet, error) {
	split := strings.Split(subnet, "/")
	if len(split) != 2 {
		return tcpip.Address(""), tcpip.Subnet{}, errInvalidCIDRNotation
	}
	addr := Parse(split[0])
	ones, err := strconv.ParseInt(split[1], 10, 8)

	if err != nil {
		return tcpip.Address(""), tcpip.Subnet{}, err
	}

	mask := CIDRMask(int(ones), 8*len(addr))
	sn, err := tcpip.NewSubnet(ApplyMask(addr, mask), mask)
	return addr, sn, err
}

// Parse parses the string representation of an IPv4 or IPv6 address.
func Parse(src string) tcpip.Address {
	ip := net.ParseIP(src)
	if ip == nil {
		return ""
	}
	if ip4 := ip.To4(); ip4 != nil {
		return tcpip.Address(ip4)
	}
	return tcpip.Address(ip)
}
