// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package filter

import (
	"net"
	"reflect"
	"testing"

	"netstack/util"

	fidlnet "fidl/fuchsia/net"
	"fidl/fuchsia/net/filter"

	"gvisor.dev/gvisor/pkg/tcpip"
	"gvisor.dev/gvisor/pkg/tcpip/header"
)

func TestFromAction(t *testing.T) {
	var tests = []struct {
		description string
		action      Action
		netAction   filter.Action
		err         error
	}{
		{
			"pass",
			Pass,
			filter.ActionPass,
			nil,
		},
		{
			"drop",
			Drop,
			filter.ActionDrop,
			nil,
		},
		{
			"dropreset",
			DropReset,
			filter.ActionDropReset,
			nil,
		},
		{
			"9999",
			Action(9999),
			filter.Action(0),
			ErrUnknownAction,
		},
	}

	for _, test := range tests {
		t.Run(test.description, func(t *testing.T) {
			netAction, err := fromAction(test.action)
			if err != test.err {
				t.Errorf("err=%v, want=%v", err, test.err)
			}
			if err == nil {
				if got, want := netAction, test.netAction; got != want {
					t.Errorf("got=%s, want=%s", got, want)
				}
			}
		})
	}
}

func TestToAction(t *testing.T) {
	var tests = []struct {
		description string
		netAction   filter.Action
		action      Action
		err         error
	}{
		{
			"pass",
			filter.ActionPass,
			Pass,
			nil,
		},
		{
			"drop",
			filter.ActionDrop,
			Drop,
			nil,
		},
		{
			"reset",
			filter.ActionDropReset,
			DropReset,
			nil,
		},
		{
			"9999",
			filter.Action(9999),
			Action(0),
			ErrUnknownAction,
		},
	}

	for _, test := range tests {
		t.Run(test.description, func(t *testing.T) {
			action, err := toAction(test.netAction)
			if err != test.err {
				t.Errorf("err=%v, want=%v", err, test.err)
			}
			if err == nil {
				if got, want := action, test.action; got != want {
					t.Errorf("got=%s, want=%s", got, want)
				}
			}
		})
	}
}

func TestFromDirection(t *testing.T) {
	var tests = []struct {
		description  string
		direction    Direction
		netDirection filter.Direction
		err          error
	}{
		{
			"incoming",
			Incoming,
			filter.DirectionIncoming,
			nil,
		},
		{
			"outgoing",
			Outgoing,
			filter.DirectionOutgoing,
			nil,
		},
		{
			"9999",
			Direction(9999),
			filter.Direction(0),
			ErrUnknownDirection,
		},
	}

	for _, test := range tests {
		t.Run(test.description, func(t *testing.T) {
			netDirection, err := fromDirection(test.direction)
			if err != test.err {
				t.Errorf("err=%v, want=%v", err, test.err)
			}
			if err == nil {
				if got, want := netDirection, test.netDirection; got != want {
					t.Errorf("got=%s, want=%s", got, want)
				}
			}
		})
	}
}

func TestToDirection(t *testing.T) {
	var tests = []struct {
		description  string
		netDirection filter.Direction
		direction    Direction
		err          error
	}{
		{
			"incoming",
			filter.DirectionIncoming,
			Incoming,
			nil,
		},
		{
			"outgoing",
			filter.DirectionOutgoing,
			Outgoing,
			nil,
		},
		{
			"9999",
			filter.Direction(9999),
			Direction(0),
			ErrUnknownDirection,
		},
	}

	for _, test := range tests {
		t.Run(test.description, func(t *testing.T) {
			direction, err := toDirection(test.netDirection)
			if err != test.err {
				t.Errorf("err=%v, want=%v", err, test.err)
			}
			if err == nil {
				if got, want := direction, test.direction; got != want {
					t.Errorf("got=%s, want=%s", got, want)
				}
			}
		})
	}
}

func TestFromTransProto(t *testing.T) {
	var tests = []struct {
		description   string
		transProto    tcpip.TransportProtocolNumber
		netTransProto filter.SocketProtocol
		err           error
	}{
		{
			"any",
			0,
			filter.SocketProtocolAny,
			nil,
		},
		{
			"icmpv4",
			header.ICMPv4ProtocolNumber,
			filter.SocketProtocolIcmp,
			nil,
		},
		{
			"icmpv6",
			header.ICMPv6ProtocolNumber,
			filter.SocketProtocolIcmpv6,
			nil,
		},
		{
			"tcp",
			header.TCPProtocolNumber,
			filter.SocketProtocolTcp,
			nil,
		},
		{
			"udp",
			header.UDPProtocolNumber,
			filter.SocketProtocolUdp,
			nil,
		},
		{
			"9999",
			tcpip.TransportProtocolNumber(9999),
			filter.SocketProtocol(0),
			ErrUnknownProtocol,
		},
	}

	for _, test := range tests {
		netTransProto, err := fromTransProto(test.transProto)
		if err != test.err {
			t.Errorf("err=%v, want=%v", err, test.err)
		}
		if err == nil {
			if got, want := netTransProto, test.netTransProto; got != want {
				t.Errorf("got=%s, want=%s", got, want)
			}
		}
	}
}

func TestToTransProto(t *testing.T) {
	var tests = []struct {
		description   string
		netTransProto filter.SocketProtocol
		transProto    tcpip.TransportProtocolNumber
		err           error
	}{
		{
			"any",
			filter.SocketProtocolAny,
			0,
			nil,
		},
		{
			"tcp",
			filter.SocketProtocolTcp,
			header.TCPProtocolNumber,
			nil,
		},
		{
			"udp",
			filter.SocketProtocolUdp,
			header.UDPProtocolNumber,
			nil,
		},
		{
			"icmpv4",
			filter.SocketProtocolIcmp,
			header.ICMPv4ProtocolNumber,
			nil,
		},
		{
			"icmpv6",
			filter.SocketProtocolIcmpv6,
			header.ICMPv6ProtocolNumber,
			nil,
		},
		{
			"9999",
			filter.SocketProtocol(9999),
			0,
			ErrUnknownProtocol,
		},
	}

	for _, test := range tests {
		t.Run(test.description, func(t *testing.T) {
			transProto, err := toTransProto(test.netTransProto)
			if err != test.err {
				t.Errorf("err=%v, want=%v", err, test.err)
			}
			if err == nil {
				if got, want := transProto, test.transProto; got != want {
					t.Errorf("got=%v, want=%v", got, want)
				}
			}
		})
	}
}

