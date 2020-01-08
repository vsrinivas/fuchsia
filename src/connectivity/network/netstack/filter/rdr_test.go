package filter

import (
	"testing"
	"time"

	"gvisor.dev/gvisor/pkg/tcpip"
	"gvisor.dev/gvisor/pkg/tcpip/header"
	"gvisor.dev/gvisor/pkg/tcpip/link/channel"
	"gvisor.dev/gvisor/pkg/tcpip/network/ipv4"
	"gvisor.dev/gvisor/pkg/tcpip/stack"
	"gvisor.dev/gvisor/pkg/tcpip/transport/tcp"
	"gvisor.dev/gvisor/pkg/tcpip/transport/udp"
	"gvisor.dev/gvisor/pkg/waiter"
)

func createTestStackRouterRDR(t *testing.T) (*stack.Stack, *channel.Endpoint, *channel.Endpoint) {
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
			newDstAddr:      testLanNICAddr,
			newDstPortRange: PortRange{testLanPort, testLanPort},
		},
	}
	f.rulesetRDR.Unlock()

	linkEP1 := channel.New(1, 100, testRouterLinkAddress1)
	nic1 := tcpip.NICID(testRouterNICID1)
	filtered1 := NewEndpoint(f, linkEP1)
	if err := s.CreateNIC(nic1, filtered1); err != nil {
		t.Fatalf("CreateNIC error: %s", err)
	}
	s.AddAddress(nic1, header.IPv4ProtocolNumber, testRouterNICAddr1)

	linkEP2 := channel.New(1, 100, testRouterLinkAddress2)
	nic2 := tcpip.NICID(testRouterNICID2)
	filtered2 := NewEndpoint(f, linkEP2)
	if err := s.CreateNIC(nic2, filtered2); err != nil {
		t.Fatalf("CreateNIC error: %s", err)
	}
	s.AddAddress(nic2, header.IPv4ProtocolNumber, testRouterNICAddr2)

	s.SetRouteTable([]tcpip.Route{
		{
			Destination: testLanNet,
			NIC:         nic1,
		},
		{
			Destination: testWanNet,
			NIC:         nic2,
		},
	})
	s.SetForwarding(true)
	return s, linkEP1, linkEP2
}

func TestRDROneWayWanToLanUDP(t *testing.T) {
	sLan, sLanLinkEP := createTestStackLan(t)
	sWan, sWanLinkEP := createTestStackWan(t)
	_, sRouterLinkEP1, sRouterLinkEP2 := createTestStackRouterRDR(t)

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
	receiverRouter := tcpip.FullAddress{Addr: testRouterNICAddr2, Port: testRouterPort}

	if err := epLanUDP.Bind(receiverLan); err != nil {
		t.Fatalf("Bind error: %s", err)
	}

	waitEntryLan, chLan := waiter.NewChannelEntry(nil)
	wqLan.EventRegister(&waitEntryLan, waiter.EventIn)

	if _, _, err := epWanUDP.Write(tcpip.SlicePayload("hello"), tcpip.WriteOptions{To: &receiverRouter}); err != nil {
		t.Fatalf("failed to write: %s", err)
	}

	select {
	case <-chLan:
	case <-time.After(1 * time.Second):
		t.Fatalf("Read timeout")
	}
	wqLan.EventUnregister(&waitEntryLan)

	var sender tcpip.FullAddress
	recvd, _, err := epLanUDP.Read(&sender)
	if err != nil {
		t.Fatalf("failed to read: %s", err)
	}
	if got, want := sender.Addr, testWanNICAddr; got != want {
		t.Errorf("sender.Addr %s, want %s", got, want)
	}
	// sender.Port is random.
	if got, want := string(recvd), "hello"; got != want {
		t.Errorf("got %s, want %s", got, want)
	}
}

