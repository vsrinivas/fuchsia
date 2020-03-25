// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package dhcp

import (
	"context"
	"errors"
	"fmt"
	"math"
	"math/rand"
	"sync"
	"sync/atomic"
	"testing"
	"time"

	"netstack/packetbuffer"

	"github.com/google/go-cmp/cmp"
	"gvisor.dev/gvisor/pkg/tcpip"
	"gvisor.dev/gvisor/pkg/tcpip/buffer"
	"gvisor.dev/gvisor/pkg/tcpip/header"
	"gvisor.dev/gvisor/pkg/tcpip/link/loopback"
	"gvisor.dev/gvisor/pkg/tcpip/link/sniffer"
	"gvisor.dev/gvisor/pkg/tcpip/network/ipv4"
	"gvisor.dev/gvisor/pkg/tcpip/stack"
	"gvisor.dev/gvisor/pkg/tcpip/transport/udp"
	"gvisor.dev/gvisor/pkg/waiter"
)

const (
	testNICID  = tcpip.NICID(1)
	serverAddr = tcpip.Address("\xc0\xa8\x03\x01")

	linkAddr1 = tcpip.LinkAddress("\x52\x11\x22\x33\x44\x52")
	linkAddr2 = tcpip.LinkAddress("\x52\x11\x22\x33\x44\x53")

	defaultAcquireTimeout = 1000 * time.Millisecond
	defaultBackoffTime    = 100 * time.Millisecond
	defaultResendTime     = 400 * time.Millisecond
)

func createTestStack() *stack.Stack {
	return stack.New(stack.Options{
		NetworkProtocols: []stack.NetworkProtocol{
			ipv4.NewProtocol(),
		},
		TransportProtocols: []stack.TransportProtocol{
			udp.NewProtocol(),
		},
	})
}

var _ stack.LinkEndpoint = (*endpoint)(nil)

type endpoint struct {
	dispatcher stack.NetworkDispatcher
	remote     []*endpoint
	// onWritePacket returns a slice of packets to send and a delay to be added before every send.
	onWritePacket func(tcpip.PacketBuffer) ([]tcpip.PacketBuffer, time.Duration)

	stack.LinkEndpoint
}

func (*endpoint) MTU() uint32 {
	// Determined experimentally; must be large enough to hold the longest packet
	// we try to parse in these tests, since our hook point is before fragment
	// reassembly (so it can't span multiple IP packets).
	return headerBaseSize + 100
}

func (*endpoint) Capabilities() stack.LinkEndpointCapabilities {
	return 0
}

func (*endpoint) MaxHeaderLength() uint16 {
	return 0
}

func (*endpoint) LinkAddress() tcpip.LinkAddress {
	return tcpip.LinkAddress([]byte(nil))
}

func (e *endpoint) Attach(dispatcher stack.NetworkDispatcher) {
	e.dispatcher = dispatcher
}

func (e *endpoint) IsAttached() bool {
	return e.dispatcher != nil
}

func (e *endpoint) WritePacket(r *stack.Route, _ *stack.GSO, protocol tcpip.NetworkProtocolNumber, pkt tcpip.PacketBuffer) *tcpip.Error {
	pkts := []tcpip.PacketBuffer{pkt}
	var delay time.Duration

	if fn := e.onWritePacket; fn != nil {
		pkts, delay = fn(pkt)
	}

	for _, pkt := range pkts {
		time.Sleep(delay)
		for _, remote := range e.remote {
			if !remote.IsAttached() {
				panic(fmt.Sprintf("ep: %+v remote endpoint: %+v has not been `Attach`ed; call stack.CreateNIC to attach it", e, remote))
			}
			// the "remote" address for `other` is our local address and vice versa.
			remote.dispatcher.DeliverNetworkPacket(remote, r.LocalLinkAddress, r.RemoteLinkAddress, protocol, packetbuffer.OutboundToInbound(pkt))
		}
	}
	return nil
}

func addEndpointToStack(t *testing.T, addresses []tcpip.Address, nicid tcpip.NICID, s *stack.Stack, linkEP stack.LinkEndpoint) {
	t.Helper()
	if testing.Verbose() {
		linkEP = sniffer.New(linkEP)
	}

	if err := s.CreateNIC(nicid, linkEP); err != nil {
		t.Fatalf("failed CreateNIC(%d, %v): %s", nicid, linkEP, err)
	}
	for _, address := range addresses {
		if err := s.AddAddress(nicid, ipv4.ProtocolNumber, address); err != nil {
			t.Fatalf("failed AddAddress(%d, %d, %v): %s", nicid, ipv4.ProtocolNumber, address, err)
		}
	}

	s.SetRouteTable([]tcpip.Route{{
		Destination: header.IPv4EmptySubnet,
		NIC:         nicid,
	}})
}