func TestFromAddress(t *testing.T) {
	var tests = []struct {
		description string
		address     tcpip.Address
		netAddress  fidlnet.IpAddress
		err         error
	}{
		{
			"ipv4",
			util.Parse("1.2.3.4"),
			func() fidlnet.IpAddress {
				var addr fidlnet.IpAddress
				addr.SetIpv4(fidlnet.Ipv4Address{Addr: [4]uint8{1, 2, 3, 4}})
				return addr
			}(),
			nil,
		},
		{
			"ipv6",
			util.Parse("a:b:c::2:3:4"),
			func() fidlnet.IpAddress {
				var addrV6 fidlnet.Ipv6Address
				copy(addrV6.Addr[:], "\x00\x0a\x00\x0b\x00\x0c\x00\x00\x00\x00\x00\x02\x00\x03\x00\x04")
				var addr fidlnet.IpAddress
				addr.SetIpv6(addrV6)
				return addr
			}(),
			nil,
		},
		{
			"unknown address type",
			tcpip.Address("12345"),
			fidlnet.IpAddress{},
			ErrUnknownAddressType,
		},
	}

	for _, test := range tests {
		t.Run(test.description, func(t *testing.T) {
			netAddress, err := fromAddress(test.address)
			if err != test.err {
				t.Errorf("err=%v, want=%v", err, test.err)
			}
			if err == nil {
				got, want := netAddress, test.netAddress
				if got.Which() != want.Which() {
					t.Errorf("got.Which()=%v, want.Which()=%v", got.Which(), want.Which())
				}
				switch got.Which() {
				case fidlnet.IpAddressIpv4:
					if got.Ipv4 != want.Ipv4 {
						t.Errorf("got.Ipv4=%s, want.Ipv4=%s", got.Ipv4, want.Ipv4)
					}
				case fidlnet.IpAddressIpv6:
					if got.Ipv6 != want.Ipv6 {
						t.Errorf("got.Ipv6=%s, want.Ipv6=%s", got.Ipv6, want.Ipv6)
					}
				default:
					t.Errorf("unxpected AddressType: got.Which()=%v", got.Which())
				}
			}
		})
	}
}

func TestToAddress(t *testing.T) {
	var tests = []struct {
		description string
		netAddress  fidlnet.IpAddress
		address     tcpip.Address
		err         error
	}{
		{
			"ipv4",
			func() fidlnet.IpAddress {
				var addr fidlnet.IpAddress
				addr.SetIpv4(fidlnet.Ipv4Address{Addr: [4]uint8{1, 2, 3, 4}})
				return addr
			}(),
			util.Parse("1.2.3.4"),
			nil,
		},
		{
			"ipv6",
			func() fidlnet.IpAddress {
				var addrV6 fidlnet.Ipv6Address
				copy(addrV6.Addr[:], "\x00\x0a\x00\x0b\x00\x0c\x00\x00\x00\x00\x00\x02\x00\x03\x00\x04")
				var addr fidlnet.IpAddress
				addr.SetIpv6(addrV6)
				return addr
			}(),
			util.Parse("a:b:c::2:3:4"),
			nil,
		},
		{
			"unknown address type",
			fidlnet.IpAddress{},
			tcpip.Address(""),
			ErrUnknownAddressType,
		},
	}

	for _, test := range tests {
		t.Run(test.description, func(t *testing.T) {
			address, err := toAddress(&test.netAddress)
			if err != test.err {
				t.Errorf("err=%v, want=%v", err, test.err)
			}
			if err == nil {
				if got, want := address, test.address; got != want {
					t.Errorf("got=%s, want=%s", got, want)
				}
			}
		})
	}
}

func parseCIDR(s string) (tcpip.Subnet, error) {
	_, subnet, err := net.ParseCIDR(s)
	if err != nil {
		return tcpip.Subnet{}, err
	}
	return tcpip.NewSubnet(tcpip.Address(subnet.IP), tcpip.AddressMask(subnet.Mask))
}

