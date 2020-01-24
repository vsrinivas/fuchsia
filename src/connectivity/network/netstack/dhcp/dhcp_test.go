// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package dhcp

import (
	"context"
	"sync/atomic"
	"testing"
	"time"

	"netstack/packetbuffer"

	"github.com/pkg/errors"
	"gvisor.dev/gvisor/pkg/tcpip"
	"gvisor.dev/gvisor/pkg/tcpip/buffer"
	"gvisor.dev/gvisor/pkg/tcpip/header"
	"gvisor.dev/gvisor/pkg/tcpip/link/channel"
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
	defaulBackoffTime     = 100 * time.Millisecond
	defaultResendTime     = 400 * time.Millisecond
)

const defaultMTU = 65536

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

func addChannelToStack(t *testing.T, addresses []tcpip.Address, nicid tcpip.NICID, s *stack.Stack) *channel.Endpoint {
	t.Helper()
	ch := channel.New(256, defaultMTU, "")
	var linkEP stack.LinkEndpoint = ch
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

	return ch
}

func createTestStackWithChannel(t *testing.T, addresses []tcpip.Address) (*stack.Stack, *channel.Endpoint) {
	t.Helper()
	s := createTestStack()
	ch := addChannelToStack(t, addresses, testNICID, s)
	return s, ch
}

