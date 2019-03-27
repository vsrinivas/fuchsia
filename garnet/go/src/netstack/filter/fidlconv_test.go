// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package filter

import (
	"reflect"
	"testing"

	"netstack/util"

	"fidl/fuchsia/net"
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
		{filter.SocketProtocolIp, tcpip.TransportProtocolNumber(0), ErrBadProtocol},
		{filter.SocketProtocolIpv6, tcpip.TransportProtocolNumber(0), ErrBadProtocol},
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
	var b1 net.IpAddress

	b1.SetIpv4(net.Ipv4Address{Addr: [4]uint8{1, 2, 3, 4}})

	a2 := util.Parse("a:b:c::2:3:4")
	var b2v6 net.Ipv6Address
	copy(b2v6.Addr[:], "\x00\x0a\x00\x0b\x00\x0c\x00\x00\x00\x00\x00\x02\x00\x03\x00\x04")
	var b2 net.IpAddress

	b2.SetIpv6(b2v6)

	var tests = []struct {
		address    tcpip.Address
		netAddress net.IpAddress
		err        error
	}{
		{a1, b1, nil},
		{a2, b2, nil},
		{tcpip.Address("12345"), net.IpAddress{}, ErrUnknownAddressType},
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
		case net.IpAddressIpv4:
			if got.Ipv4 != want.Ipv4 {
				t.Errorf("fromAddress got.Ipv4=%v, want.Ipv4=%v", got.Ipv4, want.Ipv4)
			}
		case net.IpAddressIpv6:
			if got.Ipv6 != want.Ipv6 {
				t.Errorf("fromAddress got.Ipv6=%v, want.Ipv6=%v", got.Ipv6, want.Ipv6)
			}
		default:
			t.Errorf("fromAddress unxpected AddressType: got.Which()=%v", got.Which())
		}
	}
}

