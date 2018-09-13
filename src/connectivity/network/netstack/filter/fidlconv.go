// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package filter

import (
	"fidl/fuchsia/net"
	"fidl/fuchsia/net/filter"
	"netstack/fidlconv"

	"gvisor.dev/gvisor/pkg/tcpip"
	"gvisor.dev/gvisor/pkg/tcpip/header"
)

func fromAction(o Action) (filter.Action, error) {
	switch o {
	case Pass:
		return filter.ActionPass, nil
	case Drop:
		return filter.ActionDrop, nil
	case DropReset:
		return filter.ActionDropReset, nil
	default:
		return filter.Action(0), ErrUnknownAction
	}
}

func toAction(o filter.Action) (Action, error) {
	switch o {
	case filter.ActionPass:
		return Pass, nil
	case filter.ActionDrop:
		return Drop, nil
	case filter.ActionDropReset:
		return DropReset, nil
	default:
		return Action(0), ErrUnknownAction
	}
}

func fromDirection(o Direction) (filter.Direction, error) {
	switch o {
	case Incoming:
		return filter.DirectionIncoming, nil
	case Outgoing:
		return filter.DirectionOutgoing, nil
	default:
		return filter.Direction(0), ErrUnknownDirection
	}
}

func toDirection(o filter.Direction) (Direction, error) {
	switch o {
	case filter.DirectionIncoming:
		return Incoming, nil
	case filter.DirectionOutgoing:
		return Outgoing, nil
	default:
		return Direction(0), ErrUnknownDirection
	}
}

func fromTransProto(o tcpip.TransportProtocolNumber) (filter.SocketProtocol, error) {
	switch o {
	case 0:
		return filter.SocketProtocolAny, nil
	case header.ICMPv4ProtocolNumber:
		return filter.SocketProtocolIcmp, nil
	case header.TCPProtocolNumber:
		return filter.SocketProtocolTcp, nil
	case header.UDPProtocolNumber:
		return filter.SocketProtocolUdp, nil
	case header.ICMPv6ProtocolNumber:
		return filter.SocketProtocolIcmpv6, nil
	default:
		return filter.SocketProtocol(0), ErrUnknownProtocol
	}
}

func toTransProto(o filter.SocketProtocol) (tcpip.TransportProtocolNumber, error) {
	switch o {
	case filter.SocketProtocolAny:
		return 0, nil
	case filter.SocketProtocolIcmp:
		return header.ICMPv4ProtocolNumber, nil
	case filter.SocketProtocolTcp:
		return header.TCPProtocolNumber, nil
	case filter.SocketProtocolUdp:
		return header.UDPProtocolNumber, nil
	case filter.SocketProtocolIcmpv6:
		return header.ICMPv6ProtocolNumber, nil
	default:
		return 0, ErrUnknownProtocol
	}
}

func fromAddress(o tcpip.Address) (net.IpAddress, error) {
	switch len(o) {
	case 4:
		addr := net.IpAddress{}
		ipv4 := net.Ipv4Address{}
		copy(ipv4.Addr[:], o)
		addr.SetIpv4(ipv4)
		return addr, nil
	case 16:
		addr := net.IpAddress{}
		ipv6 := net.Ipv6Address{}
		copy(ipv6.Addr[:], o)
		addr.SetIpv6(ipv6)
		return addr, nil
	default:
		return net.IpAddress{}, ErrUnknownAddressType
	}
}

func toAddress(o *net.IpAddress) (tcpip.Address, error) {
	switch o.Which() {
	case net.IpAddressIpv4:
		return tcpip.Address(o.Ipv4.Addr[:]), nil
	case net.IpAddressIpv6:
		return tcpip.Address(o.Ipv6.Addr[:]), nil
	default:
		return tcpip.Address(""), ErrUnknownAddressType
	}
}

func fromSubnet(o *tcpip.Subnet) (net.Subnet, error) {
	addr, err := fromAddress(o.ID())
	if err != nil {
		return net.Subnet{}, ErrBadAddress
	}
	return net.Subnet{
		Addr:      addr,
		PrefixLen: uint8(o.Prefix()),
	}, nil
}

