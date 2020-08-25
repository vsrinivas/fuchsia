// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Filter endpoint implements a LinkEndpoint interface, which can wrap another
// LinkEndpoint.

package filter

import (
	"sync/atomic"

	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/packetbuffer"

	"gvisor.dev/gvisor/pkg/tcpip"
	"gvisor.dev/gvisor/pkg/tcpip/buffer"
	"gvisor.dev/gvisor/pkg/tcpip/link/nested"
	"gvisor.dev/gvisor/pkg/tcpip/stack"
)

var _ stack.LinkEndpoint = (*Endpoint)(nil)
var _ stack.GSOEndpoint = (*Endpoint)(nil)
var _ stack.NetworkDispatcher = (*Endpoint)(nil)

type Endpoint struct {
	filter  *Filter
	enabled uint32
	nested.Endpoint
}

// New creates a new Filter endpoint by wrapping a lower LinkEndpoint.
func NewEndpoint(filter *Filter, lower stack.LinkEndpoint) *Endpoint {
	ep := &Endpoint{
		filter:  filter,
		enabled: 1,
	}
	ep.Endpoint.Init(lower, ep)
	return ep
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
func (e *Endpoint) DeliverNetworkPacket(dstLinkAddr, srcLinkAddr tcpip.LinkAddress, protocol tcpip.NetworkProtocolNumber, pkt *stack.PacketBuffer) {
	if atomic.LoadUint32(&e.enabled) == 0 {
		e.Endpoint.DeliverNetworkPacket(dstLinkAddr, srcLinkAddr, protocol, pkt)
		return
	}

	// The filter expects the packet's header to be in the packet buffer's header.
	//
	// Since we are delivering the packet to a NetworkDispatcher, we do not need
	// to allocate bytes for a LinkEndpoint's header.
	//
	// TODO(50424): Support using a buffer.VectorisedView when parsing packets
	// so we don't need to create a single view here.
	hdr := packetbuffer.ToView(pkt)
	if e.filter.Run(Incoming, protocol, hdr, buffer.VectorisedView{}) != Pass {
		return
	}

	e.Endpoint.DeliverNetworkPacket(dstLinkAddr, srcLinkAddr, protocol, stack.NewPacketBuffer(stack.PacketBufferOptions{
		Data: hdr.ToVectorisedView(),
	}))
}

// WritePacket implements stack.LinkEndpoint.
func (e *Endpoint) WritePacket(r *stack.Route, gso *stack.GSO, protocol tcpip.NetworkProtocolNumber, pkt *stack.PacketBuffer) *tcpip.Error {
	if atomic.LoadUint32(&e.enabled) == 0 {
		return e.Endpoint.WritePacket(r, gso, protocol, pkt)
	}

	// The filter expects the packet's header to be in the packet buffer's
	// header.
	//
	// TODO(50424): Support using a buffer.VectorisedView when parsing packets
	// so we don't need to create a single view here.
	hdr := packetbuffer.ToView(pkt)
	if e.filter.Run(Outgoing, protocol, hdr, buffer.VectorisedView{}) == Pass {
		return e.Endpoint.WritePacket(r, gso, protocol, stack.NewPacketBuffer(stack.PacketBufferOptions{
			ReserveHeaderBytes: int(e.MaxHeaderLength()),
			Data:               hdr.ToVectorisedView(),
		}))
	}

	return nil
}

// WritePackets implements stack.LinkEndpoint.
func (e *Endpoint) WritePackets(r *stack.Route, gso *stack.GSO, pkts stack.PacketBufferList, protocol tcpip.NetworkProtocolNumber) (int, *tcpip.Error) {
	if atomic.LoadUint32(&e.enabled) == 0 {
		return e.Endpoint.WritePackets(r, gso, pkts, protocol)
	}

	var filtered stack.PacketBufferList
	for pkt := pkts.Front(); pkt != nil; pkt = pkt.Next() {
		// The filter expects the packet's header to be in the packet buffer's
		// header.
		//
		// TODO(50424): Support using a buffer.VectorisedView when parsing packets
		// so we don't need to create a single view here.
		hdr := packetbuffer.ToView(pkt)
		if e.filter.Run(Outgoing, protocol, hdr, buffer.VectorisedView{}) == Pass {
			filtered.PushBack(stack.NewPacketBuffer(stack.PacketBufferOptions{
				ReserveHeaderBytes: int(e.MaxHeaderLength()),
				Data:               hdr.ToVectorisedView(),
			}))
		}
	}

	return e.Endpoint.WritePackets(r, gso, filtered, protocol)
}
