// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package filter provides the implementation of packet filter.
package filter

import (
	"log"
	"sync/atomic"

	"github.com/google/netstack/tcpip"
	"github.com/google/netstack/tcpip/buffer"
	"github.com/google/netstack/tcpip/header"
	"github.com/google/netstack/tcpip/ports"
)

const debugFilter = false
const debugFilter2 = false

type Filter struct {
	enabled     atomic.Value // bool
	portManager *ports.PortManager
	rulesetMain RulesetMain
	rulesetNAT  RulesetNAT
	rulesetRDR  RulesetRDR
	states      *States
}

func New(pm *ports.PortManager) *Filter {
	f := &Filter{
		portManager: pm,
		states:      NewStates(),
	}
	f.enabled.Store(true)
	return f
}

// Enable enables or disables the packet filter.
func (f *Filter) Enable(b bool) {
	if b {
		log.Printf("packet filter: enabled")
	} else {
		log.Printf("packet filter: disabled")
	}
	f.enabled.Store(b)
}

// IsEnabled returns true if the packet filter is currently enabled.
func (f *Filter) IsEnabled() bool {
	return f.enabled.Load().(bool)
}

// Run is the entry point to the packet filter. It should be called from
// two hook locations in the network stack: one for incoming packets, another
// for outgoing packets.
func (f *Filter) Run(dir Direction, netProto tcpip.NetworkProtocolNumber, vv *buffer.VectorisedView) Action {
	if f.enabled.Load().(bool) == false {
		// The filter is disabled.
		return Pass
	}

	b := vv.First()
	var plb []byte
	if len(vv.Views()) >= 2 {
		plb = (vv.Views())[1]
	}

	// Lock the state maps.
	// TODO: Improve concurrency with more granular locking.
	f.states.mu.Lock()
	defer f.states.mu.Unlock()

	f.states.purgeExpiredEntries(f.portManager)

	// Parse the network protocol header.
	var transProto tcpip.TransportProtocolNumber
	var srcAddr, dstAddr tcpip.Address
	var payloadLength uint16
	var th []byte
	switch netProto {
	case header.IPv4ProtocolNumber:
		ipv4 := header.IPv4(b)
		if !ipv4.IsValid(len(b) + len(plb)) {
			if debugFilter {
				log.Printf("packet filter: ipv4 packet is not valid")
			}
			return Drop
		}
		transProto = ipv4.TransportProtocol()
		srcAddr = ipv4.SourceAddress()
		dstAddr = ipv4.DestinationAddress()
		payloadLength = ipv4.PayloadLength()
		th = b[ipv4.HeaderLength():]
	case header.IPv6ProtocolNumber:
		ipv6 := header.IPv6(b)
		if !ipv6.IsValid(len(b) + len(plb)) {
			if debugFilter {
				log.Printf("packet filter: ipv6 packet is not valid")
			}
			return Drop
		}
		transProto = ipv6.TransportProtocol()
		srcAddr = ipv6.SourceAddress()
		dstAddr = ipv6.DestinationAddress()
		payloadLength = ipv6.PayloadLength()
		th = b[header.IPv6MinimumSize:]
	case header.ARPProtocolNumber:
		// TODO: Anything?
		return Pass
	default:
		if debugFilter {
			log.Printf("packet filter: drop unknown network protocol: %v (%s)", netProto, dir)
		}
		return Drop
	}

	switch transProto {
	case header.ICMPv4ProtocolNumber:
		return f.runForICMPv4(dir, srcAddr, dstAddr, payloadLength, b, th, plb)
	case header.ICMPv6ProtocolNumber:
		// Do nothing.
		return Pass
	case header.UDPProtocolNumber:
		return f.runForUDP(dir, netProto, srcAddr, dstAddr, payloadLength, b, th, plb)
	case header.TCPProtocolNumber:
		return f.runForTCP(dir, netProto, srcAddr, dstAddr, payloadLength, b, th, plb)
	default:
		if debugFilter {
			log.Printf("packet filter: %d: drop unknown transport protocol: %d", dir, transProto)
		}
		return Drop
	}
}