// TestIPv4UnspecifiedAddressNotPrimaryDuringDHCP tests that the IPv4
// unspecified address is not a primary address when doing DHCP.
func TestIPv4UnspecifiedAddressNotPrimaryDuringDHCP(t *testing.T) {
	sent := make(chan struct{}, 1)
	e := endpoint{
		onWritePacket: func(b tcpip.PacketBuffer) ([]tcpip.PacketBuffer, time.Duration) {
			select {
			case sent <- struct{}{}:
			default:
			}
			return []tcpip.PacketBuffer{b}, 0
		},
	}
	s := createTestStack()
	addEndpointToStack(t, nil, testNICID, s, &e)
	c := NewClient(s, testNICID, linkAddr1, defaultAcquireTimeout, defaultBackoffTime, defaultResendTime, nil)

	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	errs := make(chan error)
	defer close(errs)
	go func() {
		info := c.Info()
		_, err := c.acquire(ctx, &info)
		errs <- err
	}()

	select {
	case err := <-errs:
		if errors.Is(err, context.DeadlineExceeded) {
			t.Fatal("timed out waiting for a DHCP packet to be sent")
		}
		t.Fatalf("c.acquire(_, _): %s", err)
	case <-sent:
		// Be careful not to do this if we read off the channel above to avoid
		// deadlocking when the test fails in the clause above.
		defer func() {
			// Defers run inside-out, so this one will run before the context is
			// cancelled; multiple cancellations are harmless.
			cancel()
			if err := <-errs; !errors.Is(err, context.Canceled) {
				t.Error(err)
			}
		}()
	}

	nicInfo, ok := s.NICInfo()[testNICID]
	if !ok {
		t.Fatalf("stack.NICInfo()[%d]: %s", testNICID, tcpip.ErrUnknownNICID)
	}
	for _, p := range nicInfo.ProtocolAddresses {
		if p.Protocol == header.IPv4ProtocolNumber && p.AddressWithPrefix.Address == header.IPv4Any {
			t.Fatal("got unexpected IPv4 unspecified address")
		}
	}
}

// TestSimultaneousDHCPClients makes two clients that are trying to get DHCP
// addresses at the same time.
func TestSimultaneousDHCPClients(t *testing.T) {
	// clientLinkEPs are the endpoints on which to inject packets to the client.
	var clientLinkEPs [2]endpoint

	// Synchronize the clients using a "barrier" on the server's replies to them.
	var mu struct {
		sync.Mutex
		buffered int
	}
	cond := sync.Cond{L: &mu.Mutex}
	serverLinkEP := endpoint{
		onWritePacket: func(b tcpip.PacketBuffer) ([]tcpip.PacketBuffer, time.Duration) {
			mu.Lock()
			mu.buffered++
			for mu.buffered < len(clientLinkEPs) {
				cond.Wait()
			}
			mu.Unlock()

			return []tcpip.PacketBuffer{b}, 0
		},
	}
	serverStack := createTestStack()
	addEndpointToStack(t, []tcpip.Address{serverAddr}, testNICID, serverStack, &serverLinkEP)

	// Link the server and the clients.
	for i := range clientLinkEPs {
		serverLinkEP.remote = append(serverLinkEP.remote, &clientLinkEPs[i])
		clientLinkEPs[i].remote = append(clientLinkEPs[i].remote, &serverLinkEP)
	}

	errs := make(chan error)
	defer close(errs)
	defer func() {
		for range clientLinkEPs {
			if err := <-errs; !errors.Is(err, context.Canceled) {
				t.Error(err)
			}
		}
	}()

	// Start the clients.
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	clientStack := createTestStack()
	for i := range clientLinkEPs {
		clientNICID := tcpip.NICID(i + 1)
		addEndpointToStack(t, nil, clientNICID, clientStack, &clientLinkEPs[i])
		c := NewClient(clientStack, clientNICID, linkAddr1, defaultAcquireTimeout, defaultBackoffTime, defaultResendTime, nil)
		info := c.Info()
		go func() {
			_, err := c.acquire(ctx, &info)
			errs <- err
		}()
	}

	// Start the server.
	clientAddrs := []tcpip.Address{"\xc0\xa8\x03\x02", "\xc0\xa8\x03\x03"}
	serverCfg := Config{
		ServerAddress: serverAddr,
		SubnetMask:    "\xff\xff\xff\x00",
		Gateway:       "\xc0\xa8\x03\xF0",
		DNS: []tcpip.Address{
			"\x08\x08\x08\x08", "\x08\x08\x04\x04",
		},
		LeaseLength: defaultLeaseLength,
	}
	if _, err := newEPConnServer(ctx, serverStack, clientAddrs, serverCfg); err != nil {
		t.Fatal(err)
	}
}

func (c *Client) verifyClientStats(t *testing.T, want uint64) {
	t.Helper()
	if got := c.stats.SendDiscovers.Value(); got != want {
		t.Errorf("DHCPStats.SendDiscovers=%d want=%d", got, want)
	}
	if got := c.stats.RecvOffers.Value(); got != want {
		t.Errorf("DHCPStats.RecvOffers=%d want=%d", got, want)
	}
	if got := c.stats.SendRequests.Value(); got != want {
		t.Errorf("DHCPStats.SendRequests=%d want=%d", got, want)
	}
	if got := c.stats.RecvAcks.Value(); got != want {
		t.Errorf("DHCPStats.RecvAcks=%d want=%d", got, want)
	}
}

type randSourceStub struct {
	rand.Source
	src int64
}

func (s *randSourceStub) Int63() int64 { return s.src }

