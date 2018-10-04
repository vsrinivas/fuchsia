// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fidlconv

import (
	"testing"

	"netstack/util"

	"fidl/fuchsia/net"
	"fidl/fuchsia/net/stack"

	"github.com/google/netstack/tcpip"
)

// TODO(tkilbourn): Consider moving more of these tests to "table-driven" tests.
// This is challenging because of the way FIDL unions are constructed in Go.

func TestNetIPtoTCPIPAddressIPv4(t *testing.T) {
	from := net.IpAddress{}
	from.SetIpv4(net.IPv4Address{Addr: [4]uint8{127, 0, 0, 1}})
	to := ToTCPIPAddress(from)
	expected := util.Parse("127.0.0.1")
	if to != expected {
		t.Fatalf("Expected:\n %v\nActual:\n %v", expected, to)
	}
}

func TestNetIPtoTCPIPAddressIPv6(t *testing.T) {
	from := net.IpAddress{}
	from.SetIpv6(net.IPv6Address{Addr: [16]uint8{0xfe, 0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1}})
	to := ToTCPIPAddress(from)
	expected := util.Parse("fe80::1")
	if to != expected {
		t.Fatalf("Expected:\n %v\nActual:\n %v", expected, to)
	}
}

func TestToFIDLIPAddressIPv4(t *testing.T) {
	from := util.Parse("127.0.0.1")
	to := ToNetIpAddress(from)
	expected := net.IpAddress{}
	expected.SetIpv4(net.IPv4Address{Addr: [4]uint8{127, 0, 0, 1}})
	if to != expected {
		t.Fatalf("Expected:\n %v\nActual:\n %v", expected, to)
	}
}

func TestToFIDLIPAddressIPv6(t *testing.T) {
	from := util.Parse("fe80::1")
	to := ToNetIpAddress(from)
	expected := net.IpAddress{}
	expected.SetIpv6(net.IPv6Address{Addr: [16]uint8{0xfe, 0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1}})
	if to != expected {
		t.Fatalf("Expected:\n %v\nActual:\n %v", expected, to)
	}
}

func TestToFIDLIPAddressEmptyInvalid(t *testing.T) {
	defer func() {
		if r := recover(); r == nil {
			t.Fatalf("Expected to fail on invalid address length")
		}
	}()

	from := tcpip.Address("")
	ToNetIpAddress(from)
	t.Errorf("Expected to fail on invalid address length")
}

func TestToFIDLIPAddressInvalidLength(t *testing.T) {
	defer func() {
		if r := recover(); r == nil {
			t.Fatalf("Expected to fail on invalid address length")
		}
	}()

	from := tcpip.Address("\x00\x00")
	ToNetIpAddress(from)
	t.Errorf("Expected to fail on invalid address length")
}

func TestToSubnets(t *testing.T) {
	from := []tcpip.Address{
		util.Parse("255.255.255.0"),
		util.Parse("ffff:ffff:ffff:ffff::"),
	}
	to, err := ToNetSubnets(from)
	if err != nil {
		t.Fatalf("Error in ToNetSubnets: %v", err)
	}

	ipv4Subnet := net.Subnet{}
	ipv4Subnet.Addr.SetIpv4(net.IPv4Address{Addr: [4]uint8{255, 255, 255, 0}})
	ipv4Subnet.PrefixLen = 24

	ipv6Subnet := net.Subnet{}
	ipv6Subnet.Addr.SetIpv6(net.IPv6Address{Addr: [16]uint8{255, 255, 255, 255, 255, 255, 255, 255,
		0, 0, 0, 0, 0, 0, 0, 0}})
	ipv6Subnet.PrefixLen = 64

	if len(to) != 2 {
		t.Fatalf("Expected 2 subnets, got: %v\n  %v", len(to), to)
	}
	if to[0] != ipv4Subnet {
		t.Errorf("Expected:\n %v\nActual: %v", ipv4Subnet, to[0])
	}
	if to[1] != ipv6Subnet {
		t.Errorf("Expected:\n %v\nActual: %v", ipv6Subnet, to[1])
	}
}

func TestToSubnetsInvalid(t *testing.T) {
	from := []tcpip.Address{
		util.Parse("255.255.255.0"),
		tcpip.Address(""),
	}
	to, err := ToNetSubnets(from)
	if to != nil {
		t.Errorf("Expected ToNetSubnets to return a nil array of subnets")
	}
	if err == nil {
		t.Fatalf("Expected ToNetSubnets to fail with an invalid address")
	}
}

