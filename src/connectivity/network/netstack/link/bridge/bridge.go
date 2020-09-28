// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// link/bridge implements a bridging LinkEndpoint
// It can be writable.
package bridge

import (
	"hash/fnv"
	"math"
	"sort"
	"strings"

	"fidl/fuchsia/hardware/network"

	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/link"

	"gvisor.dev/gvisor/pkg/tcpip"
	"gvisor.dev/gvisor/pkg/tcpip/buffer"
	"gvisor.dev/gvisor/pkg/tcpip/header"
	"gvisor.dev/gvisor/pkg/tcpip/stack"
)

var _ stack.LinkEndpoint = (*Endpoint)(nil)
var _ link.Controller = (*Endpoint)(nil)

type Endpoint struct {
	links           map[tcpip.LinkAddress]*BridgeableEndpoint
	dispatcher      stack.NetworkDispatcher
	mtu             uint32
	capabilities    stack.LinkEndpointCapabilities
	maxHeaderLength uint16
	linkAddress     tcpip.LinkAddress
}

// New creates a new link from a list of BridgeableEndpoints that bridges
// packets written to it and received from any of its constituent links.
//
// The new link will have the minumum of the MTUs, the maximum of the max
// header lengths, and minimum set of the capabilities. This function takes
// ownership of `links`.
func New(links []*BridgeableEndpoint) *Endpoint {
	// TODO(fxbug.dev/57022): Make sure links are all using the same kind of link.
	sort.Slice(links, func(i, j int) bool {
		return strings.Compare(string(links[i].LinkAddress()), string(links[j].LinkAddress())) > 0
	})
	ep := &Endpoint{
		links: make(map[tcpip.LinkAddress]*BridgeableEndpoint),
		mtu:   math.MaxUint32,
	}
	h := fnv.New64()
	for _, l := range links {
		linkAddress := l.LinkAddress()
		ep.links[linkAddress] = l

		// mtu is the maximum write size, which is the minimum of any link's mtu.
		if mtu := l.MTU(); mtu < ep.mtu {
			ep.mtu = mtu
		}

		// Resolution is required if any link requires it.
		ep.capabilities |= l.Capabilities() & stack.CapabilityResolutionRequired

		// maxHeaderLength is the space to reserve for possible addition
		// headers. We want to reserve enough to suffice for all links.
		if maxHeaderLength := l.MaxHeaderLength(); maxHeaderLength > ep.maxHeaderLength {
			ep.maxHeaderLength = maxHeaderLength
		}

		if _, err := h.Write([]byte(linkAddress)); err != nil {
			panic(err)
		}
	}
	b := h.Sum(nil)[:6]
	// The second bit of the first byte indicates "locally administered".
	b[0] |= 1 << 1
	ep.linkAddress = tcpip.LinkAddress(b)
	return ep
}

// Up calls SetBridge(bridge) on all the constituent links of a bridge.
//
// This causes each constituent link to delegate dispatch to the bridge,
// meaning that received packets will be written out of or dispatched back up
// the stack for another constituent link.
func (ep *Endpoint) Up() error {
	for _, l := range ep.links {
		l.SetBridge(ep)
	}
	return nil
}

// Down calls SetBridge(nil) on all the constituent links of a bridge.
//
// This causes each bridgeable endpoint to go back to its state before
// bridging, dispatching up the stack to the default NetworkDispatcher
// implementation directly.
func (ep *Endpoint) Down() error {
	for _, l := range ep.links {
		l.SetBridge(nil)
	}
	return nil
}

// SetPromiscuousMode on a bridge is a no-op, since all of the constituent
// links on a bridge need to already be in promiscuous mode for bridging to
// work.
func (ep *Endpoint) SetPromiscuousMode(bool) error {
	return nil
}

func (*Endpoint) DeviceClass() network.DeviceClass {
	return network.DeviceClassBridge
}

func (ep *Endpoint) MTU() uint32 {
	return ep.mtu
}

func (ep *Endpoint) Capabilities() stack.LinkEndpointCapabilities {
	return ep.capabilities
}

func (ep *Endpoint) MaxHeaderLength() uint16 {
	return ep.maxHeaderLength
}

func (ep *Endpoint) LinkAddress() tcpip.LinkAddress {
	return ep.linkAddress
}

func (ep *Endpoint) WritePacket(r *stack.Route, gso *stack.GSO, protocol tcpip.NetworkProtocolNumber, pkt *stack.PacketBuffer) *tcpip.Error {
	for _, l := range ep.links {
		// We need to clone the packet buffer because each bridged endpoint may try
		// to set the packet buffer's link header, but the header may only be set
		// once for the lifetime of a packet buffer.
		if err := l.WritePacket(r, gso, protocol, pkt.Clone()); err != nil {
			return err
		}
	}
	return nil
}