// When used to add jitter to backoff, 1s is substracted from random number to map [0s, +2s] -> [-1s, +1s].
var zeroJitterSource = &randSourceStub{src: int64(time.Second)}

func TestDHCP(t *testing.T) {
	s := createTestStack()
	addEndpointToStack(t, []tcpip.Address{serverAddr}, testNICID, s, loopback.New())

	clientAddrs := []tcpip.Address{"\xc0\xa8\x03\x02", "\xc0\xa8\x03\x03"}

	serverCfg := Config{
		ServerAddress: serverAddr,
		SubnetMask:    "\xff\xff\xff\x00",
		Gateway:       "\xc0\xa8\x03\xF0",
		DNS: []tcpip.Address{
			"\x08\x08\x08\x08", "\x08\x08\x04\x04",
		},
		LeaseLength: defaultLeaseLength,
	}
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	if _, err := newEPConnServer(ctx, s, clientAddrs, serverCfg); err != nil {
		t.Fatal(err)
	}

	c0 := NewClient(s, testNICID, linkAddr1, defaultAcquireTimeout, defaultBackoffTime, defaultResendTime, nil)
	// Stub out random generator to remove random jitter added to backoff time.
	c0.rand = rand.New(zeroJitterSource)
	info := c0.Info()
	{
		{
			cfg, err := c0.acquire(ctx, &info)
			if err != nil {
				t.Fatal(err)
			}
			if got, want := info.Addr.Address, clientAddrs[0]; got != want {
				t.Errorf("c.addr=%s, want=%s", got, want)
			}
			if got, want := cfg.SubnetMask, serverCfg.SubnetMask; got != want {
				t.Errorf("cfg.SubnetMask=%s, want=%s", got, want)
			}
			c0.verifyClientStats(t, 1)
		}
		{
			cfg, err := c0.acquire(ctx, &info)
			if err != nil {
				t.Fatal(err)
			}
			if got, want := info.Addr.Address, clientAddrs[0]; got != want {
				t.Errorf("c.addr=%s, want=%s", got, want)
			}
			if got, want := cfg.SubnetMask, serverCfg.SubnetMask; got != want {
				t.Errorf("cfg.SubnetMask=%s, want=%s", got, want)
			}
			c0.verifyClientStats(t, 2)
		}
	}

	{
		c1 := NewClient(s, testNICID, linkAddr2, defaultAcquireTimeout, defaultBackoffTime, defaultResendTime, nil)
		// Stub out random generator to remove random jitter added to backoff time.
		c1.rand = rand.New(zeroJitterSource)
		info := c1.Info()
		cfg, err := c1.acquire(ctx, &info)
		if err != nil {
			t.Fatal(err)
		}
		if got, want := info.Addr.Address, clientAddrs[1]; got != want {
			t.Errorf("c.addr=%s, want=%s", got, want)
		}
		if got, want := cfg.SubnetMask, serverCfg.SubnetMask; got != want {
			t.Errorf("cfg.SubnetMask=%s, want=%s", got, want)
		}
		c1.verifyClientStats(t, 1)
	}

	{
		if err := s.AddProtocolAddressWithOptions(testNICID, tcpip.ProtocolAddress{
			Protocol:          ipv4.ProtocolNumber,
			AddressWithPrefix: info.Addr,
		}, stack.NeverPrimaryEndpoint); err != nil {
			t.Fatalf("failed to add address to stack: %s", err)
		}
		defer s.RemoveAddress(testNICID, info.Addr.Address)
		cfg, err := c0.acquire(ctx, &info)
		if err != nil {
			t.Fatal(err)
		}
		if got, want := info.Addr.Address, clientAddrs[0]; got != want {
			t.Errorf("c.addr=%s, want=%s", got, want)
		}
		if got, want := cfg.SubnetMask, serverCfg.SubnetMask; got != want {
			t.Errorf("cfg.SubnetMask=%s, want=%s", got, want)
		}

		if diff := cmp.Diff(cfg, serverCfg); diff != "" {
			t.Errorf("(-want +got)\n%s", diff)
		}
		c0.verifyClientStats(t, 3)
	}
}

func mustMsgType(t *testing.T, b tcpip.PacketBuffer) dhcpMsgType {
	t.Helper()

	h := hdr(b.Data.First())
	if !h.isValid() {
		t.Fatalf("invalid header: %s", h)
	}
	opts, err := h.options()
	if err != nil {
		t.Fatalf("invalid header: %s, %s", err, h)
	}
	msgType, err := opts.dhcpMsgType()
	if err != nil {
		t.Fatalf("invalid header: %s, %s", err, h)
	}
	return msgType
}

