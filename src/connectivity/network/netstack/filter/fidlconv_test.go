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

	"github.com/google/netstack/tcpip"
	"github.com/google/netstack/tcpip/header"
)

func TestFromAction(t *testing.T) {
	var tests = []struct {
		action    Action
		netAction filter.Action
		err       error
	}{
		{Pass, filter.ActionPass, nil},
		{Drop, filter.ActionDrop, nil},
		{DropReset, filter.ActionDropReset, nil},
		{Action(9999), filter.Action(0), ErrUnknownAction},
	}

	for _, test := range tests {
		netAction, err := fromAction(test.action)
		if err != test.err {
			t.Errorf("fromAction unexpected error: %v", err)
		}
		if err != nil {
			continue
		}
		if got, want := netAction, test.netAction; got != want {
			t.Errorf("fromAction got=%v, want=%v", got, want)
		}
	}
}

func TestToAction(t *testing.T) {
	var tests = []struct {
		netAction filter.Action
		action    Action
		err       error
	}{
		{filter.ActionPass, Pass, nil},
		{filter.ActionDrop, Drop, nil},
		{filter.ActionDropReset, DropReset, nil},
		{filter.Action(9999), Action(0), ErrUnknownAction},
	}

	for _, test := range tests {
		action, err := toAction(test.netAction)
		if err != test.err {
			t.Errorf("toAction unexpected error: %v", err)
		}
		if err != nil {
			continue
		}
		if got, want := action, test.action; got != want {
			t.Errorf("toAction got=%v, want=%v", got, want)
		}
	}
}

func TestFromDirection(t *testing.T) {
	var tests = []struct {
		direction    Direction
		netDirection filter.Direction
		err          error
	}{
		{Incoming, filter.DirectionIncoming, nil},
		{Outgoing, filter.DirectionOutgoing, nil},
		{Direction(9999), filter.Direction(0), ErrUnknownDirection},
	}

	for _, test := range tests {
		netDirection, err := fromDirection(test.direction)
		if err != test.err {
			t.Errorf("fromDirection unexpected error: %v", err)
		}
		if err != nil {
			continue
		}
		if got, want := netDirection, test.netDirection; got != want {
			t.Errorf("fromDirection got=%v, want=%v", got, want)
		}
	}
}

func TestToDirection(t *testing.T) {
	var tests = []struct {
		netDirection filter.Direction
		direction    Direction
		err          error
	}{
		{filter.DirectionIncoming, Incoming, nil},
		{filter.DirectionOutgoing, Outgoing, nil},
		{filter.Direction(9999), Direction(0), ErrUnknownDirection},
	}

	for _, test := range tests {
		direction, err := toDirection(test.netDirection)
		if err != test.err {
			t.Errorf("toDirection unexpected error: %v", err)
		}
		if err != nil {
			continue
		}
		if got, want := direction, test.direction; got != want {
			t.Errorf("toDirection got=%v, want=%v", got, want)
		}
	}
}

func TestFromTransProto(t *testing.T) {
	var tests = []struct {
		transProto    tcpip.TransportProtocolNumber
		netTransProto filter.SocketProtocol
		err           error
	}{
		{header.ICMPv4ProtocolNumber, filter.SocketProtocolIcmp, nil},
		{header.ICMPv6ProtocolNumber, filter.SocketProtocolIcmpv6, nil},
		{header.TCPProtocolNumber, filter.SocketProtocolTcp, nil},
		{header.UDPProtocolNumber, filter.SocketProtocolUdp, nil},
		{tcpip.TransportProtocolNumber(9999), filter.SocketProtocol(0), ErrUnknownProtocol},
	}

	for _, test := range tests {
		netTransProto, err := fromTransProto(test.transProto)
		if err != test.err {
			t.Errorf("fromTransProto unexpected error: %v", err)
		}
		if err != nil {
			continue
		}
		if got, want := netTransProto, test.netTransProto; got != want {
			t.Errorf("fromTransProto got=%v, want=%v", got, want)
		}
	}
}

func TestToTransProto(t *testing.T) {
	var tests = []struct {
		netTransProto filter.SocketProtocol
		transProto    tcpip.TransportProtocolNumber
		err           error
	}{
		{filter.SocketProtocolTcp, header.TCPProtocolNumber, nil},
		{filter.SocketProtocolUdp, header.UDPProtocolNumber, nil},
		{filter.SocketProtocolIcmp, header.ICMPv4ProtocolNumber, nil},
		{filter.SocketProtocolIcmpv6, header.ICMPv6ProtocolNumber, nil},
		{filter.SocketProtocol(9999), tcpip.TransportProtocolNumber(0), ErrUnknownProtocol},
	}

	for _, test := range tests {
		transProto, err := toTransProto(test.netTransProto)
		if err != test.err {
			t.Errorf("toTransProto unxpected error: %v", err)
		}
		if err != nil {
			continue
		}
		if got, want := transProto, test.transProto; got != want {
			t.Errorf("toTransProto got=%v, want=%v", got, want)
		}
	}
}

