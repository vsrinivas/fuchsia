// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//go:build !build_with_native_toolchain

// link/bridge implements a bridging LinkEndpoint
// It can be writable.
package bridge

import (
	"fmt"
	"hash/fnv"
	"math"
	"sort"
	"strings"
	"syscall/zx"

	"fidl/fuchsia/hardware/network"

	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/link"
	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/sync"
	"go.fuchsia.dev/fuchsia/src/lib/component"
	syslog "go.fuchsia.dev/fuchsia/src/lib/syslog/go"

	"gvisor.dev/gvisor/pkg/tcpip"
	"gvisor.dev/gvisor/pkg/tcpip/header"
	"gvisor.dev/gvisor/pkg/tcpip/stack"
)

var _ stack.LinkEndpoint = (*Endpoint)(nil)
var _ link.Controller = (*Endpoint)(nil)

const tag = "bridge"

type Endpoint struct {
	links           map[tcpip.LinkAddress]*BridgeableEndpoint
	mtu             uint32
	capabilities    stack.LinkEndpointCapabilities
	maxHeaderLength uint16
	linkAddress     tcpip.LinkAddress

	mu struct {
		sync.RWMutex

		dispatcher stack.NetworkDispatcher
	}
}

// New creates a new link from a list of BridgeableEndpoints that bridges
// packets written to it and received from any of its constituent links.
//
// `links` must be non-empty, as properties of the new link are derived from
// the constituent links: it will have the minimum of the MTUs, the maximum
// of the max header lengths, and the minimum set of capabilities.
func New(links []*BridgeableEndpoint) (*Endpoint, error) {
	if len(links) == 0 {
		return nil, fmt.Errorf("creating bridge with no attached endpoints is invalid")
	}
	{
		// TODO(https://fxbug.dev/57022): Make sure links are all using the same kind of link.
		links := append([]*BridgeableEndpoint(nil), links...)
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
		// Set the second-least-significant bit of the first byte so the address is locally-administered.
		b[0] |= 1 << 1
		// Clear the least-significant bit of the first byte so the address is unicast.
		b[0] &^= 1
		ep.linkAddress = tcpip.LinkAddress(b)
		return ep, nil
	}
}

func (*Endpoint) Up() error {
	return nil
}

// TODO(https://fxbug.dev/86388): Implement disabling the bridge.
func (*Endpoint) Down() error {
	_ = syslog.WarnTf(tag, "disabling bridges is unimplemented, the bridge will still be usable")
	return nil
}

// SetPromiscuousMode on a bridge is a no-op, since all of the constituent
// links on a bridge need to already be in promiscuous mode for bridging to
// work.
func (*Endpoint) SetPromiscuousMode(bool) error {
	return nil
}

func (*Endpoint) DeviceClass() network.DeviceClass {
	return network.DeviceClassBridge
}

