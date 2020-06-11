// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package eth

import (
	"gvisor.dev/gvisor/pkg/tcpip"
	"gvisor.dev/gvisor/pkg/tcpip/header"
	"gvisor.dev/gvisor/pkg/tcpip/stack"
)

var _ stack.LinkEndpoint = (*endpoint)(nil)
var _ stack.GSOEndpoint = (*Client)(nil)
var _ stack.NetworkDispatcher = (*endpoint)(nil)

type endpoint struct {
	stack.LinkEndpoint
	dispatcher stack.NetworkDispatcher
}

func (e *endpoint) Capabilities() stack.LinkEndpointCapabilities {
	return stack.CapabilityResolutionRequired | e.LinkEndpoint.Capabilities()
}

func (e *endpoint) MaxHeaderLength() uint16 {
	return header.EthernetMinimumSize
}

func (e *endpoint) makeEthernetFields(r *stack.Route, protocol tcpip.NetworkProtocolNumber) header.EthernetFields {
	hdr := header.EthernetFields{
		SrcAddr: r.LocalLinkAddress,
		DstAddr: r.RemoteLinkAddress,
		Type:    protocol,
	}
	// Preserve the src address if it's set in the route.
	if len(hdr.SrcAddr) == 0 {
		hdr.SrcAddr = e.LinkAddress()
	}
	return hdr
}

func (e *endpoint) WritePacket(r *stack.Route, gso *stack.GSO, protocol tcpip.NetworkProtocolNumber, pkt *stack.PacketBuffer) *tcpip.Error {
	fields := e.makeEthernetFields(r, protocol)
	h := pkt.Header.Prepend(header.EthernetMinimumSize)
	header.Ethernet(h).Encode(&fields)
	pkt.LinkHeader = h
	return e.LinkEndpoint.WritePacket(r, gso, protocol, pkt)
}

func (e *endpoint) WritePackets(r *stack.Route, gso *stack.GSO, pkts stack.PacketBufferList, protocol tcpip.NetworkProtocolNumber) (int, *tcpip.Error) {
	fields := e.makeEthernetFields(r, protocol)
	for pkt := pkts.Front(); pkt != nil; pkt = pkt.Next() {
		h := pkt.Header.Prepend(header.EthernetMinimumSize)
		header.Ethernet(h).Encode(&fields)
		pkt.LinkHeader = h
	}
	return e.LinkEndpoint.WritePackets(r, gso, pkts, protocol)
}

func (e *endpoint) Attach(dispatcher stack.NetworkDispatcher) {
	e.dispatcher = dispatcher
	e.LinkEndpoint.Attach(e)
}

func (e *endpoint) DeliverNetworkPacket(dstLinkAddr, srcLinkAddr tcpip.LinkAddress, protocol tcpip.NetworkProtocolNumber, pkt *stack.PacketBuffer) {
	ethBytes, ok := pkt.Data.PullUp(header.EthernetMinimumSize)
	if !ok {
		// TODO(42949): record this in statistics.
		return
	}
	pkt.Data.TrimFront(header.EthernetMinimumSize)
	eth := header.Ethernet(ethBytes)

	if len(dstLinkAddr) == 0 {
		dstLinkAddr = eth.SourceAddress()
	}
	if len(srcLinkAddr) == 0 {
		srcLinkAddr = eth.DestinationAddress()
	}
	if protocol == 0 {
		protocol = eth.Type()
	}
	e.dispatcher.DeliverNetworkPacket(dstLinkAddr, srcLinkAddr, protocol, pkt)
}

func (e *endpoint) GSOMaxSize() uint32 {
	if e, ok := e.LinkEndpoint.(stack.GSOEndpoint); ok {
		return e.GSOMaxSize()
	}
	return 0
}

func NewLinkEndpoint(ep stack.LinkEndpoint) *endpoint {
	return &endpoint{LinkEndpoint: ep}
}
