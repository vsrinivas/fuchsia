// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package packetbuffer

import (
	"fmt"

	"gvisor.dev/gvisor/pkg/tcpip/buffer"
	"gvisor.dev/gvisor/pkg/tcpip/stack"
)

// OutboundToInbound coverts a PacketBuffer that conforms to the
// contract of WritePacket to one that conforms to the contract of
// DeliverNetworkPacket/InjectInbound/etc.
//
// The contract of WritePacket differs from that of DeliverNetworkPacket.
// WritePacket treats Header and Data as disjoint buffers; DeliverNetworkPacket
// expects Data to contain the full packet, including any Header bytes if they
// are present.
func OutboundToInbound(pkt *stack.PacketBuffer) *stack.PacketBuffer {
	views := make([]buffer.View, 1, 1+len(pkt.Data.Views()))
	views[0] = pkt.Header.View()
	views = append(views, pkt.Data.Views()...)

	return &stack.PacketBuffer{
		Data: buffer.NewVectorisedView(len(views[0])+pkt.Data.Size(), views),
	}
}

// EnsurePopulatedHeader smashes pkt.Data into pkt.Header, leaving
// maxLinkHeaderLen space in reserve.
func EnsurePopulatedHeader(pkt *stack.PacketBuffer, maxLinkHeaderLen uint16) {
	if pkt.Data.Size() == 0 {
		return
	}
	hdr := pkt.Header
	pkt.Header = buffer.NewPrependable(hdr.UsedLength() + pkt.Data.Size() + int(maxLinkHeaderLen))
	for i := len(pkt.Data.Views()); i != 0; i-- {
		view := pkt.Data.Views()[i-1]
		if n := copy(pkt.Header.Prepend(len(view)), view); n != len(view) {
			panic(fmt.Sprintf("copied %d bytes, expected %d bytes", n, len(view)))
		}
	}
	if n := copy(pkt.Header.Prepend(hdr.UsedLength()), hdr.View()); n != hdr.UsedLength() {
		panic(fmt.Sprintf("copied %d bytes, expected %d bytes", n, hdr.UsedLength()))
	}
	pkt.Data = buffer.VectorisedView{}
}