func TestFromSubnet(t *testing.T) {
	var tests = []struct {
		description string
		subnet      tcpip.Subnet
		netSubnet   fidlnet.Subnet
		err         error
	}{
		{
			"test1",
			func() tcpip.Subnet {
				subnet, err := parseCIDR("1.2.3.4/32")
				if err != nil {
					t.Errorf("ParseCIDR error: %s", err)
				}
				return subnet
			}(),
			func() fidlnet.Subnet {
				var addr fidlnet.IpAddress
				addr.SetIpv4(fidlnet.Ipv4Address{Addr: [4]uint8{1, 2, 3, 4}})
				subnet := fidlnet.Subnet{Addr: addr, PrefixLen: 32}
				return subnet
			}(),
			nil,
		},
		{
			"test2",
			func() tcpip.Subnet {
				subnet, err := parseCIDR("10.0.0.0/8")
				if err != nil {
					t.Errorf("ParseCIDR error: %s", err)
				}
				return subnet
			}(),
			func() fidlnet.Subnet {
				var addr fidlnet.IpAddress
				addr.SetIpv4(fidlnet.Ipv4Address{Addr: [4]uint8{10, 0, 0, 0}})
				subnet := fidlnet.Subnet{Addr: addr, PrefixLen: 8}
				return subnet
			}(),
			nil,
		},
		{
			"test3",
			func() tcpip.Subnet {
				subnet, err := parseCIDR("a:b:c::/96")
				if err != nil {
					t.Errorf("ParseCIDR error: %s", err)
				}
				return subnet
			}(),
			func() fidlnet.Subnet {
				var addrV6 fidlnet.Ipv6Address
				copy(addrV6.Addr[:], "\x00\x0a\x00\x0b\x00\x0c\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00")
				var addr fidlnet.IpAddress
				addr.SetIpv6(addrV6)
				subnet := fidlnet.Subnet{Addr: addr, PrefixLen: 96}
				return subnet
			}(),
			nil,
		},
	}

	for _, test := range tests {
		t.Run(test.description, func(t *testing.T) {
			netSubnet, err := fromSubnet(&test.subnet)
			if err != test.err {
				t.Errorf("err=%v, want=%v", err, test.err)
			}
			if err == nil {
				got, want := netSubnet, test.netSubnet
				if got.Addr.Which() != want.Addr.Which() {
					t.Errorf("got.Addr.Which()=%v, want.Addr.Which()=%v", got.Addr.Which(), want.Addr.Which())
				}
				switch got.Addr.Which() {
				case fidlnet.IpAddressIpv4:
					if got.Addr.Ipv4 != want.Addr.Ipv4 {
						t.Errorf("got.Addr.Ipv4=%s, want.Addr.Ipv4=%s", got.Addr.Ipv4, want.Addr.Ipv4)
					}
				case fidlnet.IpAddressIpv6:
					if got.Addr.Ipv6 != want.Addr.Ipv6 {
						t.Errorf("got.Addr.Ipv6=%s, want.Addr.Ipv6=%s", got.Addr.Ipv6, want.Addr.Ipv6)
					}
				default:
					t.Errorf("unxpected SubnetType: got.Addr.Which()=%v", got.Addr.Which())
				}
				if got.PrefixLen != want.PrefixLen {
					t.Errorf("got.PrefixLen=%d, want.PrefixLen=%d", got.PrefixLen, want.PrefixLen)
				}
			}
		})
	}
}

