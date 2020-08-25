// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package utils

import (
	"gvisor.dev/gvisor/pkg/tcpip"
	"gvisor.dev/gvisor/pkg/tcpip/header"
	"gvisor.dev/gvisor/pkg/tcpip/stack"
)

// AddEthernetHeader adds the Ethernet link header to pkt.
//
// If local is unspecified, nicLinkAddr will be used as the local link address.
func AddEthernetHeader(nicLinkAddr, local, remote tcpip.LinkAddress, protocol tcpip.NetworkProtocolNumber, pkt *stack.PacketBuffer) {
	fields := MakeEthernetFields(nicLinkAddr, local, remote, protocol)
	AddEthernetHeaderWithFields(&fields, pkt)
}

// AddEthernetHeaderWithFields adds the Ethernet link header to pkt.
func AddEthernetHeaderWithFields(fields *header.EthernetFields, pkt *stack.PacketBuffer) {
	h := pkt.LinkHeader().Push(header.EthernetMinimumSize)
	header.Ethernet(h).Encode(fields)
}

// MakeEthernetFields returns a new Ethernet header's fields with the specified
// values.
func MakeEthernetFields(nicLinkAddr, local, remote tcpip.LinkAddress, protocol tcpip.NetworkProtocolNumber) header.EthernetFields {
	hdr := header.EthernetFields{
		SrcAddr: local,
		DstAddr: remote,
		Type:    protocol,
	}
	if len(hdr.SrcAddr) == 0 {
		hdr.SrcAddr = nicLinkAddr
	}
	return hdr
}