func (f *Filter) runForICMPv4(dir Direction, srcAddr, dstAddr tcpip.Address, payloadLength uint16, b, th, plb []byte) Action {
	if s, err := f.states.findStateICMPv4(dir, header.IPv4ProtocolNumber, header.ICMPv4ProtocolNumber, srcAddr, dstAddr, payloadLength, th, plb); s != nil {
		if debugFilter2 {
			log.Printf("packet filter: icmp state found: %v", s)
		}

		// If NAT or RDR is in effect, rewrite address and port.
		// Note that findStateICMPv4 may return a state for a different transport protocol.
		switch s.transProto {
		case header.ICMPv4ProtocolNumber:
			if s.lanAddr != s.gwyAddr {
				switch dir {
				case Incoming:
					rewritePacketICMPv4(s.lanAddr, false, b, th)
				case Outgoing:
					rewritePacketICMPv4(s.gwyAddr, true, b, th)
				}
			}
		case header.UDPProtocolNumber:
			if s.lanAddr != s.gwyAddr || s.lanPort != s.gwyPort {
				switch dir {
				case Incoming:
					rewritePacketUDPv4(s.lanAddr, s.lanPort, false, b, th)
				case Outgoing:
					rewritePacketUDPv4(s.gwyAddr, s.gwyPort, true, b, th)
				}
			}
		case header.TCPProtocolNumber:
			if s.lanAddr != s.gwyAddr || s.lanPort != s.gwyPort {
				switch dir {
				case Incoming:
					rewritePacketTCPv4(s.lanAddr, s.lanPort, false, b, th)
				case Outgoing:
					rewritePacketTCPv4(s.gwyAddr, s.gwyPort, true, b, th)
				}
			}
		default:
			panic("Unsupported transport protocol")
		}

		return Pass
	} else if err != nil {
		if debugFilter {
			log.Printf("packet filter: %v", err)
		}
		return Drop
	}

	var nat *NAT
	var origAddr tcpip.Address

	if dir == Outgoing {
		if nat = f.matchNAT(header.ICMPv4ProtocolNumber, srcAddr); nat != nil {
			// Rewrite srcAddr in the packet.
			// The original values are saved in origAddr.
			origAddr = srcAddr
			srcAddr = nat.newSrcAddr
			rewritePacketICMPv4(srcAddr, true, b, th)
		}
	}

	// TODO: Add interface parameter.
	rm := f.matchMain(dir, header.ICMPv4ProtocolNumber, srcAddr, 0, dstAddr, 0)
	if rm != nil {
		if rm.log {
			// TODO: Improve the log format.
			log.Printf("packet filter: Rule matched: %v", rm)
		}
		if rm.action == DropReset {
			if nat != nil {
				// Revert the packet modified for NAT.
				rewritePacketICMPv4(origAddr, true, b, th)
			}
			// TODO: Send a Reset packet.
			return Drop
		} else if rm.action == Drop {
			return Drop
		}
	}
	if (rm != nil && rm.keepState) || nat != nil {
		f.states.createState(dir, header.ICMPv4ProtocolNumber, srcAddr, 0, dstAddr, 0, origAddr, 0, "", 0, nat != nil, false, payloadLength, b, th, plb)
	}

	return Pass
}