// WritePackets returns the number of packets in hdrs that were successfully
// written to all links.
func (ep *Endpoint) WritePackets(r *stack.Route, gso *stack.GSO, pkts stack.PacketBufferList, protocol tcpip.NetworkProtocolNumber) (int, *tcpip.Error) {
	if len(ep.links) == 0 {
		return 0, nil
	}

	// Set the initial value to the maximum positive value for an unsized integer
	// (all bits set to 1 excluding the most significant bit).
	n := int(^uint(0) >> 1)
	for _, l := range ep.links {
		// We need to clone the packet buffers because each bridged endpoint may try
		// to set the packet buffers' link header, but the header may only be set
		// once for the lifetime of a packet buffer.
		var pktsList stack.PacketBufferList
		for pkt := pkts.Front(); pkt != nil; pkt = pkt.Next() {
			pktsList.PushBack(pkt.Clone())
		}
		i, err := l.WritePackets(r, gso, pktsList, protocol)
		if err != nil {
			return 0, err
		}

		if i < n {
			n = i
		}
	}
	return n, nil
}

func (ep *Endpoint) WriteRawPacket(packet buffer.VectorisedView) *tcpip.Error {
	for _, l := range ep.links {
		if err := l.WriteRawPacket(packet); err != nil {
			return err
		}
	}
	return nil
}

func (ep *Endpoint) Attach(d stack.NetworkDispatcher) {
	ep.dispatcher = d
	if d == nil {
		for _, l := range ep.links {
			l.SetBridge(nil)
		}
	}
}

func (ep *Endpoint) IsAttached() bool {
	return ep.dispatcher != nil
}

// DeliverNetworkPacketToBridge delivers a network packet to the bridged network.
//
// Endpoint does not implement stack.NetworkEndpoint.DeliverNetworkPacket because we need
// to know which BridgeableEndpoint the packet was delivered from to prevent packet loops.
func (ep *Endpoint) DeliverNetworkPacketToBridge(rxEP *BridgeableEndpoint, srcLinkAddr, dstLinkAddr tcpip.LinkAddress, protocol tcpip.NetworkProtocolNumber, pkt *stack.PacketBuffer) {
	// Is the destination link address a multicast/broadcast?
	//
	// If the least significant bit of the first octet is 1, then the address is a
	// multicast address.
	//
	// See the IEEE Std 802-2001 document for more details. Specifically,
	// section 9.2.1 of http://ieee802.org/secmail/pdfocSP2xXA6d.pdf:
	// "A 48-bit universal address consists of two parts. The first 24 bits
	// correspond to the OUI as assigned by the IEEE, except that the
	// assignee may set the LSB of the first octet to 1 for group addresses
	// or set it to 0 for individual addresses."
	flood := len(dstLinkAddr) == header.EthernetAddressSize && dstLinkAddr[0]&1 == 1

	if !flood {
		switch dstLinkAddr {
		case ep.linkAddress:
			ep.dispatcher.DeliverNetworkPacket(srcLinkAddr, dstLinkAddr, protocol, pkt)
			return
		default:
			if l, ok := ep.links[dstLinkAddr]; ok {
				l.Endpoint.DeliverNetworkPacket(srcLinkAddr, dstLinkAddr, protocol, pkt)
				return
			}
		}
	}

	// The bridge `ep` isn't included in ep.links below and we don't want to write
	// out of rxEP, otherwise the rest of this function would just be
	// "ep.WritePacket and if flood, also deliver to ep.links."
	if flood {
		// We need to clone the packet buffer because the stack may attempt to set the
		// network or transport headers which may only be done once for the lifetime
		// of a packet buffer.
		ep.dispatcher.DeliverNetworkPacket(srcLinkAddr, dstLinkAddr, protocol, pkt.Clone())
	}

	// NB: This isn't really a valid Route; Route is a public type but cannot
	// be instantiated fully outside of the stack package, because its
	// underlying referencedNetworkEndpoint cannot be accessed.
	// This means that methods on Route that depend on accessing the
	// underlying LinkEndpoint like MTU() will panic, but it would be
	// extremely strange for the LinkEndpoint we're calling WritePacket on to
	// access itself so indirectly.
	r := stack.Route{LocalLinkAddress: srcLinkAddr, RemoteLinkAddress: dstLinkAddr, NetProto: protocol}

	// TODO(fxbug.dev/20778): Learn which destinations are on which links and restrict transmission, like a bridge.
	for _, l := range ep.links {
		if flood {
			l.Endpoint.DeliverNetworkPacket(srcLinkAddr, dstLinkAddr, protocol, pkt.Clone())
		}
		// Don't write back out interface from which the frame arrived
		// because that causes interoperability issues with a router.
		if l != rxEP {
			// We need to clone the packet buffers because each bridged endpoint may try
			// to set the packet buffers' link header, but the header may only be set
			// once for the lifetime of a packet buffer.
			l.WritePacket(&r, nil, protocol, pkt.Clone())
		}
	}
}

// Wait implements stack.LinkEndpoint.
func (ep *Endpoint) Wait() {
	for _, e := range ep.links {
		e.Wait()
	}
}

// ARPHardwareType implements stack.LinkEndpoint.
func (e *Endpoint) ARPHardwareType() header.ARPHardwareType {
	// Use the first bridged endpoint.
	for _, link := range e.links {
		return link.ARPHardwareType()
	}

	return header.ARPHardwareNone
}

// AddHeader implements stack.LinkEndpoint.
func (e *Endpoint) AddHeader(local, remote tcpip.LinkAddress, protocol tcpip.NetworkProtocolNumber, pkt *stack.PacketBuffer) {
	// Use the first bridged endpoint.
	for _, link := range e.links {
		if len(local) == 0 {
			local = e.LinkAddress()
		}
		link.AddHeader(local, remote, protocol, pkt)
		return
	}
}