func TestDelayRetransmission(t *testing.T) {
	for _, tc := range []struct {
		name              string
		cancelBeforeOffer bool
		cancelBeforeAck   bool
		success           bool
	}{
		{
			name:              "Success",
			cancelBeforeOffer: false,
			cancelBeforeAck:   false,
			success:           true,
		},
		{
			name:              "CancelBeforeOffer",
			cancelBeforeOffer: true,
			cancelBeforeAck:   false,
			success:           false,
		},
		{
			name:              "CancelBeforeAck",
			cancelBeforeOffer: false,
			cancelBeforeAck:   true,
			success:           false,
		},
	} {
		t.Run(tc.name, func(t *testing.T) {
			ctx, cancel := context.WithCancel(context.Background())
			defer cancel()

			var serverLinkEP, clientLinkEP endpoint
			serverLinkEP.remote = append(serverLinkEP.remote, &clientLinkEP)
			clientLinkEP.remote = append(clientLinkEP.remote, &serverLinkEP)

			serverLinkEP.onWritePacket = func(b tcpip.PacketBuffer) ([]tcpip.PacketBuffer, time.Duration) {
				func() {
					switch mustMsgType(t, b) {
					case dhcpOFFER:
						if !tc.cancelBeforeOffer {
							return
						}
					case dhcpACK:
						if !tc.cancelBeforeAck {
							return
						}
					default:
						return
					}

					// Allow the other goroutine to begin waiting for a response.
					time.Sleep(10 * time.Millisecond)
					cancel()
					// Allow the other goroutine (presumably waiting for a response) to
					// notice it has been timed out.
					time.Sleep(10 * time.Millisecond)
				}()

				return []tcpip.PacketBuffer{b}, 0
			}

			serverStack := createTestStack()
			addEndpointToStack(t, []tcpip.Address{serverAddr}, testNICID, serverStack, &serverLinkEP)

			clientStack := createTestStack()
			addEndpointToStack(t, nil, testNICID, clientStack, &clientLinkEP)

			clientAddrs := []tcpip.Address{"\xc0\xa8\x03\x02", "\xc0\xa8\x03\x03"}

			serverCfg := Config{
				ServerAddress: serverAddr,
				SubnetMask:    "\xff\xff\xff\x00",
				Gateway:       "\xc0\xa8\x03\xF0",
				DNS: []tcpip.Address{
					"\x08\x08\x08\x08", "\x08\x08\x04\x04",
				},
				LeaseLength: defaultLeaseLength,
			}

			{
				ctx, cancel := context.WithCancel(context.Background())
				defer cancel()
				if _, err := newEPConnServer(ctx, serverStack, clientAddrs, serverCfg); err != nil {
					t.Fatal(err)
				}
			}

			c := NewClient(clientStack, testNICID, linkAddr1, 0, 0, math.MaxInt64, nil)
			info := c.Info()
			cfg, err := c.acquire(ctx, &info)
			if tc.success {
				if err != nil {
					t.Fatal(err)
				}
				if got, want := info.Addr.Address, clientAddrs[0]; got != want {
					t.Errorf("c.addr=%s, want=%s", got, want)
				}
				if got, want := cfg.SubnetMask, serverCfg.SubnetMask; got != want {
					t.Errorf("cfg.SubnetMask=%s, want=%s", got, want)
				}
			} else {
				if !errors.Is(err, ctx.Err()) {
					t.Errorf("got err=%v, want=%s", err, ctx.Err())
				}
			}
		})
	}
}

func TestExponentialBackoff(t *testing.T) {
	for _, v := range []struct {
		retran    time.Duration
		iteration uint
		jitter    time.Duration
		want      time.Duration
	}{
		{retran: time.Millisecond, iteration: 0, jitter: -100 * time.Second, want: 0},
		{retran: 50 * time.Millisecond, iteration: 1, want: 100 * time.Millisecond},
		{retran: 100 * time.Millisecond, iteration: 2, want: 400 * time.Millisecond},
		{retran: time.Second, iteration: 0, want: time.Second},
		{retran: time.Second, iteration: 0, jitter: -400 * time.Millisecond, want: 600 * time.Millisecond},
		{retran: time.Second, iteration: 1, want: 2 * time.Second},
		{retran: time.Second, iteration: 2, want: 4 * time.Second},
		{retran: time.Second, iteration: 3, want: 8 * time.Second},
		{retran: time.Second, iteration: 6, want: 64 * time.Second},
		{retran: time.Second, iteration: 7, want: 64 * time.Second},
		{retran: time.Second, iteration: 10, want: 64 * time.Second},
	} {
		t.Run(fmt.Sprintf("baseRetransmission=%s,jitter=%s,iteration=%d", v.retran, v.jitter, v.iteration), func(t *testing.T) {
			c := NewClient(nil, 0, "", 0, 0, v.retran, nil)
			c.rand = rand.New(&randSourceStub{src: zeroJitterSource.src + int64(v.jitter)})
			if got := c.exponentialBackoff(v.iteration); got != v.want {
				t.Errorf("c.exponentialBackoff(%d) = %s, want: %s", v.iteration, got, v.want)
			}
		})
	}
}