func TestRDRRoundtripWanToLanUDP(t *testing.T) {
	sLan, sLanLinkEP := createTestStackLan(t)
	sWan, sWanLinkEP := createTestStackWan(t)
	_, sRouterLinkEP1, sRouterLinkEP2 := createTestStackRouterRDR(t)

	go link(sWanLinkEP, sRouterLinkEP2)
	go link(sRouterLinkEP1, sLanLinkEP)

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

	receiverLan := tcpip.FullAddress{Addr: testLanNICAddr, Port: testLanPort}
	receiverRouter := tcpip.FullAddress{Addr: testRouterNICAddr2, Port: testRouterPort}
	receiverWan := tcpip.FullAddress{Addr: testWanNICAddr, Port: testWanPort}

	if err := epLanUDP.Bind(receiverLan); err != nil {
		t.Fatalf("Bind error: %s", err)
	}
	if err := epWanUDP.Bind(receiverWan); err != nil {
		t.Fatalf("Bind error: %s", err)
	}

	waitEntryLan, chLan := waiter.NewChannelEntry(nil)
	wqLan.EventRegister(&waitEntryLan, waiter.EventIn)

	if _, _, err := epWanUDP.Write(tcpip.SlicePayload("hello"), tcpip.WriteOptions{To: &receiverRouter}); err != nil {
		t.Fatalf("Write error: %s", err)
	}

	select {
	case <-chLan:
	case <-time.After(1 * time.Second):
		t.Fatalf("Read timeout")
	}
	wqLan.EventUnregister(&waitEntryLan)

	var sender tcpip.FullAddress
	recvd, _, err := epLanUDP.Read(&sender)
	if err != nil {
		t.Fatalf("Read error: %s", err)
	}
	if got, want := sender.Addr, testWanNICAddr; got != want {
		t.Errorf("sender.Addr %s, want %s", got, want)
	}
	if got, want := sender.Port, testWanPort; got != want {
		t.Errorf("sender.Addr %d, want %d", got, want)
	}
	if got, want := string(recvd), "hello"; got != want {
		t.Errorf("got %s, want %s", got, want)
	}

	waitEntryWan, chWan := waiter.NewChannelEntry(nil)
	wqWan.EventRegister(&waitEntryWan, waiter.EventIn)

	if _, _, err := epLanUDP.Write(tcpip.SlicePayload("hi"), tcpip.WriteOptions{To: &sender}); err != nil {
		t.Fatalf("Write error: %s", err)
	}

	select {
	case <-chWan:
	case <-time.After(1 * time.Second):
		t.Fatalf("Read timeout")
	}
	wqWan.EventUnregister(&waitEntryWan)

	var sender2 tcpip.FullAddress
	recvd2, _, err := epWanUDP.Read(&sender2)
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

func TestRDRWanToLanTCP(t *testing.T) {
	sLan, sLanLinkEP := createTestStackLan(t)
	sWan, sWanLinkEP := createTestStackWan(t)
	_, sRouterLinkEP1, sRouterLinkEP2 := createTestStackRouterRDR(t)

	go link(sWanLinkEP, sRouterLinkEP2)
	go link(sRouterLinkEP1, sLanLinkEP)

	go link(sLanLinkEP, sRouterLinkEP1)
	go link(sRouterLinkEP2, sWanLinkEP)

	var wqLanMaster waiter.Queue
	epLanTCPMaster, err := sLan.NewEndpoint(tcp.ProtocolNumber, ipv4.ProtocolNumber, &wqLanMaster)
	if err != nil {
		t.Fatalf("NewEndpoint error: %s", err)
	}
	var wqWan waiter.Queue
	epWanTCP, err := sWan.NewEndpoint(tcp.ProtocolNumber, ipv4.ProtocolNumber, &wqWan)
	if err != nil {
		t.Fatalf("NewEndpoint error: %s", err)
	}

	receiverLan := tcpip.FullAddress{Addr: testLanNICAddr, Port: testLanPort}
	receiverRouter := tcpip.FullAddress{Addr: testRouterNICAddr2, Port: testRouterPort}

	if err := epLanTCPMaster.Bind(receiverLan); err != nil {
		t.Fatalf("Bind error: %s", err)
	}
	if err := epLanTCPMaster.Listen(10); err != nil {
		t.Fatalf("Bind error: %s", err)
	}

	waitEntryWan, chWan := waiter.NewChannelEntry(nil)
	wqWan.EventRegister(&waitEntryWan, waiter.EventOut)

	waitEntryLanMaster, chLanMaster := waiter.NewChannelEntry(nil)
	wqLanMaster.EventRegister(&waitEntryLanMaster, waiter.EventIn)

	if err := epWanTCP.Connect(receiverRouter); err != nil {
		if err != tcpip.ErrConnectStarted {
			t.Fatalf("Connect error: %s", err)
		}
	}

	select {
	case <-chLanMaster:
	case <-time.After(1 * time.Second):
		t.Fatalf("Accept timeout")
	}

	epLanTCP, wqLan, err := epLanTCPMaster.Accept()
	if err != nil {
		t.Fatalf("Accept error: %s", err)
	}
	wqLanMaster.EventUnregister(&waitEntryLanMaster)

	select {
	case <-chWan:
	case <-time.After(1 * time.Second):
		t.Fatalf("Connect timeout")
	}
	wqWan.EventUnregister(&waitEntryWan)

	sender, err := epLanTCP.GetRemoteAddress()
	if got, want := sender.Addr, testWanNICAddr; got != want {
		t.Errorf("sender.Addr %s, want %s", got, want)
	}
	// sender.Port is random.

	waitEntryLan, chLan := waiter.NewChannelEntry(nil)
	wqLan.EventRegister(&waitEntryLan, waiter.EventIn)

	if _, _, err := epWanTCP.Write(tcpip.SlicePayload("hello"), tcpip.WriteOptions{}); err != nil {
		t.Fatalf("Write error: %s", err)
	}

	select {
	case <-chLan:
	case <-time.After(1 * time.Second):
		t.Fatalf("Read timeout")
	}
	wqLan.EventUnregister(&waitEntryLan)

	recvd, _, err := epLanTCP.Read(nil)
	if err != nil {
		t.Fatalf("Read error: %s", err)
	}
	if got, want := string(recvd), "hello"; got != want {
		t.Errorf("got %s, want %s", got, want)
	}

	wqWan.EventRegister(&waitEntryWan, waiter.EventIn)

	if _, _, err := epLanTCP.Write(tcpip.SlicePayload("hi"), tcpip.WriteOptions{}); err != nil {
		t.Fatalf("Write error: %s", err)
	}

	select {
	case <-chWan:
	case <-time.After(1 * time.Second):
		t.Fatalf("Read timeout")
	}
	wqWan.EventUnregister(&waitEntryWan)

	recvd2, _, err := epWanTCP.Read(nil)
	if err != nil {
		t.Fatalf("Read error: %s", err)
	}
	if got, want := string(recvd2), "hi"; got != want {
		t.Errorf("got %s, want %s", got, want)
	}
}
