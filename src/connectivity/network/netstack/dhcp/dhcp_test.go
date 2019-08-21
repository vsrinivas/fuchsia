// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package dhcp

import (
	"context"
	"strings"
	"sync/atomic"
	"testing"
	"time"

	"github.com/google/netstack/tcpip"
	"github.com/google/netstack/tcpip/buffer"
	tcpipHeader "github.com/google/netstack/tcpip/header"
	"github.com/google/netstack/tcpip/link/channel"
	"github.com/google/netstack/tcpip/link/sniffer"
	"github.com/google/netstack/tcpip/network/ipv4"
	"github.com/google/netstack/tcpip/stack"
	"github.com/google/netstack/tcpip/transport/udp"
	"github.com/google/netstack/waiter"
)

const (
	nicid      = tcpip.NICID(1)
	serverAddr = tcpip.Address("\xc0\xa8\x03\x01")

	defaultAcquireTimeout = 1000 * time.Millisecond
	defaultRetryTime      = 100 * time.Millisecond
)

func createTestStack(t *testing.T) *stack.Stack {
	s, linkEP := createTestStackWithChannel(t)

	go func() {
		for pkt := range linkEP.C {
			linkEP.Inject(pkt.Proto, buffer.NewVectorisedView(len(pkt.Header)+len(pkt.Payload), []buffer.View{pkt.Header, pkt.Payload}))
		}
	}()

	return s
}

func createTestStackWithChannel(t *testing.T) (*stack.Stack, *channel.Endpoint) {
	const defaultMTU = 65536
	id, linkEP := channel.New(256, defaultMTU, "")
	if testing.Verbose() {
		id = sniffer.New(id)
	}

	s := stack.New([]string{ipv4.ProtocolName}, []string{udp.ProtocolName}, stack.Options{})

	if err := s.CreateNIC(nicid, id); err != nil {
		t.Fatal(err)
	}
	if err := s.AddAddress(nicid, ipv4.ProtocolNumber, serverAddr); err != nil {
		t.Fatal(err)
	}

	s.SetRouteTable([]tcpip.Route{{
		Destination: tcpip.Address(strings.Repeat("\x00", 4)),
		Mask:        tcpip.AddressMask(strings.Repeat("\x00", 4)),
		Gateway:     "",
		NIC:         nicid,
	}})

	return s, linkEP
}

