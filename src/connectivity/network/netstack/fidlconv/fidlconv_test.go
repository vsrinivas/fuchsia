// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//go:build !build_with_native_toolchain

package fidlconv

import (
	"net"
	"testing"

	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/util"

	fidlnet "fidl/fuchsia/net"
	"fidl/fuchsia/net/stack"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
	"gvisor.dev/gvisor/pkg/tcpip"
)

// TODO(tkilbourn): Consider moving more of these tests to "table-driven" tests.
// This is challenging because of the way FIDL unions are constructed in Go.

func TestNetIPtoTCPIPAddressIPv4(t *testing.T) {
	from := fidlnet.IpAddress{}
	from.SetIpv4(fidlnet.Ipv4Address{Addr: [4]uint8{127, 0, 0, 1}})
	to := ToTCPIPAddress(from)
	expected := util.Parse("127.0.0.1")
	if to != expected {
		t.Fatalf("Expected:\n %v\nActual:\n %v", expected, to)
	}
}

func TestNetIPtoTCPIPAddressIPv6(t *testing.T) {
	from := fidlnet.IpAddress{}
	from.SetIpv6(fidlnet.Ipv6Address{Addr: [16]uint8{0xfe, 0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1}})
	to := ToTCPIPAddress(from)
	expected := util.Parse("fe80::1")
	if to != expected {
		t.Fatalf("Expected:\n %v\nActual:\n %v", expected, to)
	}
}

func TestToFIDLIPAddressIPv4(t *testing.T) {
	from := util.Parse("127.0.0.1")
	to := ToNetIpAddress(from)
	expected := fidlnet.IpAddress{}
	expected.SetIpv4(fidlnet.Ipv4Address{Addr: [4]uint8{127, 0, 0, 1}})
	if to != expected {
		t.Fatalf("Expected:\n %v\nActual:\n %v", expected, to)
	}
}

func TestToFIDLIPAddressIPv6(t *testing.T) {
	from := util.Parse("fe80::1")
	to := ToNetIpAddress(from)
	expected := fidlnet.IpAddress{}
	expected.SetIpv6(fidlnet.Ipv6Address{Addr: [16]uint8{0xfe, 0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1}})
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
		netSubnet := fidlnet.Subnet{
			PrefixLen: testCase.prefix,
		}
		netSubnet.Addr.SetIpv4(fidlnet.Ipv4Address{Addr: testCase.addr})
		to := ToTCPIPSubnet(netSubnet)
		_, ipNet, err := net.ParseCIDR(testCase.expected)
		if err != nil {
			t.Fatalf("Error creating tcpip.Subnet: %v", err)
		}
		expected, err := tcpip.NewSubnet(tcpip.Address(ipNet.IP), tcpip.AddressMask(ipNet.Mask))
		if err != nil {
			t.Fatal(err)
		}
		if to != expected {
			t.Errorf("Expected:\n {%v, %v}\nActual: {%v, %v}",
				[]byte(expected.ID()), []byte(expected.Mask()),
				[]byte(to.ID()), []byte(to.Mask()))
		}
	}
}

func TestForwardingEntryAndTcpipRouteConversions(t *testing.T) {
	const gateway = "efghijklmnopqrst"

	destination, err := tcpip.NewSubnet("\xab\xcd\x00\x00", "\xff\xff\xe0\x00")
	if err != nil {
		t.Fatal(err)
	}
	for _, tc := range []struct {
		dest func(*stack.ForwardingEntry)
		want tcpip.Route
	}{
		{
			dest: func(fe *stack.ForwardingEntry) {
				fe.DeviceId = 789
			},
			want: tcpip.Route{
				Destination: destination,
				NIC:         789,
			},
		},
		{
			dest: func(fe *stack.ForwardingEntry) {
				nextHop := ToNetIpAddress(gateway)
				fe.NextHop = &nextHop
			},
			want: tcpip.Route{
				Destination: destination,
				Gateway:     gateway,
			},
		},
		{
			dest: func(fe *stack.ForwardingEntry) {
				fe.DeviceId = 789
				nextHop := ToNetIpAddress(gateway)
				fe.NextHop = &nextHop
			},
			want: tcpip.Route{
				Destination: destination,
				Gateway:     gateway,
				NIC:         789,
			},
		},
	} {
		fe := stack.ForwardingEntry{
			Subnet: fidlnet.Subnet{
				Addr:      ToNetIpAddress(destination.ID()),
				PrefixLen: 19,
			},
		}
		tc.dest(&fe)
		got := ForwardingEntryToTCPIPRoute(fe)
		if got != tc.want {
			t.Errorf("got ForwardingEntryToTCPIPRoute(%v) = %v, want = %v", fe, got, tc.want)
		}
		roundtripFe := TCPIPRouteToForwardingEntry(got)
		if diff := cmp.Diff(roundtripFe, fe, cmpopts.IgnoreTypes(struct{}{})); diff != "" {
			t.Fatalf("forwarding entry mismatch (-want +got):\n%s", diff)
		}
	}
}

