// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package filter provides the implementation of packet filter.
package filter

import (
	"log"

	"github.com/google/netstack/tcpip"
	"github.com/google/netstack/tcpip/header"
)

const debugFilter = false

var Enabled bool

// Run is the entry point to the packet filter. It should be called from
// two hook locations in the network stack: one for incoming packets, another
// for outgoing packets.
func Run(dir Direction, netProto tcpip.NetworkProtocolNumber, b, plb []byte) Action {
	// Parse the network protocol header.
	var transProto tcpip.TransportProtocolNumber
	var srcAddr, dstAddr tcpip.Address
	var th []byte
	switch netProto {
	case header.IPv4ProtocolNumber:
		ipv4 := header.IPv4(b)
		transProto = ipv4.TransportProtocol()
		srcAddr = ipv4.SourceAddress()
		dstAddr = ipv4.DestinationAddress()
		th = b[ipv4.HeaderLength():]
	case header.IPv6ProtocolNumber:
		ipv6 := header.IPv6(b)
		transProto = ipv6.TransportProtocol()
		srcAddr = ipv6.SourceAddress()
		dstAddr = ipv6.DestinationAddress()
		th = b[header.IPv6MinimumSize:]
	case header.ARPProtocolNumber:
		// TODO: Anything?
		return Pass
	default:
		if debugFilter {
			log.Printf("drop unknown network protocol")
		}
		return Drop
	}

	// Parse the transport protocol header.
	var srcPort, dstPort uint16
	checkNAT := false
	checkRDR := false

	switch transProto {
	case header.ICMPv4ProtocolNumber:
		if dir == Outgoing {
			checkNAT = true
		}
	case header.ICMPv6ProtocolNumber:
		// Do nothing.
	case header.UDPProtocolNumber:
		udp := header.UDP(th)
		srcPort = udp.SourcePort()
		dstPort = udp.DestinationPort()
		if dir == Outgoing {
			checkNAT = true
		} else {
			checkRDR = true
		}
	case header.TCPProtocolNumber:
		tcp := header.TCP(th)
		srcPort = tcp.SourcePort()
		dstPort = tcp.DestinationPort()
		if dir == Outgoing {
			checkNAT = true
		} else {
			checkRDR = true
		}
	default:
		if debugFilter {
			log.Printf("%d: drop unknown transport protocol: %d", dir, transProto)
		}
		return Drop
	}

	// Find if we are already tracking the connection.
	if state := findState(dir, transProto, srcAddr, srcPort, dstAddr, dstPort); state != nil {
		return Pass
	}

	var nat *NAT
	var rdr *RDR

	if checkNAT {
		if nat = matchNAT(transProto, srcAddr); nat != nil {
			// TODO: Rewrite source address and port.
		}
	}
	if checkRDR {
		if rdr = matchRDR(transProto, dstAddr, dstPort); rdr != nil {
			// TODO: Rewrite dest address and port.
		}
	}

	// TODO: Add interface parameter.
	rm := matchMain(dir, transProto, srcAddr, srcPort, dstAddr, dstPort)
	if rm != nil {
		if rm.log {
			// TODO: Improve the log format.
			log.Printf("Rule matched: %v", rm)
		}
		if rm.action == DropReset {
			// TODO: Revert the changes for NAT and RDR.
			// TODO: Send a Reset packet.
			return Drop
		} else if rm.action == Drop {
			return Drop
		}
	}
	if rm != nil || nat != nil || rdr != nil {
		// TODO: Start a connection state tracking.
	}

	return Pass
}

func matchMain(dir Direction, transProto tcpip.TransportProtocolNumber, srcAddr tcpip.Address, srcPort uint16, dstAddr tcpip.Address, dstPort uint16) *Rule {
	rulesetMain.RLock()
	defer rulesetMain.RUnlock()
	var rm *Rule
	for _, r := range rulesetMain.v {
		if r.direction == dir &&
			r.transProto == transProto &&
			(r.srcNet == nil || r.srcNet.Contains(srcAddr) != r.srcNot) &&
			(r.srcPort == 0 || r.srcPort == srcPort) &&
			(r.dstNet == nil || r.dstNet.Contains(dstAddr) != r.dstNot) &&
			(r.dstPort == 0 || r.dstPort == dstPort) {
			rm = r
			if r.quick {
				break
			}
		}
	}
	return rm
}

func matchNAT(transProto tcpip.TransportProtocolNumber, srcAddr tcpip.Address) *NAT {
	rulesetNAT.RLock()
	defer rulesetNAT.RUnlock()
	for _, r := range rulesetNAT.v {
		if r.transProto == transProto &&
			r.srcNet.Contains(srcAddr) {
			return r
		}
	}
	return nil
}

func matchRDR(transProto tcpip.TransportProtocolNumber, dstAddr tcpip.Address, dstPort uint16) *RDR {
	rulesetRDR.RLock()
	defer rulesetRDR.RUnlock()
	for _, r := range rulesetRDR.v {
		if r.transProto == transProto &&
			r.dstNet.Contains(dstAddr) &&
			r.dstPort == dstPort {
			return r
		}
	}
	return nil
}