func TestFromRules(t *testing.T) {
	srcSubnetV4, err := parseCIDR("1.2.3.4/32")
	if err != nil {
		t.Errorf("ParseCIDR error: %s", err)
	}
	dstSubnet, err := parseCIDR("5.6.7.8/32")
	if err != nil {
		t.Errorf("ParseCIDR error: %s", err)
	}
	srcSubnetV6, err := parseCIDR("0102:0304:0506:0708:090a:0b0c:0d0e:0f10/128")
	if err != nil {
		t.Errorf("ParseCIDR error: %s", err)
	}
	dstSubnetV6, err := parseCIDR("1112:1314:1516:1718:191a:1b1c:1d1e:1f20/128")
	if err != nil {
		t.Errorf("ParseCIDR error: %s", err)
	}

	netSrcSubnet := fidlnet.Subnet{PrefixLen: 32}
	netSrcSubnet.Addr.SetIpv4(fidlnet.Ipv4Address{Addr: [4]uint8{1, 2, 3, 4}})
	netDstSubnetV4 := fidlnet.Subnet{PrefixLen: 32}
	netDstSubnetV4.Addr.SetIpv4(fidlnet.Ipv4Address{Addr: [4]uint8{5, 6, 7, 8}})
	netSrcSubnetV6 := fidlnet.Subnet{PrefixLen: 128}
	netSrcSubnetV6.Addr.SetIpv6(fidlnet.Ipv6Address{Addr: [16]uint8{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16}})
	netDstSubnetV6 := fidlnet.Subnet{PrefixLen: 128}
	netDstSubnetV6.Addr.SetIpv6(fidlnet.Ipv6Address{Addr: [16]uint8{17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32}})

	var tests = []struct {
		description string
		rules       []Rule
		newRules    []filter.Rule
		err         error
	}{
		{
			"4 rules",
			[]Rule{
				{
					action:               Drop,
					direction:            Incoming,
					quick:                false,
					transProto:           header.TCPProtocolNumber,
					srcSubnet:            &srcSubnetV4,
					srcSubnetInvertMatch: false,
					srcPortRange:         PortRange{1000, 1001},
					dstSubnet:            &dstSubnet,
					dstSubnetInvertMatch: false,
					dstPortRange:         PortRange{1500, 1501},
					nic:                  tcpip.NICID(1),
					log:                  false,
					keepState:            false,
				},
				{
					action:               Drop,
					direction:            Incoming,
					quick:                false,
					transProto:           header.TCPProtocolNumber,
					srcSubnet:            &srcSubnetV6,
					srcSubnetInvertMatch: false,
					srcPortRange:         PortRange{1000, 1001},
					dstSubnet:            &dstSubnetV6,
					dstSubnetInvertMatch: false,
					dstPortRange:         PortRange{1500, 1501},
					nic:                  tcpip.NICID(1),
					log:                  false,
					keepState:            false,
				},
				{
					action:               Pass,
					direction:            Outgoing,
					quick:                true,
					transProto:           header.UDPProtocolNumber,
					srcSubnet:            &srcSubnetV4,
					srcSubnetInvertMatch: true,
					srcPortRange:         PortRange{2000, 2001},
					dstSubnet:            &dstSubnet,
					dstSubnetInvertMatch: true,
					dstPortRange:         PortRange{2500, 2501},
					nic:                  tcpip.NICID(2),
					log:                  true,
					keepState:            true,
				},
				{
					action:               Pass,
					direction:            Outgoing,
					quick:                true,
					transProto:           header.UDPProtocolNumber,
					srcSubnet:            nil,
					srcSubnetInvertMatch: true,
					srcPortRange:         PortRange{2000, 2001},
					dstSubnet:            nil,
					dstSubnetInvertMatch: true,
					dstPortRange:         PortRange{2500, 2501},
					nic:                  tcpip.NICID(3),
					log:                  true,
					keepState:            true,
				},
			},
			[]filter.Rule{
				{
					Action:               filter.ActionDrop,
					Direction:            filter.DirectionIncoming,
					Quick:                false,
					Proto:                filter.SocketProtocolTcp,
					SrcSubnet:            &netSrcSubnet,
					SrcSubnetInvertMatch: false,
					SrcPortRange:         filter.PortRange{Start: 1000, End: 1001},
					DstSubnet:            &netDstSubnetV4,
					DstSubnetInvertMatch: false,
					DstPortRange:         filter.PortRange{Start: 1500, End: 1501},
					Nic:                  1,
					Log:                  false,
					KeepState:            false,
				},
				{
					Action:               filter.ActionDrop,
					Direction:            filter.DirectionIncoming,
					Quick:                false,
					Proto:                filter.SocketProtocolTcp,
					SrcSubnet:            &netSrcSubnetV6,
					SrcSubnetInvertMatch: false,
					SrcPortRange:         filter.PortRange{Start: 1000, End: 1001},
					DstSubnet:            &netDstSubnetV6,
					DstSubnetInvertMatch: false,
					DstPortRange:         filter.PortRange{Start: 1500, End: 1501},
					Nic:                  1,
					Log:                  false,
					KeepState:            false,
				},
				{
					Action:               filter.ActionPass,
					Direction:            filter.DirectionOutgoing,
					Quick:                true,
					Proto:                filter.SocketProtocolUdp,
					SrcSubnet:            &netSrcSubnet,
					SrcSubnetInvertMatch: true,
					SrcPortRange:         filter.PortRange{Start: 2000, End: 2001},
					DstSubnet:            &netDstSubnetV4,
					DstSubnetInvertMatch: true,
					DstPortRange:         filter.PortRange{Start: 2500, End: 2501},
					Nic:                  2,
					Log:                  true,
					KeepState:            true,
				},
				{
					Action:               filter.ActionPass,
					Direction:            filter.DirectionOutgoing,
					Quick:                true,
					Proto:                filter.SocketProtocolUdp,
					SrcSubnet:            nil,
					SrcSubnetInvertMatch: true,
					SrcPortRange:         filter.PortRange{Start: 2000, End: 2001},
					DstSubnet:            nil,
					DstSubnetInvertMatch: true,
					DstPortRange:         filter.PortRange{Start: 2500, End: 2501},
					Nic:                  3,
					Log:                  true,
					KeepState:            true,
				},
			},
			nil,
		},
		{
			"invalid port range",
			[]Rule{
				{
					action:               Drop,
					direction:            Incoming,
					quick:                false,
					transProto:           header.TCPProtocolNumber,
					srcSubnet:            &srcSubnetV4,
					srcSubnetInvertMatch: false,
					srcPortRange:         PortRange{1001, 1000},
					dstSubnet:            &dstSubnet,
					dstSubnetInvertMatch: false,
					dstPortRange:         PortRange{1500, 1501},
					nic:                  tcpip.NICID(1),
					log:                  false,
					keepState:            false,
				},
			},
			nil,
			ErrBadPortRange,
		},
		{
			"mixed ip versions in a rule",
			[]Rule{
				{
					action:               Drop,
					direction:            Incoming,
					quick:                false,
					transProto:           header.TCPProtocolNumber,
					srcSubnet:            &srcSubnetV4,
					srcSubnetInvertMatch: false,
					srcPortRange:         PortRange{1000, 1001},
					dstSubnet:            &dstSubnetV6,
					dstSubnetInvertMatch: false,
					dstPortRange:         PortRange{1500, 1501},
					nic:                  tcpip.NICID(1),
					log:                  false,
					keepState:            false,
				},
			},
			nil,
			ErrBadRule,
		},
	}

	for _, test := range tests {
		t.Run(test.description, func(t *testing.T) {
			got, err := fromRules(test.rules)
			if err != test.err {
				t.Errorf("err=%v, want=%v", err, test.err)
			}
			if err == nil {
				want := test.newRules

				if len(got) != len(want) {
					t.Errorf("len(got)=%d, len(want)=%d", len(got), len(want))
				}

				for i := range want {
					if !reflect.DeepEqual(got[i], want[i]) {
						t.Errorf("got[%d]=%v, want[%d]=%v", i, got[i], i, want[i])
					}
				}
			}
		})
	}
}

