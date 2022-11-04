// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package bridge_test

import (
	"bytes"
	"errors"
	"fmt"
	"strings"
	"testing"
	"time"

	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/link/bridge"
	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/util"

	"gvisor.dev/gvisor/pkg/bufferv2"
	"gvisor.dev/gvisor/pkg/tcpip"
	"gvisor.dev/gvisor/pkg/tcpip/header"
	"gvisor.dev/gvisor/pkg/tcpip/link/ethernet"
	"gvisor.dev/gvisor/pkg/tcpip/link/loopback"
	"gvisor.dev/gvisor/pkg/tcpip/link/pipe"
	"gvisor.dev/gvisor/pkg/tcpip/link/sniffer"
	"gvisor.dev/gvisor/pkg/tcpip/network/arp"
	"gvisor.dev/gvisor/pkg/tcpip/network/ipv4"
	"gvisor.dev/gvisor/pkg/tcpip/network/ipv6"
	"gvisor.dev/gvisor/pkg/tcpip/stack"
	"gvisor.dev/gvisor/pkg/tcpip/transport/tcp"
	"gvisor.dev/gvisor/pkg/waiter"
)

const (
	// 0xFFFF is a reserved ethertype value.
	fakeNetworkProtocol = 0xffff

	linkAddr1 = tcpip.LinkAddress("\x02\x03\x04\x05\x06\x07")
	linkAddr2 = tcpip.LinkAddress("\x02\x03\x04\x05\x06\x08")
	linkAddr3 = tcpip.LinkAddress("\x02\x03\x04\x05\x06\x09")
	linkAddr4 = tcpip.LinkAddress("\x02\x03\x04\x05\x06\x0a")
	linkAddr5 = tcpip.LinkAddress("\x02\x03\x04\x05\x06\x0b")
	linkAddr6 = tcpip.LinkAddress("\x02\x03\x04\x05\x06\x0c")
)

var (
	timeoutReceiveReady    = errors.New("receiveready")
	timeoutSendReady       = errors.New("sendready")
	timeoutPayloadReceived = errors.New("payloadreceived")
)

type endpointWithAttributes struct {
	stack.LinkEndpoint
	capabilities    stack.LinkEndpointCapabilities
	maxHeaderLength uint16
}

func (ep *endpointWithAttributes) Capabilities() stack.LinkEndpointCapabilities {
	return ep.LinkEndpoint.Capabilities() | ep.capabilities
}

func (ep *endpointWithAttributes) MaxHeaderLength() uint16 {
	return ep.LinkEndpoint.MaxHeaderLength() + ep.maxHeaderLength
}

func TestEndpointAttributes(t *testing.T) {
	ep1 := bridge.NewEndpoint(&endpointWithAttributes{
		LinkEndpoint:    loopback.New(),
		capabilities:    stack.CapabilityLoopback,
		maxHeaderLength: 5,
	})
	ep2 := bridge.NewEndpoint(&endpointWithAttributes{
		LinkEndpoint:    loopback.New(),
		capabilities:    stack.CapabilityLoopback | stack.CapabilityResolutionRequired,
		maxHeaderLength: 10,
	})
	bridgeEP, err := bridge.New([]*bridge.BridgeableEndpoint{ep1, ep2})
	if err != nil {
		t.Fatalf("failed to create bridge: %s", err)
	}

	if got, want := bridgeEP.Capabilities(), stack.CapabilityResolutionRequired; got != want {
		t.Errorf("got Capabilities = %b, want = %b", got, want)
	}

	if got, want := bridgeEP.MaxHeaderLength(), ep2.MaxHeaderLength(); got != want {
		t.Errorf("got MaxHeaderLength = %d, want = %d", got, want)
	}

	if got, want := bridgeEP.MTU(), ep2.MTU(); got != want {
		t.Errorf("got MTU = %d, want = %d", got, want)
	}

	if linkAddr := bridgeEP.LinkAddress(); linkAddr[0]&0x2 == 0 {
		t.Errorf("bridge.LinkAddress() expected to be locally administered MAC address, got: %s", linkAddr)
	}
}

var _ stack.NetworkDispatcher = (*testNetworkDispatcher)(nil)

type testNetworkDispatcher struct {
	pkt   stack.PacketBufferPtr
	count int
}

func (t *testNetworkDispatcher) release() {
	if pkt := t.pkt; pkt != (stack.PacketBufferPtr{}) {
		pkt.DecRef()
	}

	*t = testNetworkDispatcher{}
}

