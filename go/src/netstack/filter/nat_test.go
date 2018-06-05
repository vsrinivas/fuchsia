package filter

import (
	"testing"
	"time"

	"github.com/google/netstack/tcpip"
	"github.com/google/netstack/tcpip/buffer"
	"github.com/google/netstack/tcpip/header"
	"github.com/google/netstack/tcpip/link/bufwritingchannel"
	"github.com/google/netstack/tcpip/link/channel"
	"github.com/google/netstack/tcpip/network/ipv4"
	"github.com/google/netstack/tcpip/stack"
	"github.com/google/netstack/tcpip/transport/tcp"
	"github.com/google/netstack/tcpip/transport/udp"
	"github.com/google/netstack/waiter"
)

// TODO: make these tests table-driven.

var (
	testLanNet     = tcpip.Parse("192.168.42.0")
	testLanNetMask = tcpip.Parse("255.255.255.0")

	testWanNet     = tcpip.Parse("10.0.0.0")
	testWanNetMask = tcpip.Parse("255.0.0.0")

	testLanNICAddr     = tcpip.Parse("192.168.42.10")
	testRouterNICAddr1 = tcpip.Parse("192.168.42.1")
	testRouterNICAddr2 = tcpip.Parse("10.0.0.1")
	testWanNICAddr     = tcpip.Parse("10.0.0.2")

	testLanPort    = uint16(10000)
	testRouterPort = uint16(8080)
	testWanPort    = uint16(80)

	testLanNICID     = 1
	testRouterNICID1 = 2
	testRouterNICID2 = 3
	testWanNICID     = 4

	testLanLinkAddress     = tcpip.LinkAddress("\x00\x00\x00\x00\x00\x01")
	testRouterLinkAddress1 = tcpip.LinkAddress("\x00\x00\x00\x00\x00\x02")
	testRouterLinkAddress2 = tcpip.LinkAddress("\x00\x00\x00\x00\x00\x03")
	testWanLinkAddress     = tcpip.LinkAddress("\x00\x00\x00\x00\x00\x04")
)

func createTestStackLan(t *testing.T) (*stack.Stack, stack.LinkEndpoint) {
	s := stack.New([]string{ipv4.ProtocolName}, []string{udp.ProtocolName, tcp.ProtocolName})
	id, linkEP := bufwritingchannel.New(1, 100, testLanLinkAddress)
	nic := tcpip.NICID(testLanNICID)
	if err := s.CreateDisabledNIC(nic, id); err != nil {
		t.Fatalf("CreateDisableNIC error: %s", err)
	}
	s.EnableNIC(nic)
	s.AddAddress(nic, header.IPv4ProtocolNumber, testLanNICAddr)
	s.SetRouteTable([]tcpip.Route{
		{
			Destination: testLanNet,
			Mask:        testLanNetMask,
			NIC:         nic,
		},
		{
			Destination: testWanNet,
			Mask:        testWanNetMask,
			NIC:         nic,
		},
	})
	return s, linkEP
}

func createTestStackWan(t *testing.T) (*stack.Stack, stack.LinkEndpoint) {
	s := stack.New([]string{ipv4.ProtocolName}, []string{udp.ProtocolName, tcp.ProtocolName})
	id, linkEP := bufwritingchannel.New(1, 100, testWanLinkAddress)
	nic := tcpip.NICID(testWanNICID)
	if err := s.CreateDisabledNIC(nic, id); err != nil {
		t.Fatalf("CreateDisableNIC error: %s", err)
	}
	s.EnableNIC(nic)
	s.AddAddress(nic, header.IPv4ProtocolNumber, testWanNICAddr)
	s.SetRouteTable([]tcpip.Route{
		{
			Destination: testWanNet,
			Mask:        testWanNetMask,
			NIC:         nic,
		},
	})
	return s, linkEP
}

