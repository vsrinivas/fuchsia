// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package filter

import (
	"encoding/binary"
	"fmt"
	"log"
	"sync"
	"sync/atomic"
	"time"

	"github.com/google/netstack/tcpip"
	"github.com/google/netstack/tcpip/header"
	"github.com/google/netstack/tcpip/ports"
)

// Key is a key for the connection state maps.
type Key struct {
	transProto tcpip.TransportProtocolNumber
	srcAddr    tcpip.Address
	srcPort    uint16
	dstAddr    tcpip.Address
	dstPort    uint16
}

type State struct {
	transProto tcpip.TransportProtocolNumber
	dir        Direction
	lanAddr    tcpip.Address
	lanPort    uint16
	gwyAddr    tcpip.Address
	gwyPort    uint16
	extAddr    tcpip.Address
	extPort    uint16
	rsvdAddr   tcpip.Address
	rsvdPort   uint16
	srcEp      *Endpoint
	dstEp      *Endpoint
	createTime time.Time
	expireTime time.Time
	packets    uint32
	bytes      uint32
}

func (s *State) String() string {
	return fmt.Sprintf("%s:%d %s:%d %s:%d [Lo=%d Hi=%d Win=%d WS=%d] [Lo=%d Hi=%d Win=%d WS=%d] %s:%s",
		s.lanAddr, s.lanPort, s.gwyAddr, s.gwyPort, s.extAddr, s.extPort,
		s.srcEp.seqLo, s.srcEp.seqHi, s.srcEp.maxWin, s.srcEp.wscale,
		s.dstEp.seqLo, s.dstEp.seqHi, s.dstEp.maxWin, s.dstEp.wscale,
		s.srcEp.state, s.dstEp.state)
}

const (
	TCPMaxAckWindow  = 65535 + 1500 // Defined as slightly larger than the max TCP data length (65535).
	TCPMaxZWPDataLen = 1            // Max data length we expect for TCP Zero Window Probe.
)

const (
	ICMPExpireDefault = 10 * time.Second

	UDPExpireAfterMultiple = 1 * time.Minute
	UDPExpireDefault       = 20 * time.Second

	TCPExpireAfterFinWait     = 5 * time.Second
	TCPExpireAfterClosing     = 5 * time.Minute
	TCPExpireAfterEstablished = 30 * time.Second
	TCPExpireDefault          = 24 * time.Hour

	ExpireIntervalMin = 10 * time.Second
)

// EndpointState represents the connection state of an Endpoint.
type EndpointState int

const (
	// Note that we currently allow numeric comparison between two
	// EndpointStates so that the state related logic can be described
	// compactly. We assume an EndpointState's nemeric value will only
	// increase monotonically during the lifetime of the endpoint
	// (e.g. TCPFirstPacket => TCPOpening => TCPEstablished => TCPClosing =>
	// TCPFinWait => TCPClosed).

	// ICMP states.
	// (TODO: consider more definitions.)
	ICMPFirstPacket EndpointState = iota

	// UDP states.
	UDPFirstPacket
	UDPSingle
	UDPMultiple

	// TCP states.
	TCPFirstPacket
	TCPOpening
	TCPEstablished
	TCPClosing
	TCPFinWait
	TCPClosed
)

func (state EndpointState) String() string {
	switch state {
	case ICMPFirstPacket:
		return "ICMPFirstPacket"
	case UDPFirstPacket:
		return "UDPFirstPacket"
	case UDPSingle:
		return "UDPSingle"
	case UDPMultiple:
		return "UDPMultiple"
	case TCPFirstPacket:
		return "TCPFirstPacket"
	case TCPOpening:
		return "TCPOpening"
	case TCPEstablished:
		return "TCPEstablished"
	case TCPClosing:
		return "TCPClosing"
	case TCPFinWait:
		return "TCPFinWait"
	case TCPClosed:
		return "TCPClosed"
	default:
		panic("Unknown state")
	}
}

