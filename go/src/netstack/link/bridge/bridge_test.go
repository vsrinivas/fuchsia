package bridge_test

import (
	"bytes"
	"testing"

	"netstack/link/bridge"

	"github.com/google/netstack/tcpip"
	"github.com/google/netstack/tcpip/buffer"
	"github.com/google/netstack/tcpip/header"
	"github.com/google/netstack/tcpip/link/channel"
	"github.com/google/netstack/tcpip/network/ipv4"
	"github.com/google/netstack/tcpip/stack"
	"github.com/google/netstack/tcpip/transport/tcp"
	"github.com/google/netstack/waiter"
)

func makeStack() *stack.Stack {
	return stack.New([]string{ipv4.ProtocolName}, []string{tcp.ProtocolName}, stack.Options{})
}

func makeStackWithChannel(linkaddr tcpip.LinkAddress, addr tcpip.Address) (*stack.Stack, *channel.Endpoint, *tcpip.Error) {
	s := makeStack()
	id, ep := channel.New(1, 100, linkaddr)
	if err := s.CreateNIC(1, id); err != nil {
		return nil, nil, err
	}
	if err := s.AddAddress(1, header.IPv4ProtocolNumber, addr); err != nil {
		return nil, nil, err
	}
	return s, ep, nil
}

func link(a, b *channel.Endpoint) {
	for p := range a.C {
		var vv buffer.VectorisedView
		if p.Header != nil {
			vv = buffer.NewVectorisedView(len(p.Header)+len(p.Payload), []buffer.View{p.Header, p.Payload})
		} else {
			vv = buffer.NewVectorisedView(len(p.Payload), []buffer.View{p.Payload})
		}
		b.Inject(p.Proto, vv)
	}
}

func TestCombineCapabilities(t *testing.T) {
	resolutionRequired := stack.LinkEndpointCapabilities(stack.CapabilityResolutionRequired)
	var resolutionNotRequired stack.LinkEndpointCapabilities
	if bridge.CombineCapabilities(resolutionRequired, resolutionNotRequired) != resolutionRequired {
		t.Errorf("got bridge.Combinecapabilities(%#v, %#v) == %#v, want == %#v", resolutionRequired, resolutionNotRequired, bridge.CombineCapabilities(resolutionRequired, resolutionNotRequired), resolutionRequired)
	}

	checksumAndLoopback := stack.LinkEndpointCapabilities(stack.CapabilityChecksumOffload | stack.CapabilityLoopback)
	var noChecksumAndLoopback stack.LinkEndpointCapabilities
	if bridge.CombineCapabilities(checksumAndLoopback, noChecksumAndLoopback) != noChecksumAndLoopback {
		t.Errorf("got bridge.Combinecapabilities(%#v, %#v) == %#v, want == %#v", checksumAndLoopback, noChecksumAndLoopback, bridge.CombineCapabilities(checksumAndLoopback, noChecksumAndLoopback), noChecksumAndLoopback)
	}
}

func TestBridge(t *testing.T) {
	s1addr := tcpip.Address([]byte{192, 168, 42, 10})
	s1, s1ep, err := makeStackWithChannel(tcpip.LinkAddress(bytes.Repeat([]byte{1}, 6)), s1addr)
	if err != nil {
		t.Fatal(err)
	}

	s2addr := tcpip.Address([]byte{10, 0, 0, 1})
	s2, s2ep, err := makeStackWithChannel(tcpip.LinkAddress(bytes.Repeat([]byte{2}, 6)), s2addr)
	if err != nil {
		t.Fatal(err)
	}

	s1.SetRouteTable([]tcpip.Route{
		{
			Destination: s2addr,
			Mask:        "\xff\xff\xff\xff",
			NIC:         1,
		},
	})

	{
		_, ep1 := channel.New(1, 101, tcpip.LinkAddress(bytes.Repeat([]byte{3}, 6)))
		_, ep2 := channel.New(1, 100, tcpip.LinkAddress(bytes.Repeat([]byte{4}, 6)))
		bridge1 := bridge.New([]stack.LinkEndpoint{ep1, ep2})
		if err := makeStack().CreateNIC(1, stack.RegisterLinkEndpoint(bridge1)); err != nil {
			t.Fatal(err)
		}
		if bridge1.MTU() != 100 {
			t.Errorf("got bridge1.MTU() == %d but want 100", bridge1.MTU())
		}
		if bridge1.LinkAddress()[0]&0x2 == 0 {
			t.Errorf("bridge1.LinkAddress() expected to be locally administered MAC address")
		}
		go link(s1ep, ep1)
		go link(ep1, s1ep)
		go link(ep2, s2ep)
		go link(s2ep, ep2)
	}

	wq := new(waiter.Queue)
	s1txep, err := s1.NewEndpoint(tcp.ProtocolNumber, ipv4.ProtocolNumber, wq)
	if err != nil {
		t.Fatal(err)
	}
	defer s1txep.Close()
	s2txep, err := s2.NewEndpoint(tcp.ProtocolNumber, ipv4.ProtocolNumber, wq)
	if err != nil {
		t.Fatal(err)
	}
	defer s2txep.Close()

	s2fulladdr := tcpip.FullAddress{Addr: s2addr, Port: 8080}
	if err := s2txep.Bind(s2fulladdr, nil); err != nil {
		t.Fatal(err)
	}
	if err := s2txep.Listen(1); err != nil {
		t.Fatal(err)
	}

	if err := s1txep.Connect(s2fulladdr); err != tcpip.ErrConnectStarted {
		t.Logf("s1.Stats() = %+v", s1.Stats())
		t.Fatal(err)
	}

	// Wait for the inbound TCP connection.
	{
		waitEntry, notifyCh := waiter.NewChannelEntry(nil)
		wq.EventRegister(&waitEntry, waiter.EventIn)
		<-notifyCh
		wq.EventUnregister(&waitEntry)
	}
	ep, wq, err := s2txep.Accept()
	if err != nil {
		t.Logf("s2.Stats() = %+v", s1.Stats())
		t.Fatal(err)
	}

	const payload = "hello"
	if _, _, err := s1txep.Write(tcpip.SlicePayload(payload), tcpip.WriteOptions{To: &s2fulladdr}); err != nil {
		t.Logf("s1.Stats() = %+v", s1.Stats())
		t.Fatal(err)
	}

	// Wait for the inbound packet.
	{
		waitEntry, notifyCh := waiter.NewChannelEntry(nil)
		wq.EventRegister(&waitEntry, waiter.EventIn)
		<-notifyCh
		wq.EventUnregister(&waitEntry)
	}
	recvd, _, err := ep.Read(nil)
	if err != nil {
		t.Logf("s2.Stats() = %+v", s1.Stats())
		t.Fatal(err)
	}
	if got := string(recvd); got != payload {
		t.Errorf("got Read(...) = %v, want = %v", got, payload)
	}
}