func TestRetransmissionExponentialBackoff(t *testing.T) {
	for _, v := range []struct {
		serverDelay, baseRetran time.Duration
		wantTimeouts            uint64
	}{
		{
			serverDelay:  10 * time.Millisecond,
			baseRetran:   100 * time.Millisecond,
			wantTimeouts: 0,
		},
		{
			serverDelay:  11 * time.Millisecond,
			baseRetran:   10 * time.Millisecond,
			wantTimeouts: 1,
		},
		{
			serverDelay:  80 * time.Millisecond,
			baseRetran:   10 * time.Millisecond,
			wantTimeouts: 3,
		},
	} {
		t.Run(fmt.Sprintf("serverDelay=%s,baseRetransmission=%s", v.serverDelay, v.baseRetran), func(t *testing.T) {
			var serverLinkEP, clientLinkEP endpoint
			serverLinkEP.remote = append(serverLinkEP.remote, &clientLinkEP)
			clientLinkEP.remote = append(clientLinkEP.remote, &serverLinkEP)
			var offerSent bool
			serverLinkEP.onWritePacket = func(b tcpip.PacketBuffer) ([]tcpip.PacketBuffer, time.Duration) {
				switch typ := mustMsgType(t, b); typ {
				case dhcpOFFER:
					// Only respond to the first DHCPDISCOVER to avoid unwanted delays on responses to DHCPREQUEST.
					if offerSent {
						return nil, 0
					}
					offerSent = true
					return []tcpip.PacketBuffer{b}, v.serverDelay
				case dhcpACK:
					return []tcpip.PacketBuffer{b}, v.serverDelay
				default:
					t.Fatalf("test server is sending packet with unexpected type: %s", typ)
					return nil, 0
				}
			}

			serverStack := createTestStack()
			addEndpointToStack(t, []tcpip.Address{serverAddr}, testNICID, serverStack, &serverLinkEP)

			clientStack := createTestStack()
			addEndpointToStack(t, nil, testNICID, clientStack, &clientLinkEP)

			clientAddrs := []tcpip.Address{"\xc0\xa8\x03\x02", "\xc0\xa8\x03\x03"}

			serverCfg := Config{
				ServerAddress: serverAddr,
				SubnetMask:    "\xff\xff\xff\x00",
				Gateway:       "\xc0\xa8\x03\xF0",
				DNS: []tcpip.Address{
					"\x08\x08\x08\x08", "\x08\x08\x04\x04",
				},
				LeaseLength: defaultLeaseLength,
			}

			ctx, cancel := context.WithCancel(context.Background())
			defer cancel()
			if _, err := newEPConnServer(ctx, serverStack, clientAddrs, serverCfg); err != nil {
				t.Fatal(err)
			}

			c := NewClient(clientStack, testNICID, linkAddr1, 0, 0, v.baseRetran, nil)
			// Stub out random generator to remove random jitter added to backoff time.
			c.rand = rand.New(zeroJitterSource)
			info := c.Info()
			if _, err := c.acquire(ctx, &info); err != nil {
				t.Fatalf("c.acquire(ctx, &c.Info()) failed: %s", err)
			}
			if got := c.stats.RecvOfferTimeout.Value(); got != v.wantTimeouts {
				t.Errorf("c.acquire(ctx, &c.Info()) got RecvOfferTimeout count: %d, want: %d", got, v.wantTimeouts)
			}
			if got := c.stats.RecvOffers.Value(); got != 1 {
				t.Errorf("c.acquire(ctx, &c.Info()) got RecvOffers count: %d, want: 1", got)
			}
			if got := c.stats.RecvAckTimeout.Value(); got != v.wantTimeouts {
				t.Errorf("c.acquire(ctx, &c.Info()) got RecvAckTimeout count: %d, want: %d", got, v.wantTimeouts)
			}
			if got := c.stats.RecvAcks.Value(); got != 1 {
				t.Errorf("c.acquire(ctx, &c.Info()) got RecvAcks count: %d, want: 1", got)
			}
		})
	}
}

// mustCloneWithNewMsgType returns a clone of the specified packet buffer
// with DHCP message type set to `msgType` specified in the argument.
// This function does not make a deep copy of packet buffer passed in except
// for the part it has to modify.
func mustCloneWithNewMsgType(t *testing.T, b tcpip.PacketBuffer, msgType dhcpMsgType) tcpip.PacketBuffer {
	t.Helper()

	// Create a deep copy of the DHCP header from `b`, so we don't mutate the original.
	h := hdr(append([]byte(nil), b.Data.First()...))
	opts, err := h.options()
	if err != nil {
		t.Fatalf("failed to get options from header: %s", err)
	}
	var found bool
	for i, opt := range opts {
		if opt.code == optDHCPMsgType {
			found = true
			opts[i] = option{
				code: optDHCPMsgType,
				body: []byte{byte(msgType)},
			}
			break
		}
	}
	if !found {
		t.Fatal("no DHCP message type header found while cloning packet and setting new header")
	}
	h.setOptions(opts)

	b.Data = buffer.NewViewFromBytes(h).ToVectorisedView()
	return b
}