func (f *Filter) runForUDP(dir Direction, netProto tcpip.NetworkProtocolNumber, srcAddr, dstAddr tcpip.Address, payloadLength uint16, b, th, plb []byte) Action {
	if len(th) < header.UDPMinimumSize {
		if debugFilter2 {
			log.Printf("packet filter: udp packet too short")
		}
		return Drop
	}
	udp := header.UDP(th)
	srcPort := udp.SourcePort()
	dstPort := udp.DestinationPort()

	if s, err := f.states.findStateUDP(dir, netProto, header.UDPProtocolNumber, srcAddr, srcPort, dstAddr, dstPort, payloadLength, th); s != nil {
		if debugFilter2 {
			log.Printf("packet filter: udp state found: %v", s)
		}
		// If NAT or RDR is in effect, rewrite address and port.
		if s.lanAddr != s.gwyAddr || s.lanPort != s.gwyPort {
			switch netProto {
			case header.IPv4ProtocolNumber:
				switch dir {
				case Incoming:
					rewritePacketUDPv4(s.lanAddr, s.lanPort, false, b, th)
				case Outgoing:
					rewritePacketUDPv4(s.gwyAddr, s.gwyPort, true, b, th)
				}
			case header.IPv6ProtocolNumber:
				switch dir {
				case Incoming:
					rewritePacketUDPv6(s.lanAddr, s.lanPort, false, b, th)
				case Outgoing:
					rewritePacketUDPv6(s.gwyAddr, s.gwyPort, true, b, th)
				}
			}
		}

		return Pass
	} else if err != nil {
		if debugFilter {
			log.Printf("packet filter: %v", err)
		}
		return Drop
	}

	var nat *NAT
	var rdr *RDR
	var origAddr tcpip.Address
	var origPort uint16
	var newAddr tcpip.Address
	var newPort uint16

	switch dir {
	case Incoming:
		if rdr = f.matchRDR(header.UDPProtocolNumber, dstAddr, dstPort); rdr != nil {
			if debugFilter2 {
				log.Printf("packet filter: RDR rule matched: proto: %d, dstAddr: %s, dstPort: %d, newDstAddr: %s, newDstPort: %d, nic: %d", rdr.transProto, rdr.dstAddr, rdr.dstPort, rdr.newDstAddr, rdr.newDstPort, rdr.nic)
			}
			// Rewrite dstAddr and dstPort in the packet.
			// The original values are saved in origAddr and origPort.
			origAddr = dstAddr
			dstAddr = rdr.newDstAddr
			origPort = dstPort
			dstPort = rdr.newDstPort
			if debugFilter2 {
				log.Printf("packet filter: RDR: rewrite orig(%s:%d) with new(%s:%d)", origAddr, origPort, dstAddr, dstPort)
			}
			switch netProto {
			case header.IPv4ProtocolNumber:
				rewritePacketUDPv4(dstAddr, dstPort, false, b, th)
			case header.IPv6ProtocolNumber:
				rewritePacketUDPv6(dstAddr, dstPort, false, b, th)
			}
		}
	case Outgoing:
		if nat = f.matchNAT(header.UDPProtocolNumber, srcAddr); nat != nil {
			if debugFilter2 {
				log.Printf("packet filter: NAT rule matched: proto: %d, srcNet: %s(%s), srcAddr: %s, nic: %d", nat.transProto, nat.srcNet.ID(), tcpip.Address(nat.srcNet.Mask()), nat.newSrcAddr, nat.nic)
			}
			newAddr = nat.newSrcAddr
			// Reserve a new port.
			netProtos := []tcpip.NetworkProtocolNumber{header.IPv4ProtocolNumber, header.IPv6ProtocolNumber}
			var e *tcpip.Error
			newPort, e = f.portManager.ReservePort(netProtos, header.UDPProtocolNumber, newAddr, 0)
			if e != nil {
				if debugFilter {
					log.Printf("packet filter: ReservePort: %v", e)
				}
				return Drop
			}
			// Rewrite srcAddr and srcPort in the packet.
			// The original values are saved in origAddr and origPort.
			origAddr = srcAddr
			srcAddr = newAddr
			origPort = srcPort
			srcPort = newPort
			if debugFilter2 {
				log.Printf("packet filter: NAT: rewrite orig(%s:%d) with new(%s:%d)", origAddr, origPort, srcAddr, srcPort)
			}
			switch netProto {
			case header.IPv4ProtocolNumber:
				rewritePacketUDPv4(srcAddr, srcPort, true, b, th)
			case header.IPv6ProtocolNumber:
				rewritePacketUDPv6(srcAddr, srcPort, true, b, th)
			}
		}
	}

	// TODO: Add interface parameter.
	rm := f.matchMain(dir, header.UDPProtocolNumber, srcAddr, srcPort, dstAddr, dstPort)
	if rm != nil {
		if rm.log {
			// TODO: Improve the log format.
			log.Printf("packet filter: Rule matched: %v", rm)
		}
		if rm.action == DropReset {
			if nat != nil {
				// Revert the packet modified for NAT.
				switch netProto {
				case header.IPv4ProtocolNumber:
					rewritePacketUDPv4(origAddr, origPort, true, b, th)
				case header.IPv6ProtocolNumber:
					rewritePacketUDPv6(origAddr, origPort, true, b, th)
				}
			}
			if rdr != nil {
				// Revert the packet modified for RDR.
				switch netProto {
				case header.IPv4ProtocolNumber:
					rewritePacketUDPv4(origAddr, origPort, false, b, th)
				case header.IPv6ProtocolNumber:
					rewritePacketUDPv6(origAddr, origPort, false, b, th)
				}
			}
			// TODO: Send a Reset packet.
			return Drop
		} else if rm.action == Drop {
			return Drop
		}
	}
	if (rm != nil && rm.keepState) || nat != nil || rdr != nil {
		f.states.createState(dir, header.UDPProtocolNumber, srcAddr, srcPort, dstAddr, dstPort, origAddr, origPort, newAddr, newPort, nat != nil, rdr != nil, payloadLength, b, th, plb)
	}

	return Pass
}

