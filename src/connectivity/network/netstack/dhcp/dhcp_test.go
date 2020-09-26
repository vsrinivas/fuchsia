// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package dhcp

import (
	"context"
	"errors"
	"fmt"
	"math/rand"
	"sync"
	"testing"
	"time"

	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/packetbuffer"

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
	defaultRetransTime    = 400 * time.Millisecond
)

var (
	defaultClientAddrs = []tcpip.Address{"\xc0\xa8\x03\x02", "\xc0\xa8\x03\x03"}
	defaultServerCfg   = Config{
		ServerAddress: serverAddr,
		SubnetMask:    "\xff\xff\xff\x00",
		Router: []tcpip.Address{
			"\xc0\xa8\x03\xF0",
		},
		DNS: []tcpip.Address{
			"\x08\x08\x08\x08", "\x08\x08\x04\x04",
		},
		LeaseLength: defaultLeaseLength,
	}
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
	// onWritePacket returns the packet to send or nil if no packets should be sent.
	onWritePacket func(*stack.PacketBuffer) *stack.PacketBuffer

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

func (*endpoint) ARPHardwareType() header.ARPHardwareType { return header.ARPHardwareNone }

func (e *endpoint) WritePacket(r *stack.Route, _ *stack.GSO, protocol tcpip.NetworkProtocolNumber, pkt *stack.PacketBuffer) *tcpip.Error {
	if fn := e.onWritePacket; fn != nil {
		p := fn(pkt)
		if p == nil {
			return nil
		}
		pkt = p
	}
	for _, remote := range e.remote {
		if !remote.IsAttached() {
			panic(fmt.Sprintf("ep: %+v remote endpoint: %+v has not been `Attach`ed; call stack.CreateNIC to attach it", e, remote))
		}
		// the "remote" address for `other` is our local address and vice versa.
		remote.dispatcher.DeliverNetworkPacket(r.LocalLinkAddress, r.RemoteLinkAddress, protocol, packetbuffer.OutboundToInbound(pkt))
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

func newZeroJitterClient(s *stack.Stack, nicid tcpip.NICID, linkAddr tcpip.LinkAddress, acquisition, backoff, retransmission time.Duration, acquiredFunc AcquiredFunc) *Client {
	c := NewClient(s, nicid, linkAddr, acquisition, backoff, retransmission, acquiredFunc)
	// Stub out random generator to remove random jitter added to backoff time.
	//
	// When used to add jitter to backoff, 1s is subtracted from random number to
	// map [0s, +2s] -> [-1s, +1s], so add 1s here to compensate for that.
	//
	// Otherwise the added jitter can result in close-to-zero timeouts, causing
	// the client to miss responses in acquisition due to either server response
	// delays or channel select races.
	c.rand = rand.New(&randSourceStub{src: int64(time.Second)})
	return c
}

// TestIPv4UnspecifiedAddressNotPrimaryDuringDHCP tests that the IPv4
// unspecified address is not a primary address when doing DHCP.
func TestIPv4UnspecifiedAddressNotPrimaryDuringDHCP(t *testing.T) {
	sent := make(chan struct{}, 1)
	e := endpoint{
		onWritePacket: func(b *stack.PacketBuffer) *stack.PacketBuffer {
			select {
			case sent <- struct{}{}:
			default:
			}
			return b
		},
	}
	s := createTestStack()
	addEndpointToStack(t, nil, testNICID, s, &e)
	c := newZeroJitterClient(s, testNICID, linkAddr1, defaultAcquireTimeout, defaultBackoffTime, defaultRetransTime, nil)

	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	errs := make(chan error)
	defer close(errs)
	go func() {
		info := c.Info()
		_, err := acquire(ctx, c, &info)
		errs <- err
	}()

	select {
	case err := <-errs:
		if errors.Is(err, context.DeadlineExceeded) {
			t.Fatal("timed out waiting for a DHCP packet to be sent")
		}
		t.Fatalf("acquire(_, _, _): %s", err)
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
		onWritePacket: func(b *stack.PacketBuffer) *stack.PacketBuffer {
			mu.Lock()
			mu.buffered++
			for mu.buffered < len(clientLinkEPs) {
				cond.Wait()
			}
			mu.Unlock()
			return b
		},
	}
	serverStack := createTestStack()
	addEndpointToStack(t, []tcpip.Address{serverAddr}, testNICID, serverStack, &serverLinkEP)

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

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	clientStack := createTestStack()
	for i := range clientLinkEPs {
		clientNICID := tcpip.NICID(i + 1)
		addEndpointToStack(t, nil, clientNICID, clientStack, &clientLinkEPs[i])
		c := newZeroJitterClient(clientStack, clientNICID, linkAddr1, defaultAcquireTimeout, defaultBackoffTime, defaultRetransTime, nil)
		info := c.Info()
		go func() {
			_, err := acquire(ctx, c, &info)
			errs <- err
		}()
	}

	if _, err := newEPConnServer(ctx, serverStack, defaultClientAddrs, defaultServerCfg); err != nil {
		t.Fatalf("newEPConnServer failed: %s", err)
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

func setupTestEnv(ctx context.Context, t *testing.T, serverCfg Config) (clientStack *stack.Stack, client, server *endpoint, _ *Client) {
	var serverLinkEP, clientLinkEP endpoint
	serverLinkEP.remote = append(serverLinkEP.remote, &clientLinkEP)
	clientLinkEP.remote = append(clientLinkEP.remote, &serverLinkEP)

	serverStack := createTestStack()
	addEndpointToStack(t, []tcpip.Address{serverAddr}, testNICID, serverStack, &serverLinkEP)

	clientStack = createTestStack()
	addEndpointToStack(t, nil, testNICID, clientStack, &clientLinkEP)

	if _, err := newEPConnServer(ctx, serverStack, defaultClientAddrs, serverCfg); err != nil {
		t.Fatalf("newEPConnServer failed: %s", err)
	}
	c := newZeroJitterClient(clientStack, testNICID, linkAddr1, defaultAcquireTimeout, defaultBackoffTime, defaultRetransTime, nil)
	return clientStack, &clientLinkEP, &serverLinkEP, c
}

func TestDHCP(t *testing.T) {
	s := createTestStack()
	addEndpointToStack(t, []tcpip.Address{serverAddr}, testNICID, s, loopback.New())

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	if _, err := newEPConnServer(ctx, s, defaultClientAddrs, defaultServerCfg); err != nil {
		t.Fatalf("newEPConnServer failed: %s", err)
	}

	c0 := newZeroJitterClient(s, testNICID, linkAddr1, defaultAcquireTimeout, defaultBackoffTime, defaultRetransTime, nil)
	info := c0.Info()
	{
		{
			cfg, err := acquire(ctx, c0, &info)
			if err != nil {
				t.Fatal(err)
			}
			if got, want := info.Addr.Address, defaultClientAddrs[0]; got != want {
				t.Errorf("c.addr=%s, want=%s", got, want)
			}
			if got, want := cfg.SubnetMask, defaultServerCfg.SubnetMask; got != want {
				t.Errorf("cfg.SubnetMask=%s, want=%s", got, want)
			}
			c0.verifyClientStats(t, 1)
		}
		{
			cfg, err := acquire(ctx, c0, &info)
			if err != nil {
				t.Fatal(err)
			}
			if got, want := info.Addr.Address, defaultClientAddrs[0]; got != want {
				t.Errorf("c.addr=%s, want=%s", got, want)
			}
			if got, want := cfg.SubnetMask, defaultServerCfg.SubnetMask; got != want {
				t.Errorf("cfg.SubnetMask=%s, want=%s", got, want)
			}
			c0.verifyClientStats(t, 2)
		}
	}

	{
		c1 := newZeroJitterClient(s, testNICID, linkAddr2, defaultAcquireTimeout, defaultBackoffTime, defaultRetransTime, nil)
		info := c1.Info()
		cfg, err := acquire(ctx, c1, &info)
		if err != nil {
			t.Fatal(err)
		}
		if got, want := info.Addr.Address, defaultClientAddrs[1]; got != want {
			t.Errorf("c.addr=%s, want=%s", got, want)
		}
		if got, want := cfg.SubnetMask, defaultServerCfg.SubnetMask; got != want {
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
		cfg, err := acquire(ctx, c0, &info)
		if err != nil {
			t.Fatal(err)
		}
		if got, want := info.Addr.Address, defaultClientAddrs[0]; got != want {
			t.Errorf("c.addr=%s, want=%s", got, want)
		}
		if got, want := cfg.SubnetMask, defaultServerCfg.SubnetMask; got != want {
			t.Errorf("cfg.SubnetMask=%s, want=%s", got, want)
		}

		if diff := cmp.Diff(cfg, defaultServerCfg); diff != "" {
			t.Errorf("(-want +got)\n%s", diff)
		}
		c0.verifyClientStats(t, 3)
	}
}

func mustMsgType(t *testing.T, b *stack.PacketBuffer) dhcpMsgType {
	t.Helper()

	h := hdr(b.Data.ToView())
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

			_, _, serverEP, c := setupTestEnv(ctx, t, defaultServerCfg)
			serverEP.onWritePacket = func(b *stack.PacketBuffer) *stack.PacketBuffer {
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
				return b
			}

			info := c.Info()
			cfg, err := acquire(ctx, c, &info)
			if tc.success {
				if err != nil {
					t.Fatal(err)
				}
				if got, want := info.Addr.Address, defaultClientAddrs[0]; got != want {
					t.Errorf("c.addr=%s, want=%s", got, want)
				}
				if got, want := cfg.SubnetMask, defaultServerCfg.SubnetMask; got != want {
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
	for _, tc := range []struct {
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
		t.Run(fmt.Sprintf("baseRetransmission=%s,jitter=%s,iteration=%d", tc.retran, tc.jitter, tc.iteration), func(t *testing.T) {
			c := NewClient(nil, 0, "", 0, 0, tc.retran, nil)
			// When used to add jitter to backoff, 1s is subtracted from random number
			// to map [0s, +2s] -> [-1s, +1s], so add 1s here to compensate for that.
			c.rand = rand.New(&randSourceStub{src: int64(time.Second + tc.jitter)})
			if got := c.exponentialBackoff(tc.iteration); got != tc.want {
				t.Errorf("c.exponentialBackoff(%d) = %s, want: %s", tc.iteration, got, tc.want)
			}
		})
	}
}

func waitForSignal(ctx context.Context, ch <-chan struct{}) {
	select {
	case <-ch:
	case <-ctx.Done():
	}
}

func signal(ctx context.Context, ch chan struct{}) {
	select {
	case ch <- struct{}{}:
	case <-ctx.Done():
	}
}

func signalTimeout(ctx context.Context, timeout chan time.Time) {
	select {
	case timeout <- time.Time{}:
	case <-ctx.Done():
	}
}

func TestRetransmissionExponentialBackoff(t *testing.T) {
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	// The actual value of retransTimeout does not matter because the timer is
	// stubbed out in this test.
	retransTimeout := time.Millisecond

	for _, tc := range []struct {
		offerTimeouts, ackTimeouts int
		wantTimeouts               []time.Duration
	}{
		{
			wantTimeouts: []time.Duration{
				// No timeouts, got a DHCP offer on first try.
				retransTimeout,
				// No timeouts, got a DHCP ack on first try.
				retransTimeout,
			},
		},
		{
			offerTimeouts: 1,
			ackTimeouts:   1,
			wantTimeouts: []time.Duration{
				// 1 timeout waiting for DHCP offer.
				retransTimeout,
				// Successfully received a DHCP offer.
				2 * retransTimeout,
				// 1 timeouts waiting for DHCP ack.
				retransTimeout,
				// Successfully received a DHCP ack.
				2 * retransTimeout,
			},
		},
		{
			offerTimeouts: 3,
			ackTimeouts:   5,
			wantTimeouts: []time.Duration{
				// 3 timeouts waiting for DHCP offer.
				retransTimeout,
				2 * retransTimeout,
				4 * retransTimeout,
				// Successfully received a DHCP offer.
				8 * retransTimeout,
				// 5 timeouts waiting for DHCP ack.
				retransTimeout,
				2 * retransTimeout,
				4 * retransTimeout,
				8 * retransTimeout,
				16 * retransTimeout,
				// Successfully received a DHCP ack.
				32 * retransTimeout,
			},
		},
		{
			offerTimeouts: 5,
			ackTimeouts:   2,
			wantTimeouts: []time.Duration{
				// 5 timeouts waiting for DHCP offer.
				retransTimeout,
				2 * retransTimeout,
				4 * retransTimeout,
				8 * retransTimeout,
				16 * retransTimeout,
				// Successfully received a DHCP offer.
				32 * retransTimeout,
				// 2 timeouts waiting for DHCP ack.
				retransTimeout,
				2 * retransTimeout,
				// Successfully received a DHCP ack.
				4 * retransTimeout,
			},
		},
	} {
		t.Run(fmt.Sprintf("offerTimeouts=%d,ackTimeouts=%d", tc.offerTimeouts, tc.ackTimeouts), func(t *testing.T) {
			_, clientEP, serverEP, c := setupTestEnv(ctx, t, defaultServerCfg)
			info := c.Info()
			info.Retransmission = retransTimeout
			c.info.Store(info)

			timeoutCh := make(chan time.Time)
			var gotTimeouts []time.Duration
			c.retransTimeout = func(d time.Duration) <-chan time.Time {
				gotTimeouts = append(gotTimeouts, d)
				return timeoutCh
			}

			requestSent := make(chan struct{})
			clientEP.onWritePacket = func(b *stack.PacketBuffer) *stack.PacketBuffer {
				defer signal(ctx, requestSent)

				return b
			}

			unblockResponse := make(chan struct{})
			var dropServerPackets bool
			serverEP.onWritePacket = func(b *stack.PacketBuffer) *stack.PacketBuffer {
				waitForSignal(ctx, unblockResponse)
				if dropServerPackets {
					return nil
				}
				return b
			}

			var wg sync.WaitGroup
			wg.Add(1)
			go func() {
				defer wg.Done()
				// Configure the server to drop all responses, causing the client to
				// timeout waiting for response and retransmit another DHCP discover.
				dropServerPackets = true
				for i := 0; i < tc.offerTimeouts; i++ {
					waitForSignal(ctx, requestSent)
					signalTimeout(ctx, timeoutCh)
					signal(ctx, unblockResponse)
				}
				// Allow the server to respond to DHCP discover, so the client can start
				// sending DHCP requests.
				waitForSignal(ctx, requestSent)
				dropServerPackets = false
				signal(ctx, unblockResponse)

				// Wait for the client to send a DHCP request to confirm it has
				// successfully received a DHCP offer and moved to the requesting phase.
				//
				// Then configure the server to drop all responses, causing the client
				// to timeout waiting for response and retransmit another DHCP request.
				for i := 0; i < tc.ackTimeouts; i++ {
					waitForSignal(ctx, requestSent)
					if i == 0 {
						dropServerPackets = true
					}
					signalTimeout(ctx, timeoutCh)
					signal(ctx, unblockResponse)
				}
				// Allow server to respond to DHCP requests, so the client can acquire
				// an address.
				waitForSignal(ctx, requestSent)
				dropServerPackets = false
				signal(ctx, unblockResponse)
			}()

			if _, err := acquire(ctx, c, &info); err != nil {
				t.Fatalf("acquire(_, _, _) failed: %s", err)
			}

			wg.Wait()

			if diff := cmp.Diff(tc.wantTimeouts, gotTimeouts); diff != "" {
				t.Errorf("acquire(_, _, _) got timeouts diff (-want +got):\n%s", diff)
			}
			if got := c.stats.RecvOfferTimeout.Value(); int(got) != tc.offerTimeouts {
				t.Errorf("acquire(_, _, _) got RecvOfferTimeout count: %d, want: %d", got, tc.offerTimeouts)
			}
			if got := c.stats.RecvOffers.Value(); got != 1 {
				t.Errorf("acquire(_, _, _) got RecvOffers count: %d, want: 1", got)
			}
			if got := c.stats.RecvAckTimeout.Value(); int(got) != tc.ackTimeouts {
				t.Errorf("acquire(_, _, _) got RecvAckTimeout count: %d, want: %d", got, tc.ackTimeouts)
			}
			if got := c.stats.RecvAcks.Value(); got != 1 {
				t.Errorf("acquire(_, _, _) got RecvAcks count: %d, want: 1", got)
			}
		})
	}
}

// mustCloneWithNewMsgType returns a clone of the specified packet buffer
// with DHCP message type set to `msgType` specified in the argument.
// This function does not make a deep copy of packet buffer passed in except
// for the part it has to modify.
func mustCloneWithNewMsgType(t *testing.T, b *stack.PacketBuffer, msgType dhcpMsgType) *stack.PacketBuffer {
	t.Helper()

	b = b.Clone()

	h := hdr(b.Data.ToView())
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

	// Disable checksum verification since we've surely invalidated it.
	header.UDP(b.TransportHeader().View()).SetChecksum(0)

	b.Data = buffer.NewViewFromBytes(h).ToVectorisedView()
	return b
}

func TestRetransmissionTimeoutWithUnexpectedPackets(t *testing.T) {
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	_, clientEP, serverEP, c := setupTestEnv(ctx, t, defaultServerCfg)

	timeoutCh := make(chan time.Time)
	c.retransTimeout = func(time.Duration) <-chan time.Time {
		return timeoutCh
	}

	requestSent := make(chan struct{})
	clientEP.onWritePacket = func(b *stack.PacketBuffer) *stack.PacketBuffer {
		defer signal(ctx, requestSent)
		return b
	}

	unblockResponse := make(chan struct{})
	responseSent := make(chan struct{})
	var serverShouldDecline bool
	serverEP.onWritePacket = func(b *stack.PacketBuffer) *stack.PacketBuffer {
		defer signal(ctx, responseSent)
		waitForSignal(ctx, unblockResponse)

		if serverShouldDecline {
			b = mustCloneWithNewMsgType(t, b, dhcpDECLINE)
		}
		return b
	}

	var wg sync.WaitGroup
	wg.Add(1)
	go func() {
		wg.Done()
		// Run the same exchange for 2 rounds. One for DHCP discover, another for
		// DHCP request.
		for i := 0; i < 2; i++ {
			// Receive unexpected response, then timeout.
			waitForSignal(ctx, requestSent)

			serverShouldDecline = true
			signal(ctx, unblockResponse)
			// Must wait for an unexpected response by the server before moving
			// forward, otherwise the timout signal below will cause the client to
			// send another request and change the server's behavior on how to
			// respond.
			waitForSignal(ctx, responseSent)

			signalTimeout(ctx, timeoutCh)

			// Receive expected response and move on to the next phase.
			waitForSignal(ctx, requestSent)

			serverShouldDecline = false
			signal(ctx, unblockResponse)
			waitForSignal(ctx, responseSent)
		}
	}()

	info := c.Info()
	if _, err := acquire(ctx, c, &info); err != nil {
		t.Fatalf("acquire(_, _, _) failed: %s", err)
	}

	wg.Wait()

	if got := c.stats.RecvOfferTimeout.Value(); got != 1 {
		t.Errorf("acquire(_, _, _) got RecvOfferTimeout count: %d, want: 1", got)
	}
	if got := c.stats.RecvOfferUnexpectedType.Value(); got != 1 {
		t.Errorf("acquire(_, _, _) got RecvOfferUnexpectedType count: %d, want: 1", got)
	}
	if got := c.stats.RecvOffers.Value(); got != 1 {
		t.Errorf("acquire(_, _, _) got RecvOffers count: %d, want: 1", got)
	}
	if got := c.stats.RecvAckTimeout.Value(); got != 1 {
		t.Errorf("acquire(_, _, _) got RecvAckTimeout count: %d, want: 1", got)
	}
	if got := c.stats.RecvAckUnexpectedType.Value(); got != 1 {
		t.Errorf("acquire(_, _, _) got RecvAckUnexpectedType count: %d, want: 1", got)
	}
	if got := c.stats.RecvAcks.Value(); got != 1 {
		t.Errorf("acquire(_, _, _) got RecvAcks count: %d, want: 1", got)
	}
}

// stubTimeNow returns a function that can be used to stub out `time.Now` in test.
//
// The stub function consumes the first duration from `durations` each time it
// is called, and makes the time in test advance for the corresponding amount.
// After all durations are consumed, the call to the stub function will first
// signal the input done channel and then block until context is cancelled.
func stubTimeNow(ctx context.Context, t0 time.Time, durations []time.Duration, done chan struct{}) func() time.Time {
	t := t0
	return func() time.Time {
		if len(durations) == 0 {
			done <- struct{}{}
			<-ctx.Done()
			// The time returned here doesn't matter, the client is going to exit
			// due to context cancellation.
			return time.Time{}
		}
		var d time.Duration
		d, durations = durations[0], durations[1:]
		t = t.Add(d)
		return t
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
		// The following 3 durations are included in DHCP responses.
		// They are multiples of a second because that's the smallest time granularity
		// DHCP messages support.
		renewTime   Seconds = 1
		rebindTime  Seconds = 2
		leaseLength Seconds = 3
	)

	for _, tc := range []struct {
		name           string
		typ            testType
		acquireTimeout time.Duration
		// The time durations to advance in test when the current time is requested.
		durations []time.Duration
	}{
		{
			name:           "Renew",
			typ:            testRenew,
			acquireTimeout: defaultAcquireTimeout,
			durations: []time.Duration{
				// First acquisition.
				0,
				// Transition to renew.
				renewTime.Duration(),
				// Second acquisition from renew.
				0,
			},
		},
		{
			name:           "Rebind",
			typ:            testRebind,
			acquireTimeout: defaultAcquireTimeout,
			durations: []time.Duration{
				// First acquisition.
				0,
				// Transition to renew. Acquisition from renew should fail.
				renewTime.Duration(),
				// Transition to rebind.
				(rebindTime - renewTime).Duration(),
				// Second acquisition from rebind.
				0,
			},
		},
		{
			// Test the client is not stuck in retransimission longer than it should.
			// If the client keeps retransmitting until the acquisition timeout
			// configured in this test, the lease will expire after it's done,
			// causing it to miss REBIND.
			name: "RebindWithLargeAcquisitionTimeout",
			typ:  testRebind,
			// A large enough duration for the test to timeout.
			acquireTimeout: 1000 * time.Hour,
			durations: []time.Duration{
				// First acquisition.
				0,
				// Transition to renew. Acquisition from renew should fail.
				renewTime.Duration(),
				// Transition to rebind.
				(rebindTime - renewTime).Duration(),
				// Second acquisition from rebind.
				0,
			},
		},
		{
			name:           "LeaseExpire",
			typ:            testLeaseExpire,
			acquireTimeout: defaultAcquireTimeout,
			durations: []time.Duration{
				// First acquisition.
				0,
				// Transition to renew. Acquisition from renew should fail.
				renewTime.Duration(),
				// Transition to rebind. Acquisition from rebind should fail.
				(rebindTime - renewTime).Duration(),
				// Transition to lease expiration.
				(leaseLength - rebindTime).Duration(),
				// Second acquisition after lease expiration.
				0,
			},
		},
		{
			// Test the client is not stuck in retransimission longer than it should.
			// If the client keeps retransmitting until the acquisition timeout
			// configured in this test, the test will timeout before the client can
			// reinitialize after lease expiration.
			name: "LeaseExpireWithLargeAcquisitionTimeout",
			typ:  testLeaseExpire,
			// A large enough duration for the test to timeout.
			acquireTimeout: 1000 * time.Hour,
			durations: []time.Duration{
				// First acquisition.
				0,
				// Transition to renew. Acquisition from renew should fail.
				renewTime.Duration(),
				// Transition to rebind. Acquisition from rebind should fail.
				(rebindTime - renewTime).Duration(),
				// Transition to lease expiration.
				(leaseLength - rebindTime).Duration(),
				// Second acquisition after lease expiration.
				0,
			},
		},
	} {
		t.Run(tc.name, func(t *testing.T) {
			var wg sync.WaitGroup
			defer wg.Wait()
			ctx, cancel := context.WithCancel(context.Background())
			defer cancel()

			c := newZeroJitterClient(nil, testNICID, linkAddr1, tc.acquireTimeout, defaultBackoffTime, defaultRetransTime, nil)

			c.acquire = func(ctx context.Context, _ *Client, info *Info) (Config, error) {
				timeout := false
				switch info.State {
				case renewing:
					if tc.typ == testRebind {
						timeout = true
					}
					fallthrough
				case rebinding:
					if tc.typ == testLeaseExpire {
						timeout = true
					}
				}
				if timeout {
					// Simulates a timeout using the deadline from context.
					<-ctx.Done()
					return Config{}, fmt.Errorf("fake test timeout error: %w", ctx.Err())
				}

				info.Addr = tcpip.AddressWithPrefix{
					Address:   tcpip.Address("\xc0\xa8\x03\x02"),
					PrefixLen: 24,
				}
				return Config{
					RenewTime:   renewTime,
					RebindTime:  rebindTime,
					LeaseLength: leaseLength,
				}, nil
			}

			clientTransitionsDone := make(chan struct{})
			c.now = stubTimeNow(ctx, time.Time{}, tc.durations, clientTransitionsDone)

			count := 0
			var curAddr tcpip.AddressWithPrefix
			addrCh := make(chan tcpip.AddressWithPrefix)
			c.acquiredFunc = func(oldAddr, newAddr tcpip.AddressWithPrefix, cfg Config) {
				if oldAddr != curAddr {
					t.Fatalf("aquisition %d: curAddr=%s, oldAddr=%s", count, curAddr, oldAddr)
				}

				count++
				curAddr = newAddr

				// Respond to context cancellation to avoid deadlock when enclosing test
				// times out.
				select {
				case <-ctx.Done():
				case addrCh <- curAddr:
				}
			}

			wg.Add(1)
			go func() {
				c.Run(ctx)
				wg.Done()
			}()

			wantAddr := <-addrCh
			t.Logf("got first address: %s", wantAddr)

			// The first address is always acquired through init selecting state.
			if got := c.stats.InitAcquire.Value(); got != 1 {
				t.Errorf("client entered initselecting state %d times, want: 1", got)
			}

			if tc.typ == testLeaseExpire {
				if gotAddr, wantAddr := <-addrCh, (tcpip.AddressWithPrefix{}); gotAddr != wantAddr {
					t.Fatalf("lease did not correctly expire: got unexpected address = %s, want = %s", gotAddr, wantAddr)
				}
			}

			if gotAddr := <-addrCh; gotAddr != wantAddr {
				t.Fatalf("incorrect new address: got = %s, want = %s", gotAddr, wantAddr)
			}

			<-clientTransitionsDone

			switch tc.typ {
			case testRenew:
				if got := c.stats.RenewAcquire.Value(); got != 1 {
					t.Errorf("client entered renew state %d times, want: 1", got)
				}
			case testRebind:
				if got := c.stats.RebindAcquire.Value(); got != 1 {
					t.Errorf("client entered rebind state %d times, want: 1", got)
				}
			case testLeaseExpire:
				if got := c.stats.RenewAcquire.Value(); got == 0 {
					t.Error("client did not renew before lease expiration, want at least once")
				}
				if got := c.stats.RebindAcquire.Value(); got == 0 {
					t.Error("client did not rebind before lease expiration, want at least once")
				}
			}
		})
	}
}

// Test the client stays in the init selecting state after lease expiration,
// and keeps retrying when previous acquisition fails.
func TestStateTransitionAfterLeaseExpirationWithNoResponse(t *testing.T) {
	const (
		leaseLength    Seconds = 1
		acquireTimeout         = time.Nanosecond
		backoffTime            = time.Nanosecond
	)

	var wg sync.WaitGroup
	defer wg.Wait()

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	c := newZeroJitterClient(nil, testNICID, linkAddr1, acquireTimeout, backoffTime, defaultRetransTime, nil)
	// Only returns a valid config in the first acquisition. Blocks until context
	// cancellation in following acquisitions. This makes sure the client is stuck
	// in init selecting state after lease expiration.
	firstAcquisition := true
	c.acquire = func(ctx context.Context, _ *Client, info *Info) (Config, error) {
		if !firstAcquisition {
			// Simulates a timeout using the deadline from context.
			<-ctx.Done()
			return Config{}, fmt.Errorf("fake test timeout error: %w", ctx.Err())
		}
		firstAcquisition = false
		info.Addr = tcpip.AddressWithPrefix{
			Address:   tcpip.Address("\xc0\xa8\x03\x02"),
			PrefixLen: 24,
		}
		return Config{LeaseLength: leaseLength}, nil
	}

	// wantInitCount is the number of times we want the client to enter init
	// selecting after lease expiration before test exits. This makes us believe
	// the client is correctly staying in the init selecting state.
	const wantInitCount = 10
	durations := []time.Duration{
		0, // first acquisition
		// Transition to lease expiration: this will cause the client to go into
		// init once.
		leaseLength.Duration(),
	}
	// Make the client enter init N-1 more times.
	for i := 0; i < wantInitCount-1; i++ {
		durations = append(durations, 0)
	}

	clientTransitionsDone := make(chan struct{})
	c.now = stubTimeNow(ctx, time.Time{}, durations, clientTransitionsDone)

	var curAddr tcpip.AddressWithPrefix
	addrCh := make(chan tcpip.AddressWithPrefix)
	c.acquiredFunc = func(oldAddr, newAddr tcpip.AddressWithPrefix, cfg Config) {
		// Respond to context cancellation to avoid deadlock when enclosing test
		// times out.
		select {
		case <-ctx.Done():
		case addrCh <- curAddr:
		}
	}

	wg.Add(1)
	go func() {
		c.Run(ctx)
		wg.Done()
	}()

	gotAddr := <-addrCh
	t.Logf("got first address: %s", gotAddr)

	initCountAfterFirstAcquisition := c.stats.InitAcquire.Value()
	// The first address is always acquired through init selecting state.
	if initCountAfterFirstAcquisition != 1 {
		t.Errorf("client entered initselecting state %d times, want: 1", initCountAfterFirstAcquisition)
	}

	if gotAddr, wantAddr := <-addrCh, (tcpip.AddressWithPrefix{}); gotAddr != wantAddr {
		t.Fatalf("lease did not correctly expire: got unexpected address = %s, want = %s", gotAddr, wantAddr)
	}

	<-clientTransitionsDone

	// Minus the first init where the client has gone through to acquire the
	// first address.
	if gotInit := c.stats.InitAcquire.Value() - initCountAfterFirstAcquisition; int(gotInit) != wantInitCount {
		t.Errorf("got %d inits after lease expiration, want: %d", gotInit, wantInitCount)
	}

	if gotRenew := c.stats.RenewAcquire.Value(); gotRenew != 0 {
		t.Errorf("got %d renews after lease expiration, want: 0", gotRenew)
	}
	if gotRebind := c.stats.RebindAcquire.Value(); gotRebind != 0 {
		t.Errorf("got %d rebinds after lease expiration, want: 0", gotRebind)
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
	if err = ep.SetSockOptBool(tcpip.BroadcastOption, true); err != nil {
		t.Fatalf("dhcp: setsockopt: %s", err)
	}

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	c1, c2 := teeConn(newEPConn(ctx, wq, ep))

	if _, err := NewServer(ctx, c1, []tcpip.Address{"\xc0\xa8\x03\x02"}, Config{
		ServerAddress: "\xc0\xa8\x03\x01",
		SubnetMask:    "\xff\xff\xff\x00",
		Router:        []tcpip.Address{"\xc0\xa8\x03\xF0"},
		DNS:           []tcpip.Address{"\x08\x08\x08\x08"},
		LeaseLength:   Seconds(30 * 60),
	}); err != nil {
		t.Fatal(err)
	}
	if _, err := NewServer(ctx, c2, []tcpip.Address{"\xc0\xa8\x04\x02"}, Config{
		ServerAddress: "\xc0\xa8\x04\x01",
		SubnetMask:    "\xff\xff\xff\x00",
		Router:        []tcpip.Address{"\xc0\xa8\x03\xF0"},
		DNS:           []tcpip.Address{"\x08\x08\x08\x08"},
		LeaseLength:   Seconds(30 * 60),
	}); err != nil {
		t.Fatal(err)
	}

	c := newZeroJitterClient(s, testNICID, linkAddr1, defaultAcquireTimeout, defaultBackoffTime, defaultRetransTime, nil)
	info := c.Info()
	if _, err := acquire(ctx, c, &info); err != nil {
		t.Fatal(err)
	}
}