// Endpoint maintains the current state and the sequence number information of an endpoint.
type Endpoint struct {
	seqLo  seqnum // Max seqnum sent.
	seqHi  seqnum // Max seqnum the peer ACK'ed + win.
	maxWin uint16
	wscale int8 // 0 to 14. -1 means wscale is not supported.
	state  EndpointState
}

func (s *State) updateStateICMPv4(dir Direction, dataLen uint16) error {
	s.packets++
	s.bytes += uint32(dataLen)
	s.expireTime = time.Now().Add(ICMPExpireDefault)
	return nil
}

func (s *State) updateStateUDP(dir Direction, dataLen uint16) error {
	var srcEp, dstEp *Endpoint
	if dir == s.dir {
		srcEp = s.srcEp
		dstEp = s.dstEp
	} else {
		srcEp = s.dstEp
		dstEp = s.srcEp
	}

	s.packets++
	s.bytes += uint32(dataLen)

	// Update state.
	if srcEp.state < UDPSingle {
		srcEp.state = UDPSingle
	}
	if dstEp.state == UDPSingle {
		dstEp.state = UDPMultiple
	}

	// Update expire time.
	if srcEp.state == UDPMultiple && dstEp.state == UDPMultiple {
		s.expireTime = time.Now().Add(UDPExpireAfterMultiple)
	} else {
		s.expireTime = time.Now().Add(UDPExpireDefault)
	}

	if debugFilter {
		log.Printf("packet filter: updated state: %v", s)
	}

	return nil
}

func (s *State) updateStateTCP(dir Direction, dataLen uint16, win uint16, seq, ack seqnum, flags uint8, wscale int) error {
	end := seq.Add(uint32(dataLen))

	if flags&header.TCPFlagSyn != 0 {
		end++
	}
	if flags&header.TCPFlagFin != 0 {
		end++
	}

	var srcEp, dstEp *Endpoint
	if dir == s.dir {
		srcEp = s.srcEp
		dstEp = s.dstEp
	} else {
		srcEp = s.dstEp
		dstEp = s.srcEp
	}

	// Sequence tracking algorithm from Guido van Rooij's paper.

	if srcEp.state == TCPFirstPacket {
		// This is the first packet from this end. Initialize the state.
		srcEp.seqLo = end
		srcEp.seqHi = end + TCPMaxZWPDataLen
		srcEp.maxWin = TCPMaxZWPDataLen
		srcEp.wscale = int8(wscale)
	}

	sws := uint8(0)
	dws := uint8(0)
	if srcEp.wscale >= 0 && dstEp.wscale >= 0 && flags&header.TCPFlagSyn == 0 {
		sws = uint8(srcEp.wscale)
		dws = uint8(dstEp.wscale)
	}

	if flags&header.TCPFlagAck == 0 {
		// Pretend the ACK flag was set.
		ack = dstEp.seqLo
	} else if ack == 0 &&
		flags&(header.TCPFlagAck|header.TCPFlagRst) == (header.TCPFlagAck|header.TCPFlagRst) {
		// Broken TCP stacks set the ACK flag in RST packets, but leave the ack
		// field 0. Pretend the ACK is valid.
		ack = dstEp.seqLo
	}

	if seq == end {
		// If there's no data, assume seq is valid and only look at ack below.
		seq = srcEp.seqLo
		end = seq
	}

	ackskew := int32(dstEp.seqLo - ack)

	// Check the boundaries for seq and ack (See Rooij's paper):
	// I.   seq + dataLen <= srcEp.seqHi
	// II.  seq >= srcEp.seqLo - dstEp.maxWin
	// III. ack <= dstEp.seqLo + TCPMaxAckWindow
	// IV.  ack >= dstEp.seqLo - TCPMaxAckWindow
	if !end.LessThanEq(srcEp.seqHi) ||
		!seq.GreaterThanEq(srcEp.seqLo.Sub(uint32(dstEp.maxWin)<<dws)) ||
		ackskew < -TCPMaxAckWindow ||
		ackskew > (TCPMaxAckWindow<<dws) {
		return ErrBadTCPState
	}

	if ackskew < 0 {
		dstEp.seqLo = ack
	}

	s.packets++
	s.bytes += uint32(dataLen)

	if srcEp.maxWin < win {
		srcEp.maxWin = win
	}
	if end.GreaterThan(srcEp.seqLo) {
		srcEp.seqLo = end
	}
	if ack.Add(uint32(win) << sws).GreaterThanEq(dstEp.seqHi) {
		d := uint32(win) << sws
		if d < TCPMaxZWPDataLen {
			d = TCPMaxZWPDataLen
		}
		dstEp.seqHi = ack.Add(d)
	}

	// Update state.
	if flags&header.TCPFlagSyn != 0 {
		if srcEp.state < TCPOpening {
			srcEp.state = TCPOpening
		}
	}
	if flags&header.TCPFlagFin != 0 {
		if srcEp.state < TCPClosing {
			srcEp.state = TCPClosing
		}
	}
	if flags&header.TCPFlagAck != 0 {
		if dstEp.state == TCPOpening {
			dstEp.state = TCPEstablished
		} else if dstEp.state == TCPClosing {
			dstEp.state = TCPFinWait
		}
	}
	if flags&header.TCPFlagRst != 0 {
		srcEp.state = TCPClosed
		dstEp.state = TCPClosed
	}

	// Update expire time.
	if srcEp.state >= TCPFinWait && dstEp.state >= TCPFinWait {
		s.expireTime = time.Now().Add(TCPExpireAfterFinWait)
	} else if srcEp.state >= TCPClosing || dstEp.state >= TCPClosing {
		s.expireTime = time.Now().Add(TCPExpireAfterClosing)
	} else if srcEp.state < TCPEstablished || dstEp.state < TCPEstablished {
		s.expireTime = time.Now().Add(TCPExpireAfterEstablished)
	} else {
		s.expireTime = time.Now().Add(TCPExpireDefault)
	}
	return nil
}

