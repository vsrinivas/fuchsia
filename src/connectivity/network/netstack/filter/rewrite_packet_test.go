// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package filter

import (
	"testing"

	"gvisor.dev/gvisor/pkg/tcpip"
	"gvisor.dev/gvisor/pkg/tcpip/buffer"
	"gvisor.dev/gvisor/pkg/tcpip/header"
)

func TestRewritePacketICMPv4(t *testing.T) {
	var tests = []struct {
		packet   func() (buffer.View, buffer.VectorisedView)
		newAddr  tcpip.Address
		isSource bool
	}{
		{
			func() (buffer.View, buffer.VectorisedView) {
				return icmpV4Packet([]byte("payload."), &icmpV4Params{
					srcAddr:    "\x0a\x00\x00\x00",
					dstAddr:    "\x0a\x00\x00\x02",
					icmpV4Type: header.ICMPv4EchoReply,
					code:       0,
				})
			},
			"\x0b\x00\x00\x00",
			true,
		},
	}

	for _, test := range tests {
		hdr, payload := test.packet()
		ipv4 := header.IPv4(hdr)
		transportHeader := ipv4[ipv4.HeaderLength():]
		icmpv4 := header.ICMPv4(transportHeader)

		// Make sure the checksum in the original packet is correct.
		iCksum := ipv4.CalculateChecksum()
		if got, want := iCksum, uint16(0xffff); got != want {
			t.Errorf("ipv4 checksum=%x, want=%x", got, want)
		}

		if got, want := header.Checksum(icmpv4, header.ChecksumVV(payload, 0)), uint16(0xffff); got != want {
			t.Errorf("icmpv4 checksum=%x, want=%x", got, want)
		}

		rewritePacketICMPv4(test.newAddr, test.isSource, hdr, transportHeader)

		if test.isSource {
			if got, want := ipv4.SourceAddress(), test.newAddr; got != want {
				t.Errorf("ipv4.SourceAddress()=%v, want=%v", got, want)
			}
		} else {
			if got, want := ipv4.DestinationAddress(), test.newAddr; got != want {
				t.Errorf("ipv4.DestinationAddress()=%v, want=%v", got, want)
			}
		}

		// Check if the checksum in the rewritten packet is correct.
		iCksum = ipv4.CalculateChecksum()
		if got, want := iCksum, uint16(0xffff); got != want {
			t.Errorf("ipv4 checksum=%x, want=%x", got, want)
		}

		if got, want := header.Checksum(icmpv4, header.ChecksumVV(payload, 0)), uint16(0xffff); got != want {
			t.Errorf("icmpv4 checksum=%x, want=%x", got, want)
		}
	}
}