// TestIPv4UnspecifiedAddressNotPrimaryDuringDHCP tests that the IPv4
// unspecified address is not a primary address when doing DHCP.
func TestIPv4UnspecifiedAddressNotPrimaryDuringDHCP(t *testing.T) {
	const incrementalTimeout = 100 * time.Millisecond

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	s, e := createTestStackWithChannel(t, nil)
	c := NewClient(s, testNICID, linkAddr1, defaultAcquireTimeout, defaulBackoffTime, defaultResendTime, nil)

	// errs is for reporting the success or failure out of the goroutine.
	errs := make(chan error, 1)

	go func() {
		info := c.Info()
		_, err := c.acquire(ctx, &info)
		errs <- err
	}()

	select {
	case err := <-errs:
		t.Fatalf("c.acquire(_, _): %s", err)
	case <-time.After(defaultAcquireTimeout):
		t.Fatal("timed out waiting for a DHCP packet to be sent")
	case <-e.C:
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
	const clientCount = 2
	serverStack, serverLinkEP := createTestStackWithChannel(t, []tcpip.Address{serverAddr})
	defer close(serverLinkEP.C)

	// clientLinkEPs are the endpoints on which to inject packets to the client.
	var clientLinkEPs []*channel.Endpoint
	defer func() {
		for _, ep := range clientLinkEPs {
			close(ep.C)
		}
	}()

	// errs is for reporting the success or failure out of the goroutine.
	errs := make(chan error)
	defer close(errs)

	// Start the clients.
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	clientStack := createTestStack()
	for i := 0; i < clientCount; i++ {
		clientNICID := tcpip.NICID(i + 1)
		clientLinkEP := addChannelToStack(t, nil, clientNICID, clientStack)
		clientLinkEPs = append(clientLinkEPs, clientLinkEP)
		c := NewClient(clientStack, clientNICID, linkAddr1, defaultAcquireTimeout, defaulBackoffTime, defaultResendTime, nil)
		info := c.Info()
		go func() {
			// Packets from the clients get sent to the server.
			for pktInfo := range clientLinkEP.C {
				serverLinkEP.InjectInbound(pktInfo.Proto, packetbuffer.OutboundToInbound(pktInfo.Pkt))
			}
		}()
		go func() {
			_, err := c.acquire(ctx, &info)
			errs <- err
		}()
	}

	go func() {
		// Hold the packets until the first clientCount packets are received and
		// then deliver those packets to all the clients.  Each client below will
		// send one packet as part of DHCP acquire so this makes sure that all are
		// running before continuing the acquisition.
		buffered := make([]channel.PacketInfo, clientCount)
		for i := 0; i < len(buffered); i++ {
			buffered[i] = <-serverLinkEP.C
		}
		for _, pktInfo := range buffered {
			for _, clientLinkEP := range clientLinkEPs {
				clientLinkEP.InjectInbound(pktInfo.Proto, packetbuffer.OutboundToInbound(pktInfo.Pkt))
			}
		}
		for pktInfo := range serverLinkEP.C {
			for _, clientLinkEP := range clientLinkEPs {
				clientLinkEP.InjectInbound(pktInfo.Proto, packetbuffer.OutboundToInbound(pktInfo.Pkt))
			}
		}
	}()

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

	// Wait for all clients to finish and collect results.
	for i := 0; i < clientCount; i++ {
		if err := <-errs; err != nil {
			t.Error(err)
		}
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
	s, _ := createTestStackWithChannel(t, []tcpip.Address{serverAddr})
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

	c0 := NewClient(s, testNICID, linkAddr1, defaultAcquireTimeout, defaulBackoffTime, defaultResendTime, nil)
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
		c1 := NewClient(s, testNICID, linkAddr2, defaultAcquireTimeout, defaulBackoffTime, defaultResendTime, nil)
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
			serverStack, serverLinkEP := createTestStackWithChannel(t, []tcpip.Address{serverAddr})
			clientStack, clientLinkEP := createTestStackWithChannel(t, nil)

			go func() {
				for pktInfo := range serverLinkEP.C {
					clientLinkEP.InjectInbound(pktInfo.Proto, packetbuffer.OutboundToInbound(pktInfo.Pkt))
				}
			}()

			go func(discoverDelay, requestDelay time.Duration) {
				if err := func() error {
					discoverRcvd := 0
					requestRcvd := 0
					for pktInfo := range clientLinkEP.C {
						dhcp := hdr(pktInfo.Pkt.Data.First())
						opts, err := dhcp.options()
						if err != nil {
							return err
						}
						msgType, err := opts.dhcpMsgType()
						if err != nil {
							return err
						}
						// Avoid closing over a loop variable.
						pktInfo := pktInfo
						fn := func() { serverLinkEP.InjectInbound(pktInfo.Proto, packetbuffer.OutboundToInbound(pktInfo.Pkt)) }
						switch msgType {
						case dhcpDISCOVER:
							if discoverRcvd == 0 {
								discoverRcvd++
								time.AfterFunc(discoverDelay, fn)
							}
						case dhcpREQUEST:
							if requestRcvd == 0 {
								requestRcvd++
								time.AfterFunc(requestDelay, fn)
							}
						default:
							fn()
						}
					}
					return nil
				}(); err != nil {
					t.Error(err)
				}
			}(tc.requestDelay, tc.discoverDelay)
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

			c0 := NewClient(clientStack, testNICID, linkAddr1, tc.acquisition, defaulBackoffTime, tc.retransmission, nil)
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
				err := errors.Cause(err)
				switch err {
				case context.DeadlineExceeded:
					// Success case: error is expected type.
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
			serverStack, serverLinkEP := createTestStackWithChannel(t, []tcpip.Address{serverAddr})
			clientStack, clientLinkEP := createTestStackWithChannel(t, nil)

			go func() {
				for pktInfo := range serverLinkEP.C {
					clientLinkEP.InjectInbound(pktInfo.Proto, packetbuffer.OutboundToInbound(pktInfo.Pkt))
				}
			}()

			var blockData uint32 = 0
			go func() {
				for pktInfo := range clientLinkEP.C {
					if atomic.LoadUint32(&blockData) == 1 {
						continue
					}
					if typ == testRebind {
						// Only pass client broadcast packets back into the stack. This simulates
						// packet loss during the client's unicast RENEWING state, forcing
						// it into broadcast REBINDING state.
						if header.IPv4(pktInfo.Pkt.Header.View()).DestinationAddress() != header.IPv4Broadcast {
							continue
						}
					}
					serverLinkEP.InjectInbound(pktInfo.Proto, packetbuffer.OutboundToInbound(pktInfo.Pkt))
				}
			}()
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

			c := NewClient(clientStack, testNICID, linkAddr1, defaultAcquireTimeout, defaulBackoffTime, defaultResendTime, acquiredFunc)

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
	s, _ := createTestStackWithChannel(t, []tcpip.Address{serverAddr})

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

	c := NewClient(s, testNICID, linkAddr1, defaultAcquireTimeout, defaulBackoffTime, defaultResendTime, nil)
	info := c.Info()
	if _, err := c.acquire(ctx, &info); err != nil {
		t.Fatal(err)
	}
}