func TestFromAddress(t *testing.T) {
	a1 := util.Parse("1.2.3.4")
	var b1 fidlnet.IpAddress

	b1.SetIpv4(fidlnet.Ipv4Address{Addr: [4]uint8{1, 2, 3, 4}})

	a2 := util.Parse("a:b:c::2:3:4")
	var b2v6 fidlnet.Ipv6Address
	copy(b2v6.Addr[:], "\x00\x0a\x00\x0b\x00\x0c\x00\x00\x00\x00\x00\x02\x00\x03\x00\x04")
	var b2 fidlnet.IpAddress

	b2.SetIpv6(b2v6)

	var tests = []struct {
		address    tcpip.Address
		netAddress fidlnet.IpAddress
		err        error
	}{
		{a1, b1, nil},
		{a2, b2, nil},
		{tcpip.Address("12345"), fidlnet.IpAddress{}, ErrUnknownAddressType},
	}

	for _, test := range tests {
		netAddress, err := fromAddress(test.address)
		if err != test.err {
			t.Errorf("fromAddress unexpected error: %v", err)
		}
		if err != nil {
			continue
		}
		got, want := netAddress, test.netAddress
		if got.Which() != want.Which() {
			t.Errorf("fromAddress got.Which()=%v, want.Which()=%v", got.Which(), want.Which())
		}
		switch got.Which() {
		case fidlnet.IpAddressIpv4:
			if got.Ipv4 != want.Ipv4 {
				t.Errorf("fromAddress got.Ipv4=%v, want.Ipv4=%v", got.Ipv4, want.Ipv4)
			}
		case fidlnet.IpAddressIpv6:
			if got.Ipv6 != want.Ipv6 {
				t.Errorf("fromAddress got.Ipv6=%v, want.Ipv6=%v", got.Ipv6, want.Ipv6)
			}
		default:
			t.Errorf("fromAddress unxpected AddressType: got.Which()=%v", got.Which())
		}
	}
}

