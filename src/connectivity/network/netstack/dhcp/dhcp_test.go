// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//go:build !build_with_native_toolchain

package dhcp

import (
	"context"
	"errors"
	"fmt"
	"math/rand"
	"testing"
	stdtime "time"

	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/sync"
	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/time"

	"github.com/google/go-cmp/cmp"
	"gvisor.dev/gvisor/pkg/bufferv2"
	"gvisor.dev/gvisor/pkg/tcpip"
	"gvisor.dev/gvisor/pkg/tcpip/header"
	"gvisor.dev/gvisor/pkg/tcpip/link/packetsocket"
	"gvisor.dev/gvisor/pkg/tcpip/link/sniffer"
	"gvisor.dev/gvisor/pkg/tcpip/network/arp"
	"gvisor.dev/gvisor/pkg/tcpip/network/ipv4"
	"gvisor.dev/gvisor/pkg/tcpip/stack"
	"gvisor.dev/gvisor/pkg/tcpip/transport/udp"
)

const (
	testNICID  = tcpip.NICID(1)
	serverAddr = tcpip.Address("\xc0\xa8\x03\x01")

	linkAddr1 = tcpip.LinkAddress("\x52\x11\x22\x33\x44\x52")
	linkAddr2 = tcpip.LinkAddress("\x52\x11\x22\x33\x44\x53")

	defaultAcquireTimeout = 1000 * time.Millisecond
	defaultBackoffTime    = 100 * time.Millisecond
	defaultRetransTime    = 400 * time.Millisecond
	largeTime             = 60 * time.Minute
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
		NetworkProtocols: []stack.NetworkProtocolFactory{
			arp.NewProtocol,
			ipv4.NewProtocol,
		},
		TransportProtocols: []stack.TransportProtocolFactory{
			udp.NewProtocol,
		},
	})
}

var _ stack.LinkEndpoint = (*endpoint)(nil)

type endpoint struct {
	dispatcher stack.NetworkDispatcher
	remote     []*endpoint
	// onWritePacket returns the packet to send or nil if no packets should be sent.
	//
	// Returns true if the returned packet buffer is newly allocated instead of the
	// provided packet buffer.
	onWritePacket func(*stack.PacketBuffer) (*stack.PacketBuffer, bool)
	// onPacketDelivered is called after a packet is delivered to each of remote's
	// network dispatchers.
	onPacketDelivered func()

	stack.LinkEndpoint
}

func (*endpoint) MTU() uint32 {
	// Determined experimentally; must be large enough to hold the longest packet
	// we try to parse in these tests, since our hook point is before fragment
	// reassembly (so it can't span multiple IP packets).
	return headerBaseSize + 100
}

