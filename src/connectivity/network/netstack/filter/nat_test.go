// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package filter

import (
	"fmt"
	"testing"
	"time"

	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/packetbuffer"
	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/util"

	"gvisor.dev/gvisor/pkg/tcpip"
	"gvisor.dev/gvisor/pkg/tcpip/header"
	"gvisor.dev/gvisor/pkg/tcpip/link/sniffer"
	"gvisor.dev/gvisor/pkg/tcpip/network/ipv4"
	"gvisor.dev/gvisor/pkg/tcpip/stack"
	"gvisor.dev/gvisor/pkg/tcpip/transport/tcp"
	"gvisor.dev/gvisor/pkg/tcpip/transport/udp"
	"gvisor.dev/gvisor/pkg/waiter"
)

// TODO: make these tests table-driven.

func makeSubnet(addr string, m tcpip.AddressMask) tcpip.Subnet {
	subnet, err := tcpip.NewSubnet(util.Parse(addr), m)
	if err != nil {
		panic(err)
	}
	return subnet
}

var (
	testLANNet = makeSubnet("192.168.42.0", "\xff\xff\xff\x00")
	testWANNet = makeSubnet("10.0.0.0", "\xff\x00\x00\x00")

	testLANNICAddr     = util.Parse("192.168.42.10")
	testRouterNICAddr1 = util.Parse("192.168.42.1")
	testRouterNICAddr2 = util.Parse("10.0.0.1")
	testWANNICAddr     = util.Parse("10.0.0.2")

	testLANPort    = uint16(10000)
	testRouterPort = uint16(8080)
	testWANPort    = uint16(80)

	testLANNICID     = 1
	testRouterNICID1 = 2
	testRouterNICID2 = 3
	testWANNICID     = 4
)

var _ stack.LinkEndpoint = (*syncEndpoint)(nil)

type syncEndpoint struct {
	dispatcher stack.NetworkDispatcher
	remote     []*syncEndpoint

	stack.LinkEndpoint
}

func (*syncEndpoint) MTU() uint32 {
	return 100
}

func (*syncEndpoint) Capabilities() stack.LinkEndpointCapabilities {
	return 0
}

func (*syncEndpoint) MaxHeaderLength() uint16 {
	return 0
}

func (*syncEndpoint) LinkAddress() tcpip.LinkAddress {
	return tcpip.LinkAddress([]byte(nil))
}

func (e *syncEndpoint) Attach(dispatcher stack.NetworkDispatcher) {
	e.dispatcher = dispatcher
}

func (e *syncEndpoint) IsAttached() bool {
	return e.dispatcher != nil
}

func (e *syncEndpoint) WritePacket(r *stack.Route, _ *stack.GSO, protocol tcpip.NetworkProtocolNumber, pkt *stack.PacketBuffer) *tcpip.Error {
	for _, remote := range e.remote {
		if !remote.IsAttached() {
			panic(fmt.Sprintf("ep: %+v remote endpoint: %+v has not been `Attach`ed; call stack.CreateNIC to attach it", e, remote))
		}
		// the "remote" address for `other` is our local address and vice versa.
		remote.dispatcher.DeliverNetworkPacket(r.LocalLinkAddress, r.RemoteLinkAddress, protocol, packetbuffer.OutboundToInbound(pkt))
	}
	return nil
}

func createTestStackLAN(t *testing.T) (*stack.Stack, *syncEndpoint) {
	s := stack.New(stack.Options{
		NetworkProtocols: []stack.NetworkProtocol{
			ipv4.NewProtocol(),
		},
		TransportProtocols: []stack.TransportProtocol{
			tcp.NewProtocol(),
			udp.NewProtocol(),
		},
	})
	var linkEP syncEndpoint
	nic := tcpip.NICID(testLANNICID)
	{
		var linkEP stack.LinkEndpoint = &linkEP
		if testing.Verbose() {
			linkEP = sniffer.New(linkEP)
		}
		if err := s.CreateNIC(nic, linkEP); err != nil {
			t.Fatalf("CreateNIC error: %s", err)
		}
	}
	s.AddAddress(nic, header.IPv4ProtocolNumber, testLANNICAddr)
	s.SetRouteTable([]tcpip.Route{
		{
			Destination: testLANNet,
			NIC:         nic,
		},
		{
			Destination: testWANNet,
			NIC:         nic,
		},
	})
	return s, &linkEP
}

