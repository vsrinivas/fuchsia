// Copyright 2018 The Fuchsia Authors. All rights reserved.

// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package util

import (
	"net"

	"github.com/google/netstack/tcpip"
)

func IsAny(a tcpip.Address) bool {
	// An empty address is not the same as ANY.
	if len(a) == 0 {
		return false
	}
	for _, n := range a {
		if n != 0 {
			return false
		}
	}
	return true
}

func ApplyMask(addr tcpip.Address, mask tcpip.AddressMask) tcpip.Address {
	return tcpip.Address(net.IP(addr).Mask(net.IPMask(mask)))
}

func CIDRMask(ones, bits int) tcpip.AddressMask {
	return tcpip.AddressMask(net.CIDRMask(ones, bits))
}

func PrefixLength(mask tcpip.AddressMask) int {
	bits, _ := net.IPMask(mask).Size()
	return bits
}

func ipToAddress(ip net.IP) tcpip.Address {
	if v4 := ip.To4(); v4 != nil {
		return tcpip.Address(v4)
	}
	return tcpip.Address(ip)
}

func ParseCIDR(s string) (tcpip.Address, tcpip.Subnet, error) {
	ip, subnet, err := net.ParseCIDR(s)
	if err != nil {
		return "", tcpip.Subnet{}, err
	}
	sn, err := tcpip.NewSubnet(tcpip.Address(subnet.IP), tcpip.AddressMask(subnet.Mask))
	if err != nil {
		return "", tcpip.Subnet{}, err
	}
	return ipToAddress(ip), sn, nil
}

// Parse parses the string representation of an IPv4 or IPv6 address.
func Parse(src string) tcpip.Address {
	return ipToAddress(net.ParseIP(src))
}
