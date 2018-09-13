// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package packetbuffer

import (
	"gvisor.dev/gvisor/pkg/tcpip"
	"gvisor.dev/gvisor/pkg/tcpip/buffer"
)

// OutboundToInbound coverts a PacketBuffer that conforms to the
// contract of WritePacket to one that conforms to the contract of
// DeliverNetworkPacket/InjectInbound/etc.
//
// The contract of WritePacket differs from that of DeliverNetworkPacket.
// WritePacket treats Header and Data as disjoint buffers; DeliverNetworkPacket
// expects Data to contain the full packet, including any Header bytes if they
// are present.
func OutboundToInbound(pkt tcpip.PacketBuffer) tcpip.PacketBuffer {
	if hdr := pkt.Header.View(); len(hdr) > 0 {
		pkt.Data = buffer.NewVectorisedView(len(hdr)+pkt.Data.Size(), append([]buffer.View{hdr}, pkt.Data.Views()...))
		pkt.Header = buffer.Prependable{}
	}
	return pkt
}
