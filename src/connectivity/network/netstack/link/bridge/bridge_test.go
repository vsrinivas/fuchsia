package bridge_test

import (
	"bytes"
	"fmt"
	"testing"
	"time"

	"netstack/link/bridge"
	"netstack/packetbuffer"
	"netstack/util"

	"gvisor.dev/gvisor/pkg/tcpip"
	"gvisor.dev/gvisor/pkg/tcpip/buffer"
	"gvisor.dev/gvisor/pkg/tcpip/header"
	"gvisor.dev/gvisor/pkg/tcpip/link/loopback"
	"gvisor.dev/gvisor/pkg/tcpip/link/sniffer"
	"gvisor.dev/gvisor/pkg/tcpip/network/arp"
	"gvisor.dev/gvisor/pkg/tcpip/network/ipv4"
	"gvisor.dev/gvisor/pkg/tcpip/network/ipv6"
	"gvisor.dev/gvisor/pkg/tcpip/stack"
	"gvisor.dev/gvisor/pkg/tcpip/transport/tcp"
	"gvisor.dev/gvisor/pkg/waiter"
)

var (
	timeoutReceiveReady    error = fmt.Errorf("receiveready")
	timeoutSendReady       error = fmt.Errorf("sendready")
	timeoutPayloadReceived error = fmt.Errorf("payloadreceived")
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
	bridgeEP := bridge.New([]*bridge.BridgeableEndpoint{ep1, ep2})

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

type waitingEndpoint struct {
	stack.LinkEndpoint
	ch chan struct{}
}

func (we *waitingEndpoint) Wait() {
	<-we.ch
}

func TestEndpoint_Wait(t *testing.T) {
	ep := loopback.New()
	ep1 := waitingEndpoint{
		LinkEndpoint: ep,
		ch:           make(chan struct{}),
	}
	ep2 := waitingEndpoint{
		LinkEndpoint: ep,
		ch:           make(chan struct{}),
	}
	bridgeEP := bridge.New([]*bridge.BridgeableEndpoint{
		bridge.NewEndpoint(&ep1),
		bridge.NewEndpoint(&ep2),
	})
	ch := make(chan struct{})
	go func() {
		bridgeEP.Wait()
		close(ch)
	}()

	for _, ep := range []waitingEndpoint{ep1, ep2} {
		select {
		case <-ch:
			t.Fatal("bridge wait completed before constituent links")
		case <-time.After(100 * time.Millisecond):
		}
		close(ep.ch)
	}

	select {
	case <-ch:
	case <-time.After(100 * time.Millisecond):
		t.Fatal("bridge wait pending after constituent links completed")
	}
}

func TestBridge(t *testing.T) {
	for _, testCase := range []struct {
		name        string
		protocol    stack.NetworkProtocol
		addressSize int
	}{
		{name: "ipv4", protocol: ipv4.NewProtocol(), addressSize: header.IPv4AddressSize},
		{name: "ipv6", protocol: ipv6.NewProtocol(), addressSize: header.IPv6AddressSize},
	} {
		t.Run(testCase.name, func(t *testing.T) {
			// payload should be unique enough that it won't accidentally appear
			// in TCP/IP packets.
			const payload = "hello"

			/* Connection diagram:
			 s1ep <----> ep1         ep2 <----> s2ep
										^--bridge1--^
			*/
			ep1, ep2 := pipe(tcpip.LinkAddress(bytes.Repeat([]byte{1}, header.EthernetAddressSize)), tcpip.LinkAddress(bytes.Repeat([]byte{2}, header.EthernetAddressSize)))
			ep3, ep4 := pipe(tcpip.LinkAddress(bytes.Repeat([]byte{3}, header.EthernetAddressSize)), tcpip.LinkAddress(bytes.Repeat([]byte{4}, header.EthernetAddressSize)))
			s1addr := tcpip.Address(bytes.Repeat([]byte{1}, testCase.addressSize))
			s1subnet := util.PointSubnet(s1addr)
			s1, err := makeStackWithEndpoint(ep1, testCase.protocol, s1addr)
			if err != nil {
				t.Fatal(err)
			}

			baddr := tcpip.Address(bytes.Repeat([]byte{2}, testCase.addressSize))
			bsubnet := util.PointSubnet(baddr)
			sb, b, err := makeStackWithBridgedEndpoints(ep2, ep3, testCase.protocol, baddr)
			if err != nil {
				t.Fatal(err)
			}

			if err := b.Up(); err != nil {
				t.Fatal(err)
			}

			s2addr := tcpip.Address(bytes.Repeat([]byte{3}, testCase.addressSize))
			s2subnet := util.PointSubnet(s2addr)
			s2, err := makeStackWithEndpoint(ep4, testCase.protocol, s2addr)
			if err != nil {
				t.Fatal(err)
			}

			// Add an address to one of the constituent links of the bridge (in addition
			// to the address on the virtual NIC representing the bridge itself), to test
			// that constituent links are still routable.
			bcaddr := tcpip.Address(bytes.Repeat([]byte{4}, testCase.addressSize))
			bcsubnet := util.PointSubnet(bcaddr)
			if err := sb.AddAddress(1, header.ARPProtocolNumber, arp.ProtocolAddress); err != nil {
				t.Fatal(fmt.Errorf("AddAddress failed: %s", err))
			}
			if err := sb.AddAddress(1, testCase.protocol.Number(), bcaddr); err != nil {
				t.Fatal(fmt.Errorf("AddAddress failed: %s", err))
			}

			s1.SetRouteTable([]tcpip.Route{
				{
					Destination: s2subnet,
					NIC:         1,
				},
				{
					Destination: bsubnet,
					NIC:         1,
				},
				{
					Destination: bcsubnet,
					NIC:         1,
				},
			})

			sb.SetRouteTable([]tcpip.Route{
				{
					Destination: s1subnet,
					NIC:         1,
				},
			})

			s2.SetRouteTable(
				[]tcpip.Route{
					{
						Destination: s1subnet,
						NIC:         2,
					},
				},
			)

			addrs := map[tcpip.Address]*stack.Stack{
				s2addr: s2,
				baddr:  sb,
				bcaddr: sb,
			}

			stacks := map[string]*stack.Stack{
				"s1": s1, "s2": s2, "sb": sb,
			}

			ep2.onWritePacket = func(pkt tcpip.PacketBuffer) {
				for i, view := range pkt.Data.Views() {
					if bytes.Contains(view, []byte(payload)) {
						t.Errorf("did not expect payload %x to be sent back to ep1 in view %d: %x", payload, i, view)
					}
				}
			}

			for addr, toStack := range addrs {
				t.Run(fmt.Sprintf("ConnectAndWrite_%s", addr), func(t *testing.T) {
					recvd, err := connectAndWrite(s1, toStack, testCase.protocol, addr, payload)
					if err != nil {
						t.Fatal(err)
					}

					if !bytes.Equal(recvd, []byte(payload)) {
						t.Errorf("got Read(...) = %x, want = %x", recvd, payload)
					}

					for name, s := range stacks {
						stats := s.Stats()
						if n := stats.UnknownProtocolRcvdPackets.Value(); n != 0 {
							t.Errorf("stack %s received %d UnknownProtocolRcvdPackets", name, n)
						}
						if n := stats.MalformedRcvdPackets.Value(); n != 0 {
							t.Errorf("stack %s received %d MalformedRcvdPackets", name, n)
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
						// TODO(NET-690): When we implement learning, we should be able to
						// modify this test setup to get to zero invalid addresses received.
						// With the current test setup, once learning is implemented, the
						// bridge would indiscriminately forward the first packet addressed to
						// a link address to all constituent links (causing #links - 1 invalid
						// addresses received), observe which link the response packet came
						// from, and then remember which link to forward to when the next
						// packet addressed to that link address was received. We might be able
						// to get to zero invalid addresses received by learning which links a
						// given address is on via the broadcast packets sent during ARP.
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

			if err := b.Close(); err != nil {
				t.Fatal(err)
			}

			// verify that the endpoint from the constituent link on sb is still accessible
			// and the bridge endpoint and endpoint on s2 are no longer accessible from s1
			noLongerConnectable := map[tcpip.Address]*stack.Stack{
				s2addr: s2,
				baddr:  sb,
			}

			stillConnectable := map[tcpip.Address]*stack.Stack{
				bcaddr: sb,
			}

			for addr, toStack := range noLongerConnectable {
				t.Run(addr.String(), func(t *testing.T) {
					senderWaitQueue := new(waiter.Queue)
					sender, err := s1.NewEndpoint(tcp.ProtocolNumber, testCase.protocol.Number(), senderWaitQueue)
					if err != nil {
						t.Fatalf("NewEndpoint failed: %s", err)
					}
					defer sender.Close()

					receiverWaitQueue := new(waiter.Queue)
					receiver, err := toStack.NewEndpoint(tcp.ProtocolNumber, testCase.protocol.Number(), receiverWaitQueue)
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

			for addr, toStack := range stillConnectable {
				recvd, err := connectAndWrite(s1, toStack, testCase.protocol, addr, payload)
				if err != nil {
					t.Fatal(err)
				}

				if !bytes.Equal(recvd, []byte(payload)) {
					t.Errorf("got Read(...) = %x, want = %x", recvd, payload)
				}
			}
		})
	}
}

// pipe mints two linked endpoints with the given link addresses.
func pipe(addr1, addr2 tcpip.LinkAddress) (*endpoint, *endpoint) {
	ep1, ep2 := &endpoint{linkAddr: addr1}, &endpoint{linkAddr: addr2}
	ep1.linked = ep2
	ep2.linked = ep1
	return ep1, ep2
}

var _ stack.LinkEndpoint = (*endpoint)(nil)

// Use our own endpoint fake because we'd like to report
// CapabilityResolutionRequired and trigger link address resolution.
//
// `endpoint` cannot be copied.
//
// Make endpoints using `pipe()`, not using endpoint literals.
type endpoint struct {
	linkAddr      tcpip.LinkAddress
	dispatcher    stack.NetworkDispatcher
	linked        *endpoint
	onWritePacket func(tcpip.PacketBuffer)
}

func (e *endpoint) WritePacket(r *stack.Route, _ *stack.GSO, protocol tcpip.NetworkProtocolNumber, pkt tcpip.PacketBuffer) *tcpip.Error {
	if e.linked == nil {
		panic(fmt.Sprintf("ep %+v has not been linked to another endpoint; create endpoints with `pipe()`", e))
	}
	if !e.linked.IsAttached() {
		panic(fmt.Sprintf("ep: %+v linked endpoint: %+v has not been `Attach`ed; call stack.CreateNIC to attach it", e, e.linked))
	}

	if fn := e.onWritePacket; fn != nil {
		fn(pkt)
	}
	// the "remote" address for `other` is our local address and vice versa.
	e.linked.dispatcher.DeliverNetworkPacket(e.linked, r.LocalLinkAddress, r.RemoteLinkAddress, protocol, packetbuffer.OutboundToInbound(pkt))
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
	panic("not implemented")
}

func (e *endpoint) Attach(dispatcher stack.NetworkDispatcher) {
	e.dispatcher = dispatcher
}

func (e *endpoint) IsAttached() bool {
	return e.dispatcher != nil
}

func (*endpoint) Wait() {}

func (*endpoint) MTU() uint32 {
	// This value is used by IPv4 fragmentation.  It must be at least 68 bytes as
	// required by RFC 791.
	return 1000
}

func (*endpoint) Capabilities() stack.LinkEndpointCapabilities {
	return stack.CapabilityResolutionRequired
}

func (*endpoint) MaxHeaderLength() uint16 {
	return 0
}

func (e *endpoint) LinkAddress() tcpip.LinkAddress {
	return e.linkAddr
}

func makeStackWithEndpoint(ep stack.LinkEndpoint, protocol stack.NetworkProtocol, addr tcpip.Address) (*stack.Stack, error) {
	if testing.Verbose() {
		ep = sniffer.New(ep)
	}

	s := stack.New(stack.Options{
		NetworkProtocols: []stack.NetworkProtocol{
			arp.NewProtocol(),
			protocol,
		},
		TransportProtocols: []stack.TransportProtocol{
			tcp.NewProtocol(),
		},
	})
	if err := s.CreateNIC(1, ep); err != nil {
		return nil, fmt.Errorf("CreateNIC failed: %s", err)
	}
	if err := s.AddAddress(1, header.ARPProtocolNumber, arp.ProtocolAddress); err != nil {
		return nil, fmt.Errorf("AddAddress failed: %s", err)
	}
	if err := s.AddAddress(1, protocol.Number(), addr); err != nil {
		return nil, fmt.Errorf("AddAddress failed: %s", err)
	}
	return s, nil
}

func makeStackWithBridgedEndpoints(ep1, ep2 stack.LinkEndpoint, protocol stack.NetworkProtocol, baddr tcpip.Address) (*stack.Stack, *bridge.Endpoint, error) {
	if testing.Verbose() {
		ep1 = sniffer.New(ep1)
		ep2 = sniffer.New(ep2)
	}

	bep1 := bridge.NewEndpoint(ep1)
	bep2 := bridge.NewEndpoint(ep2)

	stk := stack.New(stack.Options{
		NetworkProtocols: []stack.NetworkProtocol{
			arp.NewProtocol(),
			protocol,
		},
		TransportProtocols: []stack.TransportProtocol{
			tcp.NewProtocol(),
		},
	})

	if err := stk.CreateNIC(1, bep1); err != nil {
		return nil, nil, fmt.Errorf("CreateNIC failed: %s", err)
	}
	if err := stk.CreateNIC(2, bep2); err != nil {
		return nil, nil, fmt.Errorf("CreateNIC failed: %s", err)
	}
	bridgeEP := bridge.New([]*bridge.BridgeableEndpoint{bep1, bep2})
	var bridgeLinkEP stack.LinkEndpoint = bridgeEP
	if testing.Verbose() {
		bridgeLinkEP = sniffer.New(bridgeLinkEP)
	}
	bID := tcpip.NICID(3)
	if err := stk.CreateNIC(bID, bridgeLinkEP); err != nil {
		return nil, nil, fmt.Errorf("CreateNIC failed: %s", err)
	}
	if err := stk.AddAddress(bID, protocol.Number(), baddr); err != nil {
		return nil, nil, fmt.Errorf("AddAddress failed: %s", err)
	}
	if err := stk.AddAddress(bID, header.ARPProtocolNumber, arp.ProtocolAddress); err != nil {
		return nil, nil, fmt.Errorf("AddAddress failed: %s", err)
	}

	return stk, bridgeEP, nil
}

func connectAndWrite(fromStack *stack.Stack, toStack *stack.Stack, protocol stack.NetworkProtocol, addr tcpip.Address, payload string) ([]byte, error) {
	senderWaitQueue := new(waiter.Queue)
	sender, err := fromStack.NewEndpoint(tcp.ProtocolNumber, protocol.Number(), senderWaitQueue)
	if err != nil {
		return nil, fmt.Errorf("NewEndpoint failed: %s", err)
	}
	defer sender.Close()

	receiverWaitQueue := new(waiter.Queue)
	receiver, err := toStack.NewEndpoint(tcp.ProtocolNumber, protocol.Number(), receiverWaitQueue)
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

		ep, wq, err := receiver.Accept()
		if err != nil {
			return nil, fmt.Errorf("accept failed: %s", err)
		}

		if err := write(sender, addr, payload, wq); err != nil {
			return nil, err
		}

		recvd, _, err := ep.Read(nil)
		if err != nil {
			return nil, fmt.Errorf("read failed: %s", err)
		}
		return recvd, nil
	}
}

func write(sender tcpip.Endpoint, s2fulladdr tcpip.FullAddress, payload string, wq *waiter.Queue) error {
	payloadReceivedWaitEntry, payloadReceivedNotifyCh := waiter.NewChannelEntry(nil)
	wq.EventRegister(&payloadReceivedWaitEntry, waiter.EventIn)
	defer wq.EventUnregister(&payloadReceivedWaitEntry)
	if _, _, err := sender.Write(tcpip.SlicePayload(payload), tcpip.WriteOptions{To: &s2fulladdr}); err != nil {
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
	sendReadyWaitEntry, sendReadyNotifyCh := waiter.NewChannelEntry(nil)
	senderWaitQueue.EventRegister(&sendReadyWaitEntry, waiter.EventOut)
	defer senderWaitQueue.EventUnregister(&sendReadyWaitEntry)

	receiveReadyWaitEntry, receiveReadyNotifyCh := waiter.NewChannelEntry(nil)
	receiverWaitQueue.EventRegister(&receiveReadyWaitEntry, waiter.EventIn)
	defer receiverWaitQueue.EventUnregister(&receiveReadyWaitEntry)

	if err := sender.Connect(addr); err != tcpip.ErrConnectStarted {
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