// updateStateTCPinICMP is a subset of updateStateTCP, which just checks seq.
func (s *State) updateStateTCPinICMP(dir Direction, seq seqnum) error {
	var srcEp, dstEp *Endpoint
	if dir == s.dir {
		srcEp = s.srcEp
		dstEp = s.dstEp
	} else {
		srcEp = s.dstEp
		dstEp = s.srcEp
	}

	dws := uint8(0)
	if srcEp.wscale >= 0 && dstEp.wscale >= 0 {
		dws = uint8(dstEp.wscale)
	}

	if !seq.GreaterThanEq(srcEp.seqLo.Sub(uint32(dstEp.maxWin) << dws)) {
		return ErrBadTCPState
	}
	return nil
}

// States is a collection of State we are tracking.
type States struct {
	purgeEnabled atomic.Value // bool

	mu       sync.RWMutex
	extToGwy map[Key]*State
	lanToExt map[Key]*State
}

func NewStates() *States {
	ss := &States{
		extToGwy: make(map[Key]*State),
		lanToExt: make(map[Key]*State),
	}
	ss.purgeEnabled.Store(false)
	return ss
}

func (ss *States) enablePurge() {
	ss.purgeEnabled.Store(true)
}

func (ss *States) purgeExpiredEntries(pm *ports.PortManager) {
	if ss.purgeEnabled.Load().(bool) {
		ss.purgeEnabled.Store(false)
		defer time.AfterFunc(ExpireIntervalMin, ss.enablePurge)

		now := time.Now()
		for k, s := range ss.extToGwy {
			if s.expireTime.After(now) {
				if debugFilter {
					log.Printf("packet filter: delete state: %v (ExtToGwy)", s)
				}
				delete(ss.extToGwy, k)
			}
			if s.rsvdPort != 0 {
				// Release the reserved port.
				netProtos := []tcpip.NetworkProtocolNumber{header.IPv4ProtocolNumber, header.IPv6ProtocolNumber}
				pm.ReleasePort(netProtos, s.transProto, s.rsvdAddr, s.rsvdPort)
			}
		}
		for k, s := range ss.lanToExt {
			if s.expireTime.After(now) {
				if debugFilter {
					log.Printf("packet filter: delete state: %v (LanToExt)", s)
				}
				delete(ss.lanToExt, k)
			}
		}
	}
}