func TestRetransmissionTimeoutWithUnexpectedPackets(t *testing.T) {
	var serverLinkEP, clientLinkEP endpoint
	serverLinkEP.remote = append(serverLinkEP.remote, &clientLinkEP)
	clientLinkEP.remote = append(clientLinkEP.remote, &serverLinkEP)

	// Server is stubbed to only respond to the first DHCPDISCOVER to avoid unwanted
	// delays on responses to DHCPREQUEST.
	//
	// Server delay and retransmission delay are chosen so that client retransmits
	// excatly once for both DHCPDISCOVER and DHCPREQUEST.
	//
	// 0ms: Client sends DHCPDISCOVER.
	// 30ms: Sever sends DHCPNAK. Client receives and discards DHCPNAK.
	// 50ms: Client retransmits DHCPDISCOVER (server won't respond to this DISCOVER).
	// 60ms: Server sends DHCPOFFER. Client receives DHCPOFFER, sends DHCPREQUEST.
	// Similar flow of events happens for DHCPREQUEST.
	const serverDelay = 30 * time.Millisecond
	const retransmissionDelay = 50 * time.Millisecond
	var offerSent bool

	serverLinkEP.onWritePacket = func(b tcpip.PacketBuffer) ([]tcpip.PacketBuffer, time.Duration) {
		switch typ := mustMsgType(t, b); typ {
		case dhcpOFFER:
			if offerSent {
				return nil, 0
			}
			offerSent = true
			return []tcpip.PacketBuffer{
				mustCloneWithNewMsgType(t, b, dhcpNAK),
				b,
			}, serverDelay
		case dhcpACK:
			return []tcpip.PacketBuffer{
				mustCloneWithNewMsgType(t, b, dhcpOFFER),
				b,
			}, serverDelay
		default:
			t.Fatalf("test server is sending packet with unexpected type: %s", typ)
			return nil, 0
		}
	}

	serverStack := createTestStack()
	addEndpointToStack(t, []tcpip.Address{serverAddr}, testNICID, serverStack, &serverLinkEP)

	clientStack := createTestStack()
	addEndpointToStack(t, nil, testNICID, clientStack, &clientLinkEP)

	clientAddrs := []tcpip.Address{"\xc0\xa8\x03\x02", "\xc0\xa8\x03\x03"}

	serverCfg := Config{
		ServerAddress: serverAddr,
		SubnetMask:    "\xff\xff\xff\x00",
		Gateway:       "\xc0\xa8\x03\xF0",
		DNS: []tcpip.Address{
			"\x08\x08\x08\x08", "\x08\x08\x04\x04",
		},
		LeaseLength: defaultLeaseLength,
	}

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	if _, err := newEPConnServer(ctx, serverStack, clientAddrs, serverCfg); err != nil {
		t.Fatal(err)
	}

	c := NewClient(clientStack, testNICID, linkAddr1, 0, 0, retransmissionDelay, nil)
	// Stub out random generator to remove random jitter added to backoff time.
	c.rand = rand.New(zeroJitterSource)
	info := c.Info()
	if _, err := c.acquire(ctx, &info); err != nil {
		t.Fatalf("c.acquire(ctx, &c.Info()) failed: %s", err)
	}
	if got := c.stats.RecvOfferTimeout.Value(); got != 1 {
		t.Errorf("c.acquire(ctx, &c.Info()) got RecvOfferTimeout count: %d, want: 1", got)
	}
	if got := c.stats.RecvOffers.Value(); got != 1 {
		t.Errorf("c.acquire(ctx, &c.Info()) got RecvOffers count: %d, want: 1", got)
	}
	if got := c.stats.RecvOfferUnexpectedType.Value(); got != 1 {
		t.Errorf("c.acquire(ctx, &c.Info()) got RecvOfferUnexpectedType count: %d, want: 1", got)
	}
	if got := c.stats.RecvAckTimeout.Value(); got != 1 {
		t.Errorf("c.acquire(ctx, &c.Info()) got RecvAckTimeout count: %d, want: 1", got)
	}
	if got := c.stats.RecvAckUnexpectedType.Value(); got != 1 {
		t.Errorf("c.acquire(ctx, &c.Info()) got RecvAckUnexpectedType count: %d, want: 1", got)
	}
	if got := c.stats.RecvAcks.Value(); got != 1 {
		t.Errorf("c.acquire(ctx, &c.Info()) got RecvAcks count: %d, want: 1", got)
	}
}

