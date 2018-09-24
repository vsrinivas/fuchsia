// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Filter endpoint implements a LinkEndpoint interface, which can wrap another
// LinkEndpoint.

package filter

import (
	"github.com/google/netstack/tcpip"
	"github.com/google/netstack/tcpip/buffer"
	"github.com/google/netstack/tcpip/stack"
)

type endpoint struct {
	filter     *Filter
	dispatcher stack.NetworkDispatcher
	stack.LinkEndpoint
}

// New creates a new Filter endpoint by wrapping a lower LinkEndpoint.
// The lower endpoint must implement stack.BufferWritingLinkEndpoint.
func NewEndpoint(filter *Filter, lower tcpip.LinkEndpointID) tcpip.LinkEndpointID {
	lowerEP := stack.FindLinkEndpoint(lower)
	if _, ok := lowerEP.(stack.BufferWritingLinkEndpoint); !ok {
		panic("The lowerEP doesn't implement stack.BufferWritingLinkEndpoint")
	}
	return stack.RegisterLinkEndpoint(&endpoint{
		filter:       filter,
		LinkEndpoint: lowerEP,
	})
}

// DeliverNetworkPacket is called when a packet arrives at the lower endpoint.
// It calls Run before dispatching the packet to the upper endpoint.
func (e *endpoint) DeliverNetworkPacket(linkEP stack.LinkEndpoint, dstLinkAddr, srcLinkAddr tcpip.LinkAddress, protocol tcpip.NetworkProtocolNumber, vv *buffer.VectorisedView) {
	if e.filter.Run(Incoming, protocol, vv) != Pass {
		return
	}
	e.dispatcher.DeliverNetworkPacket(e, dstLinkAddr, srcLinkAddr, protocol, vv)
}

// Attach sets a dispatcher and call Attach on the lower endpoint.
func (e *endpoint) Attach(dispatcher stack.NetworkDispatcher) {
	e.dispatcher = dispatcher
	e.LinkEndpoint.Attach(e)
}

// WritePacket is called when a packet arrives is written to the lower
// endpoint. It calls Run to what to do with the packet.
func (e *endpoint) WritePacket(r *stack.Route, hdr *buffer.Prependable, payload buffer.View, protocol tcpip.NetworkProtocolNumber) *tcpip.Error {
	vs := []buffer.View{hdr.UsedBytes(), payload}
	vv := buffer.NewVectorisedView(hdr.UsedLength()+len(payload), vs)
	return e.WriteBuffer(r, &vv, protocol)
}

func (e *endpoint) WriteBuffer(r *stack.Route, vv *buffer.VectorisedView, protocol tcpip.NetworkProtocolNumber) *tcpip.Error {
	if e.filter.Run(Outgoing, protocol, vv) != Pass {
		return nil
	}
	if lowerEP, ok := e.LinkEndpoint.(stack.BufferWritingLinkEndpoint); ok {
		return lowerEP.WriteBuffer(r, vv, protocol)
	}
	return nil
}