func TestRewritePacketUDPv4(t *testing.T) {
	var tests = []struct {
		packet   func() (buffer.View, buffer.VectorisedView)
		newAddr  tcpip.Address
		newPort  uint16
		isSource bool
	}{
		{
			func() (buffer.View, buffer.VectorisedView) {
				return udpV4Packet([]byte("payload"), &udpParams{
					srcAddr: "\x0a\x00\x00\x00",
					srcPort: 100,
					dstAddr: "\x0a\x00\x00\x02",
					dstPort: 200,
				})
			},
			"\x0b\x00\x00\x00",
			101,
			true,
		},
		{
			func() (buffer.View, buffer.VectorisedView) {
				return udpV4Packet([]byte("payload"), &udpParams{
					srcAddr:       "\x0a\x00\x00\x00",
					srcPort:       100,
					dstAddr:       "\x0a\x00\x00\x02",
					dstPort:       200,
					noUDPChecksum: true,
				})
			},
			"\x0b\x00\x00\x00",
			101,
			false,
		},
	}

	for _, test := range tests {
		hdr, payload := test.packet()
		ipv4 := header.IPv4(hdr)
		transportHeader := ipv4[ipv4.HeaderLength():]
		udp := header.UDP(transportHeader)

		// Make sure the checksum in the original packet is correct.
		iCksum := ipv4.CalculateChecksum()
		if got, want := iCksum, uint16(0xffff); got != want {
			t.Errorf("ipv4 checksum=%x, want=%x", got, want)
		}

		noUDPChecksum := false
		if udp.Checksum() == 0 {
			noUDPChecksum = true
		} else {
			length := uint16(len(transportHeader) + payload.Size())
			tCksum := header.PseudoHeaderChecksum(header.UDPProtocolNumber, ipv4.SourceAddress(), ipv4.DestinationAddress(), length)
			tCksum = header.ChecksumVV(payload, tCksum)
			tCksum = udp.CalculateChecksum(tCksum)
			if got, want := tCksum, uint16(0xffff); got != want {
				t.Errorf("udp checksum=%x, want=%x", got, want)
			}
		}

		rewritePacketUDPv4(test.newAddr, test.newPort, test.isSource, hdr, transportHeader)

		if test.isSource {
			if got, want := ipv4.SourceAddress(), test.newAddr; got != want {
				t.Errorf("ipv4.SourceAddress()=%v, want=%v", got, want)
			}
			if got, want := udp.SourcePort(), test.newPort; got != want {
				t.Errorf("ipv4.SourcePort()=%v, want=%v", got, want)
			}
		} else {
			if got, want := ipv4.DestinationAddress(), test.newAddr; got != want {
				t.Errorf("ipv4.DestinationAddress()=%v, want=%v", got, want)
			}
			if got, want := udp.DestinationPort(), test.newPort; got != want {
				t.Errorf("ipv4.DestinationPort()=%v, want=%v", got, want)
			}
		}

		// Check if the checksum in the rewritten packet is correct.
		iCksum = ipv4.CalculateChecksum()
		if got, want := iCksum, uint16(0xffff); got != want {
			t.Errorf("ipv4 checksum=%x, want=%x", got, want)
		}

		if noUDPChecksum {
			if got, want := udp.Checksum(), uint16(0); got != want {
				t.Errorf("udp checksum=%x, want=%x", got, want)
			}
		} else {
			length := uint16(len(transportHeader) + payload.Size())
			tCksum := header.PseudoHeaderChecksum(header.UDPProtocolNumber, ipv4.SourceAddress(), ipv4.DestinationAddress(), length)
			tCksum = header.ChecksumVV(payload, tCksum)
			tCksum = udp.CalculateChecksum(tCksum)
			if got, want := tCksum, uint16(0xffff); got != want {
				t.Errorf("udp checksum=%x, want=%x", got, want)
			}
		}
	}
}

