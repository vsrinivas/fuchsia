// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package testutil

import (
	"github.com/google/go-cmp/cmp"
	"gvisor.dev/gvisor/pkg/tcpip/stack"
)

// The headers and data from a stack.PacketBuffer.
type PacketBufferParts struct {
	LinkHeader      []byte
	NetworkHeader   []byte
	TransportHeader []byte
	Data            []byte
}

// PacketBufferCmpTransformer transforms a stack.PacketBuffer to a
// PacketBufferParts which represents the PacketBuffer for the purposes of our
// tests.
var PacketBufferCmpTransformer = cmp.Transformer("packetBufferToParts", func(pkt stack.PacketBufferPtr) PacketBufferParts {
	return PacketBufferParts{
		LinkHeader:      pkt.LinkHeader().Slice(),
		NetworkHeader:   pkt.NetworkHeader().Slice(),
		TransportHeader: pkt.TransportHeader().Slice(),
		Data:            pkt.Data().AsRange().ToSlice(),
	}
})
