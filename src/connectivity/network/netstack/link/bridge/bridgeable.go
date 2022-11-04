// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//go:build !build_with_native_toolchain

package bridge

import (
	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/sync"

	"gvisor.dev/gvisor/pkg/tcpip"
	"gvisor.dev/gvisor/pkg/tcpip/link/nested"
	"gvisor.dev/gvisor/pkg/tcpip/stack"
)

var _ stack.LinkEndpoint = (*BridgeableEndpoint)(nil)
var _ stack.GSOEndpoint = (*BridgeableEndpoint)(nil)
var _ stack.NetworkDispatcher = (*BridgeableEndpoint)(nil)

type BridgeableEndpoint struct {
	nested.Endpoint
	mu struct {
		sync.RWMutex
		bridge *Endpoint
	}
}

func NewEndpoint(lower stack.LinkEndpoint) *BridgeableEndpoint {
	ep := &BridgeableEndpoint{}
	ep.Endpoint.Init(lower, ep)
	return ep
}

func (e *BridgeableEndpoint) IsBridged() bool {
	e.mu.Lock()
	defer e.mu.Unlock()

	return e.mu.bridge != nil
}

func (e *BridgeableEndpoint) SetBridge(b *Endpoint) {
	e.mu.Lock()
	defer e.mu.Unlock()

	e.mu.bridge = b
}

func (e *BridgeableEndpoint) DeliverNetworkPacket(protocol tcpip.NetworkProtocolNumber, pkt stack.PacketBufferPtr) {
	e.mu.RLock()
	b := e.mu.bridge
	e.mu.RUnlock()

	if b != nil {
		b.DeliverNetworkPacketToBridge(e, protocol, pkt)
		return
	}

	e.Endpoint.DeliverNetworkPacket(protocol, pkt)
}