func TestRewritePacketTCPv4(t *testing.T) {
	var tests = []struct {
		packet   func() (buffer.View, buffer.VectorisedView)
		newAddr  tcpip.Address
		newPort  uint16
		isSource bool
	}{
		{
			func() (buffer.View, buffer.VectorisedView) {
				return tcpV4Packet([]byte("payload"), &tcpParams{
					srcAddr: "\x0a\x00\x00\x00",
					srcPort: 100,
					dstAddr: "\x0a\x00\x00\x02",
					dstPort: 200,
				})
			},
			"\x0b\x00\x00\x00",
			101,
			true,
		},
		{
			func() (buffer.View, buffer.VectorisedView) {
				return tcpV4Packet([]byte("payload"), &tcpParams{
					srcAddr: "\x0a\x00\x00\x00",
					srcPort: 100,
					dstAddr: "\x0a\x00\x00\x02",
					dstPort: 200,
				})
			},
			"\x0b\x00\x00\x00",
			101,
			false,
		},
	}

	for _, test := range tests {
		hdr, payload := test.packet()
		ipv4 := header.IPv4(hdr)
		transportHeader := ipv4[ipv4.HeaderLength():]
		tcp := header.TCP(transportHeader)

		// Make sure the checksum in the original packet is correct.
		iCksum := ipv4.CalculateChecksum()
		if got, want := iCksum, uint16(0xffff); got != want {
			t.Errorf("ipv4 checksum=%x, want=%x", got, want)
		}
		length := uint16(len(transportHeader) + payload.Size())
		tCksum := header.PseudoHeaderChecksum(header.TCPProtocolNumber, ipv4.SourceAddress(), ipv4.DestinationAddress(), length)
		tCksum = header.ChecksumVV(payload, tCksum)
		tCksum = tcp.CalculateChecksum(tCksum)
		if got, want := tCksum, uint16(0xffff); got != want {
			t.Errorf("tcp checksum=%x, want=%x", got, want)
		}

		rewritePacketTCPv4(test.newAddr, test.newPort, test.isSource, hdr, transportHeader)

		if test.isSource {
			if got, want := ipv4.SourceAddress(), test.newAddr; got != want {
				t.Errorf("ipv4.SourceAddress()=%v, want=%v", got, want)
			}
			if got, want := tcp.SourcePort(), test.newPort; got != want {
				t.Errorf("ipv4.SourcePort()=%v, want=%v", got, want)
			}
		} else {
			if got, want := ipv4.DestinationAddress(), test.newAddr; got != want {
				t.Errorf("ipv4.DestinationAddress()=%v, want=%v", got, want)
			}
			if got, want := tcp.DestinationPort(), test.newPort; got != want {
				t.Errorf("ipv4.DestinationPort()=%v, want=%v", got, want)
			}
		}

		// Check if the checksum in the rewritten packet is correct.
		iCksum = ipv4.CalculateChecksum()
		if got, want := iCksum, uint16(0xffff); got != want {
			t.Errorf("ipv4 checksum=%x, want=%x", got, want)
		}
		length = uint16(len(transportHeader) + payload.Size())
		tCksum = header.PseudoHeaderChecksum(header.TCPProtocolNumber, ipv4.SourceAddress(), ipv4.DestinationAddress(), length)
		tCksum = header.ChecksumVV(payload, tCksum)
		tCksum = tcp.CalculateChecksum(tCksum)
		if got, want := tCksum, uint16(0xffff); got != want {
			t.Errorf("tcp checksum=%x, want=%x", got, want)
		}
	}
}

func TestRewritePacketUDPv6(t *testing.T) {
	var tests = []struct {
		packet   func() (buffer.View, buffer.VectorisedView)
		newAddr  tcpip.Address
		newPort  uint16
		isSource bool
	}{
		{
			func() (buffer.View, buffer.VectorisedView) {
				return udpV6Packet([]byte("payload"), &udpParams{
					srcAddr: "\x0a\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01",
					srcPort: 100,
					dstAddr: "\x0a\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02",
					dstPort: 200,
				})
			},
			"\x0b\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01",
			101,
			true,
		},
	}

	for _, test := range tests {
		hdr, payload := test.packet()
		ipv6 := header.IPv6(hdr)
		transportHeader := ipv6[header.IPv6MinimumSize:]
		udp := header.UDP(transportHeader)

		// Make sure the checksum in the original packet is correct.
		noUDPChecksum := false
		if udp.Checksum() == 0 {
			noUDPChecksum = true
		} else {
			length := uint16(len(transportHeader) + payload.Size())
			tCksum := header.PseudoHeaderChecksum(header.UDPProtocolNumber, ipv6.SourceAddress(), ipv6.DestinationAddress(), length)
			tCksum = header.ChecksumVV(payload, tCksum)
			tCksum = udp.CalculateChecksum(tCksum)
			if got, want := tCksum, uint16(0xffff); got != want {
				t.Errorf("udp checksum=%x, want=%x", got, want)
			}
		}

		rewritePacketUDPv6(test.newAddr, test.newPort, test.isSource, hdr, transportHeader)

		if test.isSource {
			if got, want := ipv6.SourceAddress(), test.newAddr; got != want {
				t.Errorf("ipv6.SourceAddress()=%v, want=%v", got, want)
			}
			if got, want := udp.SourcePort(), test.newPort; got != want {
				t.Errorf("ipv6.SourcePort()=%v, want=%v", got, want)
			}
		} else {
			if got, want := ipv6.DestinationAddress(), test.newAddr; got != want {
				t.Errorf("ipv6.DestinationAddress()=%v, want=%v", got, want)
			}
			if got, want := udp.DestinationPort(), test.newPort; got != want {
				t.Errorf("ipv6.DestinationPort()=%v, want=%v", got, want)
			}
		}

		// Check if the checksum in the rewritten packet is correct.
		if noUDPChecksum {
			if got, want := udp.Checksum(), uint16(0); got != want {
				t.Errorf("udp checksum=%x, want=%x", got, want)
			}
		} else {
			length := uint16(len(transportHeader) + payload.Size())
			tCksum := header.PseudoHeaderChecksum(header.UDPProtocolNumber, ipv6.SourceAddress(), ipv6.DestinationAddress(), length)
			tCksum = header.ChecksumVV(payload, tCksum)
			tCksum = udp.CalculateChecksum(tCksum)
			if got, want := tCksum, uint16(0xffff); got != want {
				t.Errorf("udp checksum=%x, want=%x", got, want)
			}
		}
	}
}

