// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package filter

import (
	"gvisor.dev/gvisor/pkg/tcpip"
	"gvisor.dev/gvisor/pkg/tcpip/buffer"
	"gvisor.dev/gvisor/pkg/tcpip/header"
	"gvisor.dev/gvisor/pkg/tcpip/transport/tcp"
	"gvisor.dev/gvisor/pkg/tcpip/transport/udp"
)

type icmpV4Params struct {
	srcAddr    tcpip.Address
	dstAddr    tcpip.Address
	icmpV4Type header.ICMPv4Type
	code       header.ICMPv4Code
}

func icmpV4Packet(payload []byte, p *icmpV4Params) (buffer.View, buffer.VectorisedView) {
	hdr := buffer.NewPrependable(header.IPv4MinimumSize + header.ICMPv4MinimumSize)

	// Create the ICMP header.
	i := header.ICMPv4(hdr.Prepend(header.ICMPv4MinimumSize))
	i.SetType(p.icmpV4Type)
	i.SetCode(p.code)

	// Calculate the ICMP checksum and set it.
	i.SetChecksum(^header.Checksum(i, header.Checksum(payload, 0)))

	// Create the IPv4 header.
	ip := header.IPv4(hdr.Prepend(header.IPv4MinimumSize))
	ip.Encode(&header.IPv4Fields{
		IHL:         header.IPv4MinimumSize,
		TotalLength: uint16(header.IPv4MinimumSize + header.ICMPv4MinimumSize + len(payload)),
		TTL:         65,
		Protocol:    uint8(header.ICMPv4ProtocolNumber),
		SrcAddr:     p.srcAddr,
		DstAddr:     p.dstAddr,
	})
	ip.SetChecksum(^ip.CalculateChecksum())

	return hdr.View(), buffer.View(payload).ToVectorisedView()
}

type udpParams struct {
	srcAddr       tcpip.Address
	srcPort       uint16
	dstAddr       tcpip.Address
	dstPort       uint16
	noUDPChecksum bool
}

func udpV4Packet(payload []byte, p *udpParams) (buffer.View, buffer.VectorisedView) {
	hdr := buffer.NewPrependable(header.IPv4MinimumSize + header.UDPMinimumSize)

	// Create the UDP header.
	u := header.UDP(hdr.Prepend(header.UDPMinimumSize))
	u.Encode(&header.UDPFields{
		SrcPort: p.srcPort,
		DstPort: p.dstPort,
		Length:  uint16(header.UDPMinimumSize + len(payload)),
	})

	if p.noUDPChecksum {
		u.SetChecksum(0)
	} else {
		length := uint16(header.UDPMinimumSize + len(payload))
		xsum := header.PseudoHeaderChecksum(udp.ProtocolNumber, p.srcAddr, p.dstAddr, length)
		xsum = header.Checksum(payload, xsum)
		u.SetChecksum(^u.CalculateChecksum(xsum))
	}

	// Create the IPv4 header.
	ip := header.IPv4(hdr.Prepend(header.IPv4MinimumSize))
	ip.Encode(&header.IPv4Fields{
		IHL:         header.IPv4MinimumSize,
		TotalLength: uint16(header.IPv4MinimumSize + header.UDPMinimumSize + len(payload)),
		TTL:         65,
		Protocol:    uint8(udp.ProtocolNumber),
		SrcAddr:     p.srcAddr,
		DstAddr:     p.dstAddr,
	})
	ip.SetChecksum(^ip.CalculateChecksum())

	return hdr.View(), buffer.View(payload).ToVectorisedView()
}