func fromRule(o *Rule) (filter.Rule, error) {
	action, err := fromAction(o.action)
	if err != nil {
		return filter.Rule{}, err
	}
	direction, err := fromDirection(o.direction)
	if err != nil {
		return filter.Rule{}, err
	}
	transProto, err := fromTransProto(o.transProto)
	if err != nil {
		return filter.Rule{}, err
	}
	var srcSubnet, dstSubnet *net.Subnet
	if o.srcSubnet != nil {
		subnet, err := fromSubnet(o.srcSubnet)
		if err != nil {
			return filter.Rule{}, err
		}
		srcSubnet = &subnet
	}
	if o.dstSubnet != nil {
		subnet, err := fromSubnet(o.dstSubnet)
		if err != nil {
			return filter.Rule{}, err
		}
		dstSubnet = &subnet
	}
	srcPortRange := filter.PortRange{Start: o.srcPortRange.start, End: o.srcPortRange.end}
	if srcPortRange.Start > srcPortRange.End {
		return filter.Rule{}, ErrBadPortRange
	}
	dstPortRange := filter.PortRange{Start: o.dstPortRange.start, End: o.dstPortRange.end}
	if dstPortRange.Start > dstPortRange.End {
		return filter.Rule{}, ErrBadPortRange
	}
	if !o.IsValid() {
		return filter.Rule{}, ErrBadRule
	}
	return filter.Rule{
		Action:               action,
		Direction:            direction,
		Quick:                o.quick,
		Proto:                transProto,
		SrcSubnet:            srcSubnet,
		SrcSubnetInvertMatch: o.srcSubnetInvertMatch,
		SrcPortRange:         srcPortRange,
		DstSubnet:            dstSubnet,
		DstSubnetInvertMatch: o.dstSubnetInvertMatch,
		DstPortRange:         dstPortRange,
		Nic:                  uint32(o.nic),
		Log:                  o.log,
		KeepState:            o.keepState,
	}, nil
}

func toRule(o *filter.Rule) (Rule, error) {
	action, err := toAction(o.Action)
	if err != nil {
		return Rule{}, err
	}
	direction, err := toDirection(o.Direction)
	if err != nil {
		return Rule{}, err
	}
	transProto, err := toTransProto(o.Proto)
	if err != nil {
		return Rule{}, err
	}
	var srcSubnet, dstSubnet *tcpip.Subnet
	if o.SrcSubnet != nil {
		subnet := fidlconv.ToTCPIPSubnet(*o.SrcSubnet)
		srcSubnet = &subnet
	}
	if o.DstSubnet != nil {
		subnet := fidlconv.ToTCPIPSubnet(*o.DstSubnet)
		dstSubnet = &subnet
	}
	srcPortRange := PortRange{start: o.SrcPortRange.Start, end: o.SrcPortRange.End}
	if !srcPortRange.IsValid() {
		return Rule{}, ErrBadPortRange
	}
	dstPortRange := PortRange{start: o.DstPortRange.Start, end: o.DstPortRange.End}
	if !dstPortRange.IsValid() {
		return Rule{}, ErrBadPortRange
	}
	r := Rule{
		action:               action,
		direction:            direction,
		quick:                o.Quick,
		transProto:           transProto,
		srcSubnet:            srcSubnet,
		srcSubnetInvertMatch: o.SrcSubnetInvertMatch,
		srcPortRange:         srcPortRange,
		dstSubnet:            dstSubnet,
		dstSubnetInvertMatch: o.DstSubnetInvertMatch,
		dstPortRange:         dstPortRange,
		nic:                  tcpip.NICID(o.Nic),
		log:                  o.Log,
		keepState:            o.KeepState,
	}
	if !r.IsValid() {
		return Rule{}, ErrBadRule
	}
	return r, nil
}

func fromRules(rs []Rule) ([]filter.Rule, error) {
	var nrs []filter.Rule
	for i := range rs {
		nr, err := fromRule(&rs[i])
		if err != nil {
			return nil, err
		}
		nrs = append(nrs, nr)
	}
	return nrs, nil
}

func toRules(nrs []filter.Rule) ([]Rule, error) {
	var rs []Rule
	for i := range nrs {
		r, err := toRule(&nrs[i])
		if err != nil {
			return nil, err
		}
		rs = append(rs, r)
	}
	return rs, nil
}

func fromNAT(o *NAT) (filter.Nat, error) {
	transProto, err := fromTransProto(o.transProto)
	if err != nil {
		return filter.Nat{}, err
	}
	srcSubnet, err := fromSubnet(o.srcSubnet)
	if err != nil {
		return filter.Nat{}, err
	}
	newSrcAddr, err := fromAddress(o.newSrcAddr)
	if err != nil {
		return filter.Nat{}, err
	}
	if !o.IsValid() {
		return filter.Nat{}, ErrBadRule
	}
	return filter.Nat{
		Proto:      transProto,
		SrcSubnet:  srcSubnet,
		NewSrcAddr: newSrcAddr,
		Nic:        uint32(o.nic),
	}, nil
}