func (ss *States) getState(dir Direction, transProto tcpip.TransportProtocolNumber, srcAddr tcpip.Address, srcPort uint16, dstAddr tcpip.Address, dstPort uint16) *State {
	switch dir {
	case Incoming:
		return ss.extToGwy[Key{transProto, srcAddr, srcPort, dstAddr, dstPort}]
	case Outgoing:
		return ss.lanToExt[Key{transProto, srcAddr, srcPort, dstAddr, dstPort}]
	default:
		panic("unknown direction")
	}
}

func (ss *States) createState(dir Direction, transProto tcpip.TransportProtocolNumber, srcAddr tcpip.Address, srcPort uint16, dstAddr tcpip.Address, dstPort uint16, origAddr tcpip.Address, origPort uint16, rsvdAddr tcpip.Address, rsvdPort uint16, isNAT bool, isRDR bool, payloadLength uint16, b, th, plb []byte) *State {
	var srcEp, dstEp *Endpoint
	var createTime, expireTime time.Time
	var dataLen uint16
	switch transProto {
	case header.ICMPv4ProtocolNumber:
		if len(th) < header.ICMPv4MinimumSize {
			return nil
		}
		icmp := header.ICMPv4(th)
		if isICMPv4ErrorMessage(icmp.Type()) {
			// We don't have to create a state for this.
			return nil
		}
		dataLen = payloadLength - 8 // ICMP header size is 8.
		var id uint16
		if len(th) > 4 {
			id = binary.BigEndian.Uint16(th[4:])
		} else if len(th)+len(plb) > 4 {
			id = binary.BigEndian.Uint16(plb[4-len(th):])
		}
		srcPort = id
		dstPort = id
		srcEp = &Endpoint{
			seqLo:  0,
			seqHi:  0,
			maxWin: 0,
			state:  ICMPFirstPacket,
		}
		dstEp = &Endpoint{
			seqLo:  0,
			seqHi:  0,
			maxWin: 0,
			state:  ICMPFirstPacket,
		}
		createTime = time.Now()
		expireTime = createTime.Add(20 * time.Second)
	case header.UDPProtocolNumber:
		dataLen = payloadLength - header.UDPMinimumSize
		srcEp = &Endpoint{
			seqLo:  0,
			seqHi:  0,
			maxWin: 0,
			state:  UDPSingle,
		}
		dstEp = &Endpoint{
			seqLo:  0,
			seqHi:  0,
			maxWin: 0,
			state:  UDPFirstPacket,
		}
		createTime = time.Now()
		expireTime = createTime.Add(30 * time.Second)
	case header.TCPProtocolNumber:
		tcp := header.TCP(th)
		flags := tcp.Flags()
		dataLen = payloadLength - uint16(tcp.DataOffset())
		wscale := -1
		if flags&header.TCPFlagSyn != 0 {
			dataLen++
			synOpts := header.ParseSynOptions(tcp.Options(), flags&header.TCPFlagAck != 0)
			wscale = synOpts.WS
		}
		if flags&header.TCPFlagFin != 0 {
			dataLen++
		}
		seqLo := seqnum(tcp.SequenceNumber()).Add(uint32(dataLen))
		maxWin := tcp.WindowSize()
		if maxWin < TCPMaxZWPDataLen {
			maxWin = TCPMaxZWPDataLen
		}
		srcEp = &Endpoint{
			seqLo:  seqLo,
			seqHi:  seqLo + TCPMaxZWPDataLen,
			maxWin: maxWin,
			wscale: int8(wscale),
			state:  TCPOpening,
		}
		dstEp = &Endpoint{ // Assign temporary values as we haven't seen a packet.
			seqLo:  0,
			seqHi:  TCPMaxZWPDataLen,
			maxWin: TCPMaxZWPDataLen,
			wscale: -1,
			state:  TCPFirstPacket,
		}
		createTime = time.Now()
		expireTime = createTime.Add(60 * time.Second)
	}

	var lanAddr, gwyAddr, extAddr tcpip.Address
	var lanPort, gwyPort, extPort uint16
	switch dir {
	case Incoming:
		extAddr = srcAddr
		extPort = srcPort
		if isRDR {
			gwyAddr = origAddr
			gwyPort = origPort
		} else {
			gwyAddr = dstAddr
			gwyPort = dstPort
		}
		lanAddr = dstAddr
		lanPort = dstPort
	case Outgoing:
		if isNAT {
			lanAddr = origAddr
			lanPort = origPort
		} else {
			lanAddr = srcAddr
			lanPort = srcPort
		}
		gwyAddr = srcAddr
		gwyPort = srcPort
		extAddr = dstAddr
		extPort = dstPort
	}

	s := &State{
		transProto: transProto,
		dir:        dir,
		lanAddr:    lanAddr,
		lanPort:    lanPort,
		gwyAddr:    gwyAddr,
		gwyPort:    gwyPort,
		extAddr:    extAddr,
		extPort:    extPort,
		rsvdAddr:   rsvdAddr,
		rsvdPort:   rsvdPort,
		srcEp:      srcEp,
		dstEp:      dstEp,
		createTime: createTime,
		expireTime: expireTime,
		packets:    1,
		bytes:      uint32(dataLen),
	}

	kLanToExt := Key{transProto, lanAddr, lanPort, extAddr, extPort}
	kExtToGwy := Key{transProto, extAddr, extPort, gwyAddr, gwyPort}
	ss.lanToExt[kLanToExt] = s
	ss.extToGwy[kExtToGwy] = s

	if debugFilter {
		log.Printf("packet filter: new state: %v", s)
	}

	return s
}

