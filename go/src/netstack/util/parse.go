// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package util

import (
	"fmt"

	"errors"
	"github.com/google/netstack/tcpip"
	"strconv"
	"strings"
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
	for i := 0; i < len(src); i++ {
		switch src[i] {
		case '.':
			return parseIP4(src)
		case ':':
			return parseIP6(src)
		}
	}
	return ""
}

func parseIP4(src string) tcpip.Address {
	var addr [4]byte
	_, err := fmt.Sscanf(src, "%d.%d.%d.%d", &addr[0], &addr[1], &addr[2], &addr[3])
	if err != nil {
		return ""
	}
	return tcpip.Address(addr[:])
}

func parseIP6(src string) (res tcpip.Address) {
	a := make([]byte, 0, 16) // cap(a) is constant throughout
	expansion := -1          // index of '::' expansion in a

	if len(src) >= 2 && src[:2] == "::" {
		if len(src) == 2 {
			return tcpip.Address(a[:cap(a)])
		}
		expansion = 0
		src = src[2:]
	}

	for len(a) < cap(a) && len(src) > 0 {
		var x uint16
		var ok bool
		x, src, ok = parseHex(src)
		if !ok {
			return ""
		}
		a = append(a, uint8(x>>8), uint8(x))

		if len(src) == 0 {
			break
		}

		// Next is either ":..." or "::[...]".
		if src[0] != ':' || len(src) == 1 {
			return ""
		}
		src = src[1:]
		if src[0] == ':' {
			if expansion >= 0 {
				return "" // only one expansion allowed
			}
			expansion = len(a)
			src = src[1:]
		}
	}
	if len(src) != 0 {
		return ""
	}

	if missing := cap(a) - len(a); missing > 0 {
		if expansion < 0 {
			return ""
		}
		a = a[:cap(a)]
		copy(a[expansion+missing:], a[expansion:])
		for i := 0; i < missing; i++ {
			a[i+expansion] = 0
		}
	}

	return tcpip.Address(a)
}

func parseHex(src string) (x uint16, remaining string, ok bool) {
	if len(src) == 0 {
		return 0, src, false
	}
loop:
	for len(src) > 0 {
		v := src[0]
		switch {
		case '0' <= v && v <= '9':
			v = v - '0'
		case 'a' <= v && v <= 'f':
			v = v - 'a' + 10
		case 'A' <= v && v <= 'F':
			v = v - 'A' + 10
		case v == ':':
			break loop
		default:
			return 0, src, false
		}
		src = src[1:]
		x = (x << 4) | uint16(v)
	}
	return x, src, true
}