func createTestStackRouterNAT(t *testing.T) (*stack.Stack, stack.LinkEndpoint, stack.LinkEndpoint) {
	s := stack.New([]string{ipv4.ProtocolName}, []string{udp.ProtocolName, tcp.ProtocolName})

	f := New(s.PortManager)
	srcNet, terr := tcpip.NewSubnet(testLanNet, tcpip.AddressMask(testLanNetMask))
	if terr != nil {
		t.Fatalf("NewSubnet error: %s", terr)
	}
	f.rulesetNAT.Lock()
	f.rulesetNAT.v = []*NAT{
		&NAT{
			transProto: header.UDPProtocolNumber,
			srcNet:     &srcNet,
			newSrcAddr: testRouterNICAddr2,
		},
		&NAT{
			transProto: header.TCPProtocolNumber,
			srcNet:     &srcNet,
			newSrcAddr: testRouterNICAddr2,
		},
	}
	f.rulesetNAT.Unlock()

	id1, linkEP1 := bufwritingchannel.New(1, 100, testRouterLinkAddress1)
	nic1 := tcpip.NICID(testRouterNICID1)
	if err := s.CreateDisabledNIC(nic1, NewEndpoint(f, id1)); err != nil {
		t.Fatalf("CreateDisableNIC error: %s", err)
	}
	s.EnableNIC(nic1)
	s.AddAddress(nic1, header.IPv4ProtocolNumber, testRouterNICAddr1)

	id2, linkEP2 := bufwritingchannel.New(1, 100, testRouterLinkAddress2)
	nic2 := tcpip.NICID(testRouterNICID2)
	if err := s.CreateDisabledNIC(nic2, NewEndpoint(f, id2)); err != nil {
		t.Fatalf("CreateDisableNIC error: %s", err)
	}
	s.EnableNIC(nic2)
	s.AddAddress(nic2, header.IPv4ProtocolNumber, testRouterNICAddr2)

	s.SetRouteTable([]tcpip.Route{
		{
			Destination: testLanNet,
			Mask:        testLanNetMask,
			NIC:         nic1,
		},
		{
			Destination: testWanNet,
			Mask:        testWanNetMask,
			NIC:         nic2,
		},
	})
	s.SetForwarding(true)
	return s, linkEP1, linkEP2
}

