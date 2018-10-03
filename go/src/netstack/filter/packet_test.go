// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package filter

import (
	"github.com/google/netstack/tcpip"
	"github.com/google/netstack/tcpip/buffer"
	"github.com/google/netstack/tcpip/header"
	"github.com/google/netstack/tcpip/transport/tcp"
	"github.com/google/netstack/tcpip/transport/udp"
)

type icmpV4Params struct {
	srcAddr    tcpip.Address
	dstAddr    tcpip.Address
	icmpV4Type header.ICMPv4Type
	code       byte
}

// icmpV4Packet generates an ICMP packet with IPv4 header for tests.
func icmpV4Packet(payload []byte, p *icmpV4Params) []byte {
	// Allocate a buffer for data and headers.
	buf := buffer.NewView(header.ICMPv4MinimumSize + header.IPv4MinimumSize + len(payload))
	copy(buf[len(buf)-len(payload):], payload)

	// Create the IPv4 header.
	ip := header.IPv4(buf)
	ip.Encode(&header.IPv4Fields{
		IHL:         header.IPv4MinimumSize,
		TotalLength: uint16(len(buf)),
		TTL:         65,
		Protocol:    uint8(header.ICMPv4ProtocolNumber),
		SrcAddr:     p.srcAddr,
		DstAddr:     p.dstAddr,
	})
	ip.SetChecksum(^ip.CalculateChecksum())

	// Create the ICMP header.
	i := header.ICMPv4(buf[header.IPv4MinimumSize : header.IPv4MinimumSize+header.ICMPv4MinimumSize])
	i.SetType(p.icmpV4Type)
	i.SetCode(p.code)

	// Calculate the ICMP checksum and set it.
	i.SetChecksum(^header.Checksum(i, header.Checksum(payload, 0)))

	return buf
}

type udpParams struct {
	srcAddr       tcpip.Address
	srcPort       uint16
	dstAddr       tcpip.Address
	dstPort       uint16
	noUDPChecksum bool
}

// udpV4Packet generates an UDP packet with IPv4 header for tests.
func udpV4Packet(payload []byte, p *udpParams) []byte {
	// Allocate a buffer for data and headers.
	buf := buffer.NewView(header.UDPMinimumSize + header.IPv4MinimumSize + len(payload))
	copy(buf[len(buf)-len(payload):], payload)

	// Create the IPv4 header.
	ip := header.IPv4(buf)
	ip.Encode(&header.IPv4Fields{
		IHL:         header.IPv4MinimumSize,
		TotalLength: uint16(len(buf)),
		TTL:         65,
		Protocol:    uint8(udp.ProtocolNumber),
		SrcAddr:     p.srcAddr,
		DstAddr:     p.dstAddr,
	})
	ip.SetChecksum(^ip.CalculateChecksum())

	// Create the UDP header.
	u := header.UDP(buf[header.IPv4MinimumSize:])
	u.Encode(&header.UDPFields{
		SrcPort: p.srcPort,
		DstPort: p.dstPort,
		Length:  uint16(header.UDPMinimumSize + len(payload)),
	})

	if p.noUDPChecksum {
		u.SetChecksum(0)
	} else {
		// Calculate the UDP pseudo-header checksum.
		xsum := header.Checksum([]byte(p.srcAddr), 0)
		xsum = header.Checksum([]byte(p.dstAddr), xsum)
		xsum = header.Checksum([]byte{0, uint8(udp.ProtocolNumber)}, xsum)

		// Calculate the UDP checksum and set it.
		length := uint16(header.UDPMinimumSize + len(payload))
		xsum = header.Checksum(payload, xsum)
		u.SetChecksum(^u.CalculateChecksum(xsum, length))
	}

	return buf
}

type tcpParams struct {
	srcAddr tcpip.Address
	srcPort uint16
	dstAddr tcpip.Address
	dstPort uint16
	seqNum  uint32
	ackNum  uint32
	flags   int
	tcpOpts []byte
	rcvWnd  uint16
}

