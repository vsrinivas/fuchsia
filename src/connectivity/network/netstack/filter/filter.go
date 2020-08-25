// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package filter provides the implementation of packet filter.
package filter

import (
	"sync/atomic"

	syslog "go.fuchsia.dev/fuchsia/src/lib/syslog/go"

	"gvisor.dev/gvisor/pkg/tcpip"
	"gvisor.dev/gvisor/pkg/tcpip/buffer"
	"gvisor.dev/gvisor/pkg/tcpip/header"
	"gvisor.dev/gvisor/pkg/tcpip/ports"
)

const chatty = false

type Filter struct {
	enabled     atomic.Value // bool
	portManager *ports.PortManager
	rulesetMain RulesetMain
	rulesetNAT  RulesetNAT
	rulesetRDR  RulesetRDR
	states      *States
}

const tag = "filter"

func New(pm *ports.PortManager) *Filter {
	f := &Filter{
		portManager: pm,
		states:      NewStates(),
	}
	f.states.enablePurge()
	f.enabled.Store(true)
	return f
}

// Enable enables or disables the packet filter.
func (f *Filter) Enable(b bool) {
	if b {
		syslog.InfoTf(tag, "enabled")
	} else {
		syslog.InfoTf(tag, "disabled")
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
func (f *Filter) Run(dir Direction, netProto tcpip.NetworkProtocolNumber, hdr buffer.View, payload buffer.VectorisedView) Action {
	if f.enabled.Load().(bool) == false {
		// The filter is disabled.
		return Pass
	}

	f.states.purgeExpiredEntries(f.portManager)

	// Parse the network protocol header.
	var transProto tcpip.TransportProtocolNumber
	var srcAddr, dstAddr tcpip.Address
	var payloadLength uint16
	var transportHeader []byte
	switch netProto {
	case header.IPv4ProtocolNumber:
		ipv4 := header.IPv4(hdr)
		if !ipv4.IsValid(len(hdr) + payload.Size()) {
			syslog.VLogTf(syslog.TraceVerbosity, tag, "ipv4 packet is not valid")
			return Drop
		}
		transProto = ipv4.TransportProtocol()
		srcAddr = ipv4.SourceAddress()
		dstAddr = ipv4.DestinationAddress()
		payloadLength = ipv4.PayloadLength()
		transportHeader = ipv4[ipv4.HeaderLength():]
		if ipv4.FragmentOffset() != 0 {
			return Pass
		}
	case header.IPv6ProtocolNumber:
		ipv6 := header.IPv6(hdr)
		if !ipv6.IsValid(len(hdr) + payload.Size()) {
			syslog.VLogTf(syslog.TraceVerbosity, tag, "ipv6 packet is not valid")
			return Drop
		}
		transProto = ipv6.TransportProtocol()
		srcAddr = ipv6.SourceAddress()
		dstAddr = ipv6.DestinationAddress()
		payloadLength = ipv6.PayloadLength()
		transportHeader = ipv6[header.IPv6MinimumSize:]
	case header.ARPProtocolNumber:
		// TODO: Anything?
		return Pass
	default:
		syslog.VLogTf(syslog.TraceVerbosity, tag, "drop unknown network protocol: %v (%v)", netProto, dir)
		return Drop
	}

	f.states.mut.Lock()
	defer f.states.mut.Unlock()

	switch transProto {
	case header.ICMPv4ProtocolNumber:
		return f.runForICMPv4(dir, srcAddr, dstAddr, payloadLength, hdr, transportHeader, payload)
	case header.ICMPv6ProtocolNumber:
		// Do nothing.
		return Pass
	case header.UDPProtocolNumber:
		return f.runForUDP(dir, netProto, srcAddr, dstAddr, payloadLength, hdr, transportHeader, payload)
	case header.TCPProtocolNumber:
		return f.runForTCP(dir, netProto, srcAddr, dstAddr, payloadLength, hdr, transportHeader, payload)
	default:
		syslog.VLogTf(syslog.TraceVerbosity, tag, "drop unknown transport protocol: %v (%v)", transProto, dir)
		return Drop
	}
}

func (f *Filter) runForICMPv4(dir Direction, srcAddr, dstAddr tcpip.Address, payloadLength uint16, hdr buffer.View, transportHeader []byte, payload buffer.VectorisedView) Action {
	if s, err := f.states.findStateICMPv4(dir, header.IPv4ProtocolNumber, header.ICMPv4ProtocolNumber, srcAddr, dstAddr, payloadLength, transportHeader, payload); s != nil {
		if chatty {
			syslog.VLogTf(syslog.TraceVerbosity, tag, "icmp state found: %v", s)
		}

		// If NAT or RDR is in effect, rewrite address and port.
		// Note that findStateICMPv4 may return a state for a different transport protocol.
		switch s.transProto {
		case header.ICMPv4ProtocolNumber:
			if s.lanAddr != s.gwyAddr {
				switch dir {
				case Incoming:
					rewritePacketICMPv4(s.lanAddr, false, hdr, transportHeader)
				case Outgoing:
					rewritePacketICMPv4(s.gwyAddr, true, hdr, transportHeader)
				}
			}
		case header.UDPProtocolNumber:
			if s.lanAddr != s.gwyAddr || s.lanPort != s.gwyPort {
				switch dir {
				case Incoming:
					rewritePacketUDPv4(s.lanAddr, s.lanPort, false, hdr, transportHeader)
				case Outgoing:
					rewritePacketUDPv4(s.gwyAddr, s.gwyPort, true, hdr, transportHeader)
				}
			}
		case header.TCPProtocolNumber:
			if s.lanAddr != s.gwyAddr || s.lanPort != s.gwyPort {
				switch dir {
				case Incoming:
					rewritePacketTCPv4(s.lanAddr, s.lanPort, false, hdr, transportHeader)
				case Outgoing:
					rewritePacketTCPv4(s.gwyAddr, s.gwyPort, true, hdr, transportHeader)
				}
			}
		default:
			panic("Unsupported transport protocol")
		}

		return Pass
	} else if err != nil {
		syslog.VLogTf(syslog.DebugVerbosity, tag, "%v", err)
		return Drop
	}

	var nat *NAT
	var origAddr tcpip.Address
	var nicID tcpip.NICID

	if dir == Outgoing {
		if nat = f.matchNAT(header.ICMPv4ProtocolNumber, srcAddr); nat != nil {
			// Rewrite srcAddr in the packet.
			// The original values are saved in origAddr.
			origAddr = srcAddr
			srcAddr = nat.newSrcAddr
			nicID = nat.nic
			rewritePacketICMPv4(srcAddr, true, hdr, transportHeader)
		}
	}

	// TODO: Add interface parameter.
	rm := f.matchMain(dir, header.ICMPv4ProtocolNumber, srcAddr, 0, dstAddr, 0)
	if rm != nil {
		if rm.nic != 0 {
			nicID = rm.nic
		}
		if rm.log {
			syslog.InfoTf(tag, "%v %v %v %v %v", rm.action, dir, "icmp", srcAddr, dstAddr)
		}
		if rm.action == DropReset {
			if nat != nil {
				// Revert the packet modified for NAT.
				rewritePacketICMPv4(origAddr, true, hdr, transportHeader)
			}
			// TODO: Send a Reset packet.
			return Drop
		} else if rm.action == Drop {
			return Drop
		}
	}
	if (rm != nil && rm.keepState) || nat != nil {
		f.states.createState(dir, nicID, header.ICMPv4ProtocolNumber, srcAddr, 0, dstAddr, 0, origAddr, 0, "", 0, nat != nil, false, payloadLength, hdr, transportHeader, payload)
	}

	return Pass
}

func (f *Filter) runForUDP(dir Direction, netProto tcpip.NetworkProtocolNumber, srcAddr, dstAddr tcpip.Address, payloadLength uint16, hdr buffer.View, transportHeader []byte, payload buffer.VectorisedView) Action {
	if len(transportHeader) < header.UDPMinimumSize {
		syslog.VLogTf(syslog.DebugVerbosity, tag, "udp packet too short")
		return Drop
	}
	udp := header.UDP(transportHeader)
	srcPort := udp.SourcePort()
	dstPort := udp.DestinationPort()

	if s, err := f.states.findStateUDP(dir, netProto, header.UDPProtocolNumber, srcAddr, srcPort, dstAddr, dstPort, payloadLength, transportHeader); s != nil {
		if chatty {
			syslog.VLogTf(syslog.DebugVerbosity, tag, "udp state found: %v", s)
		}

		// If NAT or RDR is in effect, rewrite address and port.
		if s.lanAddr != s.gwyAddr || s.lanPort != s.gwyPort {
			switch netProto {
			case header.IPv4ProtocolNumber:
				switch dir {
				case Incoming:
					rewritePacketUDPv4(s.lanAddr, s.lanPort, false, hdr, transportHeader)
				case Outgoing:
					rewritePacketUDPv4(s.gwyAddr, s.gwyPort, true, hdr, transportHeader)
				}
			case header.IPv6ProtocolNumber:
				switch dir {
				case Incoming:
					rewritePacketUDPv6(s.lanAddr, s.lanPort, false, hdr, transportHeader)
				case Outgoing:
					rewritePacketUDPv6(s.gwyAddr, s.gwyPort, true, hdr, transportHeader)
				}
			}
		}

		return Pass
	} else if err != nil {
		syslog.VLogTf(syslog.DebugVerbosity, tag, "%v", err)
		return Drop
	}

	var nat *NAT
	var rdr *RDR
	var origAddr tcpip.Address
	var origPort uint16
	var newAddr tcpip.Address
	var newPort uint16
	var nicID tcpip.NICID // TODO: nicID should be initialized rather than relying on a match.

	switch dir {
	case Incoming:
		if rdr = f.matchRDR(header.UDPProtocolNumber, dstAddr, dstPort); rdr != nil {
			syslog.VLogTf(syslog.DebugVerbosity, tag, "RDR rule matched: proto: %d, dstAddr: %s, dstPortRange: %v, newDstAddr: %s, newDstPortRange: %v, nic: %d", rdr.transProto, rdr.dstAddr, rdr.dstPortRange, rdr.newDstAddr, rdr.newDstPortRange, rdr.nic)
			// Rewrite dstAddr and dstPort in the packet.
			// The original values are saved in origAddr and origPort.
			origAddr = dstAddr
			dstAddr = rdr.newDstAddr
			origPort = dstPort
			dstPort = rdr.newDstPort(dstPort)
			nicID = rdr.nic
			syslog.VLogTf(syslog.TraceVerbosity, tag, "RDR: rewrite orig(%s:%d) with new(%s:%d)", origAddr, origPort, dstAddr, dstPort)
			switch netProto {
			case header.IPv4ProtocolNumber:
				rewritePacketUDPv4(dstAddr, dstPort, false, hdr, transportHeader)
			case header.IPv6ProtocolNumber:
				rewritePacketUDPv6(dstAddr, dstPort, false, hdr, transportHeader)
			}
		}
	case Outgoing:
		if nat = f.matchNAT(header.UDPProtocolNumber, srcAddr); nat != nil {
			syslog.VLogTf(syslog.TraceVerbosity, tag, "NAT rule matched: proto: %d, srcNet: %s(%s), srcAddr: %s, nic: %d", nat.transProto, nat.srcSubnet.ID(), tcpip.Address(nat.srcSubnet.Mask()), nat.newSrcAddr, nat.nic)
			newAddr = nat.newSrcAddr
			// Reserve a new port.
			netProtos := []tcpip.NetworkProtocolNumber{header.IPv4ProtocolNumber, header.IPv6ProtocolNumber}
			var e *tcpip.Error
			nicID = nat.nic
			newPort, e = f.portManager.ReservePort(netProtos, header.UDPProtocolNumber, newAddr, 0, ports.Flags{}, nat.nic, tcpip.FullAddress{
				Addr: dstAddr,
				Port: dstPort,
			}, nil)
			if e != nil {
				syslog.VLogTf(syslog.TraceVerbosity, tag, "ReservePort: %v", e)
				return Drop
			}
			// Rewrite srcAddr and srcPort in the packet.
			// The original values are saved in origAddr and origPort.
			origAddr = srcAddr
			srcAddr = newAddr
			origPort = srcPort
			srcPort = newPort
			nicID = nat.nic
			syslog.VLogTf(syslog.TraceVerbosity, tag, "NAT: rewrite orig(%s:%d) with new(%s:%d)", origAddr, origPort, srcAddr, srcPort)
			switch netProto {
			case header.IPv4ProtocolNumber:
				rewritePacketUDPv4(srcAddr, srcPort, true, hdr, transportHeader)
			case header.IPv6ProtocolNumber:
				rewritePacketUDPv6(srcAddr, srcPort, true, hdr, transportHeader)
			}
		}
	}

	// TODO: Add interface parameter.
	rm := f.matchMain(dir, header.UDPProtocolNumber, srcAddr, srcPort, dstAddr, dstPort)
	if rm != nil {
		if rm.nic != 0 {
			nicID = rm.nic
		}
		if rm.log {
			syslog.InfoTf(tag, "%v %v %v %v:%v %v:%v",
				rm.action, dir, "udp", srcAddr, srcPort, dstAddr, dstPort)
		}
		if rm.action == DropReset {
			if nat != nil {
				// Revert the packet modified for NAT.
				switch netProto {
				case header.IPv4ProtocolNumber:
					rewritePacketUDPv4(origAddr, origPort, true, hdr, transportHeader)
				case header.IPv6ProtocolNumber:
					rewritePacketUDPv6(origAddr, origPort, true, hdr, transportHeader)
				}
			}
			if rdr != nil {
				// Revert the packet modified for RDR.
				switch netProto {
				case header.IPv4ProtocolNumber:
					rewritePacketUDPv4(origAddr, origPort, false, hdr, transportHeader)
				case header.IPv6ProtocolNumber:
					rewritePacketUDPv6(origAddr, origPort, false, hdr, transportHeader)
				}
			}
			// TODO: Send a Reset packet.
			return Drop
		} else if rm.action == Drop {
			return Drop
		}
	}
	if (rm != nil && rm.keepState) || nat != nil || rdr != nil {
		f.states.createState(dir, nicID, header.UDPProtocolNumber, srcAddr, srcPort, dstAddr, dstPort, origAddr, origPort, newAddr, newPort, nat != nil, rdr != nil, payloadLength, hdr, transportHeader, payload)
	}

	return Pass
}

func (f *Filter) runForTCP(dir Direction, netProto tcpip.NetworkProtocolNumber, srcAddr, dstAddr tcpip.Address, payloadLength uint16, hdr buffer.View, transportHeader []byte, payload buffer.VectorisedView) Action {
	if len(transportHeader) < header.TCPMinimumSize {
		syslog.VLogTf(syslog.DebugVerbosity, tag, "tcp packet too short")
		return Drop
	}
	tcp := header.TCP(transportHeader)
	srcPort := tcp.SourcePort()
	dstPort := tcp.DestinationPort()

	s, err := f.states.findStateTCP(dir, netProto, header.TCPProtocolNumber, srcAddr, srcPort, dstAddr, dstPort, payloadLength, transportHeader)
	if err != nil {
		syslog.WarnTf(tag, "%s", err)
		return Drop
	}
	if s != nil {
		if chatty {
			syslog.VLogTf(syslog.TraceVerbosity, tag, "tcp state found: %v", s)
		}

		// If NAT or RDR is in effect, rewrite address and port.
		if s.lanAddr != s.gwyAddr || s.lanPort != s.gwyPort {
			switch netProto {
			case header.IPv4ProtocolNumber:
				switch dir {
				case Incoming:
					rewritePacketTCPv4(s.lanAddr, s.lanPort, false, hdr, transportHeader)
				case Outgoing:
					rewritePacketTCPv4(s.gwyAddr, s.gwyPort, true, hdr, transportHeader)
				}
			case header.IPv6ProtocolNumber:
				switch dir {
				case Incoming:
					rewritePacketTCPv6(s.lanAddr, s.lanPort, false, hdr, transportHeader)
				case Outgoing:
					rewritePacketTCPv6(s.gwyAddr, s.gwyPort, true, hdr, transportHeader)
				}
			}
		}

		return Pass
	}

	var nat *NAT
	var rdr *RDR
	var origAddr tcpip.Address
	var origPort uint16
	var newAddr tcpip.Address
	var newPort uint16
	var nicID tcpip.NICID // TODO: nicID should be initalized rather than relying on a match.

	switch dir {
	case Incoming:
		if rdr = f.matchRDR(header.TCPProtocolNumber, dstAddr, dstPort); rdr != nil {
			// Rewrite dstAddr and dstPort in the packet.
			// The original values are saved in origAddr and origPort.
			origAddr = dstAddr
			dstAddr = rdr.newDstAddr
			origPort = dstPort
			dstPort = rdr.newDstPort(dstPort)
			nicID = rdr.nic
			switch netProto {
			case header.IPv4ProtocolNumber:
				rewritePacketTCPv4(dstAddr, dstPort, false, hdr, transportHeader)
			case header.IPv6ProtocolNumber:
				rewritePacketTCPv6(dstAddr, dstPort, false, hdr, transportHeader)
			}
		}
	case Outgoing:
		if nat = f.matchNAT(header.TCPProtocolNumber, srcAddr); nat != nil {
			newAddr = nat.newSrcAddr
			// Reserve a new port.
			netProtos := []tcpip.NetworkProtocolNumber{header.IPv4ProtocolNumber, header.IPv6ProtocolNumber}
			var err *tcpip.Error
			newPort, err = f.portManager.ReservePort(netProtos, header.TCPProtocolNumber, newAddr, 0, ports.Flags{}, nicID, tcpip.FullAddress{
				Addr: dstAddr,
				Port: dstPort,
			}, nil)
			if err != nil {
				syslog.WarnTf(tag, "ReservePort: %s", err)
				return Drop
			}
			// Rewrite srcAddr and srcPort in the packet.
			// The original values are saved in origAddr and origPort.
			origAddr = srcAddr
			srcAddr = newAddr
			origPort = srcPort
			srcPort = newPort
			nicID = nat.nic
			switch netProto {
			case header.IPv4ProtocolNumber:
				rewritePacketTCPv4(srcAddr, srcPort, true, hdr, transportHeader)
			case header.IPv6ProtocolNumber:
				rewritePacketTCPv6(srcAddr, srcPort, true, hdr, transportHeader)
			}
		}
	}

	// TODO: Add interface parameter.
	rm := f.matchMain(dir, header.TCPProtocolNumber, srcAddr, srcPort, dstAddr, dstPort)
	if rm != nil {
		if rm.log {
			syslog.InfoTf(tag, "%v %v %v %v:%v %v:%v",
				rm.action, dir, "tcp", srcAddr, srcPort, dstAddr, dstPort)
		}
		if rm.action == DropReset {
			if nat != nil {
				switch netProto {
				case header.IPv4ProtocolNumber:
					rewritePacketTCPv4(origAddr, origPort, true, hdr, transportHeader)
				case header.IPv6ProtocolNumber:
					rewritePacketTCPv6(origAddr, origPort, true, hdr, transportHeader)
				}
			}
			if rdr != nil {
				// Revert the packet modified for RDR.
				switch netProto {
				case header.IPv4ProtocolNumber:
					rewritePacketTCPv4(origAddr, origPort, false, hdr, transportHeader)
				case header.IPv6ProtocolNumber:
					rewritePacketTCPv6(origAddr, origPort, false, hdr, transportHeader)
				}
			}
			// TODO: Send a Reset packet.
			return Drop
		} else if rm.action == Drop {
			return Drop
		}
	}
	if (rm != nil && rm.keepState) || nat != nil || rdr != nil {
		f.states.createState(dir, nicID, header.TCPProtocolNumber, srcAddr, srcPort, dstAddr, dstPort, origAddr, origPort, newAddr, newPort, nat != nil, rdr != nil, payloadLength, hdr, transportHeader, payload)
	}

	return Pass
}

func (f *Filter) matchMain(dir Direction, transProto tcpip.TransportProtocolNumber, srcAddr tcpip.Address, srcPort uint16, dstAddr tcpip.Address, dstPort uint16) *Rule {
	f.rulesetMain.RLock()
	defer f.rulesetMain.RUnlock()
	var rm *Rule
	for i := range f.rulesetMain.v {
		r := &f.rulesetMain.v[i]
		if r.Match(dir, transProto, srcAddr, srcPort, dstAddr, dstPort) {
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
	for i := range f.rulesetNAT.v {
		r := &f.rulesetNAT.v[i]
		if r.Match(transProto, srcAddr) {
			return r
		}
	}
	return nil
}

func (f *Filter) matchRDR(transProto tcpip.TransportProtocolNumber, dstAddr tcpip.Address, dstPort uint16) *RDR {
	f.rulesetRDR.RLock()
	defer f.rulesetRDR.RUnlock()
	for i := range f.rulesetRDR.v {
		r := &f.rulesetRDR.v[i]
		if r.Match(transProto, dstAddr, dstPort) {
			return r
		}
	}
	return nil
}