func (*endpoint) Capabilities() stack.LinkEndpointCapabilities {
	return stack.CapabilityResolutionRequired
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

func (*endpoint) AddHeader(*stack.PacketBuffer) {}

func (e *endpoint) writePacket(pkt *stack.PacketBuffer) tcpip.Error {
	newBuf := false
	if pkt.NetworkProtocolNumber == ipv4.ProtocolNumber {
		if fn := e.onWritePacket; fn != nil {
			pkt, newBuf = fn(pkt)
			if pkt == nil {
				return nil
			}
		}
	}

	// DeliverNetworkPacket needs to be called in a new goroutine to avoid a
	// deadlock. However, tests use onPacketDelivered as a way to synchronize
	// tests with packet delivery so it also needs to be called in the goroutine.
	//
	// As of writing, a deadlock may occur when performing link resolution as
	// the neighbor table will send a solicitation while holding a lock and the
	// response advertisement will be sent in the same stack that sent the
	// solictation. When the response is received, the stack attempts to take
	// the same lock it already took before sending the solicitation, leading to
	// a deadlock. Basically, we attempt to lock the same lock twice in the same
	// call stack.
	//
	// TODO(gvisor.dev/issue/5289): don't use a new goroutine once we support
	// send and receive queues.
	if !newBuf {
		pkt.IncRef()
	}
	go func() {
		defer pkt.DecRef()

		for _, remote := range e.remote {
			if !remote.IsAttached() {
				panic(fmt.Sprintf("ep: %+v remote endpoint: %+v has not been `Attach`ed; call stack.CreateNIC to attach it", e, remote))
			}
			// the "remote" address for `other` is our local address and vice versa.
			func() {
				newPkt := pkt.CloneToInbound()
				defer newPkt.DecRef()
				newPkt.PktType = tcpip.PacketBroadcast

				remote.dispatcher.DeliverNetworkPacket(
					pkt.NetworkProtocolNumber,
					newPkt,
				)
			}()
		}

		if pkt.NetworkProtocolNumber == ipv4.ProtocolNumber {
			if fn := e.onPacketDelivered; fn != nil {
				fn()
			}
		}
	}()

	return nil
}

func (e *endpoint) WritePackets(pkts stack.PacketBufferList) (int, tcpip.Error) {
	i := 0
	for _, pkt := range pkts.AsSlice() {
		if err := e.writePacket(pkt); err != nil {
			return i, err
		}
	}
	return i, nil
}

func addEndpointToStack(t *testing.T, addresses []tcpip.Address, nicid tcpip.NICID, s *stack.Stack, linkEP stack.LinkEndpoint) {
	t.Helper()

	linkEP = packetsocket.New(linkEP)
	if testing.Verbose() {
		linkEP = sniffer.New(linkEP)
	}

	if err := s.CreateNIC(nicid, linkEP); err != nil {
		t.Fatalf("failed CreateNIC(%d, %v): %s", nicid, linkEP, err)
	}
	for _, address := range addresses {
		protocolAddress := tcpip.ProtocolAddress{
			Protocol:          ipv4.ProtocolNumber,
			AddressWithPrefix: address.WithPrefix(),
		}
		if err := s.AddProtocolAddress(nicid, protocolAddress, stack.AddressProperties{}); err != nil {
			t.Fatalf("AddProtocolAddress(%d, %#v, {}): %s", nicid, protocolAddress, err)
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
		onWritePacket: func(pkt *stack.PacketBuffer) (*stack.PacketBuffer, bool) {
			mu.Lock()
			mu.buffered++
			for mu.buffered < len(clientLinkEPs) {
				cond.Wait()
			}
			mu.Unlock()
			return pkt, false
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
			_, err := acquire(ctx, c, t.Name(), &info)
			errs <- err
		}()
	}

	if _, err := newEPConnServer(ctx, serverStack, defaultClientAddrs, defaultServerCfg, testServerOptions{}); err != nil {
		t.Fatalf("newEPConnServer failed: %s", err)
	}
}

func (c *Client) verifyClientStats(t *testing.T, want uint64) {
	t.Helper()
	if got := c.stats.SendDiscovers.Value(); got != want {
		t.Errorf("DHCPStats.SendDiscovers=%d want=%d", got, want)
	}
	if got := (c.stats.RecvOffers.Value() -
		c.stats.RecvOfferNoServerAddress.Value() -
		c.stats.RecvOfferOptsDecodeErrors.Value()); got != want {
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

func setupTestEnv(ctx context.Context, t *testing.T, serverCfg Config, testServerOptions testServerOptions) (clientStack, serverStack *stack.Stack, client, server *endpoint, _ *Client) {
	var serverLinkEP, clientLinkEP endpoint
	serverLinkEP.remote = append(serverLinkEP.remote, &clientLinkEP)
	clientLinkEP.remote = append(clientLinkEP.remote, &serverLinkEP)

	serverStack = createTestStack()
	addEndpointToStack(t, []tcpip.Address{serverAddr}, testNICID, serverStack, &serverLinkEP)

	clientStack = createTestStack()
	addEndpointToStack(t, nil, testNICID, clientStack, &clientLinkEP)

	if _, err := newEPConnServer(ctx, serverStack, defaultClientAddrs, serverCfg, testServerOptions); err != nil {
		t.Fatalf("newEPConnServer failed: %s", err)
	}
	c := newZeroJitterClient(clientStack, testNICID, linkAddr1, defaultAcquireTimeout, defaultBackoffTime, defaultRetransTime, nil)
	return clientStack, serverStack, &clientLinkEP, &serverLinkEP, c
}

type testDHCPCase struct {
	name              string
	testServerOptions testServerOptions
}

func TestDHCP(t *testing.T) {
	for _, tc := range []testDHCPCase{
		{"omitting Server Identifier in DHCPACK and DHCPNAK", testServerOptions{omitServerIdentifierWhenNotRequired: true}},
		{"including Server Identifier in DHCPACK and DHCPNAK", testServerOptions{omitServerIdentifierWhenNotRequired: false}},
		{"needing to drop a DHCPOFFER missing Server Identifier", testServerOptions{sendOfferWithoutServerIdentifierFirst: true}},
		{"needing to drop a DHCPOFFER with invalid options", testServerOptions{sendOfferWithInvalidOptionsFirst: true}},
	} {
		t.Run(tc.name, func(t *testing.T) {
			testDHCP(t, tc)
		})
	}
}

func testDHCP(t *testing.T, tc testDHCPCase) {
	var serverLinkEP, clientLinkEP endpoint
	serverLinkEP.remote = append(serverLinkEP.remote, &clientLinkEP)
	clientLinkEP.remote = append(clientLinkEP.remote, &serverLinkEP)

	serverStack := createTestStack()
	addEndpointToStack(t, []tcpip.Address{serverAddr}, testNICID, serverStack, &serverLinkEP)

	clientStack := createTestStack()
	addEndpointToStack(t, nil, testNICID, clientStack, &clientLinkEP)

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	if _, err := newEPConnServer(ctx, serverStack, defaultClientAddrs, defaultServerCfg, tc.testServerOptions); err != nil {
		t.Fatalf("newEPConnServer failed: %s", err)
	}

	// Avoid retransmissions by providing a large retransmit time, to make the
	// counter checks deterministic in this test.
	c0 := newZeroJitterClient(clientStack, testNICID, linkAddr1, defaultAcquireTimeout, defaultBackoffTime, largeTime, nil)
	info := c0.Info()
	{
		{
			cfg, err := acquire(ctx, c0, t.Name(), &info)
			if err != nil {
				t.Fatal(err)
			}
			if got, want := info.Acquired.Address, defaultClientAddrs[0]; got != want {
				t.Errorf("c.addr=%s, want=%s", got, want)
			}
			if got, want := cfg.SubnetMask, defaultServerCfg.SubnetMask; got != want {
				t.Errorf("cfg.SubnetMask=%s, want=%s", got, want)
			}
			if got, want := cfg.ServerAddress, defaultServerCfg.ServerAddress; got != want {
				t.Errorf("cfg.ServerAddress=%s, want=%s", got, want)
			}
			c0.verifyClientStats(t, 1)
		}
		{
			cfg, err := acquire(ctx, c0, t.Name(), &info)
			if err != nil {
				t.Fatal(err)
			}
			if got, want := info.Acquired.Address, defaultClientAddrs[0]; got != want {
				t.Errorf("c.addr=%s, want=%s", got, want)
			}
			if got, want := cfg.SubnetMask, defaultServerCfg.SubnetMask; got != want {
				t.Errorf("cfg.SubnetMask=%s, want=%s", got, want)
			}
			if got, want := cfg.ServerAddress, defaultServerCfg.ServerAddress; got != want {
				t.Errorf("cfg.ServerAddress=%s, want=%s", got, want)
			}
			c0.verifyClientStats(t, 2)
		}
	}

	{
		c1 := newZeroJitterClient(clientStack, testNICID, linkAddr2, defaultAcquireTimeout, defaultBackoffTime, largeTime, nil)
		info := c1.Info()
		cfg, err := acquire(ctx, c1, t.Name(), &info)
		if err != nil {
			t.Fatal(err)
		}
		if got, want := info.Acquired.Address, defaultClientAddrs[1]; got != want {
			t.Errorf("c.addr=%s, want=%s", got, want)
		}
		if got, want := cfg.SubnetMask, defaultServerCfg.SubnetMask; got != want {
			t.Errorf("cfg.SubnetMask=%s, want=%s", got, want)
		}
		if got, want := cfg.ServerAddress, defaultServerCfg.ServerAddress; got != want {
			t.Errorf("cfg.ServerAddress=%s, want=%s", got, want)
		}
		c1.verifyClientStats(t, 1)
	}

	{
		protocolAddress := tcpip.ProtocolAddress{
			Protocol:          ipv4.ProtocolNumber,
			AddressWithPrefix: info.Acquired,
		}
		properties := stack.AddressProperties{PEB: stack.NeverPrimaryEndpoint}
		if err := clientStack.AddProtocolAddress(testNICID, protocolAddress, properties); err != nil {
			t.Fatalf("AddProtocolAddress(%d, %#v, %#v): %s", testNICID, protocolAddress, properties, err)
		}
		defer clientStack.RemoveAddress(testNICID, info.Acquired.Address)
		cfg, err := acquire(ctx, c0, t.Name(), &info)
		if err != nil {
			t.Fatal(err)
		}
		if got, want := info.Acquired.Address, defaultClientAddrs[0]; got != want {
			t.Errorf("c.addr=%s, want=%s", got, want)
		}
		if got, want := cfg.SubnetMask, defaultServerCfg.SubnetMask; got != want {
			t.Errorf("cfg.SubnetMask=%s, want=%s", got, want)
		}
		if got, want := cfg.ServerAddress, defaultServerCfg.ServerAddress; got != want {
			t.Errorf("cfg.ServerAddress=%s, want=%s", got, want)
		}

		if diff := cmp.Diff(cfg, defaultServerCfg, cmp.AllowUnexported(time.Time{})); diff != "" {
			t.Errorf("got config diff (-want +got):\n%s", diff)
		}
		c0.verifyClientStats(t, 3)
	}
}

func mustMsgType(t *testing.T, h hdr) dhcpMsgType {
	t.Helper()

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

			_, _, _, serverEP, c := setupTestEnv(ctx, t, defaultServerCfg, testServerOptions{})
			serverEP.onWritePacket = func(pkt *stack.PacketBuffer) (*stack.PacketBuffer, bool) {
				func() {
					switch mustMsgType(t, hdr(pkt.Data().AsRange().ToSlice())) {
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
				return pkt, false
			}

			info := c.Info()
			cfg, err := acquire(ctx, c, t.Name(), &info)
			if tc.success {
				if err != nil {
					t.Fatal(err)
				}
				if got, want := info.Acquired.Address, defaultClientAddrs[0]; got != want {
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
	s := createTestStack()
	if err := s.CreateNIC(testNICID, &endpoint{}); err != nil {
		t.Fatalf("s.CreateNIC(_, nil) = %s", err)
	}
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
			c := NewClient(s, testNICID, "", 0, 0, tc.retran, nil)
			// When used to add jitter to backoff, 1s is subtracted from random number
			// to map [0s, +2s] -> [-1s, +1s], so add 1s here to compensate for that.
			c.rand = rand.New(&randSourceStub{src: int64(time.Second + tc.jitter)})
			if got := c.exponentialBackoff(tc.iteration); got != tc.want {
				t.Errorf("c.exponentialBackoff(%d) = %s, want: %s", tc.iteration, got, tc.want)
			}
		})
	}
}

func removeLostAddAcquired(t *testing.T, clientStack *stack.Stack, lost, acquired tcpip.AddressWithPrefix) {
	if lost != (tcpip.AddressWithPrefix{}) {
		if err := clientStack.RemoveAddress(testNICID, lost.Address); err != nil {
			t.Fatalf("RemoveAddress(%s): %s", lost.Address, err)
		}
	}
	if acquired != (tcpip.AddressWithPrefix{}) {
		protocolAddress := tcpip.ProtocolAddress{
			Protocol:          ipv4.ProtocolNumber,
			AddressWithPrefix: acquired,
		}
		if err := clientStack.AddProtocolAddress(testNICID, protocolAddress, stack.AddressProperties{}); err != nil {
			t.Fatalf("AddProtocolAddress(%d, %#v, {}): %s", testNICID, protocolAddress, err)
		}
	}
}

func TestAcquisitionAfterNAK(t *testing.T) {
	for _, tc := range []struct {
		name      string
		nakNthReq uint32
		// The time durations to advance in test when the current time is requested.
		durations                     []time.Duration
		initialStateTransitionTimeout bool
		wantInitAcq                   uint64
		wantRenewAcq                  uint64
		wantRebindAcq                 uint64
		wantNaks                      uint64
		wantAcks                      uint64
		wantDiscovers                 uint64
		wantReqs                      uint64
		testServerOptions             testServerOptions
	}{
		{
			name: "initial acquisition",
			// Nak the first address acquisition and let the client retry.
			nakNthReq: 1,
			durations: []time.Duration{
				// Start first acquisition.
				0,
				// Fail acquisition due to NAK and backoff.
				0,
				// Successful acquisition.
				0,
			},
			// Fires while waiting to retransmit after receiving NAK.
			initialStateTransitionTimeout: true,
			wantInitAcq:                   2,
			wantNaks:                      1,
			wantAcks:                      1,
			wantDiscovers:                 2,
			wantReqs:                      2,
			testServerOptions:             testServerOptions{},
		},
		{
			name: "initial acquisition, omit server identifier in ACK and NAK",
			// Nak the first address acquisition and let the client retry.
			nakNthReq: 1,
			durations: []time.Duration{
				// Start first acquisition.
				0,
				// Fail acquisition due to NAK and backoff.
				0,
				// Successful acquisition.
				0,
			},
			// Fires while waiting to retransmit after receiving NAK.
			initialStateTransitionTimeout: true,
			wantInitAcq:                   2,
			wantNaks:                      1,
			wantAcks:                      1,
			wantDiscovers:                 2,
			wantReqs:                      2,
			testServerOptions:             testServerOptions{omitServerIdentifierWhenNotRequired: true},
		},
		{
			name: "renew",
			// Let the first address acquisition go through so client can renew.
			nakNthReq: 2,
			durations: []time.Duration{
				// First acquisition.
				0,
				// Transition to renew.
				defaultRenewTime(defaultLeaseLength).Duration(),
				// Backoff while renewing.
				0,
				// Retry after NAK.
				0,
				// Calculate renew acquisition timeout.
				0,
				// Second acquisition after NAK.
				0,
			},
			// Fires while waiting to transition to RENEW after ACK.
			initialStateTransitionTimeout: true,
			wantInitAcq:                   2,
			wantRenewAcq:                  1,
			wantNaks:                      1,
			wantAcks:                      2,
			wantDiscovers:                 2,
			wantReqs:                      3,
			testServerOptions:             testServerOptions{},
		},
		{
			name: "rebind",
			// Let the first address acquisition go through so client can rebind.
			nakNthReq: 2,
			durations: []time.Duration{
				// First acquisition.
				0,
				// Transition to rebind.
				defaultRebindTime(defaultLeaseLength).Duration(),
				// Backoff while rebinding.
				0,
				// Retry after NAK.
				0,
				// Calculate rebind acquisition timeout.
				0,
				// Second acquisition after NAK.
				0,
			},
			// Transition to REBIND occurs immediately without waiting for timeout.
			initialStateTransitionTimeout: false,
			wantInitAcq:                   2,
			wantRebindAcq:                 1,
			wantNaks:                      1,
			wantAcks:                      2,
			wantDiscovers:                 2,
			wantReqs:                      3,
			testServerOptions:             testServerOptions{},
		},
	} {
		t.Run(tc.name, func(t *testing.T) {
			ctx, cancel := context.WithCancel(context.Background())
			var wg sync.WaitGroup
			defer func() {
				cancel()
				wg.Wait()
			}()

			clientStack, _, clientEP, serverEP, c := setupTestEnv(ctx, t, defaultServerCfg, tc.testServerOptions)
			clientTransitionsDone := make(chan struct{})
			c.now = stubTimeNow(ctx, time.Now(), tc.durations, clientTransitionsDone)
			// Do not allow the context to time out. A context time out in this test
			// causes superfluous DHCP messages to be sent, leading to flakey
			// assertions.
			c.contextWithTimeout = func(ctx context.Context, _ time.Duration) (context.Context, context.CancelFunc) {
				return context.WithCancel(ctx)
			}
			timeoutCh := make(chan time.Time)
			c.retransTimeout = func(time.Duration) <-chan time.Time {
				return timeoutCh
			}

			requestDelivered := make(chan struct{})
			clientEP.onPacketDelivered = func() {
				signal(ctx, requestDelivered)
			}

			unblockResponse := make(chan struct{})
			var ackCnt uint32
			serverEP.onWritePacket = func(pkt *stack.PacketBuffer) (*stack.PacketBuffer, bool) {
				waitForSignal(ctx, unblockResponse)
				if mustMsgType(t, hdr(pkt.Data().AsRange().ToSlice())) != dhcpACK {
					return pkt, false
				}

				ackCnt++
				if ackCnt != tc.nakNthReq {
					return pkt, false
				}

				pkt = mustCloneWithNewMsgType(t, pkt, dhcpNAK)

				// Add a message option. An earlier version of the client incorrectly
				// rejected dhcpNAK with a populated message.
				h := hdr(pkt.Data().AsRange().ToSlice())
				opts, err := h.options()
				if err != nil {
					t.Fatalf("failed to get options from header: %s", err)
				}
				messageOption := option{
					code: optMessage,
					body: []byte("no lease for you"),
				}
				opts = append(opts, messageOption)
				b := make(hdr, len(h)+1+len(messageOption.body))
				if n, l := copy(b, h), len(h); n != l {
					t.Errorf("failed to copy header bytes, want=%d got=%d", l, n)
				}
				b.setOptions(opts)
				pkt.Data().CapLength(0)
				pkt.Data().AppendView(bufferv2.NewViewWithData(b))

				// Rewrite all the headers and IP checksum. Yes, this is all
				// required.
				delta := uint16(len(b) - len(h))
				udpHeader := header.UDP(pkt.TransportHeader().Slice())
				udpHeader.SetLength(udpHeader.Length() + delta)
				ipv4Header := header.IPv4(pkt.NetworkHeader().Slice())
				ipv4Header.SetTotalLength(ipv4Header.TotalLength() + delta)
				ipv4Header.SetChecksum(0)
				ipv4Header.SetChecksum(^ipv4Header.CalculateChecksum())
				return pkt, true
			}
			responseDelivered := make(chan struct{})
			serverEP.onPacketDelivered = func() {
				signal(ctx, responseDelivered)
			}

			wg.Add(1)
			go func() {
				defer wg.Done()
				for i := uint64(0); i < tc.wantNaks+tc.wantAcks; i++ {
					waitForSignal(ctx, requestDelivered)
					signal(ctx, unblockResponse)
					waitForSignal(ctx, responseDelivered)
					waitForSignal(ctx, requestDelivered)
					signal(ctx, unblockResponse)
					waitForSignal(ctx, responseDelivered)
					if tc.initialStateTransitionTimeout && i == 0 {
						signalTimeout(ctx, timeoutCh)
					}
				}
			}()

			c.acquiredFunc = func(_ context.Context, lost, acquired tcpip.AddressWithPrefix, _ Config) {
				removeLostAddAcquired(t, clientStack, lost, acquired)
			}

			wg.Add(1)
			go func() {
				defer wg.Done()
				if got, want := c.Run(ctx), (tcpip.AddressWithPrefix{
					Address:   defaultClientAddrs[0],
					PrefixLen: defaultServerCfg.SubnetMask.Prefix(),
				}); got != want {
					t.Errorf("got c.Run(_): %s, want: %s", got, want)
				}
			}()

			<-clientTransitionsDone

			if got := c.stats.InitAcquire.Value(); got != tc.wantInitAcq {
				t.Errorf("got InitAcquire count: %d, want: %d", got, tc.wantInitAcq)
			}
			if got := c.stats.RenewAcquire.Value(); got != tc.wantRenewAcq {
				t.Errorf("got RenewAcquire count: %d, want: %d", got, tc.wantRenewAcq)
			}
			if got := c.stats.RebindAcquire.Value(); got != tc.wantRebindAcq {
				t.Errorf("got RebindAcquire count: %d, want: %d", got, tc.wantRebindAcq)
			}
			if got := c.stats.RecvNaks.Value(); got != tc.wantNaks {
				t.Errorf("got RecvNaks count: %d, want: %d", got, tc.wantNaks)
			}
			if got := c.stats.RecvAcks.Value(); got != tc.wantAcks {
				t.Errorf("got RecvAcks count: %d, want: %d", got, tc.wantAcks)
			}
			if got := c.stats.SendDiscovers.Value(); got != tc.wantDiscovers {
				t.Errorf("got SendDiscovers count: %d, want: %d", got, tc.wantDiscovers)
			}
			if got := c.stats.SendRequests.Value(); got != tc.wantReqs {
				t.Errorf("got SendRequests count: %d, want: %d", got, tc.wantReqs)
			}
			if got := c.stats.ReacquireAfterNAK.Value(); got != 1 {
				t.Errorf("got ReacquireAfterNAK count: %d, want 1", got)
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
			ctx, cancel := context.WithCancel(context.Background())
			defer cancel()

			_, _, clientEP, serverEP, c := setupTestEnv(ctx, t, defaultServerCfg, testServerOptions{})
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
			clientEP.onWritePacket = func(pkt *stack.PacketBuffer) (*stack.PacketBuffer, bool) {
				signal(ctx, requestSent)

				return pkt, false
			}

			unblockResponse := make(chan struct{})
			var dropServerPackets bool
			serverEP.onWritePacket = func(pkt *stack.PacketBuffer) (*stack.PacketBuffer, bool) {
				waitForSignal(ctx, unblockResponse)
				if dropServerPackets {
					return nil, false
				}
				return pkt, false
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

			if _, err := acquire(ctx, c, t.Name(), &info); err != nil {
				t.Fatalf("acquire(...) failed: %s", err)
			}

			wg.Wait()

			if diff := cmp.Diff(tc.wantTimeouts, gotTimeouts); diff != "" {
				t.Errorf("acquire(...) got timeouts diff (-want +got):\n%s", diff)
			}
			if got := c.stats.RecvOfferTimeout.Value(); int(got) != tc.offerTimeouts {
				t.Errorf("acquire(...) got RecvOfferTimeout count: %d, want: %d", got, tc.offerTimeouts)
			}
			if got := c.stats.RecvOffers.Value(); got != 1 {
				t.Errorf("acquire(...) got RecvOffers count: %d, want: 1", got)
			}
			if got := c.stats.RecvAckTimeout.Value(); int(got) != tc.ackTimeouts {
				t.Errorf("acquire(...) got RecvAckTimeout count: %d, want: %d", got, tc.ackTimeouts)
			}
			if got := c.stats.RecvAcks.Value(); got != 1 {
				t.Errorf("acquire(...) got RecvAcks count: %d, want: 1", got)
			}
		})
	}
}

// Test backoff in renew and rebind conforms to RFC 2131 4.4.5. That is, backoff
// in renew should be half of remaining time to T2, and backoff in rebind should
// be half of the remaining time to lease expiration.
//
// https://tools.ietf.org/html/rfc2131#page-41
func TestRenewRebindBackoff(t *testing.T) {
	for i, tc := range []struct {
		state           dhcpClientState
		rebindTime      time.Duration
		leaseExpiration time.Duration
		wantTimeouts    []time.Duration
	}{
		{
			state:      renewing,
			rebindTime: 800 * time.Second,
			wantTimeouts: []time.Duration{
				400 * time.Second,
				200 * time.Second,
				100 * time.Second,
				60 * time.Second,
				60 * time.Second,
			},
		},
		{
			state:      renewing,
			rebindTime: 1600 * time.Second,
			wantTimeouts: []time.Duration{
				800 * time.Second,
				400 * time.Second,
				200 * time.Second,
				100 * time.Second,
				60 * time.Second,
				60 * time.Second,
			},
		},
		{
			state:           rebinding,
			leaseExpiration: 800 * time.Second,
			wantTimeouts: []time.Duration{
				400 * time.Second,
				200 * time.Second,
				100 * time.Second,
				60 * time.Second,
				60 * time.Second,
			},
		},
	} {
		t.Run(fmt.Sprintf("%d:%s", i, tc.state), func(t *testing.T) {
			ctx, cancel := context.WithCancel(context.Background())
			defer cancel()

			clientStack, _, _, serverEP, c := setupTestEnv(ctx, t, defaultServerCfg, testServerOptions{})

			now := time.Now()
			info := c.Info()
			info.RebindTime = now.Add(tc.rebindTime)
			info.LeaseExpiration = now.Add(tc.leaseExpiration)
			c.info.Store(info)

			serverEP.onWritePacket = func(*stack.PacketBuffer) (*stack.PacketBuffer, bool) {
				// Don't send any response, keep the client renewing / rebinding
				// to test backoff in these states.
				return nil, false
			}

			// Start from time 0, and then advance time in test based on expected
			// timeouts. This plus the stubbed out `retransTimeout` below, simulates
			// time passing in this test.
			durationsBetweenNows := append(
				[]time.Duration{0},
				tc.wantTimeouts[:len(tc.wantTimeouts)-1]...,
			)
			c.now = stubTimeNow(ctx, now, durationsBetweenNows, nil)

			timeoutCh := make(chan time.Time)
			var gotTimeouts []time.Duration
			c.retransTimeout = func(d time.Duration) <-chan time.Time {
				gotTimeouts = append(gotTimeouts, d)
				return timeoutCh
			}

			errs := make(chan error)
			go func() {
				info := c.Info()
				if tc.state != initSelecting {
					// Uphold the invariant that an address is assigned in this state. Without this, ARP
					// would immediately fail as it tries to resolve the server's unicast link address.
					assigned := tcpip.AddressWithPrefix{
						Address:   defaultClientAddrs[0],
						PrefixLen: 24,
					}
					info.Assigned = assigned
					info.Acquired = assigned
					protocolAddress := tcpip.ProtocolAddress{
						Protocol:          ipv4.ProtocolNumber,
						AddressWithPrefix: assigned,
					}
					if err := clientStack.AddProtocolAddress(testNICID, protocolAddress, stack.AddressProperties{}); err != nil {
						t.Fatalf("AddProtocolAddress(%d, %#v, {}): %s", testNICID, protocolAddress, err)
					}
				}
				info.State = tc.state
				info.Config.ServerAddress = serverAddr
				_, err := acquire(ctx, c, t.Name(), &info)
				errs <- err
			}()

			// Block `acquire` after the last `now` is called (happens before timeout
			// chan is used), so the test is consistent. Otherwise `acquire` in the
			// goroutine above will continue to retry and extra timeouts will be
			// appended to `gotTimeouts`.
			for i := 0; i < len(durationsBetweenNows)-1; i++ {
				select {
				case timeoutCh <- time.Time{}:
				case err := <-errs:
					t.Fatalf("acquire(...) failed: %s", err)
				}
			}
			cancel()
			if err := <-errs; !errors.Is(err, context.Canceled) {
				t.Fatalf("acquire(...) failed: %s", err)
			}

			if diff := cmp.Diff(tc.wantTimeouts, gotTimeouts); diff != "" {
				t.Errorf("got retransmission timeouts diff (-want +got):\n%s", diff)
			}
		})
	}
}

// mustCloneWithNewMsgType returns a clone of the specified packet buffer
// with DHCP message type set to `msgType` specified in the argument.
// This function does not make a deep copy of packet buffer passed in except
// for the part it has to modify.
func mustCloneWithNewMsgType(t *testing.T, pkt *stack.PacketBuffer, msgType dhcpMsgType) *stack.PacketBuffer {
	t.Helper()

	pkt = pkt.Clone()

	h := hdr(pkt.Data().AsRange().ToSlice())
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
	header.UDP(pkt.TransportHeader().Slice()).SetChecksum(0)

	pkt.Data().CapLength(0)
	pkt.Data().AppendView(bufferv2.NewViewWithData(h))
	return pkt
}

func TestRetransmissionTimeoutWithUnexpectedPackets(t *testing.T) {
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	_, _, clientEP, serverEP, c := setupTestEnv(ctx, t, defaultServerCfg, testServerOptions{})

	timeoutCh := make(chan time.Time)
	c.retransTimeout = func(time.Duration) <-chan time.Time {
		return timeoutCh
	}

	requestSent := make(chan struct{})
	clientEP.onPacketDelivered = func() {
		signal(ctx, requestSent)
	}

	unblockResponse := make(chan struct{})
	responseSent := make(chan struct{})

	var serverShouldDecline bool
	serverEP.onWritePacket = func(pkt *stack.PacketBuffer) (*stack.PacketBuffer, bool) {
		waitForSignal(ctx, unblockResponse)

		if serverShouldDecline {
			return mustCloneWithNewMsgType(t, pkt, dhcpDECLINE), true
		}
		return pkt, false
	}
	serverEP.onPacketDelivered = func() {
		signal(ctx, responseSent)
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
	if _, err := acquire(ctx, c, t.Name(), &info); err != nil {
		t.Fatalf("acquire(...) failed: %s", err)
	}

	wg.Wait()

	if got := c.stats.RecvOfferTimeout.Value(); got != 1 {
		t.Errorf("acquire(...) got RecvOfferTimeout count: %d, want: 1", got)
	}
	if got := c.stats.RecvOfferUnexpectedType.Value(); got != 1 {
		t.Errorf("acquire(...) got RecvOfferUnexpectedType count: %d, want: 1", got)
	}
	if got := c.stats.RecvOffers.Value(); got != 1 {
		t.Errorf("acquire(...) got RecvOffers count: %d, want: 1", got)
	}
	if got := c.stats.RecvAckTimeout.Value(); got != 1 {
		t.Errorf("acquire(...) got RecvAckTimeout count: %d, want: 1", got)
	}
	if got := c.stats.RecvAckUnexpectedType.Value(); got != 1 {
		t.Errorf("acquire(...) got RecvAckUnexpectedType count: %d, want: 1", got)
	}
	if got := c.stats.RecvAcks.Value(); got != 1 {
		t.Errorf("acquire(...) got RecvAcks count: %d, want: 1", got)
	}
}

func TestClientDropsIrrelevantFrames(t *testing.T) {
	// TODO(https://fxbug.dev/79556): Extend this test to cover the stat tracking frames discarded
	// due to an invalid PacketType.
	for _, tc := range []struct {
		clientPort     uint16
		transportProto tcpip.TransportProtocolNumber
	}{
		{
			clientPort:     2,
			transportProto: header.UDPProtocolNumber,
		},
		{
			clientPort:     ClientPort,
			transportProto: header.TCPProtocolNumber,
		},
	} {
		ctx, cancel := context.WithCancel(context.Background())
		defer cancel()

		_, _, clientEP, serverEP, c := setupTestEnv(ctx, t, defaultServerCfg, testServerOptions{})

		timeoutCh := make(chan time.Time)
		c.retransTimeout = func(time.Duration) <-chan time.Time {
			return timeoutCh
		}

		requestSent := make(chan struct{})
		clientEP.onPacketDelivered = func() {
			signal(ctx, requestSent)
		}

		unblockResponse := make(chan struct{})
		responseSent := make(chan struct{})

		serverEP.onWritePacket = func(pkt *stack.PacketBuffer) (*stack.PacketBuffer, bool) {
			waitForSignal(ctx, unblockResponse)

			// Here, we modify fields in the DHCP packet based on the parameters
			// specified in each test case. Each of these modifications can cause
			// the client to discard the packet when the given field is set to an
			// unexpected value.
			pkt = pkt.Clone()

			// Destination port number.
			h := header.UDP(pkt.TransportHeader().Slice())
			h.SetDestinationPort(tc.clientPort)

			// Transport protocol number.
			ip := header.IPv4(pkt.NetworkHeader().Slice())
			ip.Encode(&header.IPv4Fields{
				Protocol:    uint8(tc.transportProto),
				TotalLength: ip.TotalLength(),
			})
			ip.SetChecksum(0)
			ip.SetChecksum(^ip.CalculateChecksum())

			return pkt, true
		}
		serverEP.onPacketDelivered = func() {
			signal(ctx, responseSent)
		}

		var wg sync.WaitGroup
		wg.Add(1)
		go func() {
			wg.Done()

			// Send dhcpOFFER.
			waitForSignal(ctx, requestSent)

			// Receive discardable response from server.
			signal(ctx, unblockResponse)
			waitForSignal(ctx, responseSent)

			// Then timeout.
			signalTimeout(ctx, timeoutCh)
			cancel()
		}()

		info := c.Info()
		if _, err := acquire(ctx, c, t.Name(), &info); err == nil {
			t.Fatal("acquire(...) expected to fail after context is cancelled")
		}

		wg.Wait()

		boolToInt := func(val bool) uint64 {
			if val {
				return 1
			}
			return 0
		}

		expectInvalidPort := tc.clientPort != ClientPort
		var gotInvalidPortCount uint64
		if got, ok := c.stats.PacketDiscardStats.InvalidPort.Get(uint64(tc.clientPort)); ok {
			gotInvalidPortCount = got.Value()
		}

		if gotInvalidPortCount != boolToInt(expectInvalidPort) {
			t.Errorf("acquire(...) got discarded packet count (invalid port): %d, want: %d", gotInvalidPortCount, boolToInt(expectInvalidPort))
		}

		expectInvalidTransProto := tc.transportProto != header.UDPProtocolNumber
		var gotInvalidTransProtoCount uint64
		if got, ok := c.stats.PacketDiscardStats.InvalidTransProto.Get(uint64(tc.transportProto)); ok {
			gotInvalidTransProtoCount = got.Value()
		}

		if gotInvalidTransProtoCount != boolToInt(expectInvalidTransProto) {
			t.Errorf("acquire(...) got discarded packet count (invalid transport protocol number): %d, want: %d", gotInvalidTransProtoCount, boolToInt(expectInvalidTransProto))
		}

		if got := c.stats.RecvOffers.Value(); got != 0 {
			t.Errorf("acquire(...) got RecvOffers count: %d, want: 0", got)
		}
		if got := c.stats.RecvOfferTimeout.Value(); got != 1 {
			t.Errorf("acquire(...) got RecvOfferTimeout count: %d, want: 1", got)
		}
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

func (c *Client) verifyRecentStateHistory(t *testing.T, expectedStateHistory []dhcpClientState) error {
	t.Helper()

	var got []string
	for _, e := range c.StateRecentHistory() {
		got = append(got, e.Content)
	}

	var want []string
	for _, s := range expectedStateHistory {
		want = append(want, s.String())
	}

	if diff := cmp.Diff(want, got); diff != "" {
		return fmt.Errorf("c.StateRecentHistory() mismatch (-want +got):\n%s", diff)
	}

	return nil
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
		durations            []time.Duration
		expectedStateHistory []dhcpClientState
	}{
		{
			name:           "Renew",
			typ:            testRenew,
			acquireTimeout: defaultAcquireTimeout,
			durations: []time.Duration{
				// Start first transaction.
				0,
				// Transition to renew.
				renewTime.Duration(),
				// Start second transaction in renew.
				0,
			},
			expectedStateHistory: []dhcpClientState{
				initSelecting,
				bound,
				bound,
				renewing,
				bound,
				bound,
			},
		},
		{
			name:           "Rebind",
			typ:            testRebind,
			acquireTimeout: defaultAcquireTimeout,
			durations: []time.Duration{
				// Start first transaction.
				0,
				// Transition to renew.
				renewTime.Duration(),
				// Start second transaction in renew, and expect it to timeout.
				(rebindTime - renewTime).Duration() - 10*time.Millisecond,
				// Transition to rebind.
				10 * time.Millisecond,
				// Start third transaction in rebind.
				0,
			},
			expectedStateHistory: []dhcpClientState{
				initSelecting,
				bound,
				bound,
				renewing,
				renewing,
				rebinding,
				bound,
				bound,
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
				// Start first transaction.
				0,
				// Transition to renew.
				renewTime.Duration(),
				// Start second transaction in renew, and expect it to timeout.
				(rebindTime - renewTime).Duration() - 10*time.Millisecond,
				// Transition to rebind.
				10 * time.Millisecond,
				// Start third transaction in rebind.
				0,
			},
			expectedStateHistory: []dhcpClientState{
				initSelecting,
				bound,
				bound,
				renewing,
				renewing,
				rebinding,
				bound,
				bound,
			},
		},
		{
			name:           "LeaseExpire",
			typ:            testLeaseExpire,
			acquireTimeout: defaultAcquireTimeout,
			durations: []time.Duration{
				// Start first transaction.
				0,
				// Transition to renew.
				renewTime.Duration(),
				// Start second transaction in renew, and expect it to timeout.
				(rebindTime - renewTime).Duration() - 10*time.Millisecond,
				// Transition to rebind.
				10 * time.Millisecond,
				// Start third transaction in rebind, and expect it to timeout.
				(leaseLength - rebindTime).Duration() - 10*time.Millisecond,
				// Transition to lease expiration.
				10 * time.Millisecond,
				// Start fourth transaction.
				0,
			},
			expectedStateHistory: []dhcpClientState{
				initSelecting,
				bound,
				bound,
				renewing,
				renewing,
				rebinding,
				rebinding,
				rebinding,
				initSelecting,
				bound,
				bound,
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
				// Start first transaction.
				0,
				// Transition to renew.
				renewTime.Duration(),
				// Start second transaction in renew, and expect it to timeout.
				(rebindTime - renewTime).Duration() - 10*time.Millisecond,
				// Transition to rebind.
				10 * time.Millisecond,
				// Start third transaction in renew, and expect it to timeout.
				(leaseLength - rebindTime).Duration() - 10*time.Millisecond,
				// Transition to lease expiration.
				10 * time.Millisecond,
				// Start fourth transaction.
				0,
			},
			expectedStateHistory: []dhcpClientState{
				initSelecting,
				bound,
				bound,
				renewing,
				renewing,
				rebinding,
				rebinding,
				rebinding,
				initSelecting,
				bound,
				bound,
			},
		},
	} {
		t.Run(tc.name, func(t *testing.T) {
			var wg sync.WaitGroup
			defer wg.Wait()
			ctx, cancel := context.WithCancel(context.Background())
			defer cancel()

			s := createTestStack()
			if err := s.CreateNIC(testNICID, &endpoint{}); err != nil {
				t.Fatalf("s.CreateNIC(_, nil) = %s", err)
			}

			c := newZeroJitterClient(s, testNICID, linkAddr1, tc.acquireTimeout, defaultBackoffTime, defaultRetransTime, nil)

			c.acquire = func(ctx context.Context, _ *Client, _ string, info *Info) (Config, error) {
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

				info.Acquired = tcpip.AddressWithPrefix{
					Address:   "\xc0\xa8\x03\x02",
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
			c.acquiredFunc = func(ctx context.Context, lost, acquired tcpip.AddressWithPrefix, _ Config) {
				if lost != curAddr {
					t.Fatalf("aquisition %d: curAddr=%s, lost=%s", count, curAddr, lost)
				}

				count++
				curAddr = acquired

				// Respond to context cancellation to avoid deadlock when enclosing test
				// times out.
				select {
				case <-ctx.Done():
				case addrCh <- curAddr:
				}
			}

			wg.Add(1)
			go func() {
				defer wg.Done()
				// Avoid waiting for ARP on sending DHCPRELEASE.
				s.AddStaticNeighbor(testNICID, header.IPv4ProtocolNumber, c.Info().Config.ServerAddress, tcpip.LinkAddress([]byte{0, 1, 2, 3, 4, 5}))

				lost := c.Run(ctx)
				c.acquiredFunc(ctx, lost, tcpip.AddressWithPrefix{}, Config{})
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
			if err := c.verifyRecentStateHistory(t, tc.expectedStateHistory); err != nil {
				t.Error(err)
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

	s := createTestStack()
	if err := s.CreateNIC(testNICID, &endpoint{}); err != nil {
		t.Fatalf("s.CreateNIC(_, nil) = %s", err)
	}

	c := newZeroJitterClient(s, testNICID, linkAddr1, acquireTimeout, backoffTime, defaultRetransTime, nil)
	// Only returns a valid config in the first acquisition. Blocks until context
	// cancellation in following acquisitions. This makes sure the client is stuck
	// in init selecting state after lease expiration.
	firstAcquisition := true
	acquiredAddr := tcpip.AddressWithPrefix{
		Address:   "\xc0\xa8\x03\x02",
		PrefixLen: 24,
	}
	c.acquire = func(ctx context.Context, _ *Client, _ string, info *Info) (Config, error) {
		if !firstAcquisition {
			// Simulates a timeout using the deadline from context.
			<-ctx.Done()
			return Config{}, fmt.Errorf("fake test timeout error: %w", ctx.Err())
		}
		firstAcquisition = false
		info.Acquired = acquiredAddr
		return Config{LeaseLength: leaseLength}, nil
	}

	// wantInitCount is the number of times we want the client to enter init
	// selecting after lease expiration before test exits. This makes us believe
	// the client is correctly staying in the init selecting state.
	const wantInitCount = 10
	durations := []time.Duration{
		// Start first transaction.
		0,
		// Transition to lease expiration when scheduling state transition timer:
		// this will cause the client to go into init once.
		leaseLength.Duration(),
	}
	// Make the client enter init N-1 more times.
	for i := 0; i < wantInitCount-1; i++ {
		// Start new transaction in initSelecting.
		durations = append(durations, 0)
		// Schedule state transition timeout.
		durations = append(durations, 0)
	}
	// Start the N+1th transaction where the Nth InitAcquire increment occurs.
	durations = append(durations, 0)

	clientTransitionsDone := make(chan struct{})
	c.now = stubTimeNow(ctx, time.Time{}, durations, clientTransitionsDone)
	// Manually signal N timeouts so the client does not race on a select between
	// <-ctx.Done() and <-c.retransTimeout() for the N+1th timeout.
	retransTimeoutCh := make(chan time.Time)
	c.retransTimeout = func(time.Duration) <-chan time.Time {
		return retransTimeoutCh
	}
	wg.Add(1)
	go func() {
		defer wg.Done()
		for i := 0; i < wantInitCount; i++ {
			signalTimeout(ctx, retransTimeoutCh)
		}
	}()

	addrCh := make(chan tcpip.AddressWithPrefix)
	c.acquiredFunc = func(ctx context.Context, _, acquired tcpip.AddressWithPrefix, _ Config) {
		// Respond to context cancellation to avoid deadlock when enclosing test
		// times out.
		select {
		case <-ctx.Done():
		case addrCh <- acquired:
		}
	}

	wg.Add(1)
	go func() {
		defer wg.Done()
		// Avoid waiting for ARP on sending DHCPRELEASE.
		s.AddStaticNeighbor(testNICID, header.IPv4ProtocolNumber, c.Info().Config.ServerAddress, tcpip.LinkAddress([]byte{0, 1, 2, 3, 4, 5}))

		if got, want := c.Run(ctx), (tcpip.AddressWithPrefix{}); got != want {
			t.Errorf("got c.Run(_): %s, want: %s", got, want)
		}
		c.acquiredFunc(ctx, tcpip.AddressWithPrefix{}, tcpip.AddressWithPrefix{}, Config{})
	}()

	if gotAddr, wantAddr := <-addrCh, acquiredAddr; gotAddr != wantAddr {
		t.Fatalf("unexpected acquired address: got = %s, want = %s", gotAddr, wantAddr)
	}

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

func TestOptionOverloading(t *testing.T) {
	h := hdr([]byte{0x02, 0x01, 0x06, 0x00, 0xc8, 0x37, 0xbe, 0x73, 0x00,
		0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc0, 0xa8, 0x2b, 0x92,
		0xc0, 0xa8, 0x2b, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0f, 0x60,
		0x0a, 0x23, 0x93, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x63, 0x82, 0x53, 0x63,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0xff})
	if !h.isValid() {
		t.Fatal("failed to decode header")
	}

	contains := func(opts options, opt optionCode) bool {
		for _, o := range opts {
			if o.code == opt {
				return true
			}
		}
		return false
	}

	errorIfNotContains := func(opts options, opt optionCode) {
		if !contains(opts, opt) {
			t.Errorf("option not found: %d", opt)
		}
	}

	errorIfContains := func(opts options, opt optionCode) {
		if contains(opts, opt) {
			t.Errorf("unexpected option found: %d", opt)
		}
	}

	addOption := func(slice []byte, opt optionCode, value byte) []byte {
		slice[0] = byte(opt)
		slice[1] = 0x01
		slice[2] = value
		slice[3] = byte(optEnd)
		return slice[3:]
	}

	// Add a normal option for 'Subnet Mask' option.
	optionsField := addOption(h[headerBaseSize:], optSubnetMask, 0x01)

	opts, err := h.options()
	if err != nil {
		t.Fatalf("h.options(): %s", err)
	}

	errorIfNotContains(opts, optSubnetMask)

	// Add 'file' option overloading.
	addOption(optionsField, optOverload, byte(optOverloadValueFile))

	// Add a 'file' option for 'Time Offset' option.
	fileField := addOption(h.file(), optTimeOffset, 0x01)

	// Add a 'file' option for 'Router' option.
	addOption(fileField, optRouter, 0x01)

	opts, err = h.options()
	if err != nil {
		t.Fatalf("h.options(): %s", err)
	}

	errorIfNotContains(opts, optSubnetMask)
	errorIfNotContains(opts, optTimeOffset)
	errorIfNotContains(opts, optRouter)
	errorIfNotContains(opts, optOverload)

	// Add 'sname' option overloading.
	addOption(optionsField, optOverload, byte(optOverloadValueSname))

	// Add a 'sname' option for 'Time Server' option.
	addOption(h.sname(), optTimeServer, 0x01)

	opts, err = h.options()
	if err != nil {
		t.Fatalf("h.options(): %s", err)
	}

	errorIfNotContains(opts, optSubnetMask)
	errorIfContains(opts, optTimeOffset)
	errorIfContains(opts, optRouter)
	errorIfNotContains(opts, optTimeServer)
	errorIfNotContains(opts, optOverload)

	// Add 'both' option overloading.
	addOption(optionsField, optOverload, byte(optOverloadValueBoth))

	opts, err = h.options()
	if err != nil {
		t.Fatalf("h.options(): %s", err)
	}

	errorIfNotContains(opts, optSubnetMask)
	errorIfNotContains(opts, optTimeOffset)
	errorIfNotContains(opts, optRouter)
	errorIfNotContains(opts, optTimeServer)
	errorIfNotContains(opts, optOverload)
}

func TestTwoServers(t *testing.T) {
	var serverLinkEP, clientLinkEP endpoint
	serverLinkEP.remote = append(serverLinkEP.remote, &clientLinkEP)
	clientLinkEP.remote = append(clientLinkEP.remote, &serverLinkEP)

	serverStack := createTestStack()
	addEndpointToStack(t, []tcpip.Address{serverAddr}, testNICID, serverStack, &serverLinkEP)

	clientStack := createTestStack()
	addEndpointToStack(t, nil, testNICID, clientStack, &clientLinkEP)

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	if _, err := newEPConnServer(ctx, serverStack, []tcpip.Address{"\xc0\xa8\x03\x02"}, Config{
		ServerAddress: "\xc0\xa8\x03\x01",
		SubnetMask:    "\xff\xff\xff\x00",
		Router:        []tcpip.Address{"\xc0\xa8\x03\xF0"},
		DNS:           []tcpip.Address{"\x08\x08\x08\x08"},
		LeaseLength:   Seconds(30 * 60),
	}, testServerOptions{}); err != nil {
		t.Fatal(err)
	}
	if _, err := newEPConnServer(ctx, serverStack, []tcpip.Address{"\xc0\xa8\x04\x02"}, Config{
		ServerAddress: "\xc0\xa8\x04\x01",
		SubnetMask:    "\xff\xff\xff\x00",
		Router:        []tcpip.Address{"\xc0\xa8\x03\xF0"},
		DNS:           []tcpip.Address{"\x08\x08\x08\x08"},
		LeaseLength:   Seconds(30 * 60),
	}, testServerOptions{}); err != nil {
		t.Fatal(err)
	}

	c := newZeroJitterClient(clientStack, testNICID, linkAddr1, defaultAcquireTimeout, defaultBackoffTime, defaultRetransTime, nil)
	info := c.Info()
	if _, err := acquire(ctx, c, t.Name(), &info); err != nil {
		t.Fatal(err)
	}
}

func TestClientRestartIPHeader(t *testing.T) {
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	clientStack, _, clientEP, _, c := setupTestEnv(ctx, t, defaultServerCfg, testServerOptions{})

	type Packet struct {
		Addresses struct {
			Source, Destination tcpip.Address
		}
		Ports struct {
			Source, Destination uint16
		}
		Options options
	}

	types := []dhcpMsgType{
		dhcpDISCOVER,
		dhcpREQUEST,
		dhcpRELEASE,
	}

	const iterations = 3

	packets := make(chan Packet, len(types)*iterations)
	clientEP.onWritePacket = func(pkt *stack.PacketBuffer) (*stack.PacketBuffer, bool) {
		var packet Packet
		ipv4Packet := header.IPv4(pkt.Data().AsRange().ToSlice())
		packet.Addresses.Source = ipv4Packet.SourceAddress()
		packet.Addresses.Destination = ipv4Packet.DestinationAddress()
		udpPacket := header.UDP(ipv4Packet.Payload())
		packet.Ports.Source = udpPacket.SourcePort()
		packet.Ports.Destination = udpPacket.DestinationPort()
		dhcpPacket := hdr(udpPacket.Payload())
		options, err := dhcpPacket.options()
		if err != nil {
			t.Errorf("dhcpPacket.options(): %s", err)
		}
		packet.Options = options
		packets <- packet

		return pkt, false
	}

	// Stub retransmission and acquisition timeouts such that they never timeout.
	// Otherwise, unexpected timeouts will lead to superfluous sent packets.
	c.retransTimeout = func(time.Duration) <-chan time.Time {
		return nil
	}
	c.contextWithTimeout = func(ctx context.Context, _ time.Duration) (context.Context, context.CancelFunc) {
		return context.WithCancel(ctx)
	}

	for i := 0; i < iterations; i++ {
		func() {
			ctx, cancel := context.WithCancel(ctx)
			defer cancel()

			c.acquiredFunc = func(_ context.Context, lost, acquired tcpip.AddressWithPrefix, _ Config) {
				removeLostAddAcquired(t, clientStack, lost, acquired)
				cancel()
			}

			lost := c.Run(ctx)
			c.acquiredFunc(ctx, lost, tcpip.AddressWithPrefix{}, Config{})
		}()
	}
	close(packets)

	i := 0
	for packet := range packets {
		typ := types[i%len(types)]

		expected := Packet{
			Options: options{
				{optDHCPMsgType, []byte{byte(typ)}},
			},
		}
		expected.Ports.Source = ClientPort
		expected.Ports.Destination = ServerPort
		switch typ {
		case dhcpDISCOVER, dhcpREQUEST:
			expected.Addresses.Source = header.IPv4Any
			expected.Addresses.Destination = header.IPv4Broadcast
			expected.Options = append(expected.Options, option{
				optParamReq,
				[]byte{
					1,  // request subnet mask
					3,  // request router
					15, // domain name
					6,  // domain name server
				},
			})
			if typ == dhcpREQUEST {
				expected.Options = append(expected.Options, option{
					optDHCPServer,
					[]byte(serverAddr),
				})
			}
			// Only the very first DISCOVER doesn't request a specific address.
			if i != 0 {
				expected.Options = append(expected.Options, option{
					optReqIPAddr,
					[]byte(defaultClientAddrs[0]),
				})
			}
		case dhcpRELEASE:
			expected.Addresses.Source = defaultClientAddrs[0]
			expected.Addresses.Destination = serverAddr
			expected.Options = append(expected.Options, option{
				optDHCPServer,
				[]byte(serverAddr),
			})
		default:
			t.Fatalf("unhandled client message type %s", typ)
		}

		if diff := cmp.Diff(expected, packet, cmp.AllowUnexported(option{})); diff != "" {
			t.Errorf("%s packet mismatch (-want +got):\n%s", typ, diff)
		}

		i++
	}
}

func TestDecline(t *testing.T) {
	dadConfigs := stack.DADConfigurations{
		DupAddrDetectTransmits: 1,
		RetransmitTimer:        stdtime.Second,
	}

	var wg sync.WaitGroup
	ctx, cancel := context.WithCancel(context.Background())
	cancelCtx := func() {
		cancel()
		wg.Wait()
	}
	defer cancelCtx()

	clientStack, serverStack, clientEP, _, c := setupTestEnv(ctx, t, defaultServerCfg, testServerOptions{})
	arpEP, err := clientStack.GetNetworkEndpoint(testNICID, arp.ProtocolNumber)
	if err != nil {
		t.Fatalf("clientStack.GetNetworkEndpoint(%d, %d): %s", testNICID, arp.ProtocolNumber, err)
	}
	if ep, ok := arpEP.(stack.DuplicateAddressDetector); !ok {
		t.Fatalf("expected %T to be a stack.DuplicateAddressDetector", arpEP)
	} else {
		ep.SetDADConfigurations(dadConfigs)
	}

	// Create a misconfigured network where the server owns the address that is
	// offered to the client. The client should detect that the offered address is
	// in use when it performs DAD.
	protocolAddress := tcpip.ProtocolAddress{
		Protocol:          ipv4.ProtocolNumber,
		AddressWithPrefix: defaultClientAddrs[0].WithPrefix(),
	}
	if err := serverStack.AddProtocolAddress(testNICID, protocolAddress, stack.AddressProperties{}); err != nil {
		t.Fatalf("serverStack.AddProtocolAddress(%d, %#v, {}): %s", testNICID, protocolAddress, err)
	}

	ch := make(chan *stack.PacketBuffer, 3)
	clientEP.onWritePacket = func(pkt *stack.PacketBuffer) (*stack.PacketBuffer, bool) {
		ch <- pkt.Clone()
		return pkt, false
	}

	timeoutCh := make(chan time.Time)
	var gotTimeouts []time.Duration
	c.retransTimeout = func(d time.Duration) <-chan time.Time {
		gotTimeouts = append(gotTimeouts, d)
		return timeoutCh
	}

	wg.Add(1)
	go func() {
		if got, want := c.Run(ctx), (tcpip.AddressWithPrefix{}); got != want {
			t.Errorf("got c.Run(_): %s, want: %s", got, want)
		}
		wg.Done()
	}()

	wantOpts := options{
		{optDHCPMsgType, []byte{byte(dhcpDECLINE)}},
		{optReqIPAddr, []byte(defaultClientAddrs[0])},
		{optDHCPServer, []byte(serverAddr)},
	}

	seenDecline := false
	for {
		ipv4Packet := func() header.IPv4 {
			pkt := <-ch
			defer pkt.DecRef()
			return header.IPv4(pkt.Data().AsRange().ToSlice())
		}()
		isValid := ipv4Packet.IsValid(len(ipv4Packet))
		if !isValid {
			t.Fatal("sent invalid IPv4 packet")
		}
		if got, want := ipv4Packet.TransportProtocol(), udp.ProtocolNumber; got != want {
			t.Fatalf("got ipv4Packet.TransportProtocol() = %d, want = %d", got, want)
		}
		udpPacket := header.UDP(ipv4Packet.Payload())
		dhcpPacket := hdr(udpPacket.Payload())
		typ := mustMsgType(t, dhcpPacket)
		if typ != dhcpDECLINE {
			// We only care about DHCP DECLINE packets.
			continue
		}

		if got := ipv4Packet.SourceAddress(); got != header.IPv4Any {
			t.Errorf("got ipv4Packet.SourceAddress() = %s, want = %s", got, header.IPv4Any)
		}
		if got := ipv4Packet.DestinationAddress(); got != header.IPv4Broadcast {
			t.Errorf("got ipv4Packet.DestinationAddress() = %s, want = %s", got, header.IPv4Broadcast)
		}
		if got := udpPacket.SourcePort(); got != ClientPort {
			t.Errorf("got udpPacket.SourcePort() = %d, want = %d", got, ClientPort)
		}
		if got := udpPacket.DestinationPort(); got != ServerPort {
			t.Errorf("got udpPacket.DestinationPort() = %d, want = %d", got, ServerPort)
		}
		opts, err := dhcpPacket.options()
		if err != nil {
			t.Fatalf("dhcpPacket.options(): %s", err)
		}
		if diff := cmp.Diff(wantOpts, opts, cmp.AllowUnexported(option{})); diff != "" {
			t.Errorf("dhcpDECLINE options mismatch (-want +got):\n%s", diff)
		}

		timeoutCh <- time.Time{}
		if !seenDecline {
			seenDecline = true
			continue
		}

		break
	}

	cancelCtx()

	// The first two timeouts are to retransmit the DHCP DISCOVER and DHCP REQUEST
	// messages. The 3rd timeout is for the backoff timer in response to DAD
	// failure. We test with two cycles of acquire -> DAD failure -> decline with
	// these timeouts so we only validate the first 6 timeout values.
	//
	// Note: a 7th timeout may be requested if the DHCP client is not cancelled
	// before the 3rd cycle starts but we ignore this.
	retransTimeout := c.Info().Retransmission
	if diff := cmp.Diff([]time.Duration{
		retransTimeout,
		retransTimeout,
		minBackoffAfterDupAddrDetetected,
		retransTimeout,
		retransTimeout,
		minBackoffAfterDupAddrDetetected,
	}, gotTimeouts[:6]); diff != "" {
		t.Errorf("timeouts mismatch (-want +got):\n%s", diff)
	}
}

func TestClientRestartLeaseTime(t *testing.T) {
	ctx, cancel := context.WithCancel(context.Background())
	var wg sync.WaitGroup
	defer func() {
		cancel()
		wg.Wait()
	}()

	clientStack, _, clientEP, _, c := setupTestEnv(ctx, t, defaultServerCfg, testServerOptions{})
	// Always return the same arbitrary time.
	c.now = func() time.Time { return time.Monotonic((1234 * time.Second.Nanoseconds()) + 5678) }

	acquiredDone := make(chan struct{})
	c.acquiredFunc = func(_ context.Context, lost, acquired tcpip.AddressWithPrefix, _ Config) {
		removeLostAddAcquired(t, clientStack, lost, acquired)
		acquiredDone <- struct{}{}
	}
	var acquisitionCancel context.CancelFunc
	var mu sync.Mutex
	c.contextWithTimeout = func(ctx context.Context, _ time.Duration) (context.Context, context.CancelFunc) {
		mu.Lock()
		defer mu.Unlock()
		if acquisitionCancel != nil {
			t.Fatalf("client has more than 1 context outstanding from contextWithTimeout in an acquisition attempt")
		}
		ctx, cancel := context.WithCancel(ctx)
		acquisitionCancel = func() {
			mu.Lock()
			defer mu.Unlock()
			cancel()
			acquisitionCancel = nil
		}
		return ctx, acquisitionCancel
	}
	checkTimes := func(now time.Time, leaseLength, renew, rebind Seconds) {
		info := c.Info()
		if got, want := info.LeaseExpiration, now.Add(leaseLength.Duration()); got != want {
			t.Errorf("info.LeaseExpiration=%s, want=%s", got, want)
		}
		if got, want := info.RenewTime, now.Add(renew.Duration()); got != want {
			t.Errorf("info.RenewTime=%s, want=%s", got, want)
		}
		if got, want := info.RebindTime, now.Add(rebind.Duration()); got != want {
			t.Errorf("info.RebindTime=%s, want=%s", got, want)
		}
		if info.Config.LeaseLength != leaseLength {
			t.Errorf("info.Config.LeaseLength=%s, want=%s", info.Config.LeaseLength, leaseLength)
		}
		if info.Config.RenewTime != renew {
			t.Errorf("info.Config.RenewTime=%s, want=%s", info.Config.RenewTime, renew)
		}
		if info.Config.RebindTime != rebind {
			t.Errorf("info.Config.RebindTime=%s, want=%s", info.Config.RebindTime, rebind)
		}
	}
	renewTime, rebindTime := defaultRenewTime(defaultLeaseLength), defaultRebindTime(defaultLeaseLength)
	{
		clientCtx, clientCancel := context.WithCancel(ctx)
		// Acquire address and transition to bound state.
		wg.Add(1)
		go func() {
			defer wg.Done()
			lost := c.Run(clientCtx)
			c.acquiredFunc(clientCtx, lost, tcpip.AddressWithPrefix{}, Config{})
		}()
		<-acquiredDone

		checkTimes(c.now(), defaultLeaseLength, renewTime, rebindTime)

		// Simulate interface going down.
		clientCancel()
		<-acquiredDone

		zero := Seconds(0)
		checkTimes(time.Time{}, zero, zero, zero)
	}

	{
		clientCtx, clientCancel := context.WithCancel(ctx)
		defer func() {
			clientCancel()
			// Ensure address removal during client cleanup does not block.
			<-acquiredDone
		}()

		initialAcquisitionTimeout := true
		clientEP.onWritePacket = func(pkt *stack.PacketBuffer) (*stack.PacketBuffer, bool) {
			ipv4Packet := header.IPv4(pkt.Data().AsRange().ToSlice())
			udpPacket := header.UDP(ipv4Packet.Payload())
			dhcpPacket := hdr(udpPacket.Payload())
			opts, err := dhcpPacket.options()
			if err != nil {
				t.Fatalf("packet missing options: %s", err)
			}
			typ, err := opts.dhcpMsgType()
			if err != nil {
				t.Fatalf("packet missing message type: %s", err)
			}
			if typ == dhcpDISCOVER && initialAcquisitionTimeout {
				initialAcquisitionTimeout = false
				acquisitionCancel()
				return nil, false
			}
			return pkt, false
		}
		// Restart client and transition to bound.
		wg.Add(1)
		go func() {
			defer wg.Done()
			lost := c.Run(clientCtx)
			c.acquiredFunc(clientCtx, lost, tcpip.AddressWithPrefix{}, Config{})
		}()
		<-acquiredDone

		checkTimes(c.now(), defaultLeaseLength, renewTime, rebindTime)
		info := c.Info()
		if info.State != bound {
			t.Errorf("info.State=%s, want=%s", info.State, bound)
		}
	}
}

func TestClientUpdateInfo(t *testing.T) {
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	_, _, _, _, c := setupTestEnv(ctx, t, defaultServerCfg, testServerOptions{})

	acquired := tcpip.AddressWithPrefix{Address: defaultClientAddrs[0], PrefixLen: 24}
	offeredAt := time.Monotonic(100)
	renewTime := defaultRenewTime(defaultLeaseLength)
	rebindTime := defaultRebindTime(defaultLeaseLength)
	c.acquiredFunc = func(_ context.Context, old, acq tcpip.AddressWithPrefix, cfg Config) {
		if want := (tcpip.AddressWithPrefix{}); old != want {
			t.Errorf("old=%s, want=%s", old, want)
		}
		if acq != acquired {
			t.Errorf("acquired=%s, want=%s", acq, acquired)
		}
		if cfg.LeaseLength != defaultLeaseLength {
			t.Errorf("cfg.LeaseLength=%s, want=%s", cfg.LeaseLength, defaultLeaseLength)
		}
		if cfg.RenewTime != renewTime {
			t.Errorf("cfg.RenewTime=%s, want=%s", cfg.RenewTime, renewTime)
		}
		if cfg.RebindTime != rebindTime {
			t.Errorf("cfg.RebindTime=%s, want=%s", cfg.RebindTime, rebindTime)
		}
		if cfg.UpdatedAt != offeredAt {
			t.Errorf("cfg.UpdatedAt=%d, want=%d", cfg.UpdatedAt, offeredAt)
		}
	}
	cfg := Config{LeaseLength: defaultLeaseLength, RenewTime: renewTime, RebindTime: rebindTime}
	info := c.Info()
	c.assign(context.Background(), &info, acquired, &cfg, offeredAt)
	info = c.Info()
	if cfg.UpdatedAt != offeredAt {
		t.Errorf("cfg.UpdatedAt=%s, want=%s", cfg.UpdatedAt, offeredAt)
	}
	if diff := cmp.Diff(cfg, info.Config, cmp.AllowUnexported(time.Time{})); diff != "" {
		t.Errorf("-want +got: %s", diff)
	}
	if info.Assigned != acquired {
		t.Errorf("info.Assigned=%s, want=%s", info.Assigned, acquired)
	}
	if want := offeredAt.Add(defaultLeaseLength.Duration()); info.LeaseExpiration != want {
		t.Errorf("info.LeaseExpiration=%s, want=%s", info.LeaseExpiration, want)
	}
	if want := offeredAt.Add(renewTime.Duration()); info.RenewTime != want {
		t.Errorf("info.RenewTime=%s, want=%s", info.RenewTime, want)
	}
	if want := offeredAt.Add(rebindTime.Duration()); info.RebindTime != want {
		t.Errorf("info.RebindTime=%s, want=%s", info.RebindTime, want)
	}
	if want := bound; info.State != want {
		t.Errorf("info.State=%s, want=%s", info.State, want)
	}
}