func TestToRules(t *testing.T) {
	srcSubnetV4, err := parseCIDR("1.2.3.4/32")
	if err != nil {
		t.Errorf("ParseCIDR error: %s", err)
	}
	dstSubnetV4, err := parseCIDR("5.6.7.8/32")
	if err != nil {
		t.Errorf("ParseCIDR error: %s", err)
	}
	srcSubnetV6, err := parseCIDR("0102:0304:0506:0708:090a:0b0c:0d0e:0f10/128")
	if err != nil {
		t.Errorf("ParseCIDR error: %s", err)
	}
	dstSubnetV6, err := parseCIDR("1112:1314:1516:1718:191a:1b1c:1d1e:1f20/128")
	if err != nil {
		t.Errorf("ParseCIDR error: %s", err)
	}

	netSrcSubnetV4 := fidlnet.Subnet{PrefixLen: 32}
	netSrcSubnetV4.Addr.SetIpv4(fidlnet.Ipv4Address{Addr: [4]uint8{1, 2, 3, 4}})
	netDstSubnetV4 := fidlnet.Subnet{PrefixLen: 32}
	netDstSubnetV4.Addr.SetIpv4(fidlnet.Ipv4Address{Addr: [4]uint8{5, 6, 7, 8}})
	netSrcSubnetV6 := fidlnet.Subnet{PrefixLen: 128}
	netSrcSubnetV6.Addr.SetIpv6(fidlnet.Ipv6Address{Addr: [16]uint8{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16}})
	netDstSubnetV6 := fidlnet.Subnet{PrefixLen: 128}
	netDstSubnetV6.Addr.SetIpv6(fidlnet.Ipv6Address{Addr: [16]uint8{17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32}})

	var tests = []struct {
		description string
		rules       []filter.Rule
		newRules    []Rule
		err         error
	}{
		{
			"4 rules",
			[]filter.Rule{
				{
					Action:               filter.ActionDrop,
					Direction:            filter.DirectionIncoming,
					Quick:                false,
					Proto:                filter.SocketProtocolTcp,
					SrcSubnet:            &netSrcSubnetV4,
					SrcSubnetInvertMatch: false,
					SrcPortRange:         filter.PortRange{Start: 1000, End: 1001},
					DstSubnet:            &netDstSubnetV4,
					DstSubnetInvertMatch: false,
					DstPortRange:         filter.PortRange{Start: 1500, End: 1501},
					Nic:                  1,
					Log:                  false,
					KeepState:            false,
				},
				{
					Action:               filter.ActionDrop,
					Direction:            filter.DirectionIncoming,
					Quick:                false,
					Proto:                filter.SocketProtocolTcp,
					SrcSubnet:            &netSrcSubnetV6,
					SrcSubnetInvertMatch: false,
					SrcPortRange:         filter.PortRange{Start: 1000, End: 1001},
					DstSubnet:            &netDstSubnetV6,
					DstSubnetInvertMatch: false,
					DstPortRange:         filter.PortRange{Start: 1500, End: 1501},
					Nic:                  1,
					Log:                  false,
					KeepState:            false,
				},
				{
					Action:               filter.ActionPass,
					Direction:            filter.DirectionOutgoing,
					Quick:                true,
					Proto:                filter.SocketProtocolUdp,
					SrcSubnet:            &netSrcSubnetV4,
					SrcSubnetInvertMatch: true,
					SrcPortRange:         filter.PortRange{Start: 2000, End: 2001},
					DstSubnet:            &netDstSubnetV4,
					DstSubnetInvertMatch: true,
					DstPortRange:         filter.PortRange{Start: 2500, End: 2501},
					Nic:                  2,
					Log:                  true,
					KeepState:            true,
				},
				{
					Action:               filter.ActionPass,
					Direction:            filter.DirectionOutgoing,
					Quick:                true,
					Proto:                filter.SocketProtocolUdp,
					SrcSubnet:            nil,
					SrcSubnetInvertMatch: true,
					SrcPortRange:         filter.PortRange{Start: 2000, End: 2001},
					DstSubnet:            nil,
					DstSubnetInvertMatch: true,
					DstPortRange:         filter.PortRange{Start: 2500, End: 2501},
					Nic:                  3,
					Log:                  true,
					KeepState:            true,
				},
			},
			[]Rule{
				{
					action:               Drop,
					direction:            Incoming,
					quick:                false,
					transProto:           header.TCPProtocolNumber,
					srcSubnet:            &srcSubnetV4,
					srcSubnetInvertMatch: false,
					srcPortRange:         PortRange{1000, 1001},
					dstSubnet:            &dstSubnetV4,
					dstSubnetInvertMatch: false,
					dstPortRange:         PortRange{1500, 1501},
					nic:                  tcpip.NICID(1),
					log:                  false,
					keepState:            false,
				},
				{
					action:               Drop,
					direction:            Incoming,
					quick:                false,
					transProto:           header.TCPProtocolNumber,
					srcSubnet:            &srcSubnetV6,
					srcSubnetInvertMatch: false,
					srcPortRange:         PortRange{1000, 1001},
					dstSubnet:            &dstSubnetV6,
					dstSubnetInvertMatch: false,
					dstPortRange:         PortRange{1500, 1501},
					nic:                  tcpip.NICID(1),
					log:                  false,
					keepState:            false,
				},
				{
					action:               Pass,
					direction:            Outgoing,
					quick:                true,
					transProto:           header.UDPProtocolNumber,
					srcSubnet:            &srcSubnetV4,
					srcSubnetInvertMatch: true,
					srcPortRange:         PortRange{2000, 2001},
					dstSubnet:            &dstSubnetV4,
					dstSubnetInvertMatch: true,
					dstPortRange:         PortRange{2500, 2501},
					nic:                  tcpip.NICID(2),
					log:                  true,
					keepState:            true,
				},
				{
					action:               Pass,
					direction:            Outgoing,
					quick:                true,
					transProto:           header.UDPProtocolNumber,
					srcSubnet:            nil,
					srcSubnetInvertMatch: true,
					srcPortRange:         PortRange{2000, 2001},
					dstSubnet:            nil,
					dstSubnetInvertMatch: true,
					dstPortRange:         PortRange{2500, 2501},
					nic:                  tcpip.NICID(3),
					log:                  true,
					keepState:            true,
				},
			},
			nil,
		},
		{
			"invalid port range",
			[]filter.Rule{
				{
					Action:               filter.ActionDrop,
					Direction:            filter.DirectionIncoming,
					Quick:                false,
					Proto:                filter.SocketProtocolTcp,
					SrcSubnet:            &netSrcSubnetV4,
					SrcSubnetInvertMatch: false,
					SrcPortRange:         filter.PortRange{Start: 1001, End: 1000},
					DstSubnet:            &netDstSubnetV4,
					DstSubnetInvertMatch: false,
					DstPortRange:         filter.PortRange{Start: 1500, End: 1501},
					Nic:                  1,
					Log:                  false,
					KeepState:            false,
				},
			},
			nil,
			ErrBadPortRange,
		},
		{
			"mixed ip versions in a rule",
			[]filter.Rule{
				{
					Action:               filter.ActionDrop,
					Direction:            filter.DirectionIncoming,
					Quick:                false,
					Proto:                filter.SocketProtocolTcp,
					SrcSubnet:            &netSrcSubnetV4,
					SrcSubnetInvertMatch: false,
					SrcPortRange:         filter.PortRange{Start: 1000, End: 1001},
					DstSubnet:            &netDstSubnetV6,
					DstSubnetInvertMatch: false,
					DstPortRange:         filter.PortRange{Start: 1500, End: 1501},
					Nic:                  1,
					Log:                  false,
					KeepState:            false,
				},
			},
			nil,
			ErrBadRule,
		},
	}

	for _, test := range tests {
		t.Run(test.description, func(t *testing.T) {
			got, err := toRules(test.rules)
			if err != test.err {
				t.Errorf("err=%v, want=%v", err, test.err)
			}
			if err == nil {
				want := test.newRules

				if len(got) != len(want) {
					t.Errorf("len(got)=%d, len(want)=%d", len(got), len(want))
				}

				for i := range want {
					if !reflect.DeepEqual(got[i], want[i]) {
						t.Errorf("got[%d]=%v, want[%d]=%v", i, got[i], i, want[i])
					}
				}
			}
		})
	}
}