func (t *testNetworkDispatcher) takePkt() stack.PacketBufferPtr {
	pkt := t.pkt
	t.pkt = stack.PacketBufferPtr{}
	return pkt
}

func (t *testNetworkDispatcher) DeliverNetworkPacket(_ tcpip.NetworkProtocolNumber, pkt stack.PacketBufferPtr) {
	t.count++

	if pkt := t.pkt; pkt != (stack.PacketBufferPtr{}) {
		pkt.DecRef()
	}

	pkt.IncRef()
	t.pkt = pkt
}

func (*testNetworkDispatcher) DeliverLinkPacket(tcpip.NetworkProtocolNumber, stack.PacketBufferPtr, bool) {
	panic("not implemented")
}

var _ stack.LinkEndpoint = (*stubEndpoint)(nil)

// A stack.LinkEndpoint implementation which queues packets written to it so
// that they can be retrieved and asserted upon later.
type stubEndpoint struct {
	linkAddr tcpip.LinkAddress
	c        chan stack.PacketBufferPtr
}

func (*stubEndpoint) MTU() uint32 {
	return 65535
}

func (*stubEndpoint) MaxHeaderLength() uint16 {
	return 0
}

func (e *stubEndpoint) LinkAddress() tcpip.LinkAddress {
	return e.linkAddr
}

func (*stubEndpoint) Capabilities() stack.LinkEndpointCapabilities {
	return 0
}

func (*stubEndpoint) Attach(dispatcher stack.NetworkDispatcher) {
	panic("Attach unimplemented")
}

func (*stubEndpoint) IsAttached() bool {
	panic("IsAttached unimplemented")
}

func (*stubEndpoint) Wait() {
	panic("Wait unimplemented")
}

func (*stubEndpoint) ARPHardwareType() header.ARPHardwareType {
	panic("ARPHardwareType unimplemented")
}

func (*stubEndpoint) AddHeader(stack.PacketBufferPtr) {}

func (e *stubEndpoint) WritePackets(pkts stack.PacketBufferList) (int, tcpip.Error) {
	i := 0
	for _, pkt := range pkts.AsSlice() {
		select {
		case e.c <- pkt:
			pkt.IncRef()
			i++
		default:
			return i, &tcpip.ErrWouldBlock{}
		}
	}

	return i, nil
}

func (e *stubEndpoint) release() {
	c := e.c
	*e = stubEndpoint{}
	close(c)
	for p := range c {
		p.DecRef()
	}
}

func (e *stubEndpoint) getPacket() stack.PacketBufferPtr {
	select {
	case pkt := <-e.c:
		return pkt
	default:
		return stack.PacketBufferPtr{}
	}
}

func makeStubEndpoint(linkAddr tcpip.LinkAddress, size int) stubEndpoint {
	return stubEndpoint{
		linkAddr: linkAddr,
		c:        make(chan stack.PacketBufferPtr, size),
	}
}

// Raises a failure if `pkt` is nil or does not contain an Ethernet header with
// matching fields.
func expectPacket(t *testing.T, name string, pkt stack.PacketBufferPtr, wantSrc, wantDst tcpip.LinkAddress, wantProto tcpip.NetworkProtocolNumber, wantData []byte) {
	t.Helper()
	if pkt == (stack.PacketBufferPtr{}) {
		t.Errorf("%s: no packet received", name)
		return
	}
	eth := header.Ethernet(pkt.LinkHeader().Slice())
	if got := eth.SourceAddress(); got != wantSrc {
		t.Errorf("%s: got src = %s, want = %s", name, got, wantSrc)
	}
	if got := eth.DestinationAddress(); got != wantDst {
		t.Errorf("%s: got dst = %s, want = %s", name, got, wantDst)
	}
	if got := eth.Type(); got != wantProto {
		t.Errorf("%s: got ethertype = %d, want = %d", name, got, wantProto)
	}
	if got := pkt.Data().AsRange().ToSlice(); !bytes.Equal(got, wantData) {
		t.Errorf("%s: got data = %x, want = %x", name, got, wantData)
	}
}