func TestStateTransition(t *testing.T) {
	type testType int
	const (
		testRenew testType = iota
		testRebind
		testLeaseExpire
	)

	const (
		// acquireTimeout is the default acquisition timeout to use in tests.
		// It is small enough to make sure the client doesn't get stuck in retransmission
		// when it should transition to the next state.
		acquireTimeout = 100 * time.Millisecond
		// The following 3 durations are included in DHCP responses.
		// They are multiples of a second because that's the smallest time granularity
		// DHCP messages support.
		renewTime   Seconds = 1
		rebindTime  Seconds = 2
		leaseLength Seconds = 3
	)
	// testTimeout is the amount of time the test is willing to wait for the
	// client to bind/unbind addresses. This value has to be larger than lease length,
	// because in the worst case a test has to wait for the client to respond
	// to lease expiration.
	//
	// Function call is not supported in const initializer, so this is a var not
	// a const.
	testTimeout := leaseLength.Duration() + time.Second

	for _, tc := range []struct {
		name           string
		typ            testType
		acquireTimeout time.Duration
	}{
		{
			name:           "Renew",
			typ:            testRenew,
			acquireTimeout: acquireTimeout,
		},
		{
			name:           "Rebind",
			typ:            testRebind,
			acquireTimeout: acquireTimeout,
		},
		{
			// Test the client is not stuck in retransimission longer than it should.
			// If the client keeps retransmitting until the acquisition timeout
			// configured in this test, the lease will expire after it's done,
			// causing it to miss REBIND.
			name:           "RebindWithLargeAcquisitionTimeout",
			typ:            testRebind,
			acquireTimeout: leaseLength.Duration() + time.Second,
		},
		{
			name:           "LeaseExpire",
			typ:            testLeaseExpire,
			acquireTimeout: acquireTimeout,
		},
		{
			// Test the client is not stuck in retransimission longer than it should.
			// If the client keeps retransmitting until the acquisition timeout
			// configured in this test, the test will timeout before the client can
			// reinitialize after lease expiration.
			name:           "LeaseExpireWithLargeAcquisitionTimeout",
			typ:            testLeaseExpire,
			acquireTimeout: testTimeout + 1*time.Second,
		},
	} {
		t.Run(tc.name, func(t *testing.T) {
			var serverLinkEP, clientLinkEP endpoint
			serverLinkEP.remote = append(serverLinkEP.remote, &clientLinkEP)
			clientLinkEP.remote = append(clientLinkEP.remote, &serverLinkEP)

			var blockData uint32 = 0
			clientLinkEP.onWritePacket = func(b tcpip.PacketBuffer) ([]tcpip.PacketBuffer, time.Duration) {
				if atomic.LoadUint32(&blockData) == 1 {
					return nil, 0
				}
				if tc.typ == testRebind {
					// Only pass client broadcast packets back into the stack. This simulates
					// packet loss during the client's unicast RENEWING state, forcing
					// it into broadcast REBINDING state.
					if header.IPv4(b.Header.View()).DestinationAddress() != header.IPv4Broadcast {
						return nil, 0
					}
				}
				return []tcpip.PacketBuffer{b}, 0
			}

			serverStack := createTestStack()
			addEndpointToStack(t, []tcpip.Address{serverAddr}, testNICID, serverStack, &serverLinkEP)

			clientStack := createTestStack()
			addEndpointToStack(t, nil, testNICID, clientStack, &clientLinkEP)

			clientAddrs := []tcpip.Address{"\xc0\xa8\x03\x02"}

			serverCfg := Config{
				ServerAddress: serverAddr,
				SubnetMask:    "\xff\xff\xff\x00",
				Gateway:       "\xc0\xa8\x03\xF0",
				DNS:           []tcpip.Address{"\x08\x08\x08\x08"},
				LeaseLength:   leaseLength,
				RebindTime:    rebindTime,
				RenewTime:     renewTime,
			}
			ctx, cancel := context.WithCancel(context.Background())
			defer cancel()
			if _, err := newEPConnServer(ctx, serverStack, clientAddrs, serverCfg); err != nil {
				t.Fatal(err)
			}

			count := 0
			var curAddr tcpip.AddressWithPrefix
			addrCh := make(chan tcpip.AddressWithPrefix)
			acquiredFunc := func(oldAddr, newAddr tcpip.AddressWithPrefix, cfg Config) {
				if oldAddr != curAddr {
					t.Fatalf("aquisition %d: curAddr=%s, oldAddr=%s", count, curAddr, oldAddr)
				}

				count++
				curAddr = newAddr

				// Any address acquired by the DHCP client must be added to the stack, because the DHCP client
				// will need to send from that address when it tries to renew its lease.
				if curAddr != oldAddr {
					if oldAddr != (tcpip.AddressWithPrefix{}) {
						if err := clientStack.RemoveAddress(testNICID, oldAddr.Address); err != nil {
							t.Fatalf("RemoveAddress(%s): %s", oldAddr.Address, err)
						}
					}

					if curAddr != (tcpip.AddressWithPrefix{}) {
						protocolAddress := tcpip.ProtocolAddress{
							Protocol:          ipv4.ProtocolNumber,
							AddressWithPrefix: curAddr,
						}
						if err := clientStack.AddProtocolAddress(testNICID, protocolAddress); err != nil {
							t.Fatalf("AddProtocolAddress(%+v): %s", protocolAddress, err)
						}
					}
				}

				if curAddr != (tcpip.AddressWithPrefix{}) {
					if cfg.LeaseLength != serverCfg.LeaseLength {
						t.Fatalf("aquisition %d: lease length: %s, want %s", count, cfg.LeaseLength, serverCfg.LeaseLength)
					}
				}
				// Respond to context cancellation to avoid deadlock when
				// enclosing test times out.
				select {
				case <-ctx.Done():
				case addrCh <- curAddr:
				}
			}

			c := NewClient(clientStack, testNICID, linkAddr1, tc.acquireTimeout, defaultBackoffTime, defaultResendTime, acquiredFunc)

			c.Run(ctx)

			var addr tcpip.AddressWithPrefix
			select {
			case addr = <-addrCh:
				t.Logf("got first address: %s", addr)
			case <-time.After(testTimeout):
				t.Fatal("timeout acquiring initial address")
			}

			wantAddr := addr
			if tc.typ == testLeaseExpire {
				wantAddr = tcpip.AddressWithPrefix{}
				// Cut the data flow to block request packets during renew/rebind.
				// TODO(ckuiper): This has the potential for a race between when the thread that injects
				// data into the link EP reads the new "blockData" value and when the DHCP client thread's
				// timers expire, triggering a renewal request.
				atomic.StoreUint32(&blockData, 1)
			}

			select {
			case newAddr := <-addrCh:
				t.Logf("got new acquisition: %s", newAddr)
				if newAddr != wantAddr {
					t.Fatalf("incorrect new address: got = %s, want = %s", newAddr, wantAddr)
				}
			case <-time.After(testTimeout):
				t.Fatal("timeout acquiring renewed address")
			}
		})
	}
}