func (*Endpoint) ConnectPort(port network.PortWithCtxInterfaceRequest) {
	if err := component.CloseWithEpitaph(port.Channel, zx.ErrNotSupported); err != nil {
		_ = syslog.WarnTf(tag, "ConnectPort: CloseWithEpitaph(_, _) = %s", err)
	}
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

func clonePkts(pkts stack.PacketBufferList) stack.PacketBufferList {
	var newPkts stack.PacketBufferList
	for _, pkt := range pkts.AsSlice() {
		newPkts.PushBack(pkt.Clone())
	}
	return newPkts
}

// WritePackets returns the number of packets in hdrs that were successfully
// written to all links.
func (ep *Endpoint) WritePackets(pkts stack.PacketBufferList) (int, tcpip.Error) {
	minPkts := pkts.Len()
	var firstErr tcpip.Error
	i := 0
	for _, l := range ep.links {
		i++
		// Need to clone when writing to all but the last endpoint, since callee
		// takes ownership.
		n, err := func() (int, tcpip.Error) {
			pkts := pkts
			clonedPkt := i != len(ep.links)
			if clonedPkt {
				pkts = clonePkts(pkts)
				defer pkts.DecRef()
			}
			return l.WritePackets(pkts)
		}()
		if n < minPkts {
			minPkts = n
		}
		switch err.(type) {
		case nil:
		case *tcpip.ErrClosedForSend:
			// TODO(https://fxbug.dev/86959): Handle bridged interface removal.
			_ = syslog.WarnTf(tag, "WritePackets on bridged endpoint returned ClosedForSend")
		default:
			if firstErr == nil {
				firstErr = err
			} else {
				_ = syslog.WarnTf(tag, "already observed WritePackets error = %s, dropping error = %s", firstErr, err)
			}
		}
	}
	return minPkts, firstErr
}

func (ep *Endpoint) Attach(d stack.NetworkDispatcher) {
	ep.mu.Lock()
	defer ep.mu.Unlock()

	ep.mu.dispatcher = d
	if d == nil {
		for _, l := range ep.links {
			l.SetBridge(nil)
		}
	}
}

func (ep *Endpoint) IsAttached() bool {
	ep.mu.RLock()
	defer ep.mu.RUnlock()
	return ep.mu.dispatcher != nil
}

// DeliverNetworkPacketToBridge delivers a network packet to the bridged network.
//
// Endpoint does not implement stack.NetworkEndpoint.DeliverNetworkPacket because we need
// to know which BridgeableEndpoint the packet was delivered from to prevent packet loops.
func (ep *Endpoint) DeliverNetworkPacketToBridge(rxEP *BridgeableEndpoint, protocol tcpip.NetworkProtocolNumber, pkt stack.PacketBufferPtr) {
	ep.mu.RLock()
	dispatcher := ep.mu.dispatcher
	ep.mu.RUnlock()

	dstLinkAddr := header.Ethernet(pkt.LinkHeader().Slice()).DestinationAddress()
	if dstLinkAddr == ep.linkAddress {
		if dispatcher != nil {
			dispatcher.DeliverNetworkPacket(protocol, pkt)
		}
		return
	}

	if len(dstLinkAddr) != header.EthernetAddressSize {
		panic(fmt.Sprintf("DeliverNetworkPacket(%p, %d, _) called with non-MAC dst link add = %sr", rxEP, protocol, dstLinkAddr))
	}

	if header.IsMulticastEthernetAddress(dstLinkAddr) {
		// The bridge `ep` isn't included in ep.links below.
		//
		// Need to clone as callee takes ownership and the packet still needs to be
		// written to constituent links.
		if dispatcher != nil {
			func() {
				pkt := pkt.Clone()
				defer pkt.DecRef()
				dispatcher.DeliverNetworkPacket(protocol, pkt)
			}()
		}
	}

	// TODO(https://fxbug.dev/20778): Learn which destinations are on
	// which links and restrict transmission, like a bridge.
	i := 0
	rxFound := false
	for _, l := range ep.links {
		i++
		// Don't write back out the interface from which the frame arrived
		// because that causes interoperability issues with a router.
		if l == rxEP {
			rxFound = true
			continue
		}

		// Shadow pkt so that changes the link makes to the packet buffer
		// are not visible to links we write the packet to after.
		err := func() tcpip.Error {
			pkt := pkt
			switch i {
			case len(ep.links):
				// The last call never needs cloning.
			case len(ep.links) - 1:
				// The second-to-last call needs cloning iff the last endpoint is not rxEP.
				if !rxFound {
					break
				}
				fallthrough
			default:
				pkt = pkt.Clone()
				defer pkt.DecRef()
			}

			var pkts stack.PacketBufferList
			pkts.PushBack(pkt)
			_, err := l.WritePackets(pkts)
			return err
		}()
		switch err.(type) {
		case nil:
		case *tcpip.ErrClosedForSend:
			// TODO(https://fxbug.dev/86959): Handle bridged interface removal.
		default:
			_ = syslog.WarnTf(tag, "failed to write to bridged endpoint %p: %s", l, err)
		}
	}
}

// Wait implements stack.LinkEndpoint.
func (*Endpoint) Wait() {}

// ARPHardwareType implements stack.LinkEndpoint.
func (e *Endpoint) ARPHardwareType() header.ARPHardwareType {
	// Use the first bridged endpoint.
	for _, link := range e.links {
		return link.ARPHardwareType()
	}

	return header.ARPHardwareNone
}

// AddHeader implements stack.LinkEndpoint.
func (e *Endpoint) AddHeader(pkt stack.PacketBufferPtr) {
	// Use the first bridged endpoint.
	for _, link := range e.links {
		link.AddHeader(pkt)
		return
	}
}
