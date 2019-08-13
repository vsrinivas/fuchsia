// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package dhcp

import (
	"context"
	"strings"
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

const nicid = tcpip.NICID(1)
const serverAddr = tcpip.Address("\xc0\xa8\x03\x01")

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

	// errs is for reporting the success or failure out of the goroutine.
	errs := make(chan error)
	defer close(errs)

	// Start the clients.
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	for i := 0; i < clientCount; i++ {
		const defaultMTU = 65536
		id, clientLinkEP := channel.New(256, defaultMTU, "")
		defer close(clientLinkEP.C)
		clientLinkEPs = append(clientLinkEPs, clientLinkEP)
		clientNicid := tcpip.NICID(100 + i)
		if err := s.CreateNIC(clientNicid, id); err != nil {
			t.Fatalf("could not create NIC: %s", err)
		}
		const clientLinkAddr = tcpip.LinkAddress("\x52\x11\x22\x33\x44\x52")
		c := NewClient(s, clientNicid, clientLinkAddr, nil)
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
	c0 := NewClient(s, nicid, clientLinkAddr0, nil)
	{
		{
			cfg, err := c0.acquire(ctx, initSelecting)
			if err != nil {
				t.Fatal(err)
			}
			if got, want := c0.addr, clientAddrs[0]; got != want {
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
			if got, want := c0.addr, clientAddrs[0]; got != want {
				t.Errorf("c.addr=%s, want=%s", got, want)
			}
			if got, want := cfg.SubnetMask, serverCfg.SubnetMask; got != want {
				t.Errorf("cfg.SubnetMask=%s, want=%s", got, want)
			}
		}
	}

	{
		const clientLinkAddr1 = tcpip.LinkAddress("\x52\x11\x22\x33\x44\x53")
		c1 := NewClient(s, nicid, clientLinkAddr1, nil)
		cfg, err := c1.acquire(ctx, initSelecting)
		if err != nil {
			t.Fatal(err)
		}
		if got, want := c1.addr, clientAddrs[1]; got != want {
			t.Errorf("c.addr=%s, want=%s", got, want)
		}
		if got, want := cfg.SubnetMask, serverCfg.SubnetMask; got != want {
			t.Errorf("cfg.SubnetMask=%s, want=%s", got, want)
		}
	}

	{
		if err := s.AddAddressWithOptions(nicid, ipv4.ProtocolNumber, c0.addr, stack.NeverPrimaryEndpoint); err != nil && err != tcpip.ErrDuplicateAddress {
			t.Fatalf("failed to add address to stack: %s", err)
		}
		defer s.RemoveAddress(nicid, c0.addr)

		cfg, err := c0.acquire(ctx, initSelecting)
		if err != nil {
			t.Fatal(err)
		}
		if got, want := c0.addr, clientAddrs[0]; got != want {
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

func TestRenew(t *testing.T) {
	s := createTestStack(t)
	clientAddrs := []tcpip.Address{"\xc0\xa8\x03\x02"}

	serverCfg := Config{
		ServerAddress: serverAddr,
		SubnetMask:    "\xff\xff\xff\x00",
		Gateway:       "\xc0\xa8\x03\xF0",
		DNS:           []tcpip.Address{"\x08\x08\x08\x08"},
		LeaseLength:   3 * time.Second,
		RebindingTime: 2 * time.Second,
		RenewalTime:   1 * time.Second,
	}
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	if _, err := newEPConnServer(ctx, s, clientAddrs, serverCfg); err != nil {
		t.Fatal(err)
	}

	count := 0
	var curAddr tcpip.Address
	addrCh := make(chan tcpip.Address)
	acquiredFunc := func(oldAddr, newAddr tcpip.Address, oldSubnet, newSubnet tcpip.Subnet, cfg Config) {
		if oldAddr != curAddr {
			t.Fatalf("acquisition %d: curAddr=%v, oldAddr=%v", count, curAddr, oldAddr)
		}
		if cfg.LeaseLength != serverCfg.LeaseLength {
			t.Fatalf("acquisition %d: lease length: %v, want %v", count, cfg.LeaseLength, serverCfg.LeaseLength)
		}
		count++
		curAddr = newAddr
		// Any address acquired by the DHCP client must be added to the stack, because the DHCP client
		// will need to send from that address when it tries to renew its lease.
		if curAddr != oldAddr {
			if err := s.AddAddress(nicid, ipv4.ProtocolNumber, curAddr); err != nil {
				t.Fatalf("AddAddress() with %v failed", curAddr)
			}
		}
		if curAddr != oldAddr && oldAddr != "" {
			if err := s.RemoveAddress(nicid, oldAddr); err != nil {
				t.Fatalf("RemoveAddress() with %v failed", oldAddr)
			}
		}
		addrCh <- newAddr
	}

	const clientLinkAddr0 = tcpip.LinkAddress("\x52\x11\x22\x33\x44\x52")
	c := NewClient(s, nicid, clientLinkAddr0, acquiredFunc)

	c.Run(ctx)

	var addr tcpip.Address
	select {
	case addr = <-addrCh:
		t.Logf("got first address: %v", addr)
	case <-time.After(10 * time.Second):
		t.Fatal("timeout acquiring initial address")
	}

	select {
	case newAddr := <-addrCh:
		t.Logf("got renewal: %v", newAddr)
		if newAddr != addr {
			t.Fatalf("renewal address is %v, want %v", newAddr, addr)
		}
	case <-time.After(5 * serverCfg.RenewalTime):
		t.Fatal("timeout acquiring renewed address")
	}
}

// TODO: Extract common functionality in TestRenew and TestRebind.
func TestRebind(t *testing.T) {
	s, linkEP := createTestStackWithChannel(t)
	clientAddrs := []tcpip.Address{"\xc0\xa8\x03\x02"}

	go func() {
		for pkt := range linkEP.C {
			ipHeader := tcpipHeader.IPv4(pkt.Header)
			// Only pass broadcast packets back into the stack. This simulates packet loss during the
			// Client's unicast RENEWING state, forcing it into broadcast REBINDING state.
			if ipHeader.DestinationAddress() == tcpipHeader.IPv4Broadcast {
				linkEP.Inject(pkt.Proto, buffer.NewVectorisedView(len(pkt.Header)+len(pkt.Payload), []buffer.View{pkt.Header, pkt.Payload}))
			}
		}
	}()

	serverCfg := Config{
		ServerAddress: serverAddr,
		SubnetMask:    "\xff\xff\xff\x00",
		Gateway:       "\xc0\xa8\x03\xF0",
		DNS:           []tcpip.Address{"\x08\x08\x08\x08"},
		LeaseLength:   3 * time.Second,
		RebindingTime: 2 * time.Second,
		RenewalTime:   1 * time.Second,
	}
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	if _, err := newEPConnServer(ctx, s, clientAddrs, serverCfg); err != nil {
		t.Fatal(err)
	}

	count := 0
	var curAddr tcpip.Address
	addrCh := make(chan tcpip.Address)
	acquiredFunc := func(oldAddr, newAddr tcpip.Address, oldSubnet, newSubnet tcpip.Subnet, cfg Config) {
		if oldAddr != curAddr {
			t.Fatalf("aquisition %d: curAddr=%v, oldAddr=%v", count, curAddr, oldAddr)
		}
		if cfg.LeaseLength != serverCfg.LeaseLength {
			t.Fatalf("aquisition %d: lease length: %v, want %v", count, cfg.LeaseLength, serverCfg.LeaseLength)
		}
		count++
		curAddr = newAddr
		// Any address acquired by the DHCP client must be added to the stack, because the DHCP client
		// will need to send from that address when it tries to renew its lease.
		if curAddr != oldAddr {
			if err := s.AddAddress(nicid, ipv4.ProtocolNumber, curAddr); err != nil {
				t.Fatalf("AddAddress() with %v failed", curAddr)
			}
		}
		if curAddr != oldAddr && oldAddr != "" {
			if err := s.RemoveAddress(nicid, oldAddr); err != nil {
				t.Fatalf("RemoveAddress() with %v failed", oldAddr)
			}
		}
		addrCh <- newAddr
	}

	const clientLinkAddr0 = tcpip.LinkAddress("\x52\x11\x22\x33\x44\x52")
	c := NewClient(s, nicid, clientLinkAddr0, acquiredFunc)
	c.Run(ctx)

	var addr tcpip.Address
	select {
	case addr = <-addrCh:
		t.Logf("got first address: %v", addr)
	case <-time.After(10 * time.Second):
		t.Fatal("timeout acquiring initial address")
	}

	select {
	case newAddr := <-addrCh:
		t.Logf("got renewal: %v", newAddr)
		if newAddr != addr {
			t.Fatalf("renewal address is %v, want %v", newAddr, addr)
		}
	case <-time.After(5 * serverCfg.RebindingTime):
		t.Fatal("timeout acquiring renewed address")
	}
}

// Regression test for https://fuchsia.atlassian.net/browse/NET-17
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
		t.Errorf("h.op()=%v, want=%v", got, want)
	}

	if _, err := h.options(); err != nil {
		t.Errorf("bad options: %v", err)
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
		t.Fatalf("dhcp: server endpoint: %v", err)
	}
	if err = ep.Bind(tcpip.FullAddress{Port: ServerPort}); err != nil {
		t.Fatalf("dhcp: server bind: %v", err)
	}
	if err = ep.SetSockOpt(tcpip.BroadcastOption(1)); err != nil {
		t.Fatalf("dhcp: setsockopt: %v", err)
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
	c := NewClient(s, nicid, clientLinkAddr0, nil)
	if _, err := c.acquire(ctx, initSelecting); err != nil {
		t.Fatal(err)
	}
}