func createTestStackWAN(t *testing.T) (*stack.Stack, *syncEndpoint) {
	s := stack.New(stack.Options{
		NetworkProtocols: []stack.NetworkProtocol{
			ipv4.NewProtocol(),
		},
		TransportProtocols: []stack.TransportProtocol{
			tcp.NewProtocol(),
			udp.NewProtocol(),
		},
	})
	var linkEP syncEndpoint
	nic := tcpip.NICID(testWANNICID)
	{
		var linkEP stack.LinkEndpoint = &linkEP
		if testing.Verbose() {
			linkEP = sniffer.New(linkEP)
		}
		if err := s.CreateNIC(nic, linkEP); err != nil {
			t.Fatalf("CreateNIC error: %s", err)
		}
	}
	s.AddAddress(nic, header.IPv4ProtocolNumber, testWANNICAddr)
	s.SetRouteTable([]tcpip.Route{
		{
			Destination: testWANNet,
			NIC:         nic,
		},
	})
	return s, &linkEP
}

func createTestStackRouterNAT(t *testing.T) (*stack.Stack, *syncEndpoint, *syncEndpoint) {
	s := stack.New(stack.Options{
		NetworkProtocols: []stack.NetworkProtocol{
			ipv4.NewProtocol(),
		},
		TransportProtocols: []stack.TransportProtocol{
			tcp.NewProtocol(),
			udp.NewProtocol(),
		},
	})

	f := New(s.PortManager)
	f.rulesetNAT.Lock()
	f.rulesetNAT.v = []NAT{
		{
			srcSubnet:  &testLANNet,
			newSrcAddr: testRouterNICAddr2,
		},
	}
	f.rulesetNAT.Unlock()

	var linkEP1 syncEndpoint
	nic1 := tcpip.NICID(testRouterNICID1)
	{
		var linkEP stack.LinkEndpoint = &linkEP1
		if testing.Verbose() {
			linkEP = sniffer.New(linkEP)
		}
		linkEP = NewEndpoint(f, linkEP)
		if err := s.CreateNIC(nic1, linkEP); err != nil {
			t.Fatalf("CreateNIC error: %s", err)
		}
	}
	s.AddAddress(nic1, header.IPv4ProtocolNumber, testRouterNICAddr1)

	var linkEP2 syncEndpoint
	nic2 := tcpip.NICID(testRouterNICID2)
	{
		var linkEP stack.LinkEndpoint = &linkEP2
		if testing.Verbose() {
			linkEP = sniffer.New(linkEP)
		}
		linkEP = NewEndpoint(f, linkEP)
		if err := s.CreateNIC(nic2, linkEP); err != nil {
			t.Fatalf("CreateNIC error: %s", err)
		}
	}
	s.AddAddress(nic2, header.IPv4ProtocolNumber, testRouterNICAddr2)

	s.SetRouteTable([]tcpip.Route{
		{
			Destination: testLANNet,
			NIC:         nic1,
		},
		{
			Destination: testWANNet,
			NIC:         nic2,
		},
	})
	s.SetForwarding(true)
	return s, &linkEP1, &linkEP2
}

func TestNATOneWayLANToWANUDP(t *testing.T) {
	sLAN, sLANLinkEP := createTestStackLAN(t)
	sWAN, sWANLinkEP := createTestStackWAN(t)
	_, sRouterLinkEP1, sRouterLinkEP2 := createTestStackRouterNAT(t)

	linkEndpoints(sLANLinkEP, sRouterLinkEP1)
	linkEndpoints(sRouterLinkEP2, sWANLinkEP)

	var wqLAN waiter.Queue
	epLANUDP, err := sLAN.NewEndpoint(udp.ProtocolNumber, ipv4.ProtocolNumber, &wqLAN)
	if err != nil {
		t.Fatalf("NewEndpoint error: %s", err)
	}
	var wqWAN waiter.Queue
	epWANUDP, err := sWAN.NewEndpoint(udp.ProtocolNumber, ipv4.ProtocolNumber, &wqWAN)
	if err != nil {
		t.Fatalf("NewEndpoint error: %s", err)
	}

	receiverWAN := tcpip.FullAddress{Addr: testWANNICAddr, Port: testWANPort}

	if err := epWANUDP.Bind(receiverWAN); err != nil {
		t.Fatalf("Bind error: %s", err)
	}

	waitEntryWAN, chWAN := waiter.NewChannelEntry(nil)
	wqWAN.EventRegister(&waitEntryWAN, waiter.EventIn)

	if _, _, err := epLANUDP.Write(tcpip.SlicePayload("hello"), tcpip.WriteOptions{To: &receiverWAN}); err != nil {
		t.Fatalf("Write error: %s", err)
	}

	select {
	case <-chWAN:
	case <-time.After(1 * time.Second):
		t.Fatalf("Read timeout")
	}
	wqWAN.EventUnregister(&waitEntryWAN)

	var sender tcpip.FullAddress
	recvd, _, err := epWANUDP.Read(&sender)
	if err != nil {
		t.Fatalf("Read error: %s", err)
	}
	if got, want := sender.Addr, testRouterNICAddr2; got != want {
		t.Errorf("sender.Addr %s, want %s", got, want)
	}
	// sender.Port is random.
	if got, want := string(recvd), "hello"; got != want {
		t.Errorf("got %s, want %s", got, want)
	}
}