func toNAT(o *filter.Nat) (NAT, error) {
	transProto, err := toTransProto(o.Proto)
	if err != nil {
		return NAT{}, err
	}
	newSrcAddr, err := toAddress(&o.NewSrcAddr)
	if err != nil {
		return NAT{}, err
	}
	srcSubnet := fidlconv.ToTCPIPSubnet(o.SrcSubnet)
	n := NAT{
		transProto: transProto,
		srcSubnet:  &srcSubnet,
		newSrcAddr: newSrcAddr,
		nic:        tcpip.NICID(o.Nic),
	}
	if !n.IsValid() {
		return NAT{}, ErrBadRule
	}
	return n, nil
}

func fromNATs(ns []NAT) ([]filter.Nat, error) {
	var nns []filter.Nat
	for i := range ns {
		nn, err := fromNAT(&ns[i])
		if err != nil {
			return nil, err
		}
		nns = append(nns, nn)
	}
	return nns, nil
}

func toNATs(nns []filter.Nat) ([]NAT, error) {
	var ns []NAT
	for i := range nns {
		n, err := toNAT(&nns[i])
		if err != nil {
			return nil, err
		}
		ns = append(ns, n)
	}
	return ns, nil
}

func fromRDR(o *RDR) (filter.Rdr, error) {
	transProto, err := fromTransProto(o.transProto)
	if err != nil {
		return filter.Rdr{}, err
	}
	dstAddr, err := fromAddress(o.dstAddr)
	if err != nil {
		return filter.Rdr{}, err
	}
	newDstAddr, err := fromAddress(o.newDstAddr)
	if err != nil {
		return filter.Rdr{}, err
	}
	dstPortRange := filter.PortRange{Start: o.dstPortRange.start, End: o.dstPortRange.end}
	if dstPortRange.Start > dstPortRange.End {
		return filter.Rdr{}, ErrBadPortRange
	}
	dstPortRangeLen := dstPortRange.End - dstPortRange.Start
	newDstPortRange := filter.PortRange{Start: o.newDstPortRange.start, End: o.newDstPortRange.end}
	if newDstPortRange.Start > newDstPortRange.End {
		return filter.Rdr{}, ErrBadPortRange
	}
	newDstPortRangeLen := newDstPortRange.End - newDstPortRange.Start
	if dstPortRangeLen != newDstPortRangeLen {
		return filter.Rdr{}, ErrBadPortRange
	}
	if !o.IsValid() {
		return filter.Rdr{}, ErrBadRule
	}
	return filter.Rdr{
		Proto:           transProto,
		DstAddr:         dstAddr,
		DstPortRange:    dstPortRange,
		NewDstAddr:      newDstAddr,
		NewDstPortRange: newDstPortRange,
		Nic:             uint32(o.nic),
	}, nil
}

func toRDR(o *filter.Rdr) (RDR, error) {
	transProto, err := toTransProto(o.Proto)
	if err != nil {
		return RDR{}, err
	}
	dstAddr, err := toAddress(&o.DstAddr)
	if err != nil {
		return RDR{}, err
	}
	newDstAddr, err := toAddress(&o.NewDstAddr)
	if err != nil {
		return RDR{}, err
	}
	dstPortRange := PortRange{start: o.DstPortRange.Start, end: o.DstPortRange.End}
	dstPortRangeLen, err := dstPortRange.Length()
	if err != nil {
		return RDR{}, err
	}
	newDstPortRange := PortRange{start: o.NewDstPortRange.Start, end: o.NewDstPortRange.End}
	newDstPortRangeLen, err := newDstPortRange.Length()
	if err != nil {
		return RDR{}, err
	}
	if dstPortRangeLen != newDstPortRangeLen {
		return RDR{}, ErrBadPortRange
	}
	r := RDR{
		transProto:      transProto,
		dstAddr:         dstAddr,
		dstPortRange:    dstPortRange,
		newDstAddr:      newDstAddr,
		newDstPortRange: newDstPortRange,
		nic:             tcpip.NICID(o.Nic),
	}
	if !r.IsValid() {
		return RDR{}, ErrBadRule
	}
	return r, nil
}

func fromRDRs(rs []RDR) ([]filter.Rdr, error) {
	var nrs []filter.Rdr
	for i := range rs {
		nr, err := fromRDR(&rs[i])
		if err != nil {
			return nil, err
		}
		nrs = append(nrs, nr)
	}
	return nrs, nil
}

func toRDRs(nrs []filter.Rdr) ([]RDR, error) {
	var rs []RDR
	for i := range nrs {
		r, err := toRDR(&nrs[i])
		if err != nil {
			return nil, err
		}
		rs = append(rs, r)
	}
	return rs, nil
}
