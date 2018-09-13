// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package connectivity

import (
	"net"
	"testing"

	netfidl "fidl/fuchsia/net"
	"fidl/fuchsia/netstack"

	"netstack/fidlconv"

	"gvisor.dev/gvisor/pkg/tcpip"
)

func newV4Address(a, b, c, d uint8) netfidl.IpAddress {
	return fidlconv.ToNetIpAddress(tcpip.Address(net.IPv4(a, b, c, d).To4()))
}

func TestHasDHCPAddress(t *testing.T) {
	for _, tc := range []struct {
		name  string
		iface netstack.NetInterface2
		want  bool
	}{
		{
			name: "DHCPEnabledNoAddress",
			iface: netstack.NetInterface2{
				Flags:     netstack.NetInterfaceFlagDhcp | netstack.NetInterfaceFlagUp,
				Addr:      newV4Address(0, 0, 0, 0),
				Netmask:   newV4Address(0, 0, 0, 0),
				Broadaddr: newV4Address(0, 0, 0, 0),
				Hwaddr:    []uint8{0, 1, 2, 3, 4, 5},
			},
			want: false,
		},
		{
			name: "StaticAddress",
			iface: netstack.NetInterface2{
				Flags:     netstack.NetInterfaceFlagUp,
				Addr:      newV4Address(192, 168, 42, 10),
				Netmask:   newV4Address(255, 255, 255, 0),
				Broadaddr: newV4Address(255, 255, 255, 0),
				Hwaddr:    []uint8{1, 2, 3, 4, 5, 6},
			},
			want: false,
		},
		{
			name: "DHCPEnabledWithAddress",
			iface: netstack.NetInterface2{
				Flags:     netstack.NetInterfaceFlagDhcp | netstack.NetInterfaceFlagUp,
				Addr:      newV4Address(10, 0, 0, 1),
				Netmask:   newV4Address(255, 255, 255, 0),
				Broadaddr: newV4Address(255, 255, 255, 0),
				Hwaddr:    []uint8{1, 2, 3, 4, 5, 6},
			},
			want: true,
		},
	} {
		t.Run(tc.name, func(t *testing.T) {
			if got := hasDHCPAddress(tc.iface); got != tc.want {
				t.Fatalf("got %t, want %t; input: %#v", got, tc.want, tc.iface)
			}
		})
	}
}