// Regression test for https://bugs.fuchsia.dev/p/fuchsia/issues/detail?id=20506
func TestNoNullTerminator(t *testing.T) {
	v := "\x02\x01\x06\x00" +
		"\xc8\x37\xbe\x73\x00\x00\x80\x00\x00\x00\x00\x00\xc0\xa8\x2b\x92" +
		"\xc0\xa8\x2b\x01\x00\x00\x00\x00\x00\x0f\x60\x0a\x23\x93\x00\x00" +
		"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00" +
		"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00" +
		"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00" +
		"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00" +
		"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00" +
		"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00" +
		"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00" +
		"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00" +
		"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00" +
		"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00" +
		"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00" +
		"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00" +
		"\x00\x00\x00\x00\x00\x00\x00\x00\x63\x82\x53\x63\x35\x01\x02\x36" +
		"\x04\xc0\xa8\x2b\x01\x33\x04\x00\x00\x0e\x10\x3a\x04\x00\x00\x07" +
		"\x08\x3b\x04\x00\x00\x0c\x4e\x01\x04\xff\xff\xff\x00\x1c\x04\xc0" +
		"\xa8\x2b\xff\x03\x04\xc0\xa8\x2b\x01\x06\x04\xc0\xa8\x2b\x01\x2b" +
		"\x0f\x41\x4e\x44\x52\x4f\x49\x44\x5f\x4d\x45\x54\x45\x52\x45\x44" +
		"\xff"
	h := hdr(v)
	if !h.isValid() {
		t.Error("failed to decode header")
	}

	if got, want := h.op(), opReply; got != want {
		t.Errorf("h.op()=%s, want=%s", got, want)
	}

	if _, err := h.options(); err != nil {
		t.Errorf("bad options: %s", err)
	}
}

func teeConn(c conn) (conn, conn) {
	dup1 := &dupConn{
		c:   c,
		dup: make(chan connMsg, 8),
	}
	dup2 := &chConn{
		c:  c,
		ch: dup1.dup,
	}
	return dup1, dup2
}

type connMsg struct {
	buf  buffer.View
	addr tcpip.FullAddress
	err  error
}

type dupConn struct {
	c   conn
	dup chan connMsg
}

func (c *dupConn) Read() (buffer.View, tcpip.FullAddress, error) {
	v, addr, err := c.c.Read()
	c.dup <- connMsg{v, addr, err}
	return v, addr, err
}
func (c *dupConn) Write(b []byte, addr *tcpip.FullAddress) error { return c.c.Write(b, addr) }

type chConn struct {
	ch chan connMsg
	c  conn
}

func (c *chConn) Read() (buffer.View, tcpip.FullAddress, error) {
	msg := <-c.ch
	return msg.buf, msg.addr, msg.err
}
func (c *chConn) Write(b []byte, addr *tcpip.FullAddress) error { return c.c.Write(b, addr) }

func TestTwoServers(t *testing.T) {
	s := createTestStack()
	addEndpointToStack(t, []tcpip.Address{serverAddr}, testNICID, s, loopback.New())

	wq := new(waiter.Queue)
	ep, err := s.NewEndpoint(udp.ProtocolNumber, ipv4.ProtocolNumber, wq)
	if err != nil {
		t.Fatalf("dhcp: server endpoint: %s", err)
	}
	if err = ep.Bind(tcpip.FullAddress{Port: ServerPort}); err != nil {
		t.Fatalf("dhcp: server bind: %s", err)
	}
	if err = ep.SetSockOpt(tcpip.BroadcastOption(1)); err != nil {
		t.Fatalf("dhcp: setsockopt: %s", err)
	}

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	c1, c2 := teeConn(newEPConn(ctx, wq, ep))

	if _, err := NewServer(ctx, c1, []tcpip.Address{"\xc0\xa8\x03\x02"}, Config{
		ServerAddress: "\xc0\xa8\x03\x01",
		SubnetMask:    "\xff\xff\xff\x00",
		Gateway:       "\xc0\xa8\x03\xF0",
		DNS:           []tcpip.Address{"\x08\x08\x08\x08"},
		LeaseLength:   Seconds(30 * 60),
	}); err != nil {
		t.Fatal(err)
	}
	if _, err := NewServer(ctx, c2, []tcpip.Address{"\xc0\xa8\x04\x02"}, Config{
		ServerAddress: "\xc0\xa8\x04\x01",
		SubnetMask:    "\xff\xff\xff\x00",
		Gateway:       "\xc0\xa8\x03\xF0",
		DNS:           []tcpip.Address{"\x08\x08\x08\x08"},
		LeaseLength:   Seconds(30 * 60),
	}); err != nil {
		t.Fatal(err)
	}

	c := NewClient(s, testNICID, linkAddr1, defaultAcquireTimeout, defaultBackoffTime, defaultResendTime, nil)
	info := c.Info()
	if _, err := c.acquire(ctx, &info); err != nil {
		t.Fatal(err)
	}
}
