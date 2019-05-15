// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package eth

import (
	"syscall/zx"
	"syslog"

	"github.com/google/netstack/tcpip"
	"github.com/google/netstack/tcpip/buffer"
	"github.com/google/netstack/tcpip/header"
	"github.com/google/netstack/tcpip/stack"
)

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

func (e *endpoint) WritePacket(r *stack.Route, _ *stack.GSO, hdr buffer.Prependable, payload buffer.VectorisedView, protocol tcpip.NetworkProtocolNumber) *tcpip.Error {
	var buf Buffer
	for {
		if buf = e.client.AllocForSend(); buf != nil {
			break
		}
		if err := e.client.WaitSend(); err != nil {
			syslog.VLogTf(syslog.DebugVerbosity, "eth", "wait error: %s", err)
			return tcpip.ErrWouldBlock
		}
	}

	ethHdr := &header.EthernetFields{
		DstAddr: r.RemoteLinkAddress,
		Type:    protocol,
	}
	// Preserve the src address if it's set in the route.
	if r.LocalLinkAddress != "" {
		ethHdr.SrcAddr = r.LocalLinkAddress
	} else {
		ethHdr.SrcAddr = tcpip.LinkAddress(e.client.Info.Mac.Octets[:])
	}
	header.Ethernet(buf).Encode(ethHdr)
	used := header.EthernetMinimumSize
	used += copy(buf[used:], hdr.View())
	for _, v := range payload.Views() {
		used += copy(buf[used:], v)
	}
	if err := e.client.Send(buf[:used]); err != nil {
		syslog.VLogTf(syslog.DebugVerbosity, "eth", "send error: %s", err)
		return tcpip.ErrWouldBlock
	}

	syslog.VLogTf(syslog.TraceVerbosity, "eth", "write=%d", used)

	return nil
}

func (e *endpoint) Attach(dispatcher stack.NetworkDispatcher) {
	go func() {
		if err := func() error {
			for {
				b, err := e.client.Recv()
				if err != nil {
					if err, ok := err.(*zx.Error); ok {
						switch err.Status {
						case zx.ErrShouldWait:
							e.client.WaitRecv()
							continue
						}
					}
					return err
				}
				v := append(buffer.View(nil), b...)
				e.client.Free(b)
				eth := header.Ethernet(v)
				v.TrimFront(header.EthernetMinimumSize)
				dispatcher.DeliverNetworkPacket(e, eth.SourceAddress(), eth.DestinationAddress(), eth.Type(), v.ToVectorisedView())
			}
		}(); err != nil {
			syslog.WarnTf("eth", "dispatch error: %s", err)
		}
	}()

	e.dispatcher = dispatcher
}

func NewLinkEndpoint(client *Client) *endpoint {
	return &endpoint{client: client}
}