func (f *Filter) runForTCP(dir Direction, netProto tcpip.NetworkProtocolNumber, srcAddr, dstAddr tcpip.Address, payloadLength uint16, b, th, plb []byte) Action {
	if len(th) < header.TCPMinimumSize {
		if debugFilter {
			log.Printf("packet filter: tcp packet too short")
		}
		return Drop
	}
	tcp := header.TCP(th)
	srcPort := tcp.SourcePort()
	dstPort := tcp.DestinationPort()

	if s, err := f.states.findStateTCP(dir, netProto, header.TCPProtocolNumber, srcAddr, srcPort, dstAddr, dstPort, payloadLength, th); s != nil {
		if debugFilter2 {
			log.Printf("packet filter: tcp state found: %v", s)
		}

		// If NAT or RDR is in effect, rewrite address and port.
		if s.lanAddr != s.gwyAddr || s.lanPort != s.gwyPort {
			switch netProto {
			case header.IPv4ProtocolNumber:
				switch dir {
				case Incoming:
					rewritePacketTCPv4(s.lanAddr, s.lanPort, false, b, th)
				case Outgoing:
					rewritePacketTCPv4(s.gwyAddr, s.gwyPort, true, b, th)
				}
			case header.IPv6ProtocolNumber:
				switch dir {
				case Incoming:
					rewritePacketTCPv6(s.lanAddr, s.lanPort, false, b, th)
				case Outgoing:
					rewritePacketTCPv6(s.gwyAddr, s.gwyPort, true, b, th)
				}
			}
		}

		return Pass
	} else if err != nil {
		if debugFilter {
			log.Printf("packet filter: %v", err)
		}
		return Drop
	}

	var nat *NAT
	var rdr *RDR
	var origAddr tcpip.Address
	var origPort uint16
	var newAddr tcpip.Address
	var newPort uint16

	switch dir {
	case Incoming:
		if rdr = f.matchRDR(header.TCPProtocolNumber, dstAddr, dstPort); rdr != nil {
			// Rewrite dstAddr and dstPort in the packet.
			// The original values are saved in origAddr and origPort.
			origAddr = dstAddr
			dstAddr = rdr.newDstAddr
			origPort = dstPort
			dstPort = rdr.newDstPort
			switch netProto {
			case header.IPv4ProtocolNumber:
				rewritePacketTCPv4(dstAddr, dstPort, false, b, th)
			case header.IPv6ProtocolNumber:
				rewritePacketTCPv6(dstAddr, dstPort, false, b, th)
			}
		}
	case Outgoing:
		if nat = f.matchNAT(header.TCPProtocolNumber, srcAddr); nat != nil {
			newAddr = nat.newSrcAddr
			// Reserve a new port.
			netProtos := []tcpip.NetworkProtocolNumber{header.IPv4ProtocolNumber, header.IPv6ProtocolNumber}
			var e *tcpip.Error
			newPort, e = f.portManager.ReservePort(netProtos, header.TCPProtocolNumber, newAddr, 0)
			if e != nil {
				if debugFilter {
					log.Printf("packet filter: ReservePort: %v", e)
				}
				return Drop
			}
			// Rewrite srcAddr and srcPort in the packet.
			// The original values are saved in origAddr and origPort.
			origAddr = srcAddr
			srcAddr = newAddr
			origPort = srcPort
			srcPort = newPort
			switch netProto {
			case header.IPv4ProtocolNumber:
				rewritePacketTCPv4(srcAddr, srcPort, true, b, th)
			case header.IPv6ProtocolNumber:
				rewritePacketTCPv6(srcAddr, srcPort, true, b, th)
			}
		}
	}

	// TODO: Add interface parameter.
	rm := f.matchMain(dir, header.TCPProtocolNumber, srcAddr, srcPort, dstAddr, dstPort)
	if rm != nil {
		if rm.log {
			// TODO: Improve the log format.
			log.Printf("packet filter: Rule matched: %v", rm)
		}
		if rm.action == DropReset {
			if nat != nil {
				switch netProto {
				case header.IPv4ProtocolNumber:
					rewritePacketTCPv4(origAddr, origPort, true, b, th)
				case header.IPv6ProtocolNumber:
					rewritePacketTCPv6(origAddr, origPort, true, b, th)
				}
			}
			if rdr != nil {
				// Revert the packet modified for RDR.
				switch netProto {
				case header.IPv4ProtocolNumber:
					rewritePacketTCPv4(origAddr, origPort, false, b, th)
				case header.IPv6ProtocolNumber:
					rewritePacketTCPv6(origAddr, origPort, false, b, th)
				}
			}
			// TODO: Send a Reset packet.
			return Drop
		} else if rm.action == Drop {
			return Drop
		}
	}
	if (rm != nil && rm.keepState) || nat != nil || rdr != nil {
		f.states.createState(dir, header.TCPProtocolNumber, srcAddr, srcPort, dstAddr, dstPort, origAddr, origPort, newAddr, newPort, nat != nil, rdr != nil, payloadLength, b, th, plb)
	}

	return Pass
}

func (f *Filter) matchMain(dir Direction, transProto tcpip.TransportProtocolNumber, srcAddr tcpip.Address, srcPort uint16, dstAddr tcpip.Address, dstPort uint16) *Rule {
	f.rulesetMain.RLock()
	defer f.rulesetMain.RUnlock()
	var rm *Rule
	for _, r := range f.rulesetMain.v {
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

func (f *Filter) matchNAT(transProto tcpip.TransportProtocolNumber, srcAddr tcpip.Address) *NAT {
	f.rulesetNAT.RLock()
	defer f.rulesetNAT.RUnlock()
	for _, r := range f.rulesetNAT.v {
		if r.transProto == transProto &&
			r.srcNet.Contains(srcAddr) {
			return r
		}
	}
	return nil
}

func (f *Filter) matchRDR(transProto tcpip.TransportProtocolNumber, dstAddr tcpip.Address, dstPort uint16) *RDR {
	f.rulesetRDR.RLock()
	defer f.rulesetRDR.RUnlock()
	for _, r := range f.rulesetRDR.v {
		if r.transProto == transProto &&
			r.dstAddr == dstAddr &&
			r.dstPort == dstPort {
			return r
		}
	}
	return nil
}