func TestNATRoundtripLANToWANUDP(t *testing.T) {
	sLAN, sLANLinkEP := createTestStackLAN(t)
	sWAN, sWANLinkEP := createTestStackWAN(t)
	_, sRouterLinkEP1, sRouterLinkEP2 := createTestStackRouterNAT(t)

	linkEndpoints(sLANLinkEP, sRouterLinkEP1)
	linkEndpoints(sRouterLinkEP2, sWANLinkEP)

	linkEndpoints(sWANLinkEP, sRouterLinkEP2)
	linkEndpoints(sRouterLinkEP1, sLANLinkEP)

	var wqLAN waiter.Queue
	epLANUDP, err := sLAN.NewEndpoint(udp.ProtocolNumber, ipv4.ProtocolNumber, &wqLAN)
	if err != nil {
		t.Fatalf("NewEndpoint error: %s", err)
	}
	var wqWAN waiter.Queue
	epWANUDP, err := sWAN.NewEndpoint(udp.ProtocolNumber, ipv4.ProtocolNumber, &wqWAN)
	if err != nil {
		t.Fatalf("NewEndpoint error: %s", err)
	}

	receiverLAN := tcpip.FullAddress{Addr: testLANNICAddr, Port: testLANPort}
	receiverWAN := tcpip.FullAddress{Addr: testWANNICAddr, Port: testWANPort}

	if err := epLANUDP.Bind(receiverLAN); err != nil {
		t.Fatalf("Bind error: %s", err)
	}
	if err := epWANUDP.Bind(receiverWAN); err != nil {
		t.Fatalf("Bind error: %s", err)
	}

	waitEntryWAN, chWAN := waiter.NewChannelEntry(nil)
	wqWAN.EventRegister(&waitEntryWAN, waiter.EventIn)

	if _, _, err := epLANUDP.Write(tcpip.SlicePayload("hello"), tcpip.WriteOptions{To: &receiverWAN}); err != nil {
		t.Fatalf("Write error: %s", err)
	}

	select {
	case <-chWAN:
	case <-time.After(1 * time.Second):
		t.Fatalf("Read timeout")
	}
	wqWAN.EventUnregister(&waitEntryWAN)

	var sender tcpip.FullAddress
	recvd, _, err := epWANUDP.Read(&sender)
	if err != nil {
		t.Fatalf("Read error: %s", err)
	}
	if got, want := sender.Addr, testRouterNICAddr2; got != want {
		t.Errorf("sender.Addr %s, want %s", got, want)
	}
	// sender.Port is random.
	if got, want := string(recvd), "hello"; got != want {
		t.Errorf("got %s, want %s", got, want)
	}

	waitEntryLAN, chLAN := waiter.NewChannelEntry(nil)
	wqLAN.EventRegister(&waitEntryLAN, waiter.EventIn)

	if _, _, err := epWANUDP.Write(tcpip.SlicePayload("hi"), tcpip.WriteOptions{To: &sender}); err != nil {
		t.Fatalf("Write error: %s", err)
	}

	select {
	case <-chLAN:
	case <-time.After(1 * time.Second):
		t.Fatalf("Read timeout")
	}
	wqLAN.EventUnregister(&waitEntryLAN)

	var sender2 tcpip.FullAddress
	recvd2, _, err := epLANUDP.Read(&sender2)
	if err != nil {
		t.Fatalf("Read error: %s", err)
	}
	if got, want := sender2.Addr, receiverWAN.Addr; got != want {
		t.Errorf("sender2.Addr %s, want %s", got, want)
	}
	if got, want := sender2.Port, receiverWAN.Port; got != want {
		t.Errorf("sender2.Addr %d, want %d", got, want)
	}
	if got, want := string(recvd2), "hi"; got != want {
		t.Errorf("got %s, want %s", got, want)
	}
}

