// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package eth

import (
	"log"
	"syscall/zx"
	"syscall/zx/mxerror"

	"netstack/trace"

	"github.com/google/netstack/tcpip"
	"github.com/google/netstack/tcpip/buffer"
	"github.com/google/netstack/tcpip/header"
	"github.com/google/netstack/tcpip/stack"
)

const debug = false

var _ stack.LinkEndpoint = (*endpoint)(nil)

type endpoint struct {
	client     *Client
	dispatcher stack.NetworkDispatcher
}

func (e *endpoint) MTU() uint32 { return e.client.Info.Mtu }
func (e *endpoint) Capabilities() stack.LinkEndpointCapabilities {
	return stack.CapabilityResolutionRequired
}
func (e *endpoint) MaxHeaderLength() uint16 { return header.EthernetMinimumSize }
func (e *endpoint) LinkAddress() tcpip.LinkAddress {
	return tcpip.LinkAddress(e.client.Info.Mac.Octets[:])
}

func (e *endpoint) IsAttached() bool {
	return e.dispatcher != nil
}

func (e *endpoint) WritePacket(r *stack.Route, hdr buffer.Prependable, payload buffer.VectorisedView, protocol tcpip.NetworkProtocolNumber) *tcpip.Error {
	if r.LocalAddress != "" && r.LocalAddress == r.RemoteAddress {
		views := make([]buffer.View, 1, 1+len(payload.Views()))
		views[0] = hdr.View()
		views = append(views, payload.Views()...)
		vv := buffer.NewVectorisedView(len(views[0])+payload.Size(), views)
		e.dispatcher.DeliverNetworkPacket(e, r.RemoteLinkAddress, r.LocalLinkAddress, protocol, vv)
		return nil
	}

	trace.DebugTrace("eth write")

	eth := header.Ethernet(hdr.Prepend(header.EthernetMinimumSize))
	ethHdr := &header.EthernetFields{
		Type: protocol,
	}
	// Preserve the src address if it's set in the route.
	if r.LocalLinkAddress != "" {
		ethHdr.SrcAddr = r.LocalLinkAddress
	} else {
		ethHdr.SrcAddr = tcpip.LinkAddress(e.client.Info.Mac.Octets[:])
	}
	switch {
	case header.IsV4MulticastAddress(r.RemoteAddress):
		// RFC 1112.6.4
		ethHdr.DstAddr = tcpip.LinkAddress([]byte{0x01, 0x00, 0x5e, r.RemoteAddress[1] & 0x7f, r.RemoteAddress[2], r.RemoteAddress[3]})
	case header.IsV6MulticastAddress(r.RemoteAddress):
		// RFC 2464.7
		ethHdr.DstAddr = tcpip.LinkAddress([]byte{0x33, 0x33, r.RemoteAddress[12], r.RemoteAddress[13], r.RemoteAddress[14], r.RemoteAddress[15]})
	default:
		ethHdr.DstAddr = r.RemoteLinkAddress
	}
	eth.Encode(ethHdr)

	for {
		if buf := e.client.AllocForSend(); buf != nil {
			used := copy(buf, hdr.View())
			for _, v := range payload.Views() {
				used += copy(buf[used:], v)
			}
			if err := e.client.Send(buf[:used]); err != nil {
				trace.DebugDrop("link: send error: %v", err)
				return tcpip.ErrWouldBlock
			}
			return nil
		}
		if err := e.client.WaitSend(); err != nil {
			trace.DebugDrop("link: alloc error: %v", err)
			return tcpip.ErrWouldBlock
		}
	}
}

func (e *endpoint) Attach(dispatcher stack.NetworkDispatcher) {
	trace.DebugTraceDeep(5, "eth attach")

	go func() {
		if err := func() error {
			for {
				b, err := e.client.Recv()
				switch mxerror.Status(err) {
				case zx.ErrOk:
					// TODO: optimization: consider unpacking the destination link addr
					// and checking that we are a destination (including multicast support).

					eth := header.Ethernet(b)

					// TODO: avoid the copy? currently results in breaking sftp.
					v := buffer.NewViewFromBytes(b)
					v.TrimFront(header.EthernetMinimumSize)

					dispatcher.DeliverNetworkPacket(e, eth.SourceAddress(), eth.DestinationAddress(), eth.Type(), v.ToVectorisedView())

					e.client.Free(b)
				case zx.ErrShouldWait:
					e.client.WaitRecv()
				default:
					return err
				}
			}
		}(); err != nil {
			log.Printf("dispatch error: %v", err)
		}
	}()

	e.dispatcher = dispatcher
}

func NewLinkEndpoint(client *Client) *endpoint {
	return &endpoint{client: client}
}
