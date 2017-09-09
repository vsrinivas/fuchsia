// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package stats implements a LinkEndpoint of the netstack,
// with responsibility to keep track of traffic statistics.
// This LinkEndpoint may be inserted into any software layer as desired.
// One feasible insertaion point is between the link layer sniffer and
// the network protocol layer.

package stats

import (
	"time"

	nsfidl "garnet/public/lib/netstack/fidl/netstack"

	"github.com/google/netstack/tcpip"
	"github.com/google/netstack/tcpip/buffer"
	"github.com/google/netstack/tcpip/stack"
)

type StatsEndpoint struct {
	dispatcher stack.NetworkDispatcher
	lower      stack.LinkEndpoint

	// Storage is made of FIDL data structure
	// enabling conversion-free export and import.
	Stats nsfidl.NetInterfaceStats
}

func (e *StatsEndpoint) Wrap(lower tcpip.LinkEndpointID) tcpip.LinkEndpointID {
	e.lower = stack.FindLinkEndpoint(lower)
	e.Stats = nsfidl.NetInterfaceStats{
		UpSince: time.Now().Unix(),
	}
	return stack.RegisterLinkEndpoint(e)
}

// DeliverNetworkPacket handles incoming packet from the lower layer.
// It performs packet inspection, and extract a rich set of statistics,
// and stores them to a FIDL data structure.
func (e *StatsEndpoint) DeliverNetworkPacket(linkEP stack.LinkEndpoint, remoteLinkAddr tcpip.LinkAddress, protocol tcpip.NetworkProtocolNumber, vv *buffer.VectorisedView) {

	e.Stats.RxPktsTotal += 1
	e.Stats.RxBytesTotal += uint64(vv.Size())
	e.dispatcher.DeliverNetworkPacket(e, remoteLinkAddr, protocol, vv)
}

// Attach implements registaion of lower endpoint and dispatcher.
func (e *StatsEndpoint) Attach(dispatcher stack.NetworkDispatcher) {
	e.dispatcher = dispatcher
	e.lower.Attach(e)
}

// MTU direcly directly uses lower layer MTU().
func (e *StatsEndpoint) MTU() uint32 {
	return e.lower.MTU()
}

// MaxHeaderLength directly uses lower layer MaxHeaderLength()
func (e *StatsEndpoint) MaxHeaderLength() uint16 {
	return e.lower.MaxHeaderLength()
}

// LinkAddress directly uses the lower layer LinkAddress()
func (e *StatsEndpoint) LinkAddress() tcpip.LinkAddress {
	return e.lower.LinkAddress()
}

// WritePacket handles outgoing packet from the higher layer.
// It performs packet inspection, and extracts a rich set of statistics,
// and stores them to a FIDL data structure.
func (e *StatsEndpoint) WritePacket(r *stack.Route, hdr *buffer.Prependable, payload buffer.View, protocol tcpip.NetworkProtocolNumber) *tcpip.Error {
	e.Stats.TxPktsTotal += 1
	e.Stats.TxBytesTotal += uint64(len(payload))
	return e.lower.WritePacket(r, hdr, payload, protocol)
}