func TestFromNATs(t *testing.T) {
	srcSubnetV4, err := parseCIDR("1.2.3.4/32")
	if err != nil {
		t.Errorf("ParseCIDR error: %s", err)
	}
	srcAddrV4 := util.Parse("5.6.7.8")

	srcSubnetV6, err := parseCIDR("0102:0304:0506:0708:090a:0b0c:0d0e:0f10/128")
	if err != nil {
		t.Errorf("ParseCIDR error: %s", err)
	}
	srcAddrV6 := util.Parse("1112:1314:1516:1718:191a:1b1c:1d1e:1f20")

	netSrcSubnetV4 := fidlnet.Subnet{PrefixLen: 32}
	netSrcSubnetV4.Addr.SetIpv4(fidlnet.Ipv4Address{Addr: [4]uint8{1, 2, 3, 4}})
	var netSrcAddrV4 fidlnet.IpAddress
	netSrcAddrV4.SetIpv4(fidlnet.Ipv4Address{Addr: [4]uint8{5, 6, 7, 8}})

	var tests = []struct {
		description string
		nats        []NAT
		newNats     []filter.Nat
		err         error
	}{
		{
			"2 rules",
			[]NAT{
				{
					transProto: header.TCPProtocolNumber,
					srcSubnet:  &srcSubnetV4,
					newSrcAddr: srcAddrV4,
					nic:        tcpip.NICID(1),
				},
				{
					transProto: header.UDPProtocolNumber,
					srcSubnet:  &srcSubnetV4,
					newSrcAddr: srcAddrV4,
					nic:        tcpip.NICID(2),
				},
			},
			[]filter.Nat{
				{
					Proto:      filter.SocketProtocolTcp,
					SrcSubnet:  netSrcSubnetV4,
					NewSrcAddr: netSrcAddrV4,
					Nic:        1,
				},
				{
					Proto:      filter.SocketProtocolUdp,
					SrcSubnet:  netSrcSubnetV4,
					NewSrcAddr: netSrcAddrV4,
					Nic:        2,
				},
			},
			nil,
		},
		{
			"mixed ip versions in a rule",
			[]NAT{
				{
					transProto: header.TCPProtocolNumber,
					srcSubnet:  &srcSubnetV4,
					newSrcAddr: srcAddrV6,
					nic:        tcpip.NICID(1),
				},
			},
			nil,
			ErrBadRule,
		},
		{
			"ipv6 is not supported",
			[]NAT{
				{
					transProto: header.TCPProtocolNumber,
					srcSubnet:  &srcSubnetV6,
					newSrcAddr: srcAddrV6,
					nic:        tcpip.NICID(1),
				},
			},
			nil,
			ErrBadRule,
		},
	}

	for _, test := range tests {
		t.Run(test.description, func(t *testing.T) {
			got, err := fromNATs(test.nats)
			if err != test.err {
				t.Errorf("err=%v, want=%v", err, test.err)
			}
			if err == nil {
				want := test.newNats

				if len(got) != len(want) {
					t.Errorf("len(got)=%d, len(want)=%d", len(got), len(want))
				}

				for i := range want {
					if !reflect.DeepEqual(got[i], want[i]) {
						t.Errorf("got[%d]=%v, want[%d]=%v", i, got[i], i, want[i])
					}
				}
			}
		})
	}
}

