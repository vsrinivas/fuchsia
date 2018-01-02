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

type tcpParams struct {
	srcPort uint16
	dstPort uint16
	flags   int
	tcpOpts []byte
}

// tcpV4Packet generates a TCP packet with IPv4 header for tests.
func tcpV4Packet(payload []byte, p *tcpParams) (tcpip.NetworkProtocolNumber, []byte, buffer.View) {
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
		SrcAddr:     srcAddr,
		DstAddr:     dstAddr,
	})
	ip.SetChecksum(^ip.CalculateChecksum())

	// Create the TCP header.
	t := header.TCP(buf[header.IPv4MinimumSize:])
	t.Encode(&header.TCPFields{
		SrcPort: p.srcPort,
		DstPort: p.dstPort,
		Flags:   uint8(p.flags),
	})

	// Calculate the TCP pseudo-header checksum.
	xsum := header.Checksum([]byte(srcAddr), 0)
	xsum = header.Checksum([]byte(dstAddr), xsum)
	xsum = header.Checksum([]byte{0, uint8(tcp.ProtocolNumber)}, xsum)

	// Calculate the TCP checksum and set it.
	length := uint16(header.TCPMinimumSize + len(p.tcpOpts) + len(payload))
	xsum = header.Checksum(payload, xsum)
	t.SetChecksum(^t.CalculateChecksum(xsum, length))

	return header.IPv4ProtocolNumber, buf, payload
}

type udpParams struct {
	srcPort uint16
	dstPort uint16
}

// udpV4Packet generates an UDP packet with IPv4 header for tests.
func udpV4Packet(payload []byte, p *udpParams) (tcpip.NetworkProtocolNumber, []byte, buffer.View) {
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
		SrcAddr:     srcAddr,
		DstAddr:     dstAddr,
	})
	ip.SetChecksum(^ip.CalculateChecksum())

	// Create the UDP header.
	u := header.UDP(buf[header.IPv4MinimumSize:])
	u.Encode(&header.UDPFields{
		SrcPort: p.srcPort,
		DstPort: p.dstPort,
		Length:  uint16(header.UDPMinimumSize + len(payload)),
	})

	// Calculate the UDP pseudo-header checksum.
	xsum := header.Checksum([]byte(srcAddr), 0)
	xsum = header.Checksum([]byte(dstAddr), xsum)
	xsum = header.Checksum([]byte{0, uint8(udp.ProtocolNumber)}, xsum)

	// Calculate the UDP checksum and set it.
	length := uint16(header.UDPMinimumSize + len(payload))
	xsum = header.Checksum(payload, xsum)
	u.SetChecksum(^u.CalculateChecksum(xsum, length))

	return header.IPv4ProtocolNumber, buf, payload
}
