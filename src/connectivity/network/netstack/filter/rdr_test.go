// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package filter

import (
	"testing"
	"time"

	"gvisor.dev/gvisor/pkg/tcpip"
	"gvisor.dev/gvisor/pkg/tcpip/header"
	"gvisor.dev/gvisor/pkg/tcpip/link/sniffer"
	"gvisor.dev/gvisor/pkg/tcpip/network/ipv4"
	"gvisor.dev/gvisor/pkg/tcpip/stack"
	"gvisor.dev/gvisor/pkg/tcpip/transport/tcp"
	"gvisor.dev/gvisor/pkg/tcpip/transport/udp"
	"gvisor.dev/gvisor/pkg/waiter"
)

func createTestStackRouterRDR(t *testing.T) (*stack.Stack, *syncEndpoint, *syncEndpoint) {
	s := stack.New(stack.Options{
		NetworkProtocols: []stack.NetworkProtocol{
			ipv4.NewProtocol(),
		},
		TransportProtocols: []stack.TransportProtocol{
			udp.NewProtocol(),
		},
	})

	f := New(s.PortManager)
	f.rulesetRDR.Lock()
	f.rulesetRDR.v = []RDR{
		{
			dstAddr:         testRouterNICAddr2,
			dstPortRange:    PortRange{testRouterPort, testRouterPort},
			newDstAddr:      testLANNICAddr,
			newDstPortRange: PortRange{testLANPort, testLANPort},
		},
	}
	f.rulesetRDR.Unlock()

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

func TestRDROneWayWANToLANUDP(t *testing.T) {
	sLAN, sLANLinkEP := createTestStackLAN(t)
	sWAN, sWANLinkEP := createTestStackWAN(t)
	_, sRouterLinkEP1, sRouterLinkEP2 := createTestStackRouterRDR(t)

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
	receiverRouter := tcpip.FullAddress{Addr: testRouterNICAddr2, Port: testRouterPort}

	if err := epLANUDP.Bind(receiverLAN); err != nil {
		t.Fatalf("Bind error: %s", err)
	}

	waitEntryLAN, chLAN := waiter.NewChannelEntry(nil)
	wqLAN.EventRegister(&waitEntryLAN, waiter.EventIn)

	if _, _, err := epWANUDP.Write(tcpip.SlicePayload("hello"), tcpip.WriteOptions{To: &receiverRouter}); err != nil {
		t.Fatalf("failed to write: %s", err)
	}

	select {
	case <-chLAN:
	case <-time.After(1 * time.Second):
		t.Fatalf("Read timeout")
	}
	wqLAN.EventUnregister(&waitEntryLAN)

	var sender tcpip.FullAddress
	recvd, _, err := epLANUDP.Read(&sender)
	if err != nil {
		t.Fatalf("failed to read: %s", err)
	}
	if got, want := sender.Addr, testWANNICAddr; got != want {
		t.Errorf("sender.Addr %s, want %s", got, want)
	}
	// sender.Port is random.
	if got, want := string(recvd), "hello"; got != want {
		t.Errorf("got %s, want %s", got, want)
	}
}

func TestRDRRoundtripWANToLANUDP(t *testing.T) {
	sLAN, sLANLinkEP := createTestStackLAN(t)
	sWAN, sWANLinkEP := createTestStackWAN(t)
	_, sRouterLinkEP1, sRouterLinkEP2 := createTestStackRouterRDR(t)

	linkEndpoints(sWANLinkEP, sRouterLinkEP2)
	linkEndpoints(sRouterLinkEP1, sLANLinkEP)

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

	receiverLAN := tcpip.FullAddress{Addr: testLANNICAddr, Port: testLANPort}
	receiverRouter := tcpip.FullAddress{Addr: testRouterNICAddr2, Port: testRouterPort}
	receiverWAN := tcpip.FullAddress{Addr: testWANNICAddr, Port: testWANPort}

	if err := epLANUDP.Bind(receiverLAN); err != nil {
		t.Fatalf("Bind error: %s", err)
	}
	if err := epWANUDP.Bind(receiverWAN); err != nil {
		t.Fatalf("Bind error: %s", err)
	}

	waitEntryLAN, chLAN := waiter.NewChannelEntry(nil)
	wqLAN.EventRegister(&waitEntryLAN, waiter.EventIn)

	if _, _, err := epWANUDP.Write(tcpip.SlicePayload("hello"), tcpip.WriteOptions{To: &receiverRouter}); err != nil {
		t.Fatalf("Write error: %s", err)
	}

	select {
	case <-chLAN:
	case <-time.After(1 * time.Second):
		t.Fatalf("Read timeout")
	}
	wqLAN.EventUnregister(&waitEntryLAN)

	var sender tcpip.FullAddress
	recvd, _, err := epLANUDP.Read(&sender)
	if err != nil {
		t.Fatalf("Read error: %s", err)
	}
	if got, want := sender.Addr, testWANNICAddr; got != want {
		t.Errorf("sender.Addr %s, want %s", got, want)
	}
	if got, want := sender.Port, testWANPort; got != want {
		t.Errorf("sender.Addr %d, want %d", got, want)
	}
	if got, want := string(recvd), "hello"; got != want {
		t.Errorf("got %s, want %s", got, want)
	}

	waitEntryWAN, chWAN := waiter.NewChannelEntry(nil)
	wqWAN.EventRegister(&waitEntryWAN, waiter.EventIn)

	if _, _, err := epLANUDP.Write(tcpip.SlicePayload("hi"), tcpip.WriteOptions{To: &sender}); err != nil {
		t.Fatalf("Write error: %s", err)
	}

	select {
	case <-chWAN:
	case <-time.After(1 * time.Second):
		t.Fatalf("Read timeout")
	}
	wqWAN.EventUnregister(&waitEntryWAN)

	var sender2 tcpip.FullAddress
	recvd2, _, err := epWANUDP.Read(&sender2)
	if err != nil {
		t.Fatalf("Read error: %s", err)
	}
	if got, want := sender2.Addr, receiverRouter.Addr; got != want {
		t.Errorf("sender2.Addr %s, want %s", got, want)
	}
	if got, want := sender2.Port, receiverRouter.Port; got != want {
		t.Errorf("sender2.Addr %d, want %d", got, want)
	}
	if got, want := string(recvd2), "hi"; got != want {
		t.Errorf("got %s, want %s", got, want)
	}
}

func TestRDRWANToLANTCP(t *testing.T) {
	sLAN, sLANLinkEP := createTestStackLAN(t)
	sWAN, sWANLinkEP := createTestStackWAN(t)
	_, sRouterLinkEP1, sRouterLinkEP2 := createTestStackRouterRDR(t)

	linkEndpoints(sWANLinkEP, sRouterLinkEP2)
	linkEndpoints(sRouterLinkEP1, sLANLinkEP)

	linkEndpoints(sLANLinkEP, sRouterLinkEP1)
	linkEndpoints(sRouterLinkEP2, sWANLinkEP)

	var wqLANMaster waiter.Queue
	epLANTCPMaster, err := sLAN.NewEndpoint(tcp.ProtocolNumber, ipv4.ProtocolNumber, &wqLANMaster)
	if err != nil {
		t.Fatalf("NewEndpoint error: %s", err)
	}
	var wqWAN waiter.Queue
	epWANTCP, err := sWAN.NewEndpoint(tcp.ProtocolNumber, ipv4.ProtocolNumber, &wqWAN)
	if err != nil {
		t.Fatalf("NewEndpoint error: %s", err)
	}

	receiverLAN := tcpip.FullAddress{Addr: testLANNICAddr, Port: testLANPort}
	receiverRouter := tcpip.FullAddress{Addr: testRouterNICAddr2, Port: testRouterPort}

	if err := epLANTCPMaster.Bind(receiverLAN); err != nil {
		t.Fatalf("Bind error: %s", err)
	}
	if err := epLANTCPMaster.Listen(10); err != nil {
		t.Fatalf("Bind error: %s", err)
	}

	waitEntryWAN, chWAN := waiter.NewChannelEntry(nil)
	wqWAN.EventRegister(&waitEntryWAN, waiter.EventOut)

	waitEntryLANMaster, chLANMaster := waiter.NewChannelEntry(nil)
	wqLANMaster.EventRegister(&waitEntryLANMaster, waiter.EventIn)

	if err := epWANTCP.Connect(receiverRouter); err != nil {
		if err != tcpip.ErrConnectStarted {
			t.Fatalf("Connect error: %s", err)
		}
	}

	select {
	case <-chLANMaster:
	case <-time.After(1 * time.Second):
		t.Fatalf("Accept timeout")
	}

	epLANTCP, wqLAN, err := epLANTCPMaster.Accept(nil)
	if err != nil {
		t.Fatalf("Accept error: %s", err)
	}
	wqLANMaster.EventUnregister(&waitEntryLANMaster)

	select {
	case <-chWAN:
	case <-time.After(1 * time.Second):
		t.Fatalf("Connect timeout")
	}
	wqWAN.EventUnregister(&waitEntryWAN)

	sender, err := epLANTCP.GetRemoteAddress()
	if got, want := sender.Addr, testWANNICAddr; got != want {
		t.Errorf("sender.Addr %s, want %s", got, want)
	}
	// sender.Port is random.

	waitEntryLAN, chLAN := waiter.NewChannelEntry(nil)
	wqLAN.EventRegister(&waitEntryLAN, waiter.EventIn)

	if _, _, err := epWANTCP.Write(tcpip.SlicePayload("hello"), tcpip.WriteOptions{}); err != nil {
		t.Fatalf("Write error: %s", err)
	}

	select {
	case <-chLAN:
	case <-time.After(1 * time.Second):
		t.Fatalf("Read timeout")
	}
	wqLAN.EventUnregister(&waitEntryLAN)

	recvd, _, err := epLANTCP.Read(nil)
	if err != nil {
		t.Fatalf("Read error: %s", err)
	}
	if got, want := string(recvd), "hello"; got != want {
		t.Errorf("got %s, want %s", got, want)
	}

	wqWAN.EventRegister(&waitEntryWAN, waiter.EventIn)

	if _, _, err := epLANTCP.Write(tcpip.SlicePayload("hi"), tcpip.WriteOptions{}); err != nil {
		t.Fatalf("Write error: %s", err)
	}

	select {
	case <-chWAN:
	case <-time.After(1 * time.Second):
		t.Fatalf("Read timeout")
	}
	wqWAN.EventUnregister(&waitEntryWAN)

	recvd2, _, err := epWANTCP.Read(nil)
	if err != nil {
		t.Fatalf("Read error: %s", err)
	}
	if got, want := string(recvd2), "hi"; got != want {
		t.Errorf("got %s, want %s", got, want)
	}
}