func TestToNATs(t *testing.T) {
	srcSubnetV4, err := parseCIDR("1.2.3.4/32")
	if err != nil {
		t.Errorf("ParseCIDR error: %s", err)
	}
	srcAddrV4 := util.Parse("5.6.7.8")

	netSrcSubnetV4 := fidlnet.Subnet{PrefixLen: 32}
	netSrcSubnetV4.Addr.SetIpv4(fidlnet.Ipv4Address{Addr: [4]uint8{1, 2, 3, 4}})
	var netSrcAddrV4 fidlnet.IpAddress
	netSrcAddrV4.SetIpv4(fidlnet.Ipv4Address{Addr: [4]uint8{5, 6, 7, 8}})

	netSrcSubnetV6 := fidlnet.Subnet{PrefixLen: 128}
	netSrcSubnetV6.Addr.SetIpv6(fidlnet.Ipv6Address{Addr: [16]uint8{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16}})
	var netSrcAddrV6 fidlnet.IpAddress
	netSrcAddrV6.SetIpv6(fidlnet.Ipv6Address{Addr: [16]uint8{17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32}})

	var tests = []struct {
		description string
		nats        []filter.Nat
		newNats     []NAT
		err         error
	}{
		{
			"2 rules",
			[]filter.Nat{
				{
					Proto:      filter.SocketProtocolTcp,
					SrcSubnet:  netSrcSubnetV4,
					NewSrcAddr: netSrcAddrV4,
					Nic:        1,
				},
				{
					Proto:      filter.SocketProtocolUdp,
					SrcSubnet:  netSrcSubnetV4,
					NewSrcAddr: netSrcAddrV4,
					Nic:        2,
				},
			},
			[]NAT{
				{
					transProto: header.TCPProtocolNumber,
					srcSubnet:  &srcSubnetV4,
					newSrcAddr: srcAddrV4,
					nic:        tcpip.NICID(1),
				},
				{
					transProto: header.UDPProtocolNumber,
					srcSubnet:  &srcSubnetV4,
					newSrcAddr: srcAddrV4,
					nic:        tcpip.NICID(2),
				},
			},
			nil,
		},
		{
			"mixed ip versions in a rule",
			[]filter.Nat{
				{
					Proto:      filter.SocketProtocolTcp,
					SrcSubnet:  netSrcSubnetV4,
					NewSrcAddr: netSrcAddrV6,
					Nic:        1,
				},
			},
			nil,
			ErrBadRule,
		},
		{
			"ipv6 is not supported",
			[]filter.Nat{
				{
					Proto:      filter.SocketProtocolTcp,
					SrcSubnet:  netSrcSubnetV6,
					NewSrcAddr: netSrcAddrV6,
					Nic:        1,
				},
			},
			nil,
			ErrBadRule,
		},
	}

	for _, test := range tests {
		t.Run(test.description, func(t *testing.T) {
			got, err := toNATs(test.nats)
			if err != test.err {
				t.Errorf("err=%v, want=%v", err, test.err)
			}
			if err == nil {
				want := test.newNats

				if len(got) != len(want) {
					t.Errorf("len(got)=%d, len(want)=%d", len(got), len(want))
				}

				for i := range want {
					if !reflect.DeepEqual(got[i], want[i]) {
						t.Errorf("got[%d]=%v, want[%d]=%v", i, got[i], i, want[i])
					}
				}
			}
		})
	}
}

func TestFromRDRs(t *testing.T) {
	dstAddrV4 := util.Parse("1.2.3.4")
	dstAddrV4_2 := util.Parse("5.6.7.8")

	var netDstAddrV4 fidlnet.IpAddress
	netDstAddrV4.SetIpv4(fidlnet.Ipv4Address{Addr: [4]uint8{1, 2, 3, 4}})
	var netDstAddrV4_2 fidlnet.IpAddress
	netDstAddrV4_2.SetIpv4(fidlnet.Ipv4Address{Addr: [4]uint8{5, 6, 7, 8}})

	dstAddrV6 := util.Parse("0102:0304:0506:0708:090a:0b0c:0d0e:0f10")
	dstAddrV6_2 := util.Parse("1112:1314:1516:1718:191a:1b1c:1d1e:1f20")

	var netDstAddrV6 fidlnet.IpAddress
	netDstAddrV6.SetIpv6(fidlnet.Ipv6Address{Addr: [16]uint8{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16}})
	var netDstAddrV6_2 fidlnet.IpAddress
	netDstAddrV6_2.SetIpv6(fidlnet.Ipv6Address{Addr: [16]uint8{17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32}})

	var tests = []struct {
		description string
		rdrs        []RDR
		newRdrs     []filter.Rdr
		err         error
	}{
		{
			"3 rules",
			[]RDR{
				{
					transProto:      header.TCPProtocolNumber,
					dstAddr:         dstAddrV4,
					dstPortRange:    PortRange{1000, 1001},
					newDstAddr:      dstAddrV4_2,
					newDstPortRange: PortRange{1500, 1501},
					nic:             tcpip.NICID(1),
				},
				{
					transProto:      header.TCPProtocolNumber,
					dstAddr:         dstAddrV6,
					dstPortRange:    PortRange{1000, 1001},
					newDstAddr:      dstAddrV6_2,
					newDstPortRange: PortRange{1500, 1501},
					nic:             tcpip.NICID(1),
				},
				{
					transProto:      header.UDPProtocolNumber,
					dstAddr:         dstAddrV4,
					dstPortRange:    PortRange{2000, 2001},
					newDstAddr:      dstAddrV4_2,
					newDstPortRange: PortRange{2500, 2501},
					nic:             tcpip.NICID(2),
				},
			},
			[]filter.Rdr{
				{
					Proto:           filter.SocketProtocolTcp,
					DstAddr:         netDstAddrV4,
					DstPortRange:    filter.PortRange{Start: 1000, End: 1001},
					NewDstAddr:      netDstAddrV4_2,
					NewDstPortRange: filter.PortRange{Start: 1500, End: 1501},
					Nic:             1,
				},
				{
					Proto:           filter.SocketProtocolTcp,
					DstAddr:         netDstAddrV6,
					DstPortRange:    filter.PortRange{Start: 1000, End: 1001},
					NewDstAddr:      netDstAddrV6_2,
					NewDstPortRange: filter.PortRange{Start: 1500, End: 1501},
					Nic:             1,
				},
				{
					Proto:           filter.SocketProtocolUdp,
					DstAddr:         netDstAddrV4,
					DstPortRange:    filter.PortRange{Start: 2000, End: 2001},
					NewDstAddr:      netDstAddrV4_2,
					NewDstPortRange: filter.PortRange{Start: 2500, End: 2501},
					Nic:             2,
				},
			},
			nil,
		},
		{
			"invalid port range",
			[]RDR{
				{
					transProto:      header.TCPProtocolNumber,
					dstAddr:         dstAddrV4,
					dstPortRange:    PortRange{1001, 1000},
					newDstAddr:      dstAddrV4_2,
					newDstPortRange: PortRange{1500, 1501},
					nic:             tcpip.NICID(1),
				},
			},
			nil,
			ErrBadPortRange,
		},
		{
			"mixed ip versions in a rule",
			[]RDR{
				{
					transProto:      header.TCPProtocolNumber,
					dstAddr:         dstAddrV4,
					dstPortRange:    PortRange{1000, 1001},
					newDstAddr:      dstAddrV6_2,
					newDstPortRange: PortRange{1500, 1501},
					nic:             tcpip.NICID(1),
				},
			},
			nil,
			ErrBadRule,
		},
	}

	for _, test := range tests {
		t.Run(test.description, func(t *testing.T) {
			got, err := fromRDRs(test.rdrs)
			if err != test.err {
				t.Errorf("err=%v, want=%v", err, test.err)
			}
			if err == nil {
				want := test.newRdrs

				if len(got) != len(want) {
					t.Errorf("len(got)=%d, len(want)=%d", len(got), len(want))
				}

				for i := range want {
					if !reflect.DeepEqual(got[i], want[i]) {
						t.Errorf("got[%d]=%v, want[%d]=%v", i, got[i], i, want[i])
					}
				}
			}
		})
	}
}

