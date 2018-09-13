// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package eth

import (
	"sync"
	"syscall/zx"

	"syslog"

	"gvisor.dev/gvisor/pkg/tcpip"
	"gvisor.dev/gvisor/pkg/tcpip/buffer"
	"gvisor.dev/gvisor/pkg/tcpip/header"
	"gvisor.dev/gvisor/pkg/tcpip/stack"
)

var _ stack.LinkEndpoint = (*endpoint)(nil)

type endpoint struct {
	client     *Client
	dispatcher stack.NetworkDispatcher

	wg sync.WaitGroup
}

func (e *endpoint) MTU() uint32 { return e.client.Info.Mtu }
func (e *endpoint) Capabilities() stack.LinkEndpointCapabilities {
	return stack.CapabilityResolutionRequired
}
func (e *endpoint) MaxHeaderLength() uint16 {
	// Ethernet headers are never prepended into a buffer.Prependable, so no
	// space need be reserved for them.
	return 0
}
func (e *endpoint) LinkAddress() tcpip.LinkAddress {
	return tcpip.LinkAddress(e.client.Info.Mac.Octets[:])
}

func (e *endpoint) IsAttached() bool {
	return e.dispatcher != nil
}

func (e *endpoint) WritePacket(r *stack.Route, _ *stack.GSO, protocol tcpip.NetworkProtocolNumber, pkt tcpip.PacketBuffer) *tcpip.Error {
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
	used += copy(buf[used:], pkt.Header.View())
	for _, v := range pkt.Data.Views() {
		used += copy(buf[used:], v)
	}
	if err := e.client.Send(buf[:used]); err != nil {
		syslog.VLogTf(syslog.DebugVerbosity, "eth", "send error: %s", err)
		return tcpip.ErrWouldBlock
	}

	syslog.VLogTf(syslog.TraceVerbosity, "eth", "write=%d", used)

	return nil
}

func (e *endpoint) WritePackets(r *stack.Route, gso *stack.GSO, pkts []tcpip.PacketBuffer, protocol tcpip.NetworkProtocolNumber) (int, *tcpip.Error) {
	var n int
	for _, pkt := range pkts {
		if err := e.WritePacket(r, gso, protocol, pkt); err != nil {
			return n, err
		}
		n++
	}
	return n, nil
}

func (e *endpoint) WriteRawPacket(packet buffer.VectorisedView) *tcpip.Error {
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

	used := 0
	for _, v := range packet.Views() {
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
	e.wg.Add(1)
	go func() {
		defer e.wg.Done()
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

				// Make sure we can get an ethernet header.
				if len(v) < header.EthernetMinimumSize {
					continue
				}
				eth := header.Ethernet(v)
				v.TrimFront(header.EthernetMinimumSize)
				dispatcher.DeliverNetworkPacket(e, eth.SourceAddress(), eth.DestinationAddress(), eth.Type(), tcpip.PacketBuffer{
					Data: v.ToVectorisedView(),
				})
			}
		}(); err != nil {
			syslog.WarnTf("eth", "dispatch error: %s", err)
		}
	}()

	e.dispatcher = dispatcher
}

// Wait implements stack.LinkEndpoint. It blocks until an error in the dispatch
// goroutine(s) spawned in Attach occurs.
func (e *endpoint) Wait() {
	e.wg.Wait()
}

func NewLinkEndpoint(client *Client) *endpoint {
	return &endpoint{client: client}
}