func TestBridgeWithoutDispatcher(t *testing.T) {
	ep := makeStubEndpoint(linkAddr1, 0)
	defer ep.release()
	// DeliverNetworkPacketToBridge reads link addresses from the ethernet
	// header so make sure we are able to populate one by wrapping the
	// stub endpoint with an ethernet link endpoint.
	bep := bridge.NewEndpoint(ethernet.New(&ep))
	bridgeEP, err := bridge.New([]*bridge.BridgeableEndpoint{bep})
	if err != nil {
		t.Fatalf("failed to create bridge: %s", err)
	}

	tests := []struct {
		name        string
		dstLinkAddr tcpip.LinkAddress
	}{
		{
			name:        "To bridge",
			dstLinkAddr: bridgeEP.LinkAddress(),
		},
		{
			name:        "Flood",
			dstLinkAddr: header.EthernetBroadcastAddress,
		},
	}

	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			pkt := stack.NewPacketBuffer(stack.PacketBufferOptions{
				ReserveHeaderBytes: int(bridgeEP.MaxHeaderLength()),
			})
			defer pkt.DecRef()

			pkt.EgressRoute.LocalLinkAddress = linkAddr2
			pkt.EgressRoute.RemoteLinkAddress = test.dstLinkAddr
			bridgeEP.AddHeader(pkt)

			bridgeEP.DeliverNetworkPacketToBridge(nil /* rxEP */, 0 /* protocol */, pkt)
		})
	}
}

// TestBridgeWritePackets tests that writing to a bridge writes the packets to
// all bridged endpoints.
func TestBridgeWritePackets(t *testing.T) {
	data := [][]byte{{1, 2, 3, 4}, {5, 6, 7, 8}, {9, 10, 11, 12}}

	eps := []stubEndpoint{
		makeStubEndpoint(linkAddr1, len(data)),
		makeStubEndpoint(linkAddr2, len(data)),
		makeStubEndpoint(linkAddr3, len(data)),
	}
	defer func() {
		for _, e := range eps {
			e.release()
		}
	}()

	bridgeEP, err := bridge.New([]*bridge.BridgeableEndpoint{
		bridge.NewEndpoint(ethernet.New(&eps[0])),
		bridge.NewEndpoint(ethernet.New(&eps[1])),
		bridge.NewEndpoint(ethernet.New(&eps[2])),
	})
	if err != nil {
		t.Fatalf("failed to create bridge: %s", err)
	}
	baddr := bridgeEP.LinkAddress()

	for i := 0; i <= len(data); i++ {
		t.Run(fmt.Sprintf("WritePackets(N=%d)", i), func(t *testing.T) {
			var pkts stack.PacketBufferList
			defer pkts.DecRef()

			dstAddr := linkAddr5

			for j := 0; j < i; j++ {
				pkt := stack.NewPacketBuffer(stack.PacketBufferOptions{
					ReserveHeaderBytes: int(bridgeEP.MaxHeaderLength()),
					Payload:            bufferv2.MakeWithData(data[j]),
				})
				pkt.EgressRoute.LocalLinkAddress = baddr
				pkt.EgressRoute.RemoteLinkAddress = dstAddr
				pkt.NetworkProtocolNumber = fakeNetworkProtocol
				bridgeEP.AddHeader(pkt)
				pkts.PushBack(pkt)
			}

			got, err := bridgeEP.WritePackets(pkts)
			if err != nil {
				t.Errorf("bridgeEP.WritePackets(_): %s", err)
			}
			if got != i {
				t.Errorf("got bridgeEP.WritePackets(_) = %d, want = %d", got, i)
			}

			for j := 0; j < i; j++ {
				for id, ep := range eps {
					func() {
						pkt := ep.getPacket()
						defer pkt.DecRef()
						expectPacket(t, fmt.Sprintf("ep%d", id), pkt, baddr, dstAddr, fakeNetworkProtocol, data[j])
					}()
				}
			}
		})
	}
}

