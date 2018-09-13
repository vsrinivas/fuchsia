// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Filter endpoint implements a LinkEndpoint interface, which can wrap another
// LinkEndpoint.

package filter

import (
	"sync/atomic"

	"gvisor.dev/gvisor/pkg/tcpip"
	"gvisor.dev/gvisor/pkg/tcpip/buffer"
	"gvisor.dev/gvisor/pkg/tcpip/stack"
)

var _ stack.LinkEndpoint = (*Endpoint)(nil)
var _ stack.NetworkDispatcher = (*Endpoint)(nil)

type Endpoint struct {
	filter     *Filter
	dispatcher stack.NetworkDispatcher
	enabled    uint32
	stack.LinkEndpoint
}

// New creates a new Filter endpoint by wrapping a lower LinkEndpoint.
func NewEndpoint(filter *Filter, lower stack.LinkEndpoint) *Endpoint {
	return &Endpoint{
		filter:       filter,
		LinkEndpoint: lower,
		enabled:      1,
	}
}

func (e *Endpoint) Enable() {
	atomic.StoreUint32(&e.enabled, 1)
}

func (e *Endpoint) Disable() {
	atomic.StoreUint32(&e.enabled, 0)
}

func (e *Endpoint) IsEnabled() bool {
	return atomic.LoadUint32(&e.enabled) == 1
}

// DeliverNetworkPacket implements stack.NetworkDispatcher.
func (e *Endpoint) DeliverNetworkPacket(linkEP stack.LinkEndpoint, dstLinkAddr, srcLinkAddr tcpip.LinkAddress, protocol tcpip.NetworkProtocolNumber, pkt tcpip.PacketBuffer) {
	if atomic.LoadUint32(&e.enabled) == 1 {
		pkt := pkt
		hdr := pkt.Header
		if hdr.UsedLength() == 0 {
			hdr = buffer.NewPrependableFromView(pkt.Data.First())
			pkt.Data.RemoveFirst()
		}

		if e.filter.Run(Incoming, protocol, hdr, pkt.Data) != Pass {
			return
		}
	}
	e.dispatcher.DeliverNetworkPacket(e, dstLinkAddr, srcLinkAddr, protocol, pkt)
}

// Attach implements stack.LinkEndpoint.
func (e *Endpoint) Attach(dispatcher stack.NetworkDispatcher) {
	e.dispatcher = dispatcher
	e.LinkEndpoint.Attach(e)
}

// WritePacket implements stack.LinkEndpoint.
func (e *Endpoint) WritePacket(r *stack.Route, gso *stack.GSO, protocol tcpip.NetworkProtocolNumber, pkt tcpip.PacketBuffer) *tcpip.Error {
	if atomic.LoadUint32(&e.enabled) == 1 && e.filter.Run(Outgoing, protocol, pkt.Header, pkt.Data) != Pass {
		return nil
	}
	return e.LinkEndpoint.WritePacket(r, gso, protocol, pkt)
}

// WritePackets implements stack.LinkEndpoint.
func (e *Endpoint) WritePackets(r *stack.Route, gso *stack.GSO, pkts []tcpip.PacketBuffer, protocol tcpip.NetworkProtocolNumber) (int, *tcpip.Error) {
	if atomic.LoadUint32(&e.enabled) == 0 {
		return e.LinkEndpoint.WritePackets(r, gso, pkts, protocol)
	}
	filtered := make([]tcpip.PacketBuffer, 0, len(pkts))
	for _, pkt := range pkts {
		if e.filter.Run(Outgoing, protocol, pkt.Header, pkt.Data) != Pass {
			continue
		}
		filtered = append(filtered, pkt)
	}
	return e.LinkEndpoint.WritePackets(r, gso, filtered, protocol)
}