// tcpV4Packet generates a TCP packet with IPv4 header for tests.
func tcpV4Packet(payload []byte, p *tcpParams) []byte {
	buf := buffer.NewView(header.TCPMinimumSize + header.IPv4MinimumSize + len(p.tcpOpts) + len(payload))
	copy(buf[len(buf)-len(payload):], payload)
	copy(buf[len(buf)-len(payload)-len(p.tcpOpts):], p.tcpOpts)

	// Create the IPv4 header.
	ip := header.IPv4(buf)
	ip.Encode(&header.IPv4Fields{
		IHL:         header.IPv4MinimumSize,
		TotalLength: uint16(len(buf)),
		TTL:         65,
		Protocol:    uint8(tcp.ProtocolNumber),
		SrcAddr:     p.srcAddr,
		DstAddr:     p.dstAddr,
	})
	ip.SetChecksum(^ip.CalculateChecksum())

	// Create the TCP header.
	t := header.TCP(buf[header.IPv4MinimumSize:])
	t.Encode(&header.TCPFields{
		SrcPort:    p.srcPort,
		DstPort:    p.dstPort,
		SeqNum:     uint32(p.seqNum),
		AckNum:     uint32(p.ackNum),
		DataOffset: uint8(header.TCPMinimumSize + len(p.tcpOpts)),
		Flags:      uint8(p.flags),
		WindowSize: uint16(p.rcvWnd),
	})

	// Calculate the TCP pseudo-header checksum.
	xsum := header.Checksum([]byte(p.srcAddr), 0)
	xsum = header.Checksum([]byte(p.dstAddr), xsum)
	xsum = header.Checksum([]byte{0, uint8(tcp.ProtocolNumber)}, xsum)

	// Calculate the TCP checksum and set it.
	length := uint16(header.TCPMinimumSize + len(p.tcpOpts) + len(payload))
	xsum = header.Checksum(payload, xsum)
	t.SetChecksum(^t.CalculateChecksum(xsum, length))

	return buf
}

func udpV6Packet(payload []byte, p *udpParams) []byte {
	// Allocate a buffer for data and headers.
	buf := buffer.NewView(header.UDPMinimumSize + header.IPv6MinimumSize + len(payload))
	copy(buf[len(buf)-len(payload):], payload)

	// Create the IPv4 header.
	ip := header.IPv6(buf)
	ip.Encode(&header.IPv6Fields{
		PayloadLength: uint16(header.UDPMinimumSize + len(payload)),
		NextHeader:    uint8(header.UDPProtocolNumber),
		HopLimit:      1,
		SrcAddr:       p.srcAddr,
		DstAddr:       p.dstAddr,
	})

	// Create the UDP header.
	u := header.UDP(buf[header.IPv6MinimumSize:])
	u.Encode(&header.UDPFields{
		SrcPort: p.srcPort,
		DstPort: p.dstPort,
		Length:  uint16(header.UDPMinimumSize + len(payload)),
	})

	if p.noUDPChecksum {
		u.SetChecksum(0)
	} else {
		// Calculate the UDP pseudo-header checksum.
		xsum := header.Checksum([]byte(p.srcAddr), 0)
		xsum = header.Checksum([]byte(p.dstAddr), xsum)
		xsum = header.Checksum([]byte{0, uint8(udp.ProtocolNumber)}, xsum)

		// Calculate the UDP checksum and set it.
		length := uint16(header.UDPMinimumSize + len(payload))
		xsum = header.Checksum(payload, xsum)
		u.SetChecksum(^u.CalculateChecksum(xsum, length))
	}

	return buf
}

func tcpV6Packet(payload []byte, p *tcpParams) []byte {
	// Allocate a buffer for data and headers.
	buf := buffer.NewView(header.TCPMinimumSize + header.IPv6MinimumSize + len(p.tcpOpts) + len(payload))
	copy(buf[len(buf)-len(payload):], payload)
	copy(buf[len(buf)-len(payload)-len(p.tcpOpts):], p.tcpOpts)

	// Create the IPv4 header.
	ip := header.IPv6(buf)
	ip.Encode(&header.IPv6Fields{
		PayloadLength: uint16(header.TCPMinimumSize + len(payload)),
		NextHeader:    uint8(header.TCPProtocolNumber),
		HopLimit:      1,
		SrcAddr:       p.srcAddr,
		DstAddr:       p.dstAddr,
	})

	// Create the TCP header.
	t := header.TCP(buf[header.IPv6MinimumSize:])
	t.Encode(&header.TCPFields{
		SrcPort:    p.srcPort,
		DstPort:    p.dstPort,
		DataOffset: uint8(header.TCPMinimumSize + len(p.tcpOpts)),
		Flags:      uint8(p.flags),
	})

	// Calculate the TCP pseudo-header checksum.
	xsum := header.Checksum([]byte(p.srcAddr), 0)
	xsum = header.Checksum([]byte(p.dstAddr), xsum)
	xsum = header.Checksum([]byte{0, uint8(tcp.ProtocolNumber)}, xsum)

	// Calculate the TCP checksum and set it.
	length := uint16(header.TCPMinimumSize + len(p.tcpOpts) + len(payload))
	xsum = header.Checksum(payload, xsum)
	t.SetChecksum(^t.CalculateChecksum(xsum, length))

	return buf
}