func TestToAddress(t *testing.T) {
	var a1 fidlnet.IpAddress
	a1.SetIpv4(fidlnet.Ipv4Address{Addr: [4]uint8{1, 2, 3, 4}})

	b1 := util.Parse("1.2.3.4")

	var a2v6 fidlnet.Ipv6Address
	copy(a2v6.Addr[:], "\x00\x0a\x00\x0b\x00\x0c\x00\x00\x00\x00\x00\x02\x00\x03\x00\x04")
	var a2 fidlnet.IpAddress
	a2.SetIpv6(a2v6)

	b2 := util.Parse("a:b:c::2:3:4")

	var tests = []struct {
		netAddress fidlnet.IpAddress
		address    tcpip.Address
		err        error
	}{
		{a1, b1, nil},
		{a2, b2, nil},
		{fidlnet.IpAddress{}, tcpip.Address(""), ErrUnknownAddressType},
	}

	for _, test := range tests {
		address, err := toAddress(&test.netAddress)
		if err != test.err {
			t.Errorf("toAddress unxpected error: %v", err)
		}
		if err != nil {
			continue
		}
		if got, want := address, test.address; got != want {
			t.Errorf("toAddress got=%v, want=%v", got, want)
		}
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
	// test1
	s1, err := parseCIDR("1.2.3.4/32")
	if err != nil {
		t.Errorf("ParseCIDR error: %v", err)
	}

	var a1 fidlnet.IpAddress
	a1.SetIpv4(fidlnet.Ipv4Address{Addr: [4]uint8{1, 2, 3, 4}})
	t1 := fidlnet.Subnet{Addr: a1, PrefixLen: 32}

	// test2
	s2, err := parseCIDR("10.0.0.0/8")
	if err != nil {
		t.Errorf("ParseCIDR error: %v", err)
	}

	var a2 fidlnet.IpAddress
	a2.SetIpv4(fidlnet.Ipv4Address{Addr: [4]uint8{10, 0, 0, 0}})
	t2 := fidlnet.Subnet{Addr: a2, PrefixLen: 8}

	// test3
	s3, err := parseCIDR("a:b:c::/96")
	if err != nil {
		t.Errorf("ParseCIDR error: %v", err)
	}

	var a3v6 fidlnet.Ipv6Address
	copy(a3v6.Addr[:], "\x00\x0a\x00\x0b\x00\x0c\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00")
	var a3 fidlnet.IpAddress
	a3.SetIpv6(a3v6)
	t3 := fidlnet.Subnet{Addr: a3, PrefixLen: 96}

	var tests = []struct {
		subnet    tcpip.Subnet
		netSubnet fidlnet.Subnet
		err       error
	}{
		{s1, t1, nil},
		{s2, t2, nil},
		{s3, t3, nil},
	}

	for _, test := range tests {
		netSubnet, err := fromSubnet(&test.subnet)
		if err != test.err {
			t.Errorf("fromSubnet unexpected error: %v", err)
		}
		if err != nil {
			continue
		}
		got, want := netSubnet, test.netSubnet
		if got.Addr.Which() != want.Addr.Which() {
			t.Errorf("fromSubnet got.Addr.Which()=%v, want.Addr.Which()=%v", got.Addr.Which(), want.Addr.Which())
		}
		switch got.Addr.Which() {
		case fidlnet.IpAddressIpv4:
			if got.Addr.Ipv4 != want.Addr.Ipv4 {
				t.Errorf("fromSubnet got.Addr.Ipv4=%v, want.Addr.Ipv4=%v", got.Addr.Ipv4, want.Addr.Ipv4)
			}
		case fidlnet.IpAddressIpv6:
			if got.Addr.Ipv6 != want.Addr.Ipv6 {
				t.Errorf("fromSubnet got.Addr.Ipv6=%v, want.Addr.Ipv6=%v", got.Addr.Ipv6, want.Addr.Ipv6)
			}
		default:
			t.Errorf("fromSubnet unxpected SubnetType: got.Addr.Which()=%v", got.Addr.Which())
		}
		if got.PrefixLen != want.PrefixLen {
			t.Errorf("fromSubnet got.PrefixLen=%v, want.PrefixLen=%v", got.PrefixLen, want.PrefixLen)
		}
	}
}

func TestRules(t *testing.T) {
	srcSubnet, err := parseCIDR("1.2.3.4/32")
	if err != nil {
		t.Errorf("ParseCIDR error: %v", err)
	}
	dstSubnet, err := parseCIDR("5.6.7.8/32")
	if err != nil {
		t.Errorf("ParseCIDR error: %v", err)
	}

	netSrcSubnet := fidlnet.Subnet{PrefixLen: 32}
	netSrcSubnet.Addr.SetIpv4(fidlnet.Ipv4Address{Addr: [4]uint8{1, 2, 3, 4}})
	netDstSubnet := fidlnet.Subnet{PrefixLen: 32}
	netDstSubnet.Addr.SetIpv4(fidlnet.Ipv4Address{Addr: [4]uint8{5, 6, 7, 8}})

	rules := []Rule{
		{
			action:               Drop,
			direction:            Incoming,
			quick:                false,
			transProto:           header.TCPProtocolNumber,
			srcSubnet:            &srcSubnet,
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
			action:               Pass,
			direction:            Outgoing,
			quick:                true,
			transProto:           header.UDPProtocolNumber,
			srcSubnet:            &srcSubnet,
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
	}

	netRules := []filter.Rule{
		{
			Action:               filter.ActionDrop,
			Direction:            filter.DirectionIncoming,
			Quick:                false,
			Proto:                filter.SocketProtocolTcp,
			SrcSubnet:            &netSrcSubnet,
			SrcSubnetInvertMatch: false,
			SrcPortRange:         filter.PortRange{Start: 1000, End: 1001},
			DstSubnet:            &netDstSubnet,
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
			DstSubnet:            &netDstSubnet,
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
	}

	{
		got, err := fromRules(rules)
		if err != nil {
			t.Errorf("fromRules error: %v", err)
		}
		want := netRules

		if len(got) != len(want) {
			t.Errorf("len(got)=%v, len(want)=%v", len(got), len(want))
		}

		for i := range want {
			if !reflect.DeepEqual(got[i], want[i]) {
				t.Errorf("got[%d]=%v, want[%d]=%v", i, got[i], i, want[i])
			}
		}
	}
	{
		got, err := toRules(netRules)
		if err != nil {
			t.Errorf("toRules error: %v", err)
		}
		want := rules

		if len(got) != len(want) {
			t.Errorf("len(got)=%v, len(want)=%v", len(got), len(want))
		}

		for i := range want {
			if !reflect.DeepEqual(got[i], want[i]) {
				t.Errorf("got[%d]=%v, want[%d]=%v", i, got[i], i, want[i])
			}
		}
	}
}

func TestNATs(t *testing.T) {
	srcSubnet, err := parseCIDR("1.2.3.4/32")
	if err != nil {
		t.Errorf("ParseCIDR error: %v", err)
	}
	srcAddr := util.Parse("5.6.7.8")

	netSrcSubnet := fidlnet.Subnet{PrefixLen: 32}
	netSrcSubnet.Addr.SetIpv4(fidlnet.Ipv4Address{Addr: [4]uint8{1, 2, 3, 4}})
	var netSrcAddr fidlnet.IpAddress
	netSrcAddr.SetIpv4(fidlnet.Ipv4Address{Addr: [4]uint8{5, 6, 7, 8}})

	nats := []NAT{
		{
			transProto: header.TCPProtocolNumber,
			srcSubnet:  &srcSubnet,
			newSrcAddr: srcAddr,
			nic:        tcpip.NICID(1),
		},
		{
			transProto: header.UDPProtocolNumber,
			srcSubnet:  &srcSubnet,
			newSrcAddr: srcAddr,
			nic:        tcpip.NICID(2),
		},
	}

	netNats := []filter.Nat{
		{
			Proto:      filter.SocketProtocolTcp,
			SrcSubnet:  netSrcSubnet,
			NewSrcAddr: netSrcAddr,
			Nic:        1,
		},
		{
			Proto:      filter.SocketProtocolUdp,
			SrcSubnet:  netSrcSubnet,
			NewSrcAddr: netSrcAddr,
			Nic:        2,
		},
	}

	{
		got, err := fromNATs(nats)
		if err != nil {
			t.Errorf("fromNATs error: %v", err)
		}
		want := netNats

		if len(got) != len(want) {
			t.Errorf("len(got)=%v, len(want)=%v", len(got), len(want))
		}

		for i := range want {
			if !reflect.DeepEqual(got[i], want[i]) {
				t.Errorf("got[%d]=%v, want[%d]=%v", i, got[i], i, want[i])
			}
		}
	}
	{
		got, err := toNATs(netNats)
		if err != nil {
			t.Errorf("toNATs error: %v", err)
		}
		want := nats

		if len(got) != len(want) {
			t.Errorf("len(got)=%v, len(want)=%v", len(got), len(want))
		}

		for i := range want {
			if !reflect.DeepEqual(got[i], want[i]) {
				t.Errorf("got[%d]=%v, want[%d]=%v", i, got[i], i, want[i])
			}
		}
	}
}

func TestRDRs(t *testing.T) {
	dstAddr := util.Parse("1.2.3.4")
	dstAddr2 := util.Parse("5.6.7.8")

	var netDstAddr fidlnet.IpAddress
	netDstAddr.SetIpv4(fidlnet.Ipv4Address{Addr: [4]uint8{1, 2, 3, 4}})
	var netDstAddr2 fidlnet.IpAddress
	netDstAddr2.SetIpv4(fidlnet.Ipv4Address{Addr: [4]uint8{5, 6, 7, 8}})

	rdrs := []RDR{
		{
			transProto:      header.TCPProtocolNumber,
			dstAddr:         dstAddr,
			dstPortRange:    PortRange{1000, 1001},
			newDstAddr:      dstAddr2,
			newDstPortRange: PortRange{1500, 1501},
			nic:             tcpip.NICID(1),
		},
		{
			transProto:      header.UDPProtocolNumber,
			dstAddr:         dstAddr,
			dstPortRange:    PortRange{2000, 2001},
			newDstAddr:      dstAddr2,
			newDstPortRange: PortRange{2500, 2501},
			nic:             tcpip.NICID(2),
		},
	}

	netRdrs := []filter.Rdr{
		{
			Proto:           filter.SocketProtocolTcp,
			DstAddr:         netDstAddr,
			DstPortRange:    filter.PortRange{Start: 1000, End: 1001},
			NewDstAddr:      netDstAddr2,
			NewDstPortRange: filter.PortRange{Start: 1500, End: 1501},
			Nic:             1,
		},
		{
			Proto:           filter.SocketProtocolUdp,
			DstAddr:         netDstAddr,
			DstPortRange:    filter.PortRange{Start: 2000, End: 2001},
			NewDstAddr:      netDstAddr2,
			NewDstPortRange: filter.PortRange{Start: 2500, End: 2501},
			Nic:             2,
		},
	}

	{
		got, err := fromRDRs(rdrs)
		if err != nil {
			t.Errorf("fromRDRs error: %v", err)
		}
		want := netRdrs

		if len(got) != len(want) {
			t.Errorf("len(got)=%v, len(want)=%v", len(got), len(want))
		}

		for i := range want {
			if !reflect.DeepEqual(got[i], want[i]) {
				t.Errorf("got[%d]=%v, want[%d]=%v", i, got[i], i, want[i])
			}
		}
	}
	{
		got, err := toRDRs(netRdrs)
		if err != nil {
			t.Errorf("toRDRs error: %v", err)
		}
		want := rdrs

		if len(got) != len(want) {
			t.Errorf("len(got)=%v, len(want)=%v", len(got), len(want))
		}

		for i := range want {
			if !reflect.DeepEqual(got[i], want[i]) {
				t.Errorf("got[%d]=%v, want[%d]=%v", i, got[i], i, want[i])
			}
		}
	}
}