// TestDeliverNetworkPacketToBridge makes sure that frames are directed to the right unicast
// endpoint or floods all endpoints for multicast and broadcast frames.
func TestDeliverNetworkPacketToBridge(t *testing.T) {
	eps := []stubEndpoint{
		makeStubEndpoint(linkAddr1, 1),
		makeStubEndpoint(linkAddr2, 1),
	}
	defer func() {
		for _, e := range eps {
			e.release()
		}
	}()

	beps := []*bridge.BridgeableEndpoint{
		bridge.NewEndpoint(ethernet.New(&eps[0])),
		bridge.NewEndpoint(ethernet.New(&eps[1])),
	}

	bridgeEP, err := bridge.New(beps)
	if err != nil {
		t.Fatalf("failed to create bridge: %s", err)
	}

	data := []byte{1, 2, 3, 4}

	tests := []struct {
		name string
		rxEP *bridge.BridgeableEndpoint
	}{
		{
			name: "FromNil",
			rxEP: nil,
		},
		{
			name: "FromEP0",
			rxEP: beps[0],
		},
		{
			name: "FromEP1",
			rxEP: beps[1],
		},
	}

	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			subtests := []struct {
				name    string
				dstAddr tcpip.LinkAddress
			}{
				{
					name:    "ToMulticast",
					dstAddr: "\x01\x03\x04\x05\x06\x07",
				},
				{
					name:    "ToBroadcast",
					dstAddr: header.EthernetBroadcastAddress,
				},
				{
					name:    "ToEP0",
					dstAddr: eps[0].LinkAddress(),
				},
				{
					name:    "ToEP1",
					dstAddr: eps[1].LinkAddress(),
				},
				{
					name:    "ToBridge",
					dstAddr: bridgeEP.LinkAddress(),
				},
				{
					name:    "ToOther",
					dstAddr: linkAddr4,
				},
			}

			for _, subtest := range subtests {
				t.Run(subtest.name, func(t *testing.T) {
					var ndb testNetworkDispatcher
					defer ndb.release()
					bridgeEP.Attach(&ndb)

					srcAddr := linkAddr3
					pkt := stack.NewPacketBuffer(stack.PacketBufferOptions{
						ReserveHeaderBytes: int(bridgeEP.MaxHeaderLength()),
						Payload:            bufferv2.MakeWithData(data),
					})
					eth := header.Ethernet(pkt.LinkHeader().Push(header.EthernetMinimumSize))
					fields := header.EthernetFields{
						SrcAddr: srcAddr,
						DstAddr: subtest.dstAddr,
						Type:    fakeNetworkProtocol,
					}
					eth.Encode(&fields)
					bridgeEP.DeliverNetworkPacketToBridge(test.rxEP, fakeNetworkProtocol, pkt)

					for i, ep := range eps {
						// An endpoint on the bridge should receive all packets that do not come
						// from itself and are not destined to the bridge itself.
						func() {
							pkt := ep.getPacket()
							if pkt != (stack.PacketBufferPtr{}) {
								defer func() {
									pkt.DecRef()
								}()
							}

							if test.rxEP != beps[i] && subtest.dstAddr != bridgeEP.LinkAddress() {
								expectPacket(t, fmt.Sprintf("ep%d", i), pkt, srcAddr, subtest.dstAddr, fakeNetworkProtocol, data)
							} else if pkt != (stack.PacketBufferPtr{}) {
								t.Errorf("ep%d unexpectedly got a packet = %+v", i, pkt)
							}
						}()
					}

					// The bridge should deliver packets destined to a group address or itself.
					if subtest.dstAddr == bridgeEP.LinkAddress() || header.IsMulticastEthernetAddress(subtest.dstAddr) {
						if ndb.count != 1 {
							t.Errorf("got ndb.count = %d, want = 1", ndb.count)
						} else {
							func() {
								pkt := ndb.takePkt()
								defer pkt.DecRef()
								expectPacket(t, "bridge-dispatcher", pkt, srcAddr, subtest.dstAddr, fakeNetworkProtocol, data)
							}()
						}
					} else if ndb.count != 0 {
						t.Errorf("got ndb.count = %d, want = 0", ndb.count)
					}
				})
			}
		})
	}
}

