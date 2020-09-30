// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package eth_test

import (
	"bytes"
	"fmt"
	"testing"

	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/link/eth"
	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/util"

	"gvisor.dev/gvisor/pkg/tcpip"
	"gvisor.dev/gvisor/pkg/tcpip/buffer"
	"gvisor.dev/gvisor/pkg/tcpip/header"
	"gvisor.dev/gvisor/pkg/tcpip/network/ipv4"
	"gvisor.dev/gvisor/pkg/tcpip/stack"
	"gvisor.dev/gvisor/pkg/tcpip/transport/udp"
	"gvisor.dev/gvisor/pkg/waiter"
)

type nullEndpoint struct{}

func (*nullEndpoint) MTU() uint32 {
	return 0
}
func (*nullEndpoint) Capabilities() stack.LinkEndpointCapabilities {
	return 0
}
func (*nullEndpoint) MaxHeaderLength() uint16 {
	return 0
}
func (*nullEndpoint) LinkAddress() tcpip.LinkAddress {
	var l tcpip.LinkAddress
	return l
}
func (*nullEndpoint) WritePacket(r *stack.Route, gso *stack.GSO, protocol tcpip.NetworkProtocolNumber, pkt *stack.PacketBuffer) *tcpip.Error {
	return nil
}
func (*nullEndpoint) WritePackets(r *stack.Route, gso *stack.GSO, pkts stack.PacketBufferList, protocol tcpip.NetworkProtocolNumber) (int, *tcpip.Error) {
	return pkts.Len(), nil
}
func (*nullEndpoint) WriteRawPacket(vv buffer.VectorisedView) *tcpip.Error {
	return nil
}
func (*nullEndpoint) Attach(dispatcher stack.NetworkDispatcher) {}
func (*nullEndpoint) IsAttached() bool                          { return false }
func (*nullEndpoint) Wait()                                     {}
func (*nullEndpoint) ARPHardwareType() header.ARPHardwareType   { return header.ARPHardwareNone }
func (*nullEndpoint) AddHeader(_, _ tcpip.LinkAddress, _ tcpip.NetworkProtocolNumber, _ *stack.PacketBuffer) {
}

type nullChecksumOffloadEndpoint struct {
	nullEndpoint
}

func (*nullChecksumOffloadEndpoint) Capabilities() stack.LinkEndpointCapabilities {
	return stack.CapabilityTXChecksumOffload | stack.CapabilityRXChecksumOffload
}

var _ stack.LinkEndpoint = (*nullEndpoint)(nil)

func BenchmarkWritePacket(b *testing.B) {
	b.Skip("TODO(https://github.com/golang/go/issues/40823): re-enable")
	const nicid = 1
	for _, test := range []struct {
		name string
		ep   stack.LinkEndpoint
	}{
		{name: "checksumOffloadDisabled", ep: &nullEndpoint{}},
		{name: "checksumOffloadEnabled", ep: &nullChecksumOffloadEndpoint{}},
	} {
		b.Run(test.name, func(b *testing.B) {
			stk := stack.New(stack.Options{
				NetworkProtocols: []stack.NetworkProtocolFactory{
					ipv4.NewProtocol,
				},
				TransportProtocols: []stack.TransportProtocolFactory{
					udp.NewProtocol,
				},
			})
			if err := stk.CreateNIC(nicid, eth.NewLinkEndpoint(test.ep)); err != nil {
				b.Fatal(err)
			}
			wq := new(waiter.Queue)
			ep, err := stk.NewEndpoint(udp.ProtocolNumber, ipv4.ProtocolNumber, wq)
			if err != nil {
				b.Fatal(err)
			}

			addr := tcpip.Address(bytes.Repeat([]byte{1}, header.IPv4AddressSize))
			subnet := util.PointSubnet(addr)
			stk.SetRouteTable([]tcpip.Route{
				{
					Destination: subnet,
					NIC:         nicid,
				},
			})
			if err := stk.AddAddress(nicid, ipv4.ProtocolNumber, addr); err != nil {
				b.Fatal(err)
			}
			if err := ep.Connect(tcpip.FullAddress{Port: 8080, Addr: addr, NIC: nicid}); err != nil {
				b.Fatal(err)
			}

			for size := 2048; ; size = size >> 1 {
				payload := tcpip.SlicePayload(make([]byte, size))
				b.Run(fmt.Sprintf("len(payload)=%d", size), func(b *testing.B) {
					for i := 0; i < b.N; i++ {
						if _, _, err := ep.Write(payload, tcpip.WriteOptions{}); err != nil {
							b.Fatal(err)
						}
					}
				})
				if size == 0 {
					break
				}
			}
		})
	}
}