func TestToNetSocketAddress(t *testing.T) {
	tests := []struct {
		name     string
		fullAddr tcpip.FullAddress
		sockAddr fidlnet.SocketAddress
	}{
		{
			name: "IPv4",
			fullAddr: tcpip.FullAddress{
				Addr: "\xC0\xA8\x00\x01",
				Port: 8080,
			},
			sockAddr: func() fidlnet.SocketAddress {
				var a fidlnet.SocketAddress
				a.SetIpv4(fidlnet.Ipv4SocketAddress{
					Address: fidlnet.Ipv4Address{
						Addr: [4]uint8{192, 168, 0, 1},
					},
					Port: 8080,
				})
				return a
			}(),
		},
		{
			name: "IPv6 Global without NIC",
			fullAddr: tcpip.FullAddress{
				Addr: "\x20\x01\x48\x60\x48\x60\x00\x00\x00\x00\x00\x00\x00\x00\x88\x88",
				Port: 8080,
			},
			sockAddr: func() fidlnet.SocketAddress {
				var a fidlnet.SocketAddress
				a.SetIpv6(fidlnet.Ipv6SocketAddress{
					Address: fidlnet.Ipv6Address{
						Addr: [16]uint8{0x20, 0x01, 0x48, 0x60, 0x48, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x88, 0x88},
					},
					Port: 8080,
				})
				return a
			}(),
		},
		{
			name: "IPv6 Global with NIC",
			fullAddr: tcpip.FullAddress{
				Addr: "\x20\x01\x48\x60\x48\x60\x00\x00\x00\x00\x00\x00\x00\x00\x88\x88",
				Port: 8080,
				NIC:  2,
			},
			sockAddr: func() fidlnet.SocketAddress {
				var a fidlnet.SocketAddress
				a.SetIpv6(fidlnet.Ipv6SocketAddress{
					Address: fidlnet.Ipv6Address{
						Addr: [16]uint8{0x20, 0x01, 0x48, 0x60, 0x48, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x88, 0x88},
					},
					Port: 8080,
				})
				return a
			}(),
		},
		{
			name: "IPv6 LinkLocal Unicast with NIC",
			fullAddr: tcpip.FullAddress{
				Addr: "\xfe\x80\x48\x60\x48\x60\x00\x00\x00\x00\x00\x00\x00\x00\x88\x88",
				Port: 8080,
				NIC:  2,
			},
			sockAddr: func() fidlnet.SocketAddress {
				var a fidlnet.SocketAddress
				a.SetIpv6(fidlnet.Ipv6SocketAddress{
					Address: fidlnet.Ipv6Address{
						Addr: [16]uint8{0xfe, 0x80, 0x48, 0x60, 0x48, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x88, 0x88},
					},
					Port:      8080,
					ZoneIndex: 2,
				})
				return a
			}(),
		},
		{
			name: "IPv6 LinkLocal Multicast with NIC",
			fullAddr: tcpip.FullAddress{
				Addr: "\xff\x02\x48\x60\x48\x60\x00\x00\x00\x00\x00\x00\x00\x00\x88\x88",
				Port: 8080,
				NIC:  2,
			},
			sockAddr: func() fidlnet.SocketAddress {
				var a fidlnet.SocketAddress
				a.SetIpv6(fidlnet.Ipv6SocketAddress{
					Address: fidlnet.Ipv6Address{
						Addr: [16]uint8{0xff, 0x02, 0x48, 0x60, 0x48, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x88, 0x88},
					},
					Port:      8080,
					ZoneIndex: 2,
				})
				return a
			}(),
		},
	}

	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			if converted := ToNetSocketAddress(test.fullAddr); test.sockAddr != converted {
				t.Errorf("got ToNetSocketAddress(%+v) = %+v, want = %+v", test.fullAddr, converted, test.sockAddr)
			}
		})
	}
}

func TestBytesToAddressDroppingUnspecified(t *testing.T) {
	tests := []struct {
		name  string
		bytes []uint8
		addr  tcpip.Address
	}{
		{
			name:  "IPv4",
			bytes: []uint8{192, 0, 2, 1},
			addr:  tcpip.Address("\xC0\x00\x02\x01"),
		},
		{
			name:  "IPv4 Unspecified",
			bytes: []uint8{0, 0, 0, 0},
			addr:  tcpip.Address(""),
		},
		{
			name:  "IPv6",
			bytes: []uint8{0x20, 0x01, 0xdb, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01},
			addr:  tcpip.Address("\x20\x01\xdb\x80\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01"),
		},
		{
			name:  "IPv6 Unspecified",
			bytes: []uint8{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
			addr:  tcpip.Address(""),
		},
	}

	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			if got := BytesToAddressDroppingUnspecified(test.bytes); got != test.addr {
				t.Errorf("got BytesToAddressDroppingUnspecified(%+v) = %+v, want %+v", test.bytes, got, test.addr)
			}
		})
	}
}