func TestRewritePacketTCPv6(t *testing.T) {
	var tests = []struct {
		packet   func() (buffer.View, buffer.VectorisedView)
		newAddr  tcpip.Address
		newPort  uint16
		isSource bool
	}{
		{
			func() (buffer.View, buffer.VectorisedView) {
				return tcpV6Packet([]byte("payload"), &tcpParams{
					srcAddr: "\x0a\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01",
					srcPort: 100,
					dstAddr: "\x0a\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02",
					dstPort: 200,
				})
			},
			"\x0b\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01",
			101,
			true,
		},
	}

	for _, test := range tests {
		hdr, payload := test.packet()
		ipv6 := header.IPv6(hdr)
		transportHeader := ipv6[header.IPv6MinimumSize:]
		tcp := header.TCP(transportHeader)

		// Make sure the checksum in the original packet is correct.
		length := uint16(len(transportHeader) + payload.Size())
		tCksum := header.PseudoHeaderChecksum(header.TCPProtocolNumber, ipv6.SourceAddress(), ipv6.DestinationAddress(), length)
		tCksum = header.ChecksumVV(payload, tCksum)
		tCksum = tcp.CalculateChecksum(tCksum)
		if got, want := tCksum, uint16(0xffff); got != want {
			t.Errorf("tcp checksum=%x, want=%x", got, want)
		}

		rewritePacketTCPv6(test.newAddr, test.newPort, test.isSource, hdr, transportHeader)

		if test.isSource {
			if got, want := ipv6.SourceAddress(), test.newAddr; got != want {
				t.Errorf("ipv6.SourceAddress()=%v, want=%v", got, want)
			}
			if got, want := tcp.SourcePort(), test.newPort; got != want {
				t.Errorf("ipv6.SourcePort()=%v, want=%v", got, want)
			}
		} else {
			if got, want := ipv6.DestinationAddress(), test.newAddr; got != want {
				t.Errorf("ipv6.DestinationAddress()=%v, want=%v", got, want)
			}
			if got, want := tcp.DestinationPort(), test.newPort; got != want {
				t.Errorf("ipv6.DestinationPort()=%v, want=%v", got, want)
			}
		}

		// Check if the checksum in the rewritten packet is correct.
		length = uint16(len(transportHeader) + payload.Size())
		tCksum = header.PseudoHeaderChecksum(header.TCPProtocolNumber, ipv6.SourceAddress(), ipv6.DestinationAddress(), length)
		tCksum = header.ChecksumVV(payload, tCksum)
		tCksum = tcp.CalculateChecksum(tCksum)
		if got, want := tCksum, uint16(0xffff); got != want {
			t.Errorf("tcp checksum=%x, want=%x", got, want)
		}
	}
}
