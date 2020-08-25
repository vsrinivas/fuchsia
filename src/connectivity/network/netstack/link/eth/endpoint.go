// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package eth

import (
	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/link/eth/utils"

	"gvisor.dev/gvisor/pkg/tcpip"
	"gvisor.dev/gvisor/pkg/tcpip/header"
	"gvisor.dev/gvisor/pkg/tcpip/link/nested"
	"gvisor.dev/gvisor/pkg/tcpip/stack"
)

var _ stack.LinkEndpoint = (*endpoint)(nil)
var _ stack.GSOEndpoint = (*Client)(nil)
var _ stack.NetworkDispatcher = (*endpoint)(nil)

type endpoint struct {
	nested.Endpoint
}

func (e *endpoint) Capabilities() stack.LinkEndpointCapabilities {
	return stack.CapabilityResolutionRequired | e.Endpoint.Capabilities()
}

func (e *endpoint) MaxHeaderLength() uint16 {
	return header.EthernetMinimumSize
}

func (e *endpoint) WritePacket(r *stack.Route, gso *stack.GSO, protocol tcpip.NetworkProtocolNumber, pkt *stack.PacketBuffer) *tcpip.Error {
	e.AddHeader(r.LocalLinkAddress, r.RemoteLinkAddress, protocol, pkt)
	return e.Endpoint.WritePacket(r, gso, protocol, pkt)
}

func (e *endpoint) WritePackets(r *stack.Route, gso *stack.GSO, pkts stack.PacketBufferList, protocol tcpip.NetworkProtocolNumber) (int, *tcpip.Error) {
	fields := utils.MakeEthernetFields(e.LinkAddress(), r.LocalLinkAddress, r.RemoteLinkAddress, protocol)
	for pkt := pkts.Front(); pkt != nil; pkt = pkt.Next() {
		utils.AddEthernetHeaderWithFields(&fields, pkt)
	}
	return e.Endpoint.WritePackets(r, gso, pkts, protocol)
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
	e.Endpoint.DeliverNetworkPacket(dstLinkAddr, srcLinkAddr, protocol, pkt)
}

func (*endpoint) ARPHardwareType() header.ARPHardwareType {
	return header.ARPHardwareEther
}

func (e *endpoint) AddHeader(local, remote tcpip.LinkAddress, protocol tcpip.NetworkProtocolNumber, pkt *stack.PacketBuffer) {
	utils.AddEthernetHeader(e.LinkAddress(), local, remote, protocol, pkt)
}

func NewLinkEndpoint(ep stack.LinkEndpoint) *endpoint {
	e := &endpoint{}
	e.Endpoint.Init(ep, e)
	return e
}