func TestBridge(t *testing.T) {
	const (
		s1NICID = 1
		s2NICID = 10

		sbOtherNICID = 9000
	)

	for _, testCase := range []struct {
		name            string
		protocolFactory stack.NetworkProtocolFactory
		protocolNumber  tcpip.NetworkProtocolNumber
		addressSize     int
	}{
		{name: "ipv4", protocolFactory: ipv4.NewProtocol, protocolNumber: ipv4.ProtocolNumber, addressSize: header.IPv4AddressSize},
		{name: "ipv6", protocolFactory: ipv6.NewProtocol, protocolNumber: ipv6.ProtocolNumber, addressSize: header.IPv6AddressSize},
	} {
		t.Run(testCase.name, func(t *testing.T) {
			// payload should be unique enough that it won't accidentally appear
			// in TCP/IP packets.
			const payload = "hello"

			// Connection diagram:
			//
			// <---> ep1 <--pipe--> ep2 <--bridge--> ep3 <--pipe--> ep4
			//
			// Included are several additional endpoints to ensure bridging N > 2
			// endpoints works.
			ep1, ep2 := makePipe(linkAddr1, linkAddr2)
			ep3, ep4 := makePipe(linkAddr3, linkAddr4)
			ep5, ep6 := makePipe(linkAddr5, linkAddr6)
			s1addr := tcpip.Address(bytes.Repeat([]byte{1}, testCase.addressSize))
			s1subnet := util.PointSubnet(s1addr)
			s1, err := makeStackWithEndpoint(s1NICID, ep1, testCase.protocolFactory, testCase.protocolNumber, s1addr)
			if err != nil {
				t.Fatal(err)
			}

			baddr := tcpip.Address(bytes.Repeat([]byte{2}, testCase.addressSize))
			bsubnet := util.PointSubnet(baddr)
			sb, b, bridgeNICID := makeStackWithBridgedEndpoints(t, testCase.protocolFactory, testCase.protocolNumber, baddr, ep5, ep2, ep3)

			if err := sb.CreateNIC(sbOtherNICID, ep6); err != nil {
				t.Fatal(err)
			}

			if err := b.Up(); err != nil {
				t.Fatal(err)
			}

			s2addr := tcpip.Address(bytes.Repeat([]byte{3}, testCase.addressSize))
			s2subnet := util.PointSubnet(s2addr)
			s2, err := makeStackWithEndpoint(s2NICID, ep4, testCase.protocolFactory, testCase.protocolNumber, s2addr)
			if err != nil {
				t.Fatal(err)
			}

			// Make sure s1 can communicate with all the addresses we configured
			// above.
			s1.SetRouteTable([]tcpip.Route{
				{
					Destination: s2subnet,
					NIC:         s1NICID,
				},
				{
					Destination: bsubnet,
					NIC:         s1NICID,
				},
			})
			sb.SetRouteTable([]tcpip.Route{
				{
					Destination: s1subnet,
					NIC:         bridgeNICID,
				},
			})
			s2.SetRouteTable(
				[]tcpip.Route{
					{
						Destination: s1subnet,
						NIC:         s2NICID,
					},
				},
			)

			addrs := map[tcpip.Address]*stack.Stack{
				s2addr: s2,
				baddr:  sb,
			}

			stacks := map[string]*stack.Stack{
				"s1": s1, "s2": s2, "sb": sb,
			}

			ep2.onWritePacket = func(pkt stack.PacketBufferPtr) {
				i := 0
				buf := pkt.Data().ToBuffer()
				buf.Apply(func(view *bufferv2.View) {
					if view := view.AsSlice(); bytes.Contains(view, []byte(payload)) {
						t.Errorf("did not expect payload %x to be sent back to ep1 in view %d: %x", payload, i, view)
					}

					i++
				})
			}

			for addr, toStack := range addrs {
				t.Run(fmt.Sprintf("ConnectAndWrite_%s", addr), func(t *testing.T) {
					recvd, err := connectAndWrite(s1, toStack, testCase.protocolNumber, addr, payload)
					if err != nil {
						t.Fatal(err)
					}

					if !bytes.Equal(recvd, []byte(payload)) {
						t.Errorf("got Read(...) = %x, want = %x", recvd, payload)
					}

					for name, s := range stacks {
						sumCounts := func(counterMap *tcpip.IntegralStatCounterMap) uint64 {
							var sum uint64
							for _, key := range counterMap.Keys() {
								if counter, ok := counterMap.Get(key); ok {
									sum += counter.Value()
								}
							}
							return sum
						}
						stats := s.Stats()
						if n := sumCounts(stats.NICs.UnknownL3ProtocolRcvdPacketCounts); n != 0 {
							t.Errorf("stack %s received %d UnknownL3ProtocolRcvdPackets", name, n)
						}
						if n := sumCounts(stats.NICs.UnknownL4ProtocolRcvdPacketCounts); n != 0 {
							t.Errorf("stack %s received %d UnknownL4ProtocolRcvdPackets", name, n)
						}
						if n := stats.NICs.MalformedL4RcvdPackets.Value(); n != 0 {
							t.Errorf("stack %s received %d MalformedL4RcvdPackets", name, n)
						}
						if n := stats.DroppedPackets.Value(); n != 0 {
							t.Errorf("stack %s received %d DroppedPackets", name, n)
						}

						// The invalid address counter counts packets that have been received
						// by a stack correctly addressed at the link layer but incorrectly
						// addressed at the network layer (e.g. no network interface has the
						// address listed in the packet). This usually happens because
						// the stack is being sent packets for an IP address that it used to
						// have but doesn't have anymore.  In this case, the bridge will
						// forward a packet to all constituent links when the link address that
						// the packet is addressed to isn't found on the bridge.
						//
						// TODO(https://fxbug.dev/20778): When we implement learning, we
						// should be able to modify this test setup to get to zero invalid
						// addresses received. With the current test setup, once learning
						// is implemented, the bridge would indiscriminately forward the
						// first packet addressed to a link address to all constituent links
						// (causing #links - 1 invalid addresses received), observe which
						// link the response packet came from, and then remember which
						// link to forward to when the next packet addressed to that link
						// address was received. We might be able to get to zero invalid
						// addresses received by learning which links a given address is
						// on via the broadcast packets sent during ARP.
						// if n := stats.IP.InvalidAddressesReceived.Value(); n != 0 {
						//   t.Errorf("stack %s received %d InvalidAddressesReceived", name, n)
						// }
						if n := stats.IP.OutgoingPacketErrors.Value(); n != 0 {
							t.Errorf("stack %s received %d OutgoingPacketErrors", name, n)
						}
						if n := stats.TCP.FailedConnectionAttempts.Value(); n != 0 {
							t.Errorf("stack %s received %d FailedConnectionAttempts", name, n)
						}
						if n := stats.TCP.InvalidSegmentsReceived.Value(); n != 0 {
							t.Errorf("stack %s received %d InvalidSegmentsReceived", name, n)
						}
						if n := stats.TCP.ResetsSent.Value(); n != 0 {
							t.Errorf("stack %s received %d ResetsSent", name, n)
						}
						if n := stats.TCP.ResetsReceived.Value(); n != 0 {
							t.Errorf("stack %s received %d ResetsReceived", name, n)
						}
					}
				})
			}

			b.Attach(nil)

			// verify that the endpoint from the constituent link on sb is still accessible
			// and the bridge endpoint and endpoint on s2 are no longer accessible from s1
			noLongerConnectable := map[tcpip.Address]*stack.Stack{
				s2addr: s2,
				baddr:  sb,
			}

			for addr, toStack := range noLongerConnectable {
				t.Run(addr.String(), func(t *testing.T) {
					senderWaitQueue := new(waiter.Queue)
					sender, err := s1.NewEndpoint(tcp.ProtocolNumber, testCase.protocolNumber, senderWaitQueue)
					if err != nil {
						t.Fatalf("NewEndpoint failed: %s", err)
					}
					defer sender.Close()

					receiverWaitQueue := new(waiter.Queue)
					receiver, err := toStack.NewEndpoint(tcp.ProtocolNumber, testCase.protocolNumber, receiverWaitQueue)
					if err != nil {
						t.Fatalf("NewEndpoint failed: %s", err)
					}
					defer receiver.Close()

					if err := receiver.Bind(tcpip.FullAddress{Addr: addr}); err != nil {
						t.Fatalf("bind failed: %s", err)
					}
					if err := receiver.Listen(1); err != nil {
						t.Fatalf("listen failed: %s", err)
					}
					addr, err := receiver.GetLocalAddress()
					if err != nil {
						t.Fatalf("getlocaladdress failed: %s", err)
					}
					addr.NIC = 0

					if err := connect(sender, addr, senderWaitQueue, receiverWaitQueue); err != timeoutSendReady {
						t.Errorf("expected timeout sendready, got %v connecting to addr %+v", err, addr)
					}
				})
			}
		})
	}
}