// TestSimultaneousDHCPClients makes two clients that are trying to get DHCP
// addresses at the same time.
func TestSimultaneousDHCPClients(t *testing.T) {
	const clientCount = 2
	s, serverLinkEP := createTestStackWithChannel(t)
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
	for i := 0; i < clientCount; i++ {
		const defaultMTU = 65536
		id, clientLinkEP := channel.New(256, defaultMTU, "")
		clientLinkEPs = append(clientLinkEPs, clientLinkEP)
		clientNicid := tcpip.NICID(100 + i)
		if err := s.CreateNIC(clientNicid, id); err != nil {
			t.Fatalf("could not create NIC: %s", err)
		}
		const clientLinkAddr = tcpip.LinkAddress("\x52\x11\x22\x33\x44\x52")
		c := NewClient(s, clientNicid, clientLinkAddr, defaultAcquireTimeout, defaultRetryTime, nil)
		go func() {
			// Packets from the clients get sent to the server.
			for pkt := range clientLinkEP.C {
				serverLinkEP.Inject(pkt.Proto, buffer.NewVectorisedView(len(pkt.Header)+len(pkt.Payload), []buffer.View{pkt.Header, pkt.Payload}))
			}
		}()
		go func() {
			_, err := c.acquire(ctx, initSelecting)
			errs <- err
		}()
	}

	go func() {
		// Hold the packets until the first clientCount packets are received and
		// then deliver those packets to all the clients.  Each client below will
		// send one packet as part of DHCP acquire so this makes sure that all are
		// running before continuing the acquisition.
		var pkts []channel.PacketInfo
		for i := 0; i < clientCount; i++ {
			pkts = append(pkts, <-serverLinkEP.C)
		}
		for _, pkt := range pkts {
			for _, clientLinkEP := range clientLinkEPs {
				clientLinkEP.Inject(pkt.Proto, buffer.NewVectorisedView(len(pkt.Header)+len(pkt.Payload), []buffer.View{pkt.Header, pkt.Payload}))
			}
		}
		for pkt := range serverLinkEP.C {
			for _, clientLinkEP := range clientLinkEPs {
				clientLinkEP.Inject(pkt.Proto, buffer.NewVectorisedView(len(pkt.Header)+len(pkt.Payload), []buffer.View{pkt.Header, pkt.Payload}))
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
	if _, err := newEPConnServer(ctx, s, clientAddrs, serverCfg); err != nil {
		t.Fatal(err)
	}

	// Wait for all clients to finish and collect results.
	for i := 0; i < clientCount; i++ {
		if err := <-errs; err != nil {
			t.Fatal(err)
		}
	}
}

func TestDHCP(t *testing.T) {
	s := createTestStack(t)
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

	const clientLinkAddr0 = tcpip.LinkAddress("\x52\x11\x22\x33\x44\x52")
	c0 := NewClient(s, nicid, clientLinkAddr0, defaultAcquireTimeout, defaultRetryTime, nil)
	{
		{
			cfg, err := c0.acquire(ctx, initSelecting)
			if err != nil {
				t.Fatal(err)
			}
			if got, want := c0.addr.Address, clientAddrs[0]; got != want {
				t.Errorf("c.addr=%s, want=%s", got, want)
			}
			if got, want := cfg.SubnetMask, serverCfg.SubnetMask; got != want {
				t.Errorf("cfg.SubnetMask=%s, want=%s", got, want)
			}
		}
		{
			cfg, err := c0.acquire(ctx, initSelecting)
			if err != nil {
				t.Fatal(err)
			}
			if got, want := c0.addr.Address, clientAddrs[0]; got != want {
				t.Errorf("c.addr=%s, want=%s", got, want)
			}
			if got, want := cfg.SubnetMask, serverCfg.SubnetMask; got != want {
				t.Errorf("cfg.SubnetMask=%s, want=%s", got, want)
			}
		}
	}

	{
		const clientLinkAddr1 = tcpip.LinkAddress("\x52\x11\x22\x33\x44\x53")
		c1 := NewClient(s, nicid, clientLinkAddr1, defaultAcquireTimeout, defaultRetryTime, nil)
		cfg, err := c1.acquire(ctx, initSelecting)
		if err != nil {
			t.Fatal(err)
		}
		if got, want := c1.addr.Address, clientAddrs[1]; got != want {
			t.Errorf("c.addr=%s, want=%s", got, want)
		}
		if got, want := cfg.SubnetMask, serverCfg.SubnetMask; got != want {
			t.Errorf("cfg.SubnetMask=%s, want=%s", got, want)
		}
	}

	{
		if err := s.AddProtocolAddressWithOptions(nicid, tcpip.ProtocolAddress{
			Protocol:          ipv4.ProtocolNumber,
			AddressWithPrefix: c0.addr,
		}, stack.NeverPrimaryEndpoint); err != nil {
			t.Fatalf("failed to add address to stack: %s", err)
		}
		defer s.RemoveAddress(nicid, c0.addr.Address)

		cfg, err := c0.acquire(ctx, initSelecting)
		if err != nil {
			t.Fatal(err)
		}
		if got, want := c0.addr.Address, clientAddrs[0]; got != want {
			t.Errorf("c.addr=%s, want=%s", got, want)
		}
		if got, want := cfg.SubnetMask, serverCfg.SubnetMask; got != want {
			t.Errorf("cfg.SubnetMask=%s, want=%s", got, want)
		}

		if got, want := cfg, serverCfg; !equalConfig(got, want) {
			t.Errorf("client config:\n\t%#+v\nwant:\n\t%#+v", got, want)
		}
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
			s, linkEP := createTestStackWithChannel(t)
			clientAddrs := []tcpip.Address{"\xc0\xa8\x03\x02"}

			var blockData uint32 = 0
			go func() {
				for pkt := range linkEP.C {
					if atomic.LoadUint32(&blockData) == 1 {
						continue
					}
					if typ == testRebind {
						// Only pass broadcast packets back into the stack. This simulates
						// packet loss during the Client's unicast RENEWING state, forcing
						// it into broadcast REBINDING state.
						if tcpipHeader.IPv4(pkt.Header).DestinationAddress() != tcpipHeader.IPv4Broadcast {
							continue
						}
					}
					linkEP.Inject(pkt.Proto, buffer.NewVectorisedView(len(pkt.Header)+len(pkt.Payload), []buffer.View{pkt.Header, pkt.Payload}))
				}
			}()

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
			if _, err := newEPConnServer(ctx, s, clientAddrs, serverCfg); err != nil {
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
						if err := s.RemoveAddress(nicid, oldAddr.Address); err != nil {
							t.Fatalf("RemoveAddress(%s): %s", oldAddr.Address, err)
						}
					}

					if curAddr != (tcpip.AddressWithPrefix{}) {
						protocolAddress := tcpip.ProtocolAddress{
							Protocol:          ipv4.ProtocolNumber,
							AddressWithPrefix: curAddr,
						}
						if err := s.AddProtocolAddress(nicid, protocolAddress); err != nil {
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

			const clientLinkAddr0 = tcpip.LinkAddress("\x52\x11\x22\x33\x44\x52")
			c := NewClient(s, nicid, clientLinkAddr0, defaultAcquireTimeout, defaultRetryTime, acquiredFunc)

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
	h := header(v)
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
	s := createTestStack(t)

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

	const clientLinkAddr0 = tcpip.LinkAddress("\x52\x11\x22\x33\x44\x52")
	c := NewClient(s, nicid, clientLinkAddr0, defaultAcquireTimeout, defaultRetryTime, nil)
	if _, err := c.acquire(ctx, initSelecting); err != nil {
		t.Fatal(err)
	}
}
