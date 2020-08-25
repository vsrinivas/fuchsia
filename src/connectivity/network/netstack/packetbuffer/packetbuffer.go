// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package packetbuffer

import (
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
	return &stack.PacketBuffer{
		Data: buffer.NewVectorisedView(pkt.Size(), pkt.Views()),
	}
}

// Convert pkt to a single View.
func ToView(pkt *stack.PacketBuffer) buffer.View {
	vv := buffer.NewVectorisedView(pkt.Size(), pkt.Views())
	return vv.ToView()
}