func TestToTCPIPSubnet(t *testing.T) {
	cases := []struct {
		addr     [4]uint8
		prefix   uint8
		expected string
	}{
		{[4]uint8{255, 255, 255, 255}, 32, "255.255.255.255/32"},
		{[4]uint8{255, 255, 255, 254}, 31, "255.255.255.254/31"},
		{[4]uint8{255, 255, 255, 0}, 24, "255.255.255.0/24"},
		{[4]uint8{255, 0, 0, 0}, 8, "255.0.0.0/8"},
		{[4]uint8{128, 0, 0, 0}, 1, "128.0.0.0/1"},
		{[4]uint8{0, 0, 0, 0}, 0, "0.0.0.0/0"},
	}
	for _, testCase := range cases {
		netSubnet := newNetSubnet(testCase.addr, testCase.prefix)
		to, err := ToTCPIPSubnet(netSubnet)
		if err != nil {
			t.Errorf("Error generating tcpip.Subnet: %v", err)
			continue
		}
		_, expected, err := util.ParseCIDR(testCase.expected)
		if err != nil {
			t.Fatalf("Error creating tcpip.Subnet: %v", err)
		}
		if to != expected {
			t.Errorf("Expected:\n {%v, %v}\nActual: {%v, %v}",
				[]byte(expected.ID()), []byte(expected.Mask()),
				[]byte(to.ID()), []byte(to.Mask()))
		}
	}
}

func TestPrefixLenIPv4(t *testing.T) {
	cases := []struct {
		mask   tcpip.Address
		prefix uint8
	}{
		{util.Parse("255.255.255.255"), 32},
		{util.Parse("255.255.255.254"), 31},
		{util.Parse("255.255.255.128"), 25},
		{util.Parse("255.255.255.0"), 24},
		{util.Parse("255.0.0.0"), 8},
		{util.Parse("128.0.0.0"), 1},
		{util.Parse("0.0.0.0"), 0},
	}
	for _, testCase := range cases {
		prefixLen := GetPrefixLen(testCase.mask)
		if prefixLen != testCase.prefix {
			t.Errorf("Expected:\n %v\nActual: %v", testCase.prefix, prefixLen)
		}
	}
}

func TestPrefixLenIPv6(t *testing.T) {
	cases := []struct {
		mask   tcpip.Address
		prefix uint8
	}{
		{util.Parse("ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff"), 128},
		{util.Parse("ffff:ffff:ffff:ffff:ffff:ffff:ffff:8000"), 113},
		{util.Parse("ffff:ffff:ffff:ffff:ffff:ffff:ffff::"), 112},
		{util.Parse("ffff:ffff:ffff:ffff:ffff:ffff:fffe::"), 111},
		{util.Parse("8000::"), 1},
		{util.Parse("::"), 0},
	}
	for _, testCase := range cases {
		prefixLen := GetPrefixLen(testCase.mask)
		if prefixLen != testCase.prefix {
			t.Errorf("Case: %v\n Expected:\n %v\nActual: %v", testCase.mask, testCase.prefix, prefixLen)
		}
	}
}

func TestPrefixLenEmptyInvalid(t *testing.T) {
	defer func() {
		if r := recover(); r == nil {
			t.Fatalf("Expected to fail on invalid address length")
		}
	}()

	GetPrefixLen(tcpip.Address(""))
	t.Errorf("Expected to fail on invalid address length")
}

func TestPrefixLenInvalidLength(t *testing.T) {
	defer func() {
		if r := recover(); r == nil {
			t.Fatalf("Expected to fail on invalid address length")
		}
	}()

	GetPrefixLen(tcpip.Address("\x00\x00"))
	t.Errorf("Expected to fail on invalid address length")
}

func TestForwardingEntryAndTcpipRouteConversions(t *testing.T) {
	for _, tc := range []struct {
		dest stack.ForwardingDestination
		want tcpip.Route
	}{
		{dest: func() stack.ForwardingDestination {
			var dest stack.ForwardingDestination
			dest.SetDeviceId(789)
			return dest
		}(),
			want: tcpip.Route{
				Destination: tcpip.Address("abcd"),
				Mask:        "\xff\xff\xe0\x00",
				NIC:         789,
			}},
		{dest: func() stack.ForwardingDestination {
			var dest stack.ForwardingDestination
			dest.SetNextHop(ToNetIpAddress(tcpip.Address("efghijklmnopqrst")))
			return dest
		}(),
			want: tcpip.Route{
				Destination: tcpip.Address("abcd"),
				Mask:        "\xff\xff\xe0\x00",
				Gateway:     tcpip.Address("efghijklmnopqrst"),
			}},
	} {
		fe := stack.ForwardingEntry{
			Subnet: net.Subnet{
				Addr:      ToNetIpAddress(tcpip.Address("abcd")),
				PrefixLen: 19,
			},
			Destination: tc.dest,
		}
		got := ForwardingEntryToTcpipRoute(fe)
		if got != tc.want {
			t.Errorf("got ForwardingEntryToTcpipRoute(%v) = %v, want = %v", fe, got, tc.want)
		}
		roundtripFe := TcpipRouteToForwardingEntry(got)
		if roundtripFe != fe {
			t.Errorf("got TcpipRouteToForwardingEntry(%+v) = %+v, want = %+v", got, roundtripFe, fe)
		}
	}
}

func newNetSubnet(addr [4]uint8, prefix uint8) net.Subnet {
	subnet := net.Subnet{}
	subnet.Addr.SetIpv4(net.IPv4Address{Addr: addr})
	subnet.PrefixLen = prefix
	return subnet
}