// isICMPv4ErrorMessage returns true if t is ICMPv4 Error Message Type.
// It returns false if t is ICMPv4 Informational Message Type.
func isICMPv4ErrorMessage(t header.ICMPv4Type) bool {
	return t == header.ICMPv4DstUnreachable ||
		t == header.ICMPv4SrcQuench ||
		t == header.ICMPv4Redirect ||
		t == header.ICMPv4TimeExceeded ||
		t == header.ICMPv4ParamProblem
}

func (ss *States) findStateICMPv4(dir Direction, netProto tcpip.NetworkProtocolNumber, transProto tcpip.TransportProtocolNumber, srcAddr tcpip.Address, dstAddr tcpip.Address, payloadLength uint16, th, plb []byte) (*State, error) {
	if len(th) < header.ICMPv4MinimumSize {
		return nil, ErrPacketTooShort
	}
	icmp := header.ICMPv4(th)
	if isICMPv4ErrorMessage(icmp.Type()) {
		// This ICMPv4 packet is reporting an error detected in a transport layer, and
		// includes the IP header and the first 8 bytes of the transport header of the packet
		// that had the error.
		// For NAT and RDR, we have to rewrite the address and port in the packet.
		// We also test if the values in transport header are consistent with the connection
		// state we are tracking (but we have only the first 8 bytes and cannot do a full
		// check).
		if len(th) < header.IPv4MinimumSize {
			return nil, ErrPacketTooShort
		}
		// First, look for an IP header at the offset 8.
		b2 := th[8:]
		ipv4 := header.IPv4(b2)
		transProto2 := ipv4.TransportProtocol()
		srcAddr = ipv4.SourceAddress()
		dstAddr = ipv4.DestinationAddress()
		if len(th) < int(ipv4.HeaderLength()+8) { // Need the first 8 bytes of transport header.
			return nil, ErrPacketTooShort
		}
		// Here's the transport header.
		th2 := b2[ipv4.HeaderLength():]

		switch transProto2 {
		case header.UDPProtocolNumber:
			udp := header.UDP(th2)
			srcPort := udp.SourcePort()
			dstPort := udp.DestinationPort()
			s := ss.getState(dir, transProto2, srcAddr, srcPort, dstAddr, dstPort)
			// There is nothing we can use in the first 8 byte to update the state.
			return s, nil
		case header.TCPProtocolNumber:
			tcp := header.TCP(th2)
			srcPort := tcp.SourcePort()
			dstPort := tcp.DestinationPort()
			s := ss.getState(dir, transProto2, srcAddr, srcPort, dstAddr, dstPort)
			if s == nil {
				return nil, nil
			}
			// We have the first 8 bytes of the TCP header only, which means
			// we just use seq to test the state,
			seq := seqnum(tcp.SequenceNumber())
			err := s.updateStateTCPinICMP(dir, seq)
			if err != nil {
				return nil, err
			}
			return s, nil
		default:
			return nil, ErrUnknownProtocol
		}
	} else {
		dataLen := payloadLength - 8 // ICMP header size is 8.

		var id uint16
		// ICMP query/reply message.
		if len(th) > 4 {
			id = binary.BigEndian.Uint16(th[4:])
		} else if len(th)+len(plb) > 4 {
			id = binary.BigEndian.Uint16(plb[4-len(th):])
		}
		s := ss.getState(dir, transProto, srcAddr, id, dstAddr, id)
		if s == nil {
			return nil, nil
		}
		err := s.updateStateICMPv4(dir, dataLen)
		if err != nil {
			return nil, err
		}
		return s, nil
	}
}