// TestBridgeableEndpointDetach tests that bridgeable endpoints don't cause
// panics after attaching to a nil dispatcher.
func TestBridgeableEndpointDetach(t *testing.T) {
	ep1 := loopback.New()
	bep1 := bridge.NewEndpoint(ep1)
	var disp testNetworkDispatcher
	defer disp.release()

	if ep1.IsAttached() {
		t.Fatal("ep1.IsAttached() = true, want = false")
	}
	if bep1.IsAttached() {
		t.Fatal("bep1.IsAttached() = true, want = false")
	}

	bep1.Attach(&disp)
	if disp.count != 0 {
		t.Fatalf("got disp.count = %d, want = 0", disp.count)
	}
	if !ep1.IsAttached() {
		t.Fatal("ep1.IsAttached() = false, want = true")
	}
	if !bep1.IsAttached() {
		t.Fatal("bep1.IsAttached() = false, want = true")
	}

	func() {
		pkt := stack.NewPacketBuffer(stack.PacketBufferOptions{})
		defer pkt.DecRef()
		bep1.DeliverNetworkPacket(header.IPv4ProtocolNumber, pkt)
		if disp.count != 1 {
			t.Fatalf("got disp.count = %d, want = 1", disp.count)
		}
	}()

	bep1.Attach(nil)
	if ep1.IsAttached() {
		t.Fatal("ep1.IsAttached() = true, want = false")
	}
	if bep1.IsAttached() {
		t.Fatal("bep1.IsAttached() = true, want = false")
	}

	func() {
		pkt := stack.NewPacketBuffer(stack.PacketBufferOptions{})
		defer pkt.DecRef()
		bep1.DeliverNetworkPacket(header.IPv4ProtocolNumber, pkt)
		if disp.count != 1 {
			t.Fatalf("got disp.count = %d, want = 1", disp.count)
		}
	}()
}

