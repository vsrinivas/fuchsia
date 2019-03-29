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
func NewEndpoint(filter *Filter, lower tcpip.LinkEndpointID) tcpip.LinkEndpointID {
	return stack.RegisterLinkEndpoint(&endpoint{
		filter:       filter,
		LinkEndpoint: stack.FindLinkEndpoint(lower),
	})
}

// DeliverNetworkPacket is called when a packet arrives at the lower endpoint.
// It calls Run before dispatching the packet to the upper endpoint.
func (e *endpoint) DeliverNetworkPacket(linkEP stack.LinkEndpoint, dstLinkAddr, srcLinkAddr tcpip.LinkAddress, protocol tcpip.NetworkProtocolNumber, vv buffer.VectorisedView) {
	hdr := buffer.NewPrependableFromView(vv.First())
	payload := vv
	payload.RemoveFirst()

	if e.filter.Run(Incoming, protocol, hdr, payload) == Pass {
		e.dispatcher.DeliverNetworkPacket(e, dstLinkAddr, srcLinkAddr, protocol, vv)
	}
}

// Attach sets a dispatcher and call Attach on the lower endpoint.
func (e *endpoint) Attach(dispatcher stack.NetworkDispatcher) {
	e.dispatcher = dispatcher
	e.LinkEndpoint.Attach(e)
}

// WritePacket is called when a packet arrives is written to the lower
// endpoint. It calls Run to what to do with the packet.
func (e *endpoint) WritePacket(r *stack.Route, gso *stack.GSO, hdr buffer.Prependable, payload buffer.VectorisedView, protocol tcpip.NetworkProtocolNumber) *tcpip.Error {
	if e.filter.Run(Outgoing, protocol, hdr, payload) != Pass {
		return nil
	}
	return e.LinkEndpoint.WritePacket(r, gso, hdr, payload, protocol)
}