func TestNATLANToWANTCP(t *testing.T) {
	sLAN, sLANLinkEP := createTestStackLAN(t)
	sWAN, sWANLinkEP := createTestStackWAN(t)
	_, sRouterLinkEP1, sRouterLinkEP2 := createTestStackRouterNAT(t)

	linkEndpoints(sLANLinkEP, sRouterLinkEP1)
	linkEndpoints(sRouterLinkEP2, sWANLinkEP)

	linkEndpoints(sWANLinkEP, sRouterLinkEP2)
	linkEndpoints(sRouterLinkEP1, sLANLinkEP)

	var wqLAN waiter.Queue
	epLANTCP, err := sLAN.NewEndpoint(tcp.ProtocolNumber, ipv4.ProtocolNumber, &wqLAN)
	if err != nil {
		t.Fatalf("NewEndpoint error: %s", err)
	}
	var wqWANMaster waiter.Queue
	epWANTCPMaster, err := sWAN.NewEndpoint(tcp.ProtocolNumber, ipv4.ProtocolNumber, &wqWANMaster)
	if err != nil {
		t.Fatalf("NewEndpoint error: %s", err)
	}

	receiverWAN := tcpip.FullAddress{Addr: testWANNICAddr, Port: testWANPort}

	if err := epWANTCPMaster.Bind(receiverWAN); err != nil {
		t.Fatalf("Bind error: %s", err)
	}
	if err := epWANTCPMaster.Listen(10); err != nil {
		t.Fatalf("Bind error: %s", err)
	}

	waitEntryLAN, chLAN := waiter.NewChannelEntry(nil)
	wqLAN.EventRegister(&waitEntryLAN, waiter.EventOut)

	waitEntryWANMaster, chWANMaster := waiter.NewChannelEntry(nil)
	wqWANMaster.EventRegister(&waitEntryWANMaster, waiter.EventIn)

	if err := epLANTCP.Connect(receiverWAN); err != nil {
		if err != tcpip.ErrConnectStarted {
			t.Fatalf("Connect error: %s", err)
		}
	}

	select {
	case <-chWANMaster:
	case <-time.After(1 * time.Second):
		t.Fatalf("Accept timeout")
	}

	epWANTCP, wqWAN, err := epWANTCPMaster.Accept(nil)
	if err != nil {
		t.Fatalf("Accept error: %s", err)
	}
	wqWANMaster.EventUnregister(&waitEntryWANMaster)

	select {
	case <-chLAN:
	case <-time.After(1 * time.Second):
		t.Fatalf("Connect timeout")
	}
	wqLAN.EventUnregister(&waitEntryLAN)

	sender, err := epWANTCP.GetRemoteAddress()
	if got, want := sender.Addr, testRouterNICAddr2; got != want {
		t.Errorf("sender.Addr %s, want %s", got, want)
	}
	// sender.Port is random.

	waitEntryWAN, chWAN := waiter.NewChannelEntry(nil)
	wqWAN.EventRegister(&waitEntryWAN, waiter.EventIn)

	if _, _, err := epLANTCP.Write(tcpip.SlicePayload("hello"), tcpip.WriteOptions{}); err != nil {
		t.Fatalf("Write error: %s", err)
	}

	select {
	case <-chWAN:
	case <-time.After(1 * time.Second):
		t.Fatalf("Read timeout")
	}
	wqWAN.EventUnregister(&waitEntryWAN)

	recvd, _, err := epWANTCP.Read(nil)
	if err != nil {
		t.Fatalf("Read error: %s", err)
	}
	if got, want := string(recvd), "hello"; got != want {
		t.Errorf("got %s, want %s", got, want)
	}

	wqLAN.EventRegister(&waitEntryLAN, waiter.EventIn)

	if _, _, err := epWANTCP.Write(tcpip.SlicePayload("hi"), tcpip.WriteOptions{}); err != nil {
		t.Fatalf("Write error: %s", err)
	}

	select {
	case <-chLAN:
	case <-time.After(1 * time.Second):
		t.Fatalf("Read timeout")
	}
	wqLAN.EventUnregister(&waitEntryLAN)

	recvd2, _, err := epLANTCP.Read(nil)
	if err != nil {
		t.Fatalf("Read error: %s", err)
	}
	if got, want := string(recvd2), "hi"; got != want {
		t.Errorf("got %s, want %s", got, want)
	}
}

func linkEndpoints(a, b *syncEndpoint) {
	a.remote = append(a.remote, b)
	b.remote = append(b.remote, a)
}