func udpV6Packet(payload []byte, p *udpParams) (buffer.View, buffer.VectorisedView) {
	hdr := buffer.NewPrependable(header.IPv6MinimumSize + header.UDPMinimumSize)

	// Create the UDP header.
	u := header.UDP(hdr.Prepend(header.UDPMinimumSize))
	u.Encode(&header.UDPFields{
		SrcPort: p.srcPort,
		DstPort: p.dstPort,
		Length:  uint16(header.UDPMinimumSize + len(payload)),
	})

	if p.noUDPChecksum {
		u.SetChecksum(0)
	} else {
		length := uint16(header.UDPMinimumSize + len(payload))
		xsum := header.PseudoHeaderChecksum(udp.ProtocolNumber, p.srcAddr, p.dstAddr, length)
		xsum = header.Checksum(payload, xsum)
		u.SetChecksum(^u.CalculateChecksum(xsum))
	}

	// Create the IPv6 header.
	ip := header.IPv6(hdr.Prepend(header.IPv6MinimumSize))
	ip.Encode(&header.IPv6Fields{
		PayloadLength: uint16(header.UDPMinimumSize + len(payload)),
		NextHeader:    uint8(header.UDPProtocolNumber),
		HopLimit:      1,
		SrcAddr:       p.srcAddr,
		DstAddr:       p.dstAddr,
	})

	return hdr.View(), buffer.View(payload).ToVectorisedView()
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

func tcpV4Packet(payload []byte, p *tcpParams) (buffer.View, buffer.VectorisedView) {
	hdr := buffer.NewPrependable(header.IPv4MinimumSize + header.TCPMinimumSize + len(p.tcpOpts))

	tcpHdr := hdr.Prepend(header.TCPMinimumSize + len(p.tcpOpts))
	copy(tcpHdr[header.TCPMinimumSize:], p.tcpOpts)

	// Create the TCP header.
	t := header.TCP(tcpHdr)
	t.Encode(&header.TCPFields{
		SrcPort:    p.srcPort,
		DstPort:    p.dstPort,
		SeqNum:     uint32(p.seqNum),
		AckNum:     uint32(p.ackNum),
		DataOffset: uint8(header.TCPMinimumSize + len(p.tcpOpts)),
		Flags:      uint8(p.flags),
		WindowSize: uint16(p.rcvWnd),
	})
	length := uint16(header.TCPMinimumSize + len(p.tcpOpts) + len(payload))
	xsum := header.PseudoHeaderChecksum(tcp.ProtocolNumber, p.srcAddr, p.dstAddr, length)
	xsum = header.Checksum(payload, xsum)
	t.SetChecksum(^t.CalculateChecksum(xsum))

	// Create the IPv4 header.
	ip := header.IPv4(hdr.Prepend(header.IPv4MinimumSize))
	ip.Encode(&header.IPv4Fields{
		IHL:         header.IPv4MinimumSize,
		TotalLength: uint16(header.IPv4MinimumSize + header.TCPMinimumSize + len(payload)),
		TTL:         65,
		Protocol:    uint8(tcp.ProtocolNumber),
		SrcAddr:     p.srcAddr,
		DstAddr:     p.dstAddr,
	})
	ip.SetChecksum(^ip.CalculateChecksum())

	return hdr.View(), buffer.View(payload).ToVectorisedView()
}

func tcpV6Packet(payload []byte, p *tcpParams) (buffer.View, buffer.VectorisedView) {
	hdr := buffer.NewPrependable(header.IPv6MinimumSize + header.TCPMinimumSize + len(p.tcpOpts))

	tcpHdr := hdr.Prepend(header.TCPMinimumSize + len(p.tcpOpts))
	copy(tcpHdr[header.TCPMinimumSize:], p.tcpOpts)

	// Create the TCP header.
	t := header.TCP(tcpHdr)
	t.Encode(&header.TCPFields{
		SrcPort:    p.srcPort,
		DstPort:    p.dstPort,
		DataOffset: uint8(header.TCPMinimumSize + len(p.tcpOpts)),
		Flags:      uint8(p.flags),
	})
	length := uint16(header.TCPMinimumSize + len(p.tcpOpts) + len(payload))
	xsum := header.PseudoHeaderChecksum(tcp.ProtocolNumber, p.srcAddr, p.dstAddr, length)
	xsum = header.Checksum(payload, xsum)
	t.SetChecksum(^t.CalculateChecksum(xsum))

	// Create the IPv6 header.
	ip := header.IPv6(hdr.Prepend(header.IPv6MinimumSize))
	ip.Encode(&header.IPv6Fields{
		PayloadLength: uint16(header.TCPMinimumSize + len(payload)),
		NextHeader:    uint8(header.TCPProtocolNumber),
		HopLimit:      1,
		SrcAddr:       p.srcAddr,
		DstAddr:       p.dstAddr,
	})

	return hdr.View(), buffer.View(payload).ToVectorisedView()
}
