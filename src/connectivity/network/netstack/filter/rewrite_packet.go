// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package filter

import (
	"encoding/binary"

	"gvisor.dev/gvisor/pkg/tcpip"
	"gvisor.dev/gvisor/pkg/tcpip/buffer"
	"gvisor.dev/gvisor/pkg/tcpip/header"
)

// ChangePacket implements incremental checksum calculation.

func newChecksum(oldCksum uint16, oldVal uint16, newVal uint16, udp bool) uint16 {
	if udp && oldCksum == 0 {
		// Checksum is not set.
		return 0
	}
	c := uint32(oldCksum) + uint32(oldVal) - uint32(newVal)
	c = (c >> 16) + (c & 0xffff)
	if udp && c == 0 {
		return 0xffff
	} else {
		return uint16(c)
	}
}

func rewritePacketICMPv4(newAddr tcpip.Address, isSource bool, hdr buffer.View, transportHeader []byte) {
	ipv4 := header.IPv4(hdr)

	var oldAddr tcpip.Address
	if isSource {
		oldAddr = ipv4.SourceAddress()
		ipv4.SetSourceAddress(newAddr)
	} else {
		oldAddr = ipv4.DestinationAddress()
		ipv4.SetDestinationAddress(newAddr)
	}

	iCksum := ipv4.Checksum()
	// icmp checksum is not affected.

	oldA := []byte(oldAddr)
	newA := []byte(newAddr)
	for i := 0; i < header.IPv4AddressSize; i += 2 {
		iCksum = newChecksum(iCksum,
			binary.BigEndian.Uint16(oldA[i:]),
			binary.BigEndian.Uint16(newA[i:]), false)
	}

	ipv4.SetChecksum(iCksum)
}

func rewritePacketUDPv4(newAddr tcpip.Address, newPort uint16, isSource bool, hdr buffer.View, transportHeader []byte) {
	ipv4 := header.IPv4(hdr)

	var oldAddr tcpip.Address
	if isSource {
		oldAddr = ipv4.SourceAddress()
		ipv4.SetSourceAddress(newAddr)
	} else {
		oldAddr = ipv4.DestinationAddress()
		ipv4.SetDestinationAddress(newAddr)
	}

	iCksum := ipv4.Checksum()
	udp := header.UDP(transportHeader)
	tCksum := udp.Checksum()

	oldA := []byte(oldAddr)
	newA := []byte(newAddr)
	for i := 0; i < header.IPv4AddressSize; i += 2 {
		iCksum = newChecksum(iCksum,
			binary.BigEndian.Uint16(oldA[i:]),
			binary.BigEndian.Uint16(newA[i:]), false)
		tCksum = newChecksum(tCksum,
			binary.BigEndian.Uint16(oldA[i:]),
			binary.BigEndian.Uint16(newA[i:]), true) // set true for UDP
	}

	if isSource {
		tCksum = newChecksum(tCksum, udp.SourcePort(), newPort, true)
		udp.SetSourcePort(newPort)
	} else {
		tCksum = newChecksum(tCksum, udp.DestinationPort(), newPort, true)
		udp.SetDestinationPort(newPort)
	}

	ipv4.SetChecksum(iCksum)
	udp.SetChecksum(tCksum)
}

func rewritePacketTCPv4(newAddr tcpip.Address, newPort uint16, isSource bool, hdr buffer.View, transportHeader []byte) {
	ipv4 := header.IPv4(hdr)

	var oldAddr tcpip.Address
	if isSource {
		oldAddr = ipv4.SourceAddress()
		ipv4.SetSourceAddress(newAddr)
	} else {
		oldAddr = ipv4.DestinationAddress()
		ipv4.SetDestinationAddress(newAddr)
	}

	iCksum := ipv4.Checksum()
	tcp := header.TCP(transportHeader)
	tCksum := tcp.Checksum()

	oldA := []byte(oldAddr)
	newA := []byte(newAddr)
	for i := 0; i < header.IPv4AddressSize; i += 2 {
		iCksum = newChecksum(iCksum,
			binary.BigEndian.Uint16(oldA[i:]),
			binary.BigEndian.Uint16(newA[i:]), false)
		tCksum = newChecksum(tCksum,
			binary.BigEndian.Uint16(oldA[i:]),
			binary.BigEndian.Uint16(newA[i:]), false)
	}

	if isSource {
		tCksum = newChecksum(tCksum, tcp.SourcePort(), newPort, false)
		tcp.SetSourcePort(newPort)
	} else {
		tCksum = newChecksum(tCksum, tcp.DestinationPort(), newPort, false)
		tcp.SetDestinationPort(newPort)
	}

	ipv4.SetChecksum(iCksum)
	tcp.SetChecksum(tCksum)
}

func rewritePacketUDPv6(newAddr tcpip.Address, newPort uint16, isSource bool, hdr buffer.View, transportHeader []byte) {
	ipv6 := header.IPv6(hdr)

	var oldAddr tcpip.Address
	if isSource {
		oldAddr = ipv6.SourceAddress()
		ipv6.SetSourceAddress(newAddr)
	} else {
		oldAddr = ipv6.DestinationAddress()
		ipv6.SetDestinationAddress(newAddr)
	}

	udp := header.UDP(transportHeader)
	tCksum := udp.Checksum()

	oldA := []byte(oldAddr)
	newA := []byte(newAddr)
	for i := 0; i < header.IPv6AddressSize; i += 2 {
		tCksum = newChecksum(tCksum,
			binary.BigEndian.Uint16(oldA[i:]),
			binary.BigEndian.Uint16(newA[i:]), true) // set true for UDP
	}

	if isSource {
		tCksum = newChecksum(tCksum, udp.SourcePort(), newPort, true)
		udp.SetSourcePort(newPort)
	} else {
		tCksum = newChecksum(tCksum, udp.DestinationPort(), newPort, true)
		udp.SetDestinationPort(newPort)
	}

	udp.SetChecksum(tCksum)
}

func rewritePacketTCPv6(newAddr tcpip.Address, newPort uint16, isSource bool, hdr buffer.View, transportHeader []byte) {
	ipv6 := header.IPv6(hdr)

	var oldAddr tcpip.Address
	if isSource {
		oldAddr = ipv6.SourceAddress()
		ipv6.SetSourceAddress(newAddr)
	} else {
		oldAddr = ipv6.DestinationAddress()
		ipv6.SetDestinationAddress(newAddr)
	}

	tcp := header.TCP(transportHeader)
	tCksum := tcp.Checksum()

	oldA := []byte(oldAddr)
	newA := []byte(newAddr)
	for i := 0; i < header.IPv6AddressSize; i += 2 {
		tCksum = newChecksum(tCksum,
			binary.BigEndian.Uint16(oldA[i:]),
			binary.BigEndian.Uint16(newA[i:]), false)
	}

	if isSource {
		tCksum = newChecksum(tCksum, tcp.SourcePort(), newPort, false)
		tcp.SetSourcePort(newPort)
	} else {
		tCksum = newChecksum(tCksum, tcp.DestinationPort(), newPort, false)
		tcp.SetDestinationPort(newPort)
	}

	tcp.SetChecksum(tCksum)
}