func TestToRDRs(t *testing.T) {
	dstAddrV4 := util.Parse("1.2.3.4")
	dstAddrV4_2 := util.Parse("5.6.7.8")

	var netDstAddrV4 fidlnet.IpAddress
	netDstAddrV4.SetIpv4(fidlnet.Ipv4Address{Addr: [4]uint8{1, 2, 3, 4}})
	var netDstAddrV4_2 fidlnet.IpAddress
	netDstAddrV4_2.SetIpv4(fidlnet.Ipv4Address{Addr: [4]uint8{5, 6, 7, 8}})

	dstAddrV6 := util.Parse("0102:0304:0506:0708:090a:0b0c:0d0e:0f10")
	dstAddrV6_2 := util.Parse("1112:1314:1516:1718:191a:1b1c:1d1e:1f20")

	var netDstAddrV6 fidlnet.IpAddress
	netDstAddrV6.SetIpv6(fidlnet.Ipv6Address{Addr: [16]uint8{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16}})
	var netDstAddrV6_2 fidlnet.IpAddress
	netDstAddrV6_2.SetIpv6(fidlnet.Ipv6Address{Addr: [16]uint8{17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32}})

	var tests = []struct {
		description string
		rdrs        []filter.Rdr
		newRdrs     []RDR
		err         error
	}{
		{
			"3 rules",
			[]filter.Rdr{
				{
					Proto:           filter.SocketProtocolTcp,
					DstAddr:         netDstAddrV4,
					DstPortRange:    filter.PortRange{Start: 1000, End: 1001},
					NewDstAddr:      netDstAddrV4_2,
					NewDstPortRange: filter.PortRange{Start: 1500, End: 1501},
					Nic:             1,
				},
				{
					Proto:           filter.SocketProtocolTcp,
					DstAddr:         netDstAddrV6,
					DstPortRange:    filter.PortRange{Start: 1000, End: 1001},
					NewDstAddr:      netDstAddrV6_2,
					NewDstPortRange: filter.PortRange{Start: 1500, End: 1501},
					Nic:             1,
				},
				{
					Proto:           filter.SocketProtocolUdp,
					DstAddr:         netDstAddrV4,
					DstPortRange:    filter.PortRange{Start: 2000, End: 2001},
					NewDstAddr:      netDstAddrV4_2,
					NewDstPortRange: filter.PortRange{Start: 2500, End: 2501},
					Nic:             2,
				},
			},
			[]RDR{
				{
					transProto:      header.TCPProtocolNumber,
					dstAddr:         dstAddrV4,
					dstPortRange:    PortRange{1000, 1001},
					newDstAddr:      dstAddrV4_2,
					newDstPortRange: PortRange{1500, 1501},
					nic:             tcpip.NICID(1),
				},
				{
					transProto:      header.TCPProtocolNumber,
					dstAddr:         dstAddrV6,
					dstPortRange:    PortRange{1000, 1001},
					newDstAddr:      dstAddrV6_2,
					newDstPortRange: PortRange{1500, 1501},
					nic:             tcpip.NICID(1),
				},
				{
					transProto:      header.UDPProtocolNumber,
					dstAddr:         dstAddrV4,
					dstPortRange:    PortRange{2000, 2001},
					newDstAddr:      dstAddrV4_2,
					newDstPortRange: PortRange{2500, 2501},
					nic:             tcpip.NICID(2),
				},
			},
			nil,
		},
		{
			"invalid port range",
			[]filter.Rdr{
				{
					Proto:           filter.SocketProtocolTcp,
					DstAddr:         netDstAddrV4,
					DstPortRange:    filter.PortRange{Start: 1001, End: 1000},
					NewDstAddr:      netDstAddrV4_2,
					NewDstPortRange: filter.PortRange{Start: 1500, End: 1501},
					Nic:             1,
				},
			},
			nil,
			ErrBadPortRange,
		},
		{
			"mixed ip versions in a rule",
			[]filter.Rdr{
				{
					Proto:           filter.SocketProtocolTcp,
					DstAddr:         netDstAddrV4,
					DstPortRange:    filter.PortRange{Start: 1000, End: 1001},
					NewDstAddr:      netDstAddrV6_2,
					NewDstPortRange: filter.PortRange{Start: 1500, End: 1501},
					Nic:             1,
				},
			},
			nil,
			ErrBadRule,
		},
	}

	for _, test := range tests {
		t.Run(test.description, func(t *testing.T) {
			got, err := toRDRs(test.rdrs)
			if err != test.err {
				t.Errorf("err=%v, want=%v", err, test.err)
			}
			if err == nil {
				want := test.newRdrs

				if len(got) != len(want) {
					t.Errorf("len(got)=%v, len(want)=%v", len(got), len(want))
				}

				for i := range want {
					if !reflect.DeepEqual(got[i], want[i]) {
						t.Errorf("got[%d]=%v, want[%d]=%v", i, got[i], i, want[i])
					}
				}
			}
		})
	}
}