func (ss *States) findStateUDP(dir Direction, netProto tcpip.NetworkProtocolNumber, transProto tcpip.TransportProtocolNumber, srcAddr tcpip.Address, srcPort uint16, dstAddr tcpip.Address, dstPort uint16, payloadLength uint16, th []byte) (*State, error) {
	s := ss.getState(dir, transProto, srcAddr, srcPort, dstAddr, dstPort)
	if s == nil {
		return nil, nil
	}

	dataLen := payloadLength - header.UDPMinimumSize

	err := s.updateStateUDP(dir, dataLen)
	if err != nil {
		return nil, err
	}
	return s, nil
}

func (ss *States) findStateTCP(dir Direction, netProto tcpip.NetworkProtocolNumber, transProto tcpip.TransportProtocolNumber, srcAddr tcpip.Address, srcPort uint16, dstAddr tcpip.Address, dstPort uint16, payloadLength uint16, th []byte) (*State, error) {
	s := ss.getState(dir, transProto, srcAddr, srcPort, dstAddr, dstPort)
	if s == nil {
		return nil, nil
	}
	tcp := header.TCP(th)
	dataLen := payloadLength - uint16(tcp.DataOffset())
	win := tcp.WindowSize()
	seq := seqnum(tcp.SequenceNumber())
	ack := seqnum(tcp.AckNumber())
	flags := tcp.Flags()
	wscale := -1
	if flags&header.TCPFlagSyn != 0 {
		synOpts := header.ParseSynOptions(tcp.Options(), flags&header.TCPFlagAck != 0)
		wscale = synOpts.WS
	}

	err := s.updateStateTCP(dir, dataLen, win, seq, ack, flags, wscale)
	if err != nil {
		return nil, err
	}
	return s, nil
}
