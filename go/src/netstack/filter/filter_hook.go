// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Filter endpoint implements a LinkEndpoint interface, which can wrap another
// LinkEndpoint.

// Note: This is a tentative solution to hook the traffic in netstack, and
// likely to change.

package filter

import (
	"github.com/google/netstack/tcpip"
	"github.com/google/netstack/tcpip/buffer"
	"github.com/google/netstack/tcpip/stack"
)

type endpoint struct {
	dispatcher stack.NetworkDispatcher
	stack.LinkEndpoint
}

// New creates a new Filter endpoint by wrapping a lower LinkEndpoint.
func New(lower tcpip.LinkEndpointID) tcpip.LinkEndpointID {
	return stack.RegisterLinkEndpoint(&endpoint{
		LinkEndpoint: stack.FindLinkEndpoint(lower),
	})
}

// DeliverNetworkPacket is called when a packet arrives at the lower endpoint.
// It calls Run before dispatching the packet to the upper endpoint.
func (e *endpoint) DeliverNetworkPacket(linkEP stack.LinkEndpoint, dstLinkAddr, srcLinkAddr tcpip.LinkAddress, protocol tcpip.NetworkProtocolNumber, vv *buffer.VectorisedView) {
	if Run(Incoming, protocol, vv.First(), nil) != Pass {
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
	if Run(Outgoing, protocol, hdr.UsedBytes(), payload) != Pass {
		return nil
	}
	return e.LinkEndpoint.WritePacket(r, hdr, payload, protocol)
}