func TestNATOneWayLanToWanUDP(t *testing.T) {
	sLan, sLanLinkEP := createTestStackLan(t)
	sWan, sWanLinkEP := createTestStackWan(t)
	_, sRouterLinkEP1, sRouterLinkEP2 := createTestStackRouterNAT(t)

	go link(sLanLinkEP, sRouterLinkEP1)
	go link(sRouterLinkEP2, sWanLinkEP)

	var wqLan waiter.Queue
	epLanUDP, err := sLan.NewEndpoint(udp.ProtocolNumber, ipv4.ProtocolNumber, &wqLan)
	if err != nil {
		t.Fatalf("NewEndpoint error: %s", err)
	}
	var wqWan waiter.Queue
	epWanUDP, err := sWan.NewEndpoint(udp.ProtocolNumber, ipv4.ProtocolNumber, &wqWan)
	if err != nil {
		t.Fatalf("NewEndpoint error: %s", err)
	}

	receiverWan := tcpip.FullAddress{Addr: testWanNICAddr, Port: testWanPort}

	if err := epWanUDP.Bind(receiverWan, nil); err != nil {
		t.Fatalf("Bind error: %s", err)
	}

	waitEntryWan, chWan := waiter.NewChannelEntry(nil)
	wqWan.EventRegister(&waitEntryWan, waiter.EventIn)

	if _, err := epLanUDP.Write(buffer.View("hello"), &receiverWan); err != nil {
		t.Fatalf("Write error: %s", err)
	}

	select {
	case <-chWan:
	case <-time.After(1 * time.Second):
		t.Fatalf("Read timeout")
	}
	wqWan.EventUnregister(&waitEntryWan)

	var sender tcpip.FullAddress
	recvd, err := epWanUDP.Read(&sender)
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

func TestNATRoundtripLanToWanUDP(t *testing.T) {
	sLan, sLanLinkEP := createTestStackLan(t)
	sWan, sWanLinkEP := createTestStackWan(t)
	_, sRouterLinkEP1, sRouterLinkEP2 := createTestStackRouterNAT(t)

	go link(sLanLinkEP, sRouterLinkEP1)
	go link(sRouterLinkEP2, sWanLinkEP)

	go link(sWanLinkEP, sRouterLinkEP2)
	go link(sRouterLinkEP1, sLanLinkEP)

	var wqLan waiter.Queue
	epLanUDP, err := sLan.NewEndpoint(udp.ProtocolNumber, ipv4.ProtocolNumber, &wqLan)
	if err != nil {
		t.Fatalf("NewEndpoint error: %s", err)
	}
	var wqWan waiter.Queue
	epWanUDP, err := sWan.NewEndpoint(udp.ProtocolNumber, ipv4.ProtocolNumber, &wqWan)
	if err != nil {
		t.Fatalf("NewEndpoint error: %s", err)
	}

	receiverLan := tcpip.FullAddress{Addr: testLanNICAddr, Port: testLanPort}
	receiverWan := tcpip.FullAddress{Addr: testWanNICAddr, Port: testWanPort}

	if err := epLanUDP.Bind(receiverLan, nil); err != nil {
		t.Fatalf("Bind error: %s", err)
	}
	if err := epWanUDP.Bind(receiverWan, nil); err != nil {
		t.Fatalf("Bind error: %s", err)
	}

	waitEntryWan, chWan := waiter.NewChannelEntry(nil)
	wqWan.EventRegister(&waitEntryWan, waiter.EventIn)

	if _, err := epLanUDP.Write(buffer.View("hello"), &receiverWan); err != nil {
		t.Fatalf("Write error: %s", err)
	}

	select {
	case <-chWan:
	case <-time.After(1 * time.Second):
		t.Fatalf("Read timeout")
	}
	wqWan.EventUnregister(&waitEntryWan)

	var sender tcpip.FullAddress
	recvd, err := epWanUDP.Read(&sender)
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

	waitEntryLan, chLan := waiter.NewChannelEntry(nil)
	wqLan.EventRegister(&waitEntryLan, waiter.EventIn)

	_, err = epWanUDP.Write(buffer.View("hi"), &sender)
	if err != nil {
		t.Fatalf("Write error: %s", err)
	}

	select {
	case <-chLan:
	case <-time.After(1 * time.Second):
		t.Fatalf("Read timeout")
	}
	wqLan.EventUnregister(&waitEntryLan)

	var sender2 tcpip.FullAddress
	recvd2, err := epLanUDP.Read(&sender2)
	if err != nil {
		t.Fatalf("Read error: %s", err)
	}
	if got, want := sender2.Addr, receiverWan.Addr; got != want {
		t.Errorf("sender2.Addr %s, want %s", got, want)
	}
	if got, want := sender2.Port, receiverWan.Port; got != want {
		t.Errorf("sender2.Addr %s, want %s", got, want)
	}
	if got, want := string(recvd2), "hi"; got != want {
		t.Errorf("got %s, want %s", got, want)
	}
}

func TestNATLanToWanTCP(t *testing.T) {
	sLan, sLanLinkEP := createTestStackLan(t)
	sWan, sWanLinkEP := createTestStackWan(t)
	_, sRouterLinkEP1, sRouterLinkEP2 := createTestStackRouterNAT(t)

	go link(sLanLinkEP, sRouterLinkEP1)
	go link(sRouterLinkEP2, sWanLinkEP)

	go link(sWanLinkEP, sRouterLinkEP2)
	go link(sRouterLinkEP1, sLanLinkEP)

	var wqLan waiter.Queue
	epLanTCP, err := sLan.NewEndpoint(tcp.ProtocolNumber, ipv4.ProtocolNumber, &wqLan)
	if err != nil {
		t.Fatalf("NewEndpoint error: %s", err)
	}
	var wqWanMaster waiter.Queue
	epWanTCPMaster, err := sWan.NewEndpoint(tcp.ProtocolNumber, ipv4.ProtocolNumber, &wqWanMaster)
	if err != nil {
		t.Fatalf("NewEndpoint error: %s", err)
	}

	receiverWan := tcpip.FullAddress{Addr: testWanNICAddr, Port: testWanPort}

	if err := epWanTCPMaster.Bind(receiverWan, nil); err != nil {
		t.Fatalf("Bind error: %s", err)
	}
	if err := epWanTCPMaster.Listen(10); err != nil {
		t.Fatalf("Bind error: %s", err)
	}

	waitEntryLan, chLan := waiter.NewChannelEntry(nil)
	wqLan.EventRegister(&waitEntryLan, waiter.EventOut)

	waitEntryWanMaster, chWanMaster := waiter.NewChannelEntry(nil)
	wqWanMaster.EventRegister(&waitEntryWanMaster, waiter.EventIn)

	if err := epLanTCP.Connect(receiverWan); err != nil {
		if err != tcpip.ErrConnectStarted {
			t.Fatalf("Connect error: %s", err)
		}
	}

	select {
	case <-chWanMaster:
	case <-time.After(1 * time.Second):
		t.Fatalf("Accept timeout")
	}

	epWanTCP, wqWan, err := epWanTCPMaster.Accept()
	if err != nil {
		t.Fatalf("Accept error: %s", err)
	}
	wqWanMaster.EventUnregister(&waitEntryWanMaster)

	select {
	case <-chLan:
	case <-time.After(1 * time.Second):
		t.Fatalf("Connect timeout")
	}
	wqLan.EventUnregister(&waitEntryLan)

	sender, err := epWanTCP.GetRemoteAddress()
	if got, want := sender.Addr, testRouterNICAddr2; got != want {
		t.Errorf("sender.Addr %s, want %s", got, want)
	}
	// sender.Port is random.

	waitEntryWan, chWan := waiter.NewChannelEntry(nil)
	wqWan.EventRegister(&waitEntryWan, waiter.EventIn)

	if _, err := epLanTCP.Write(buffer.View("hello"), nil); err != nil {
		t.Fatalf("Write error: %s", err)
	}

	select {
	case <-chWan:
	case <-time.After(1 * time.Second):
		t.Fatalf("Read timeout")
	}
	wqWan.EventUnregister(&waitEntryWan)

	recvd, err := epWanTCP.Read(nil)
	if err != nil {
		t.Fatalf("Read error: %s", err)
	}
	if got, want := string(recvd), "hello"; got != want {
		t.Errorf("got %s, want %s", got, want)
	}

	wqLan.EventRegister(&waitEntryLan, waiter.EventIn)

	_, err = epWanTCP.Write(buffer.View("hi"), nil)
	if err != nil {
		t.Fatalf("Write error: %s", err)
	}

	select {
	case <-chLan:
	case <-time.After(1 * time.Second):
		t.Fatalf("Read timeout")
	}
	wqLan.EventUnregister(&waitEntryLan)

	recvd2, err := epLanTCP.Read(nil)
	if err != nil {
		t.Fatalf("Read error: %s", err)
	}
	if got, want := string(recvd2), "hi"; got != want {
		t.Errorf("got %s, want %s", got, want)
	}
}

func link(a, b stack.LinkEndpoint) {
	a2 := a.(*bufwritingchannel.Endpoint)
	b2 := b.(*bufwritingchannel.Endpoint)
	for x := range a2.C {
		b2.Inject(unpacketInfo(x))
	}
}

func unpacketInfo(p channel.PacketInfo) (tcpip.NetworkProtocolNumber, *buffer.VectorisedView) {
	n := p.Proto
	var vv buffer.VectorisedView
	if p.Header != nil {
		vv = buffer.NewVectorisedView(len(p.Header)+len(p.Payload), []buffer.View{p.Header, p.Payload})
	} else {
		vv = buffer.NewVectorisedView(len(p.Payload), []buffer.View{p.Payload})
	}
	return n, &vv
}