// makePipe mints two linked endpoints with the given link addresses.
func makePipe(addr1, addr2 tcpip.LinkAddress) (*endpoint, *endpoint) {
	ep1, ep2 := pipe.New(addr1, addr2, header.IPv6MinimumMTU+header.EthernetMinimumSize)
	return &endpoint{LinkEndpoint: ethernet.New(ep1)}, &endpoint{LinkEndpoint: ethernet.New(ep2)}
}

var _ stack.LinkEndpoint = (*endpoint)(nil)

// `endpoint` cannot be copied.
//
// Make endpoints using `makePipe()`, not using endpoint literals.
type endpoint struct {
	stack.LinkEndpoint
	onWritePacket func(stack.PacketBufferPtr)
}

func (e *endpoint) WritePackets(pkts stack.PacketBufferList) (int, tcpip.Error) {
	for _, pkt := range pkts.AsSlice() {
		if fn := e.onWritePacket; fn != nil {
			fn(pkt)
		}
	}
	return e.LinkEndpoint.WritePackets(pkts)
}

func makeStackWithEndpoint(nicID tcpip.NICID, ep stack.LinkEndpoint, protocolFactory stack.NetworkProtocolFactory, protocolNumber tcpip.NetworkProtocolNumber, addr tcpip.Address) (*stack.Stack, error) {
	if testing.Verbose() {
		ep = sniffer.New(ep)
	}

	s := stack.New(stack.Options{
		NetworkProtocols: []stack.NetworkProtocolFactory{
			arp.NewProtocol,
			protocolFactory,
		},
		TransportProtocols: []stack.TransportProtocolFactory{
			tcp.NewProtocol,
		},
	})
	if err := s.CreateNIC(nicID, ep); err != nil {
		return nil, fmt.Errorf("CreateNIC failed: %s", err)
	}
	protocolAddress := tcpip.ProtocolAddress{
		Protocol:          protocolNumber,
		AddressWithPrefix: addr.WithPrefix(),
	}
	if err := s.AddProtocolAddress(nicID, protocolAddress, stack.AddressProperties{}); err != nil {
		return nil, fmt.Errorf("AddProtocolAddress(%d, %#v, {}): %s", nicID, protocolAddress, err)
	}
	return s, nil
}

func makeStackWithBridgedEndpoints(t *testing.T, protocolFactory stack.NetworkProtocolFactory, protocolNumber tcpip.NetworkProtocolNumber, baddr tcpip.Address, eps ...stack.LinkEndpoint) (*stack.Stack, *bridge.Endpoint, tcpip.NICID) {
	t.Helper()
	if testing.Verbose() {
		for i := range eps {
			eps[i] = sniffer.New(eps[i])
		}
	}

	stk := stack.New(stack.Options{
		NetworkProtocols: []stack.NetworkProtocolFactory{
			arp.NewProtocol,
			protocolFactory,
		},
		TransportProtocols: []stack.TransportProtocolFactory{
			tcp.NewProtocol,
		},
	})

	beps := make([]*bridge.BridgeableEndpoint, len(eps))
	for i, ep := range eps {
		bep := bridge.NewEndpoint(ep)
		nicid := tcpip.NICID(i + 1)
		options := stack.NICOptions{Disabled: true}
		if err := stk.CreateNICWithOptions(nicid, bep, options); err != nil {
			t.Fatalf("CreateNICWithOptions(%d, _, %+v) failed: %s", nicid, options, err)
		}
		beps[i] = bep
	}

	bridgeEP, err := bridge.New(beps)
	if err != nil {
		t.Fatalf("failed to create bridge: %s", err)
	}
	for _, bep := range beps {
		bep.SetBridge(bridgeEP)
	}
	var bridgeLinkEP stack.LinkEndpoint = bridgeEP
	if testing.Verbose() {
		bridgeLinkEP = sniffer.New(bridgeLinkEP)
	}
	bID := tcpip.NICID(len(beps) + 1)
	if err := stk.CreateNIC(bID, bridgeLinkEP); err != nil {
		t.Fatalf("CreateNIC failed: %s", err)
	}
	protocolAddress := tcpip.ProtocolAddress{
		Protocol:          protocolNumber,
		AddressWithPrefix: baddr.WithPrefix(),
	}
	if err := stk.AddProtocolAddress(bID, protocolAddress, stack.AddressProperties{}); err != nil {
		t.Fatalf("AddProtocolAddress(%d, %#v, {}): %s", bID, protocolAddress, err)
	}

	return stk, bridgeEP, bID
}

