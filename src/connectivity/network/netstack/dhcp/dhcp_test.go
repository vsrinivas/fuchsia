// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package dhcp

import (
	"context"
	"fmt"
	"sync"
	"sync/atomic"
	"testing"
	"time"

	"netstack/packetbuffer"

	"github.com/pkg/errors"
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
	dispatcher    stack.NetworkDispatcher
	remote        []*endpoint
	onWritePacket func(tcpip.PacketBuffer) bool

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
	if fn := e.onWritePacket; fn != nil {
		if !fn(pkt) {
			return nil
		}
	}
	for _, remote := range e.remote {
		if !remote.IsAttached() {
			panic(fmt.Sprintf("ep: %+v remote endpoint: %+v has not been `Attach`ed; call stack.CreateNIC to attach it", e, remote))
		}
		// the "remote" address for `other` is our local address and vice versa.
		remote.dispatcher.DeliverNetworkPacket(remote, r.LocalLinkAddress, r.RemoteLinkAddress, protocol, packetbuffer.OutboundToInbound(pkt))
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
		onWritePacket: func(tcpip.PacketBuffer) bool {
			select {
			case sent <- struct{}{}:
			default:
			}
			return true
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
		if errors.Cause(err) == context.DeadlineExceeded {
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
			if err := <-errs; errors.Cause(err) != context.Canceled {
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
		onWritePacket: func(packetBuffer tcpip.PacketBuffer) bool {
			mu.Lock()
			mu.buffered++
			for mu.buffered < len(clientLinkEPs) {
				cond.Wait()
			}
			mu.Unlock()

			return true
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
			if err := <-errs; errors.Cause(err) != context.Canceled {
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
		LeaseLength: 24 * time.Hour,
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
		LeaseLength: 24 * time.Hour,
	}
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	if _, err := newEPConnServer(ctx, s, clientAddrs, serverCfg); err != nil {
		t.Fatal(err)
	}

	c0 := NewClient(s, testNICID, linkAddr1, defaultAcquireTimeout, defaultBackoffTime, defaultResendTime, nil)
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

		if got, want := cfg, serverCfg; !equalConfig(got, want) {
			t.Errorf("client config:\n\t%#+v\nwant:\n\t%#+v", got, want)
		}
		c0.verifyClientStats(t, 3)
	}
}

func TestDelayRetransmission(t *testing.T) {
	var delayTests = []struct {
		name           string
		discoverDelay  time.Duration
		requestDelay   time.Duration
		acquisition    time.Duration
		retransmission time.Duration
		success        bool
	}{
		{name: "DelayedDiscoverWithLongAcquisitionSucceeds", discoverDelay: 500 * time.Millisecond, requestDelay: 0, acquisition: 5 * time.Second, retransmission: 100 * time.Millisecond, success: true},
		{name: "DelayedRequestWithLongAcquisitionSucceeds", discoverDelay: 0, requestDelay: 500 * time.Millisecond, acquisition: 5 * time.Second, retransmission: 100 * time.Millisecond, success: true},
		{name: "DelayedDiscoverWithShortAcquisitionFails", discoverDelay: 1 * time.Second, requestDelay: 0, acquisition: 500 * time.Millisecond, retransmission: 100 * time.Millisecond, success: false},
		{name: "DelayedRequestWithShortAcquisitionFails", discoverDelay: 0, requestDelay: 1 * time.Second, acquisition: 500 * time.Millisecond, retransmission: 100 * time.Millisecond, success: false},
	}
	for _, tc := range delayTests {
		t.Run(tc.name, func(t *testing.T) {
			var serverLinkEP, clientLinkEP endpoint
			serverLinkEP.remote = append(serverLinkEP.remote, &clientLinkEP)
			clientLinkEP.remote = append(clientLinkEP.remote, &serverLinkEP)

			var discoverRcvd, requestRcvd int
			clientLinkEP.onWritePacket = func(packetBuffer tcpip.PacketBuffer) bool {
				h := hdr(packetBuffer.Data.First())
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
				switch msgType {
				case dhcpDISCOVER:
					if discoverRcvd == 0 {
						discoverRcvd++
						time.Sleep(tc.discoverDelay)
					}
				case dhcpREQUEST:
					if requestRcvd == 0 {
						requestRcvd++
						time.Sleep(tc.requestDelay)
					}
				}

				return true
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
				LeaseLength: 24 * time.Hour,
			}

			ctx, cancel := context.WithCancel(context.Background())
			defer cancel()
			if _, err := newEPConnServer(ctx, serverStack, clientAddrs, serverCfg); err != nil {
				t.Fatal(err)
			}

			c0 := NewClient(clientStack, testNICID, linkAddr1, tc.acquisition, defaultBackoffTime, tc.retransmission, nil)
			info := c0.Info()
			ctx, cancel = context.WithTimeout(ctx, tc.acquisition)
			defer cancel()
			cfg, err := c0.acquire(ctx, &info)
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
				switch err := errors.Cause(err); err {
				case context.DeadlineExceeded:
					// Success case.
				default:
					t.Errorf("got err=%v, want=%s", err, context.DeadlineExceeded)
				}
			}
		})
	}
}

func equalConfig(c0, c1 Config) bool {
	if c0.ServerAddress != c1.ServerAddress || c0.SubnetMask != c1.SubnetMask || c0.Gateway != c1.Gateway || c0.LeaseLength != c1.LeaseLength {
		return false
	}
	if len(c0.DNS) != len(c1.DNS) {
		return false
	}
	for i := 0; i < len(c0.DNS); i++ {
		if c0.DNS[i] != c1.DNS[i] {
			return false
		}
	}
	return true
}

func TestStateTransition(t *testing.T) {
	type testType int
	const (
		testRenew testType = iota
		testRebind
		testLeaseExpire
	)

	for _, typ := range []testType{testRenew, testRebind, testLeaseExpire} {
		var name string
		switch typ {
		case testRenew:
			name = "Renew"
		case testRebind:
			name = "Rebind"
		case testLeaseExpire:
			name = "LeaseExpire"
		default:
			t.Fatalf("unknown test type %d", typ)
		}
		t.Run(name, func(t *testing.T) {
			var serverLinkEP, clientLinkEP endpoint
			serverLinkEP.remote = append(serverLinkEP.remote, &clientLinkEP)
			clientLinkEP.remote = append(clientLinkEP.remote, &serverLinkEP)

			var blockData uint32 = 0
			clientLinkEP.onWritePacket = func(packetBuffer tcpip.PacketBuffer) bool {
				if atomic.LoadUint32(&blockData) == 1 {
					return false
				}
				if typ == testRebind {
					// Only pass client broadcast packets back into the stack. This simulates
					// packet loss during the client's unicast RENEWING state, forcing
					// it into broadcast REBINDING state.
					if header.IPv4(packetBuffer.Header.View()).DestinationAddress() != header.IPv4Broadcast {
						return false
					}
				}
				return true
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
				LeaseLength:   6 * defaultAcquireTimeout,
				RebindingTime: 4 * defaultAcquireTimeout,
				RenewalTime:   2 * defaultAcquireTimeout,
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
				addrCh <- curAddr
			}

			c := NewClient(clientStack, testNICID, linkAddr1, defaultAcquireTimeout, defaultBackoffTime, defaultResendTime, acquiredFunc)

			c.Run(ctx)

			var addr tcpip.AddressWithPrefix
			select {
			case addr = <-addrCh:
				t.Logf("got first address: %s", addr)
			case <-time.After(10 * time.Second):
				t.Fatal("timeout acquiring initial address")
			}

			wantAddr := addr
			var timeout time.Duration
			switch typ {
			case testRenew:
				timeout = serverCfg.RenewalTime
			case testRebind:
				timeout = serverCfg.RebindingTime
			case testLeaseExpire:
				timeout = serverCfg.LeaseLength
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
			case <-time.After(5 * timeout):
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
		LeaseLength:   30 * time.Minute,
	}); err != nil {
		t.Fatal(err)
	}
	if _, err := NewServer(ctx, c2, []tcpip.Address{"\xc0\xa8\x04\x02"}, Config{
		ServerAddress: "\xc0\xa8\x04\x01",
		SubnetMask:    "\xff\xff\xff\x00",
		Gateway:       "\xc0\xa8\x03\xF0",
		DNS:           []tcpip.Address{"\x08\x08\x08\x08"},
		LeaseLength:   30 * time.Minute,
	}); err != nil {
		t.Fatal(err)
	}

	c := NewClient(s, testNICID, linkAddr1, defaultAcquireTimeout, defaultBackoffTime, defaultResendTime, nil)
	info := c.Info()
	if _, err := c.acquire(ctx, &info); err != nil {
		t.Fatal(err)
	}
}
