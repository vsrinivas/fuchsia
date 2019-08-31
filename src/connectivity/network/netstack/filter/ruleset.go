// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package filter

import (
	"sync"

	"github.com/google/netstack/tcpip"
)

// Direction is which way (Incoming or Outgoing) a packet is moving in the stack.
type Direction int

const (
	Incoming Direction = iota
	Outgoing
)

func (dir Direction) String() string {
	switch dir {
	case Incoming:
		return "Incoming"
	case Outgoing:
		return "Outgoing"
	default:
		panic("Unknown direction")
	}
}

// Action indicates how a packet is handled when a rule is matched.
type Action int

const (
	Pass Action = iota
	Drop
	DropReset
)

func (action Action) String() string {
	switch action {
	case Pass:
		return "Pass"
	case Drop:
		return "Drop"
	case DropReset:
		return "DropReset"
	default:
		panic("Unknown action")
	}
}

// PortRange specifies an inclusive range of port numbers.
type PortRange struct {
	start uint16
	end   uint16
}

func (p *PortRange) IsValid() bool {
	if p.start == 0 {
		return p.end == 0
	}
	return p.start <= p.end
}

func (p *PortRange) Contains(n uint16) bool {
	return *p == PortRange{} || p.start <= n && n <= p.end
}

func (p *PortRange) Length() (uint16, error) {
	if !p.IsValid() {
		return 0, ErrBadPortRange
	}
	if *p == (PortRange{}) {
		return 0, nil
	}
	return p.end - p.start + 1, nil
}

// Rule describes the conditions and the action of a rule.
type Rule struct {
	action               Action
	direction            Direction
	quick                bool // If a rule with this flag enabled is matched, no more rules will be tested.
	transProto           tcpip.TransportProtocolNumber
	srcSubnet            *tcpip.Subnet
	srcSubnetInvertMatch bool // If true, matches any address that is NOT contained in the subnet.
	srcPortRange         PortRange
	dstSubnet            *tcpip.Subnet
	dstSubnetInvertMatch bool // If true, matches any address that is NOT contained in the subnet.
	dstPortRange         PortRange
	nic                  tcpip.NICID
	log                  bool
	keepState            bool
}

// NAT is a special rule for Network Address Translation, which rewrites
// the address of an outgoing packet.
type NAT struct {
	transProto tcpip.TransportProtocolNumber
	srcSubnet  *tcpip.Subnet
	newSrcAddr tcpip.Address
	nic        tcpip.NICID
}

// RDR is a special rule for Redirector, which forwards an incoming packet
// to a machine inside the firewall.
type RDR struct {
	transProto      tcpip.TransportProtocolNumber
	dstAddr         tcpip.Address
	dstPortRange    PortRange
	newDstAddr      tcpip.Address
	newDstPortRange PortRange
	nic             tcpip.NICID
}

func (rdr *RDR) IsValid() bool {
	if !rdr.dstPortRange.IsValid() || !rdr.newDstPortRange.IsValid() {
		return false
	}
	dstPortRangeLen, err := rdr.dstPortRange.Length()
	if err != nil {
		return false
	}
	newDstPortRangeLen, err := rdr.newDstPortRange.Length()
	if err != nil {
		return false
	}
	if dstPortRangeLen == 0 || newDstPortRangeLen == 0 {
		return false
	}
	return dstPortRangeLen == newDstPortRangeLen
}

// When n is the Nth port in the range defined by rdr.dstPortRange, newDstPortRange
// returns the Nth port in the range defined by rdr.newDstPortRange.
func (rdr *RDR) newDstPort(n uint16) uint16 {
	return n - rdr.dstPortRange.start + rdr.newDstPortRange.start
}

type RulesetMain struct {
	sync.RWMutex
	generation uint32
	v          []Rule
}

type RulesetNAT struct {
	sync.RWMutex
	generation uint32
	v          []NAT
}

type RulesetRDR struct {
	sync.RWMutex
	generation uint32
	v          []RDR
}