func TestToAddress(t *testing.T) {
	var a1 net.IpAddress
	a1.SetIpv4(net.Ipv4Address{Addr: [4]uint8{1, 2, 3, 4}})

	b1 := util.Parse("1.2.3.4")

	var a2v6 net.Ipv6Address
	copy(a2v6.Addr[:], "\x00\x0a\x00\x0b\x00\x0c\x00\x00\x00\x00\x00\x02\x00\x03\x00\x04")
	var a2 net.IpAddress
	a2.SetIpv6(a2v6)

	b2 := util.Parse("a:b:c::2:3:4")

	var tests = []struct {
		netAddress net.IpAddress
		address    tcpip.Address
		err        error
	}{
		{a1, b1, nil},
		{a2, b2, nil},
		{net.IpAddress{}, tcpip.Address(""), ErrUnknownAddressType},
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

func TestFromSubnet(t *testing.T) {
	// test1
	_, s1, err := util.ParseCIDR("1.2.3.4/32")
	if err != nil {
		t.Errorf("ParseCIDR error: %v", err)
	}

	var a1 net.IpAddress
	a1.SetIpv4(net.Ipv4Address{Addr: [4]uint8{1, 2, 3, 4}})
	t1 := net.Subnet{Addr: a1, PrefixLen: 32}

	// test2
	_, s2, err := util.ParseCIDR("10.0.0.0/8")
	if err != nil {
		t.Errorf("ParseCIDR error: %v", err)
	}

	var a2 net.IpAddress
	a2.SetIpv4(net.Ipv4Address{Addr: [4]uint8{10, 0, 0, 0}})
	t2 := net.Subnet{Addr: a2, PrefixLen: 8}

	// test3
	_, s3, err := util.ParseCIDR("a:b:c::/96")
	if err != nil {
		t.Errorf("ParseCIDR error: %v", err)
	}

	var a3v6 net.Ipv6Address
	copy(a3v6.Addr[:], "\x00\x0a\x00\x0b\x00\x0c\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00")
	var a3 net.IpAddress
	a3.SetIpv6(a3v6)
	t3 := net.Subnet{Addr: a3, PrefixLen: 96}

	var tests = []struct {
		subnet    tcpip.Subnet
		netSubnet net.Subnet
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
		case net.IpAddressIpv4:
			if got.Addr.Ipv4 != want.Addr.Ipv4 {
				t.Errorf("fromSubnet got.Addr.Ipv4=%v, want.Addr.Ipv4=%v", got.Addr.Ipv4, want.Addr.Ipv4)
			}
		case net.IpAddressIpv6:
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

func TestToSubnet(t *testing.T) {
	// test1
	var a1 net.IpAddress
	a1.SetIpv4(net.Ipv4Address{Addr: [4]uint8{1, 2, 3, 4}})
	s1 := net.Subnet{Addr: a1, PrefixLen: 32}

	_, t1, err := util.ParseCIDR("1.2.3.4/32")
	if err != nil {
		t.Errorf("ParseCIDR error: %v", err)
	}

	// test2
	var a2 net.IpAddress
	a2.SetIpv4(net.Ipv4Address{Addr: [4]uint8{10, 0, 0, 0}})
	s2 := net.Subnet{Addr: a2, PrefixLen: 8}

	_, t2, err := util.ParseCIDR("10.0.0.0/8")
	if err != nil {
		t.Errorf("ParseCIDR error: %v", err)
	}

	// test3
	var a3v6 net.Ipv6Address
	copy(a3v6.Addr[:], "\x00\x0a\x00\x0b\x00\x0c\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00")
	var a3 net.IpAddress
	a3.SetIpv6(a3v6)
	s3 := net.Subnet{Addr: a3, PrefixLen: 96}

	_, t3, err := util.ParseCIDR("a:b:c::/96")
	if err != nil {
		t.Errorf("ParseCIDR error: %v", err)
	}

	var tests = []struct {
		netSubnet net.Subnet
		subnet    tcpip.Subnet
		err       error
	}{
		{s1, t1, nil},
		{s2, t2, nil},
		{s3, t3, nil},
	}

	for _, test := range tests {
		subnet, err := toSubnet(&test.netSubnet)
		if err != test.err {
			t.Errorf("toSubnet unxpected error: %v", err)
		}
		if err != nil {
			continue
		}
		got, want := subnet, test.subnet
		if got.ID() != want.ID() {
			t.Errorf("toSubnet got.ID()=%v, want.ID()=%v", got.ID(), want.ID())
		}
		if got.Mask() != want.Mask() {
			t.Errorf("toSubnet got.Mask()=%x, want.Mask()=%x", got.Mask(), want.Mask())
		}
	}
}

func TestRules(t *testing.T) {
	_, srcSubnet, err := util.ParseCIDR("1.2.3.4/32")
	if err != nil {
		t.Errorf("ParseCIDR error: %v", err)
	}
	_, dstSubnet, err := util.ParseCIDR("5.6.7.8/32")
	if err != nil {
		t.Errorf("ParseCIDR error: %v", err)
	}

	want := []Rule{
		{
			action:               Drop,
			direction:            Incoming,
			quick:                false,
			transProto:           header.TCPProtocolNumber,
			srcSubnet:            &srcSubnet,
			srcSubnetInvertMatch: false,
			srcPort:              1000,
			dstSubnet:            &dstSubnet,
			dstSubnetInvertMatch: false,
			dstPort:              1500,
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
			srcPort:              2000,
			dstSubnet:            &dstSubnet,
			dstSubnetInvertMatch: true,
			dstPort:              2500,
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
			srcPort:              2000,
			dstSubnet:            nil,
			dstSubnetInvertMatch: true,
			dstPort:              2500,
			nic:                  tcpip.NICID(3),
			log:                  true,
			keepState:            true,
		},
	}

	tmp, err := fromRules(want)
	if err != nil {
		t.Errorf("fromRules error: %v", err)
	}
	got, err := toRules(tmp)
	if err != nil {
		t.Errorf("toRules error: %v", err)
	}
	if len(got) != len(want) {
		t.Errorf("len(got)=%v, len(want)=%v", len(got), len(want))
	}

	for i := range want {
		if !reflect.DeepEqual(got[i], want[i]) {
			t.Errorf("got[%d]=%v, want[%d]=%v", i, got[i], i, want[i])
		}
	}
}

func TestNATs(t *testing.T) {
	_, srcSubnet, err := util.ParseCIDR("1.2.3.4/32")
	if err != nil {
		t.Errorf("ParseCIDR error: %v", err)
	}
	srcAddr := util.Parse("5.6.7.8")

	want := []NAT{
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

	tmp, err := fromNATs(want)
	if err != nil {
		t.Errorf("fromRules error: %v", err)
	}
	got, err := toNATs(tmp)
	if err != nil {
		t.Errorf("toRules error: %v", err)
	}
	if len(got) != len(want) {
		t.Errorf("len(got)=%v, len(want)=%v", len(got), len(want))
	}

	for i := range want {
		if !reflect.DeepEqual(got[i], want[i]) {
			t.Errorf("got[%d]=%v, want[%d]=%v", i, got[i], i, want[i])
		}
	}
}

func TestRDRs(t *testing.T) {
	dstAddr := util.Parse("1.2.3.4")
	dstAddr2 := util.Parse("5.6.7.8")

	want := []RDR{
		{
			transProto: header.TCPProtocolNumber,
			dstAddr:    dstAddr,
			dstPort:    1000,
			newDstAddr: dstAddr2,
			newDstPort: 1500,
			nic:        tcpip.NICID(1),
		},
		{
			transProto: header.UDPProtocolNumber,
			dstAddr:    dstAddr,
			dstPort:    2000,
			newDstAddr: dstAddr2,
			newDstPort: 2500,
			nic:        tcpip.NICID(2),
		},
	}

	tmp, err := fromRDRs(want)
	if err != nil {
		t.Errorf("fromRules error: %v", err)
	}
	got, err := toRDRs(tmp)
	if err != nil {
		t.Errorf("toRules error: %v", err)
	}
	if len(got) != len(want) {
		t.Errorf("len(got)=%v, len(want)=%v", len(got), len(want))
	}

	for i := range want {
		if !reflect.DeepEqual(got[i], want[i]) {
			t.Errorf("got[%d]=%v, want[%d]=%v", i, got[i], i, want[i])
		}
	}
}
