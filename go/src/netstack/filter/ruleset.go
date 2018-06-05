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

// Rule describes the conditions and the action of a rule.
type Rule struct {
	action     Action
	direction  Direction
	quick      bool // If a rule with this flag enabled is matched, no more rules will be tested.
	transProto tcpip.TransportProtocolNumber
	srcNot     bool
	srcNet     *tcpip.Subnet
	srcPort    uint16
	dstNot     bool
	dstNet     *tcpip.Subnet
	dstPort    uint16
	nic        tcpip.NICID
	log        bool
	keepState  bool
}

// NAT is a special rule for Network Address Translation, which rewrites
// the address of an outgoing packet.
type NAT struct {
	transProto tcpip.TransportProtocolNumber
	srcNet     *tcpip.Subnet
	newSrcAddr tcpip.Address
	nic        tcpip.NICID
}

// RDR is a special rule for Redirector, which forwards an incoming packet
// to a machine inside the firewall.
type RDR struct {
	transProto tcpip.TransportProtocolNumber
	dstAddr    tcpip.Address
	dstPort    uint16
	newDstAddr tcpip.Address
	newDstPort uint16
	nic        tcpip.NICID
}

type RulesetMain struct {
	sync.RWMutex
	v []*Rule
}

type RulesetNAT struct {
	sync.RWMutex
	v []*NAT
}

type RulesetRDR struct {
	sync.RWMutex
	v []*RDR
}