func connectAndWrite(fromStack *stack.Stack, toStack *stack.Stack, protocolNumber tcpip.NetworkProtocolNumber, addr tcpip.Address, payload string) ([]byte, error) {
	senderWaitQueue := new(waiter.Queue)
	sender, err := fromStack.NewEndpoint(tcp.ProtocolNumber, protocolNumber, senderWaitQueue)
	if err != nil {
		return nil, fmt.Errorf("NewEndpoint failed: %s", err)
	}
	defer sender.Close()

	receiverWaitQueue := new(waiter.Queue)
	receiver, err := toStack.NewEndpoint(tcp.ProtocolNumber, protocolNumber, receiverWaitQueue)
	if err != nil {
		return nil, fmt.Errorf("NewEndpoint failed: %s", err)
	}
	defer receiver.Close()

	if err := receiver.Bind(tcpip.FullAddress{Addr: addr}); err != nil {
		return nil, fmt.Errorf("bind failed: %s", err)
	}
	if err := receiver.Listen(1); err != nil {
		return nil, fmt.Errorf("listen failed: %s", err)
	}
	{
		addr, err := receiver.GetLocalAddress()
		if err != nil {
			return nil, fmt.Errorf("getlocaladdress failed: %s", err)
		}
		addr.NIC = 0

		if err := connect(sender, addr, senderWaitQueue, receiverWaitQueue); err != nil {
			return nil, fmt.Errorf("connect failed: %s\n\n%+v\n\n%+v", err, fromStack.Stats(), toStack.Stats())
		}

		ep, wq, err := receiver.Accept(nil)
		if err != nil {
			return nil, fmt.Errorf("accept failed: %s", err)
		}

		if err := write(sender, addr, payload, wq); err != nil {
			return nil, err
		}

		var recvd bytes.Buffer
		if _, err := ep.Read(&recvd, tcpip.ReadOptions{}); err != nil {
			return nil, fmt.Errorf("read failed: %s", err)
		}
		return recvd.Bytes(), nil
	}
}

func write(sender tcpip.Endpoint, s2fulladdr tcpip.FullAddress, payload string, wq *waiter.Queue) error {
	payloadReceivedWaitEntry, payloadReceivedNotifyCh := waiter.NewChannelEntry(waiter.EventIn)
	wq.EventRegister(&payloadReceivedWaitEntry)
	defer wq.EventUnregister(&payloadReceivedWaitEntry)
	var r strings.Reader
	r.Reset(payload)
	if _, err := sender.Write(&r, tcpip.WriteOptions{To: &s2fulladdr}); err != nil {
		return fmt.Errorf("write failed: %s", err)
	}
	select {
	case <-payloadReceivedNotifyCh:
	case <-time.After(1 * time.Second):
		return timeoutPayloadReceived
	}
	return nil
}

func connect(sender tcpip.Endpoint, addr tcpip.FullAddress, senderWaitQueue, receiverWaitQueue *waiter.Queue) error {
	sendReadyWaitEntry, sendReadyNotifyCh := waiter.NewChannelEntry(waiter.EventOut)
	senderWaitQueue.EventRegister(&sendReadyWaitEntry)
	defer senderWaitQueue.EventUnregister(&sendReadyWaitEntry)

	receiveReadyWaitEntry, receiveReadyNotifyCh := waiter.NewChannelEntry(waiter.EventIn)
	receiverWaitQueue.EventRegister(&receiveReadyWaitEntry)
	defer receiverWaitQueue.EventUnregister(&receiveReadyWaitEntry)

	switch err := sender.Connect(addr); err.(type) {
	case *tcpip.ErrConnectStarted:
	default:
		return fmt.Errorf("connect failed: %s", err)
	}

	select {
	case <-sendReadyNotifyCh:
	case <-time.After(1 * time.Second):
		return timeoutSendReady
	}
	select {
	case <-receiveReadyNotifyCh:
	case <-time.After(1 * time.Second):
		return timeoutReceiveReady
	}

	return nil
}
