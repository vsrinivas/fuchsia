// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package netstack

import (
	"context"
	"fmt"
	"syscall/zx"
	"syscall/zx/zxwait"
	"testing"
	"time"

	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/dns"
	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/util"

	"fidl/fuchsia/cobalt"
	"fidl/fuchsia/netstack"

	networking_metrics "networking_metrics_golib"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
	"gvisor.dev/gvisor/pkg/tcpip"
	"gvisor.dev/gvisor/pkg/tcpip/header"
	"gvisor.dev/gvisor/pkg/tcpip/network/ipv6"
	"gvisor.dev/gvisor/pkg/tcpip/stack"
)

const (
	shortLifetime         = time.Nanosecond
	shortLifetimeTimeout  = time.Second
	middleLifetime        = 500 * time.Millisecond
	middleLifetimeTimeout = middleLifetime * time.Second
	incrementalTimeout    = 100 * time.Millisecond
	defaultLifetime       = time.Hour

	onInterfacesChangedEventTimeout = time.Second
)

var (
	subnet1           = newSubnet(util.Parse("abcd:1234::"), tcpip.AddressMask(util.Parse("ffff:ffff::")))
	subnet2           = newSubnet(util.Parse("abcd:1236::"), tcpip.AddressMask(util.Parse("ffff:ffff::")))
	testProtocolAddr1 = tcpip.ProtocolAddress{
		Protocol: ipv6.ProtocolNumber,
		AddressWithPrefix: tcpip.AddressWithPrefix{
			Address:   util.Parse("abcd:ee00::1"),
			PrefixLen: 64,
		},
	}
	testProtocolAddr2 = tcpip.ProtocolAddress{
		Protocol: ipv6.ProtocolNumber,
		AddressWithPrefix: tcpip.AddressWithPrefix{
			Address:   util.Parse("abcd:ef00::1"),
			PrefixLen: 64,
		},
	}
	testProtocolAddr3 = tcpip.ProtocolAddress{
		Protocol: ipv6.ProtocolNumber,
		AddressWithPrefix: tcpip.AddressWithPrefix{
			Address:   util.Parse("abcd:ff00::1"),
			PrefixLen: 64,
		},
	}
)

func newSubnet(addr tcpip.Address, mask tcpip.AddressMask) tcpip.Subnet {
	subnet, err := tcpip.NewSubnet(addr, mask)
	if err != nil {
		panic(fmt.Sprintf("NewSubnet(%s, %s): %s", addr, mask, err))
	}
	return subnet
}

// newNDPDispatcherForTest returns a new ndpDispatcher with a channel used to
// notify tests when its event queue is emptied.
//
// The dispatcher's cobalt observers are initialized such that no cobalt client
// will receive events.
func newNDPDispatcherForTest() *ndpDispatcher {
	n := newNDPDispatcher()
	n.testNotifyCh = make(chan struct{}, 1)
	// Must be called or tests may panic as the ndpDispatchere may attempt to call
	// into n.dynamicAddressSourceObs
	n.dynamicAddressSourceObs.initWithoutTimer(func() {})
	n.dhcpv6Obs.init(func() {})
	return n
}

// waitForEmptyQueue returns after the event queue is emptied.
//
// If n's event queue is empty when waitForEmptyQueue is called, then
// waitForEmptyQueue returns immediately.
func waitForEmptyQueue(n *ndpDispatcher) {
	// Wait for an empty event queue.
	for {
		n.mu.Lock()
		empty := len(n.mu.events) == 0
		n.mu.Unlock()
		if empty {
			break
		}
		<-n.testNotifyCh
	}
}

// Test that netstack service clients receive an OnInterfacesChanged event on
// DAD and SLAAC address invalidation events.
func TestSendingOnInterfacesChangedEvent(t *testing.T) {
	tests := []struct {
		name       string
		ndpEventFn func(id tcpip.NICID, ndpDisp *ndpDispatcher)
	}{
		{
			name: "On DAD event",
			ndpEventFn: func(id tcpip.NICID, ndpDisp *ndpDispatcher) {
				ndpDisp.OnDuplicateAddressDetectionStatus(id, testLinkLocalV6Addr1, false, nil)
			},
		},
		{
			name: "On SLAAC address invalidated event",
			ndpEventFn: func(id tcpip.NICID, ndpDisp *ndpDispatcher) {
				ndpDisp.OnAutoGenAddress(id, testProtocolAddr1.AddressWithPrefix)
				ndpDisp.OnAutoGenAddressInvalidated(id, testProtocolAddr1.AddressWithPrefix)
			},
		},
	}

	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			ctx, cancel := context.WithCancel(context.Background())
			defer cancel()

			ndpDisp := newNDPDispatcherForTest()
			ns := newNetstackWithNDPDispatcher(t, ndpDisp)
			ndpDisp.start(ctx)

			ifs, err := addNoopEndpoint(ns, "TestNameTooLong")
			if err != nil {
				t.Fatal(err)
			}
			if err := ifs.Up(); err != nil {
				t.Fatalf("ifs.Up(): %s", err)
			}

			req, cli, err := netstack.NewNetstackWithCtxInterfaceRequest()
			if err != nil {
				t.Fatalf("netstack.NewNetstackWithCtxInterfaceRequest(): %s", err)
			}
			defer func() {
				_ = cli.Close()
			}()

			pxy := netstack.NetstackEventProxy{Channel: req.Channel}
			ns.netstackService.mu.Lock()
			ns.netstackService.mu.proxies = map[*netstack.NetstackEventProxy]struct{}{
				&pxy: {},
			}
			ns.netstackService.mu.Unlock()

			// Send an event to ndpDisp that should trigger an OnInterfacesChanged
			// event from the netstack.
			test.ndpEventFn(ifs.nicid, ndpDisp)
			waitForEmptyQueue(ndpDisp)

			signals, err := zxwait.Wait(*cli.Channel.Handle(), zx.SignalChannelReadable|zx.SignalChannelPeerClosed, zx.Sys_deadline_after(zx.Duration(onInterfacesChangedEventTimeout.Nanoseconds())))
			if err != nil {
				t.Fatalf("zxwait.Wait(_, zx.SignalChannelReadable|zx.SignalChannelPeerClosed, %s): %s", onInterfacesChangedEventTimeout, err)
			}
			if signals&zx.SignalChannelReadable == 0 {
				t.Fatalf("got zxwait.Wait(_, zx.SignalChannelReadable|zx.SignalChannelPeerClosed, %s) = %b, want = %b", onInterfacesChangedEventTimeout, signals, zx.SignalChannelReadable)
			}
			interfaces, err := cli.ExpectOnInterfacesChanged(context.Background())
			if err != nil {
				t.Fatalf("cli.ExpectOnInterfacesChanged(): %s", err)
			}
			if l := len(interfaces); l != 1 {
				t.Errorf("got len(interfaces) = %d, want = 1", l)
			}
		})
	}
}

// Test that attempting to invalidate a default router which we do not have a
// route for is not an issue.
func TestNDPInvalidateUnknownIPv6Router(t *testing.T) {
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	ndpDisp := newNDPDispatcherForTest()
	ns := newNetstackWithNDPDispatcher(t, ndpDisp)
	ndpDisp.start(ctx)

	ifs, err := addNoopEndpoint(ns, t.Name())
	if err != nil {
		t.Fatal(err)
	}
	if err := ifs.Up(); err != nil {
		t.Fatalf("ifs.Up(): %s", err)
	}

	// Invalidate the router with IP testLinkLocalV6Addr1 from eth (even
	// though we do not yet know about it).
	ndpDisp.OnDefaultRouterInvalidated(ifs.nicid, testLinkLocalV6Addr1)
	waitForEmptyQueue(ndpDisp)
	if rt, rts := defaultV6Route(ifs.nicid, testLinkLocalV6Addr1), ns.stack.GetRouteTable(); containsRoute(rts, rt) {
		t.Fatalf("should not have route = %s in the route table, got = %s", rt, rts)
	}
}

// Test that ndpDispatcher properly handles the discovery and invalidation of
// default IPv6 routers.
func TestNDPIPv6RouterDiscovery(t *testing.T) {
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	ndpDisp := newNDPDispatcherForTest()
	ns := newNetstackWithNDPDispatcher(t, ndpDisp)
	ndpDisp.start(ctx)

	ifs1, err := addNoopEndpoint(ns, t.Name()+"1")
	if err != nil {
		t.Fatal(err)
	}
	if err := ifs1.Up(); err != nil {
		t.Fatalf("ifs1.Up(): %s", err)
	}
	ifs2, err := addNoopEndpoint(ns, t.Name()+"2")
	if err != nil {
		t.Fatal(err)
	}
	if err := ifs2.Up(); err != nil {
		t.Fatalf("ifs2.Up(): %s", err)
	}

	// Test discovering a new default router on eth1.
	accept := ndpDisp.OnDefaultRouterDiscovered(ifs1.nicid, testLinkLocalV6Addr1)
	if !accept {
		t.Fatalf("got OnDefaultRouterDiscovered(%d, %s) = false, want = true", ifs1.nicid, testLinkLocalV6Addr1)
	}
	waitForEmptyQueue(ndpDisp)
	nic1Rtr1Rt := defaultV6Route(ifs1.nicid, testLinkLocalV6Addr1)
	if rts := ns.stack.GetRouteTable(); !containsRoute(rts, nic1Rtr1Rt) {
		t.Fatalf("missing route = %s from route table, got = %s", nic1Rtr1Rt, rts)
	}

	// Test discovering a new default router on eth2 (with the same
	// link-local IP as the one discovered as eth1).
	accept = ndpDisp.OnDefaultRouterDiscovered(ifs2.nicid, testLinkLocalV6Addr1)
	if !accept {
		t.Fatalf("got OnDefaultRouterDiscovered(%d, %s) = false, want = true", ifs2.nicid, testLinkLocalV6Addr1)
	}
	waitForEmptyQueue(ndpDisp)
	nic2Rtr1Rt := defaultV6Route(ifs2.nicid, testLinkLocalV6Addr1)
	rts := ns.stack.GetRouteTable()
	if !containsRoute(rts, nic2Rtr1Rt) {
		t.Fatalf("missing route = %s from route table, got = %s", nic2Rtr1Rt, rts)
	}
	// Should still have the route from before.
	if !containsRoute(rts, nic1Rtr1Rt) {
		t.Fatalf("missing route = %s from route table, got = %s", nic1Rtr1Rt, rts)
	}

	// Test discovering another default router on eth2.
	accept = ndpDisp.OnDefaultRouterDiscovered(ifs2.nicid, testLinkLocalV6Addr2)
	if !accept {
		t.Fatalf("got OnDefaultRouterDiscovered(%d, %s) = false, want = true", ifs2.nicid, testLinkLocalV6Addr1)
	}
	waitForEmptyQueue(ndpDisp)
	nic2Rtr2Rt := defaultV6Route(ifs2.nicid, testLinkLocalV6Addr2)
	rts = ns.stack.GetRouteTable()
	if !containsRoute(rts, nic2Rtr2Rt) {
		t.Fatalf("missing route = %s from route table, got = %s", nic2Rtr2Rt, rts)
	}
	// Should still have the routes from before.
	if !containsRoute(rts, nic2Rtr1Rt) {
		t.Fatalf("missing route = %s from route table, got = %s", nic2Rtr1Rt, rts)
	}
	if !containsRoute(rts, nic1Rtr1Rt) {
		t.Fatalf("missing route = %s from route table, got = %s", nic1Rtr1Rt, rts)
	}

	// Invalidate the router with IP testLinkLocalV6Addr1 from eth2.
	ndpDisp.OnDefaultRouterInvalidated(ifs2.nicid, testLinkLocalV6Addr1)
	waitForEmptyQueue(ndpDisp)
	rts = ns.stack.GetRouteTable()
	if containsRoute(rts, nic2Rtr1Rt) {
		t.Fatalf("should not have route = %s in the route table, got = %s", nic2Rtr1Rt, rts)
	}
	// Should still have default routes through the non-invalidated
	// routers.
	if !containsRoute(rts, nic2Rtr2Rt) {
		t.Fatalf("missing route = %s from route table, got = %s", nic2Rtr2Rt, rts)
	}
	if !containsRoute(rts, nic1Rtr1Rt) {
		t.Fatalf("missing route = %s from route table, got = %s", nic1Rtr1Rt, rts)
	}

	// Invalidate the router with IP testLinkLocalV6Addr1 from eth1.
	ndpDisp.OnDefaultRouterInvalidated(ifs1.nicid, testLinkLocalV6Addr1)
	waitForEmptyQueue(ndpDisp)
	rts = ns.stack.GetRouteTable()
	if containsRoute(rts, nic1Rtr1Rt) {
		t.Fatalf("should not have route = %s in the route table, got = %s", nic1Rtr1Rt, rts)
	}
	// Should still not have the other invalidated route.
	if containsRoute(rts, nic2Rtr1Rt) {
		t.Fatalf("should not have route = %s in the route table, got = %s", nic2Rtr1Rt, rts)
	}
	// Should still have default route through the non-invalidated router.
	if !containsRoute(rts, nic2Rtr2Rt) {
		t.Fatalf("missing route = %s from route table, got = %s", nic2Rtr2Rt, rts)
	}

	// Invalidate the router with IP testLinkLocalV6Addr2 from eth2.
	ndpDisp.OnDefaultRouterInvalidated(ifs2.nicid, testLinkLocalV6Addr2)
	waitForEmptyQueue(ndpDisp)
	rts = ns.stack.GetRouteTable()
	if containsRoute(rts, nic2Rtr2Rt) {
		t.Fatalf("should not have route = %s in the route table, got = %s", nic2Rtr2Rt, rts)
	}
	// Should still not have the other invalidated route.
	if containsRoute(rts, nic1Rtr1Rt) {
		t.Fatalf("should not have route = %s in the route table, got = %s", nic1Rtr1Rt, rts)
	}
	if containsRoute(rts, nic2Rtr1Rt) {
		t.Fatalf("should not have route = %s in the route table, got = %s", nic2Rtr1Rt, rts)
	}
}

// Test that attempting to invalidate an on-link prefix which we do not have a
// route for is not an issue.
func TestNDPInvalidateUnknownIPv6Prefix(t *testing.T) {
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	ndpDisp := newNDPDispatcherForTest()
	ns := newNetstackWithNDPDispatcher(t, ndpDisp)
	ndpDisp.start(ctx)

	ifs, err := addNoopEndpoint(ns, t.Name())
	if err != nil {
		t.Fatal(err)
	}
	if err := ifs.Up(); err != nil {
		t.Fatalf("ifs.Up(): %s", err)
	}

	// Invalidate the prefix subnet1 from eth (even though we do not yet know
	// about it).
	ndpDisp.OnOnLinkPrefixInvalidated(ifs.nicid, subnet1)
	waitForEmptyQueue(ndpDisp)
	if rt, rts := onLinkV6Route(ifs.nicid, subnet1), ns.stack.GetRouteTable(); containsRoute(rts, rt) {
		t.Fatalf("should not have route = %s in the route table, got = %s", rt, rts)
	}
}

// Test that ndpDispatcher properly handles the discovery and invalidation of
// on-link IPv6 prefixes.
func TestNDPIPv6PrefixDiscovery(t *testing.T) {
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	ndpDisp := newNDPDispatcherForTest()
	ns := newNetstackWithNDPDispatcher(t, ndpDisp)
	ndpDisp.start(ctx)

	ifs1, err := addNoopEndpoint(ns, t.Name()+"1")
	if err != nil {
		t.Fatal(err)
	}
	if err := ifs1.Up(); err != nil {
		t.Fatalf("ifs1.Up(): %s", err)
	}
	ifs2, err := addNoopEndpoint(ns, t.Name()+"2")
	if err != nil {
		t.Fatal(err)
	}
	if err := ifs2.Up(); err != nil {
		t.Fatalf("ifs2.Up(): %s", err)
	}

	// Test discovering a new on-link prefix on eth1.
	accept := ndpDisp.OnOnLinkPrefixDiscovered(ifs1.nicid, subnet1)
	if !accept {
		t.Fatalf("got OnOnLinkPrefixDiscovered(%d, %s) = false, want = true", ifs1.nicid, subnet1)
	}
	waitForEmptyQueue(ndpDisp)
	nic1Sub1Rt := onLinkV6Route(ifs1.nicid, subnet1)
	if rts := ns.stack.GetRouteTable(); !containsRoute(rts, nic1Sub1Rt) {
		t.Fatalf("missing route = %s from route table, got = %s", nic1Sub1Rt, rts)
	}

	// Test discovering the same on-link prefix on eth2.
	accept = ndpDisp.OnOnLinkPrefixDiscovered(ifs2.nicid, subnet1)
	if !accept {
		t.Fatalf("got OnOnLinkPrefixDiscovered(%d, %s) = false, want = true", ifs2.nicid, subnet1)
	}
	waitForEmptyQueue(ndpDisp)
	nic2Sub1Rt := onLinkV6Route(ifs2.nicid, subnet1)
	rts := ns.stack.GetRouteTable()
	if !containsRoute(rts, nic2Sub1Rt) {
		t.Fatalf("missing route = %s from route table, got = %s", nic2Sub1Rt, rts)
	}
	// Should still have the route from before.
	if !containsRoute(rts, nic1Sub1Rt) {
		t.Fatalf("missing route = %s from route table, got = %s", nic1Sub1Rt, rts)
	}

	// Test discovering another on-link prefix on eth2.
	accept = ndpDisp.OnOnLinkPrefixDiscovered(ifs2.nicid, subnet2)
	if !accept {
		t.Fatalf("got OnOnLinkPrefixDiscovered(%d, %s) = false, want = true", ifs2.nicid, subnet2)
	}
	waitForEmptyQueue(ndpDisp)
	nic2Sub2Rt := onLinkV6Route(ifs2.nicid, subnet2)
	rts = ns.stack.GetRouteTable()
	if !containsRoute(rts, nic2Sub2Rt) {
		t.Fatalf("missing route = %s from route table, got = %s", nic2Sub2Rt, rts)
	}
	// Should still have the routes from before.
	if !containsRoute(rts, nic2Sub1Rt) {
		t.Fatalf("missing route = %s from route table, got = %s", nic2Sub1Rt, rts)
	}
	if !containsRoute(rts, nic1Sub1Rt) {
		t.Fatalf("missing route = %s from route table, got = %s", nic1Sub1Rt, rts)
	}

	// Invalidate the prefix subnet1 from eth2.
	ndpDisp.OnOnLinkPrefixInvalidated(ifs2.nicid, subnet1)
	waitForEmptyQueue(ndpDisp)
	rts = ns.stack.GetRouteTable()
	if containsRoute(rts, nic2Sub1Rt) {
		t.Fatalf("should not have route = %s in the route table, got = %s", nic2Sub1Rt, rts)
	}
	// Should still have default routes through the non-invalidated
	// routers.
	if !containsRoute(rts, nic2Sub2Rt) {
		t.Fatalf("missing route = %s from route table, got = %s", nic2Sub2Rt, rts)
	}
	if !containsRoute(rts, nic1Sub1Rt) {
		t.Fatalf("missing route = %s from route table, got = %s", nic1Sub1Rt, rts)
	}

	// Invalidate the prefix subnet1 from eth1.
	ndpDisp.OnOnLinkPrefixInvalidated(ifs1.nicid, subnet1)
	waitForEmptyQueue(ndpDisp)
	rts = ns.stack.GetRouteTable()
	if containsRoute(rts, nic1Sub1Rt) {
		t.Fatalf("should not have route = %s in the route table, got = %s", nic1Sub1Rt, rts)
	}
	// Should still not have the other invalidated route.
	if containsRoute(rts, nic2Sub1Rt) {
		t.Fatalf("should not have route = %s in the route table, got = %s", nic2Sub1Rt, rts)
	}
	// Should still have default route through the non-invalidated router.
	if !containsRoute(rts, nic2Sub2Rt) {
		t.Fatalf("missing route = %s from route table, got = %s", nic2Sub2Rt, rts)
	}

	// Invalidate the prefix subnet2 from eth2.
	ndpDisp.OnOnLinkPrefixInvalidated(ifs2.nicid, subnet2)
	waitForEmptyQueue(ndpDisp)
	rts = ns.stack.GetRouteTable()
	if containsRoute(rts, nic2Sub2Rt) {
		t.Fatalf("should not have route = %s in the route table, got = %s", nic2Sub2Rt, rts)
	}
	// Should still not have the other invalidated route.
	if containsRoute(rts, nic1Sub1Rt) {
		t.Fatalf("should not have route = %s in the route table, got = %s", nic1Sub1Rt, rts)
	}
	if containsRoute(rts, nic2Sub1Rt) {
		t.Fatalf("should not have route = %s in the route table, got = %s", nic2Sub1Rt, rts)
	}
}

// TestLinkDown tests that Recursive DNS Servers learned from NDP are
// invalidated when a NIC is brought down.
func TestLinkDown(t *testing.T) {
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	ndpDisp := newNDPDispatcherForTest()
	ns := newNetstackWithNDPDispatcher(t, ndpDisp)
	ndpDisp.start(ctx)

	ifs1, err := addNoopEndpoint(ns, t.Name()+"1")
	if err != nil {
		t.Fatal(err)
	}
	if err := ifs1.Up(); err != nil {
		t.Fatalf("ifs1.Up(): %s", err)
	}
	ifs2, err := addNoopEndpoint(ns, t.Name()+"2")
	if err != nil {
		t.Fatal(err)
	}
	if err := ifs2.Up(); err != nil {
		t.Fatalf("ifs2.Up(): %s", err)
	}

	addr1NIC1 := tcpip.FullAddress{
		Addr: "\xfe\x80\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01",
		Port: 53,
		NIC:  ifs1.nicid,
	}
	addr1NIC2 := tcpip.FullAddress{
		Addr: "\xfe\x80\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01",
		Port: 53,
		NIC:  ifs2.nicid,
	}
	addr2NIC1 := tcpip.FullAddress{
		Addr: "\xfe\x80\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02",
		Port: 53,
		NIC:  ifs1.nicid,
	}
	addr3NIC2 := tcpip.FullAddress{
		Addr: "\xfe\x80\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x03",
		Port: 53,
		NIC:  ifs2.nicid,
	}

	ndpDisp.OnRecursiveDNSServerOption(ifs1.nicid, []tcpip.Address{addr1NIC1.Addr, addr2NIC1.Addr}, defaultLifetime)
	ndpDisp.OnRecursiveDNSServerOption(ifs2.nicid, []tcpip.Address{addr1NIC2.Addr, addr3NIC2.Addr}, defaultLifetime)
	waitForEmptyQueue(ndpDisp)
	servers := ns.dnsConfig.GetServersCache()
	if !containsFullAddress(servers, addr1NIC1) {
		t.Errorf("expected %+v to be in the DNS server cache, got = %+v", addr1NIC1, servers)
	}
	if !containsFullAddress(servers, addr2NIC1) {
		t.Errorf("expected %+v to be in the DNS server cache, got = %+v", addr2NIC1, servers)
	}
	if !containsFullAddress(servers, addr1NIC2) {
		t.Errorf("expected %+v to be in the DNS server cache, got = %+v", addr1NIC2, servers)
	}
	if !containsFullAddress(servers, addr3NIC2) {
		t.Errorf("expected %+v to be in the DNS server cache, got = %+v", addr3NIC2, servers)
	}
	if l := len(servers); l != 4 {
		t.Errorf("got len(servers) = %d, want = 4; servers = %+v", l, servers)
	}

	// If the test already failed, we cannot go any further.
	if t.Failed() {
		t.FailNow()
	}

	// Bring eth2 down and make sure the DNS servers learned from that NIC are
	// invalidated.
	if err := ifs2.Down(); err != nil {
		t.Fatalf("ifs2.Down(): %s", err)
	}
	waitForEmptyQueue(ndpDisp)
	servers = ns.dnsConfig.GetServersCache()
	if !containsFullAddress(servers, addr1NIC1) {
		t.Errorf("expected %+v to be in the DNS server cache, got = %+v", addr1NIC1, servers)
	}
	if !containsFullAddress(servers, addr2NIC1) {
		t.Errorf("expected %+v to be in the DNS server cache, got = %+v", addr2NIC1, servers)
	}
	if l := len(servers); l != 2 {
		t.Errorf("got len(servers) = %d, want = 2; servers = %+v", l, servers)
	}

	// If the test already failed, we cannot go any further.
	if t.Failed() {
		t.FailNow()
	}

	// Bring eth2 up and make sure the DNS servers learned from that NIC do not
	// reappear.
	if err := ifs2.Up(); err != nil {
		t.Fatalf("ifs2.Up(): %s", err)
	}
	waitForEmptyQueue(ndpDisp)
	servers = ns.dnsConfig.GetServersCache()
	if !containsFullAddress(servers, addr1NIC1) {
		t.Errorf("expected %+v to be in the DNS server cache, got = %+v", addr1NIC1, servers)
	}
	if !containsFullAddress(servers, addr2NIC1) {
		t.Errorf("expected %+v to be in the DNS server cache, got = %+v", addr2NIC1, servers)
	}
	if l := len(servers); l != 2 {
		t.Errorf("got len(servers) = %d, want = 2; servers = %+v", l, servers)
	}
}

func containsFullAddress(list []dns.Server, item tcpip.FullAddress) bool {
	for _, i := range list {
		if i.Address == item {
			return true
		}
	}

	return false
}

func TestRecursiveDNSServers(t *testing.T) {
	t.Parallel()

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	ndpDisp := newNDPDispatcherForTest()
	ns := newNetstackWithNDPDispatcher(t, ndpDisp)
	ndpDisp.start(ctx)

	ifs1, err := addNoopEndpoint(ns, t.Name()+"1")
	if err != nil {
		t.Fatal(err)
	}
	if err := ifs1.Up(); err != nil {
		t.Fatalf("ifs1.Up(): %s", err)
	}
	ifs2, err := addNoopEndpoint(ns, t.Name()+"2")
	if err != nil {
		t.Fatal(err)
	}
	if err := ifs2.Up(); err != nil {
		t.Fatalf("ifs2.Up(): %s", err)
	}

	addr1NIC1 := tcpip.FullAddress{
		Addr: "\xfe\x80\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01",
		Port: 53,
		NIC:  ifs1.nicid,
	}
	addr1NIC2 := tcpip.FullAddress{
		Addr: "\xfe\x80\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01",
		Port: 53,
		NIC:  ifs2.nicid,
	}
	addr2NIC1 := tcpip.FullAddress{
		Addr: "\xfe\x80\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02",
		Port: 53,
		NIC:  ifs1.nicid,
	}
	addr3NIC2 := tcpip.FullAddress{
		Addr: "\xfe\x80\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x03",
		Port: 53,
		NIC:  ifs2.nicid,
	}

	ndpDisp.OnRecursiveDNSServerOption(ifs1.nicid, []tcpip.Address{addr1NIC1.Addr, addr2NIC1.Addr}, 0)
	waitForEmptyQueue(ndpDisp)
	servers := ns.dnsConfig.GetServersCache()
	if l := len(servers); l != 0 {
		t.Errorf("got len(servers) = %d, want = 0; servers = %+v", l, servers)
	}

	ndpDisp.OnRecursiveDNSServerOption(ifs1.nicid, []tcpip.Address{addr1NIC1.Addr, addr2NIC1.Addr}, defaultLifetime)
	waitForEmptyQueue(ndpDisp)
	servers = ns.dnsConfig.GetServersCache()
	if !containsFullAddress(servers, addr1NIC1) {
		t.Errorf("expected %+v to be in the server cache, got = %+v", addr1NIC1, servers)
	}
	if !containsFullAddress(servers, addr2NIC1) {
		t.Errorf("expected %+v to be in the server cache, got = %+v", addr2NIC1, servers)
	}
	if l := len(servers); l != 2 {
		t.Errorf("got len(servers) = %d, want = 2; servers = %+v", l, servers)
	}

	ndpDisp.OnRecursiveDNSServerOption(ifs2.nicid, []tcpip.Address{addr1NIC2.Addr, addr3NIC2.Addr}, defaultLifetime)
	waitForEmptyQueue(ndpDisp)
	servers = ns.dnsConfig.GetServersCache()
	if !containsFullAddress(servers, addr1NIC1) {
		t.Errorf("expected %+v to be in the server cache, got = %+v", addr1NIC1, servers)
	}
	if !containsFullAddress(servers, addr2NIC1) {
		t.Errorf("expected %+v to be in the server cache, got = %+v", addr2NIC1, servers)
	}
	if !containsFullAddress(servers, addr1NIC2) {
		t.Errorf("expected %+v to be in the server cache, got = %+v", addr1NIC2, servers)
	}
	if !containsFullAddress(servers, addr3NIC2) {
		t.Errorf("expected %+v to be in the server cache, got = %+v", addr3NIC2, servers)
	}
	if l := len(servers); l != 4 {
		t.Errorf("got len(servers) = %d, want = 4; servers = %+v", l, servers)
	}

	ndpDisp.OnRecursiveDNSServerOption(ifs2.nicid, []tcpip.Address{addr1NIC2.Addr, addr3NIC2.Addr}, 0)
	waitForEmptyQueue(ndpDisp)
	servers = ns.dnsConfig.GetServersCache()
	if !containsFullAddress(servers, addr1NIC1) {
		t.Errorf("expected %+v to be in the server cache, got = %+v", addr1NIC1, servers)
	}
	if !containsFullAddress(servers, addr2NIC1) {
		t.Errorf("expected %+v to be in the server cache, got = %+v", addr2NIC1, servers)
	}
	if l := len(servers); l != 2 {
		t.Errorf("got len(servers) = %d, want = 2; servers = %+v", l, servers)
	}

	ndpDisp.OnRecursiveDNSServerOption(ifs1.nicid, []tcpip.Address{addr1NIC1.Addr, addr2NIC1.Addr}, shortLifetime)
	waitForEmptyQueue(ndpDisp)
	for elapsedTime := time.Duration(0); elapsedTime <= shortLifetimeTimeout; elapsedTime += incrementalTimeout {
		time.Sleep(incrementalTimeout)
		servers = ns.dnsConfig.GetServersCache()
		if l := len(servers); l != 0 {
			if elapsedTime < shortLifetimeTimeout {
				continue
			}

			t.Fatalf("got len(servers) = %d, want = 0; servers = %+v", l, servers)
		}

		break
	}
}

func TestRecursiveDNSServersWithInfiniteLifetime(t *testing.T) {
	const newInfiniteLifetime = time.Second
	const newInfiniteLifetimeTimeout = 2 * time.Second
	saved := header.NDPInfiniteLifetime
	defer func() { header.NDPInfiniteLifetime = saved }()
	header.NDPInfiniteLifetime = newInfiniteLifetime

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	ndpDisp := newNDPDispatcherForTest()
	ns := newNetstackWithNDPDispatcher(t, ndpDisp)
	ndpDisp.start(ctx)

	ifs, err := addNoopEndpoint(ns, t.Name())
	if err != nil {
		t.Fatal(err)
	}
	if err := ifs.Up(); err != nil {
		t.Fatalf("ifs.Up(): %s", err)
	}

	addr1 := tcpip.FullAddress{
		Addr: "\xfe\x80\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01",
		Port: 53,
		NIC:  ifs.nicid,
	}
	addr2 := tcpip.FullAddress{
		Addr: "\xfe\x80\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02",
		Port: 53,
		NIC:  ifs.nicid,
	}
	addr3 := tcpip.FullAddress{
		Addr: "\xfe\x80\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x03",
		Port: 53,
		NIC:  ifs.nicid,
	}
	ndpDisp.OnRecursiveDNSServerOption(ifs.nicid, []tcpip.Address{addr1.Addr, addr2.Addr, addr3.Addr}, newInfiniteLifetime)
	waitForEmptyQueue(ndpDisp)
	servers := ns.dnsConfig.GetServersCache()
	if !containsFullAddress(servers, addr1) {
		t.Errorf("expected %+v to be in the server cache, got = %+v", addr1, servers)
	}
	if !containsFullAddress(servers, addr2) {
		t.Errorf("expected %+v to be in the server cache, got = %+v", addr2, servers)
	}
	if !containsFullAddress(servers, addr3) {
		t.Errorf("expected %+v to be in the server cache, got = %+v", addr3, servers)
	}
	if l := len(servers); l != 3 {
		t.Errorf("got len(servers) = %d, want = 3; servers = %+v", l, servers)
	}

	// If the test already failed, we cannot go any further.
	if t.Failed() {
		t.FailNow()
	}

	// All addresses to expire after middleLifetime.
	ndpDisp.OnRecursiveDNSServerOption(ifs.nicid, []tcpip.Address{addr1.Addr, addr2.Addr, addr3.Addr}, middleLifetime)
	// Update addr2 and addr3 to be valid forever.
	ndpDisp.OnRecursiveDNSServerOption(ifs.nicid, []tcpip.Address{addr2.Addr, addr3.Addr}, newInfiniteLifetime)
	waitForEmptyQueue(ndpDisp)
	for elapsedTime := time.Duration(0); elapsedTime <= middleLifetimeTimeout; elapsedTime += incrementalTimeout {
		time.Sleep(incrementalTimeout)
		servers = ns.dnsConfig.GetServersCache()
		if !containsFullAddress(servers, addr2) {
			if elapsedTime < middleLifetimeTimeout {
				continue
			}

			t.Errorf("expected %+v to be in the server cache, got = %+v", addr2, servers)
		}
		if !containsFullAddress(servers, addr3) {
			if elapsedTime < middleLifetimeTimeout {
				continue
			}

			t.Errorf("expected %+v to be in the server cache, got = %+v", addr3, servers)
		}
		if l := len(servers); l != 2 {
			if elapsedTime < middleLifetimeTimeout {
				continue
			}

			t.Errorf("got len(servers) = %d, want = 2; servers = %+v", l, servers)
		}

		break
	}

	// If the test already failed, we cannot go any further.
	if t.Failed() {
		t.FailNow()
	}

	// addr2 and addr3 should not expire after newInfiniteLifetime (since it
	// represents infinity).
	for elapsedTime := time.Duration(0); elapsedTime <= newInfiniteLifetimeTimeout; elapsedTime += incrementalTimeout {
		time.Sleep(incrementalTimeout)
		servers = ns.dnsConfig.GetServersCache()
		if !containsFullAddress(servers, addr2) {
			t.Errorf("expected %+v to be in the server cache, got = %+v", addr2, servers)
		}
		if !containsFullAddress(servers, addr3) {
			t.Errorf("expected %+v to be in the server cache, got = %+v", addr3, servers)
		}
		if l := len(servers); l != 2 {
			t.Errorf("got len(servers) = %d, want = 2; servers = %+v", l, servers)
		}

		// If the test failed in this iteration of the loop, we cannot go any
		// further.
		if t.Failed() {
			t.FailNow()
		}
	}
}

func TestObservationsFromDHCPv6Configuration(t *testing.T) {
	const (
		nicID1 = 1
		nicID2 = 2
	)

	linkLocalAddr := tcpip.AddressWithPrefix{
		Address:   util.Parse("fe80::1"),
		PrefixLen: 64,
	}

	// ndpDispatcher registers novel observations by calling cobaltClient.Register.
	// When the observations are pulled, we ensure that all novelty since the
	// last pull is included and duplicate observations are coalesced.
	type step struct {
		run  func(ndpDisp *ndpDispatcher)
		want []cobalt.CobaltEvent
	}

	defaultInit := func(ndpDisp *ndpDispatcher, cobaltClient *cobaltClient) {
		ndpDisp.dhcpv6Obs.init(func() {
			cobaltClient.Register(&ndpDisp.dhcpv6Obs)
		})

		ndpDisp.dynamicAddressSourceObs.initWithoutTimer(func() {
			cobaltClient.Register(&ndpDisp.dynamicAddressSourceObs)
		})
	}

	availableDynamicAddressConfigsInit := func(ndpDisp *ndpDispatcher, cobaltClient *cobaltClient) {
		// We pass a noop for dhcpv6Obs' event's registration callback since we
		// only care about the available dynamic address configurations metric.
		ndpDisp.dhcpv6Obs.init(func() {})

		ndpDisp.dynamicAddressSourceObs.initWithoutTimer(func() {
			cobaltClient.Register(&ndpDisp.dynamicAddressSourceObs)
		})
	}

	tests := []struct {
		name  string
		init  func(*ndpDispatcher, *cobaltClient)
		steps []step
	}{
		{
			name: "one_configuration",
			init: defaultInit,
			steps: []step{
				{
					run: func(ndpDisp *ndpDispatcher) {
						ndpDisp.OnDHCPv6Configuration(0, stack.DHCPv6NoConfiguration)
					},
					want: []cobalt.CobaltEvent{
						{
							MetricId:   networking_metrics.DhcpV6ConfigurationMetricId,
							EventCodes: []uint32{uint32(networking_metrics.NoConfiguration)},
							Payload:    cobalt.EventPayloadWithEvent(cobalt.Event{}),
						},
					},
				},
			},
		},
		{
			name: "multiple_configurations",
			init: defaultInit,
			steps: []step{
				{
					run: func(ndpDisp *ndpDispatcher) {
						ndpDisp.OnDHCPv6Configuration(0, stack.DHCPv6NoConfiguration)
						ndpDisp.OnDHCPv6Configuration(0, stack.DHCPv6ManagedAddress)
					},
					want: []cobalt.CobaltEvent{
						{
							MetricId:   networking_metrics.DhcpV6ConfigurationMetricId,
							EventCodes: []uint32{uint32(networking_metrics.NoConfiguration)},
							Payload:    cobalt.EventPayloadWithEvent(cobalt.Event{}),
						},
						{
							MetricId:   networking_metrics.DhcpV6ConfigurationMetricId,
							EventCodes: []uint32{uint32(networking_metrics.ManagedAddress)},
							Payload:    cobalt.EventPayloadWithEvent(cobalt.Event{}),
						},
					},
				},
			},
		},
		{
			name: "pull_between_configurations",
			init: defaultInit,
			steps: []step{
				{
					run: func(ndpDisp *ndpDispatcher) {
						ndpDisp.OnDHCPv6Configuration(0, stack.DHCPv6NoConfiguration)
					},
					want: []cobalt.CobaltEvent{
						{
							MetricId:   networking_metrics.DhcpV6ConfigurationMetricId,
							EventCodes: []uint32{uint32(networking_metrics.NoConfiguration)},
							Payload:    cobalt.EventPayloadWithEvent(cobalt.Event{}),
						},
					},
				},
				{
					run: func(ndpDisp *ndpDispatcher) {
						ndpDisp.OnDHCPv6Configuration(0, stack.DHCPv6NoConfiguration)
					},
					want: []cobalt.CobaltEvent{
						{
							MetricId:   networking_metrics.DhcpV6ConfigurationMetricId,
							EventCodes: []uint32{uint32(networking_metrics.NoConfiguration)},
							Payload:    cobalt.EventPayloadWithEvent(cobalt.Event{}),
						},
					},
				},
			},
		},
		{
			name: "duplicated_configurations",
			init: defaultInit,
			steps: []step{
				{
					run: func(ndpDisp *ndpDispatcher) {
						ndpDisp.OnDHCPv6Configuration(0, stack.DHCPv6NoConfiguration)
						ndpDisp.OnDHCPv6Configuration(0, stack.DHCPv6NoConfiguration)
					},
					want: []cobalt.CobaltEvent{
						{
							MetricId:   networking_metrics.DhcpV6ConfigurationMetricId,
							EventCodes: []uint32{uint32(networking_metrics.NoConfiguration)},
							Payload:    cobalt.EventPayloadWithEvent(cobalt.Event{}),
						},
						{
							MetricId:   networking_metrics.DhcpV6ConfigurationMetricId,
							EventCodes: []uint32{uint32(networking_metrics.NoConfiguration)},
							Payload:    cobalt.EventPayloadWithEvent(cobalt.Event{}),
						},
					},
				},
			},
		},

		{
			name: "dynamic address config with no config",
			init: availableDynamicAddressConfigsInit,
			steps: []step{
				{
					run: func(ndpDisp *ndpDispatcher) {
						ndpDisp.dynamicAddressSourceObs.hasEvents()
					},
					want: nil,
				},
				{
					run: func(ndpDisp *ndpDispatcher) {
						// Link local addresses should not increment global SLAAC address
						// count.
						ndpDisp.OnAutoGenAddress(nicID1, linkLocalAddr)
						ndpDisp.dynamicAddressSourceObs.hasEvents()
					},
					want: nil,
				},
				{
					run: func(ndpDisp *ndpDispatcher) {
						ndpDisp.OnDHCPv6Configuration(nicID1, stack.DHCPv6NoConfiguration)
						ndpDisp.dynamicAddressSourceObs.hasEvents()
					},
					want: []cobalt.CobaltEvent{
						{
							MetricId:   networking_metrics.AvailableDynamicIpv6AddressConfigMetricId,
							EventCodes: []uint32{uint32(networking_metrics.NoGlobalSlaacOrDhcpv6ManagedAddress), nicID1},
							Payload:    cobalt.EventPayloadWithEventCount(cobalt.CountEvent{Count: 1}),
						},
					},
				},
				{
					run: func(ndpDisp *ndpDispatcher) {
						ndpDisp.OnDHCPv6Configuration(nicID1, stack.DHCPv6OtherConfigurations)
						ndpDisp.dynamicAddressSourceObs.hasEvents()
					},
					want: []cobalt.CobaltEvent{
						{
							MetricId:   networking_metrics.AvailableDynamicIpv6AddressConfigMetricId,
							EventCodes: []uint32{uint32(networking_metrics.NoGlobalSlaacOrDhcpv6ManagedAddress), nicID1},
							Payload:    cobalt.EventPayloadWithEventCount(cobalt.CountEvent{Count: 1}),
						},
					},
				},
				{
					run: func(ndpDisp *ndpDispatcher) {
						ndpDisp.OnAutoGenAddress(nicID1, testProtocolAddr1.AddressWithPrefix)
						ndpDisp.OnAutoGenAddressInvalidated(nicID1, testProtocolAddr1.AddressWithPrefix)
						ndpDisp.dynamicAddressSourceObs.hasEvents()
					},
					want: []cobalt.CobaltEvent{
						{
							MetricId:   networking_metrics.AvailableDynamicIpv6AddressConfigMetricId,
							EventCodes: []uint32{uint32(networking_metrics.NoGlobalSlaacOrDhcpv6ManagedAddress), nicID1},
							Payload:    cobalt.EventPayloadWithEventCount(cobalt.CountEvent{Count: 1}),
						},
					},
				},
			},
		},
		{
			name: "dynamic address config with slaac only",
			init: availableDynamicAddressConfigsInit,
			steps: []step{
				{
					run: func(ndpDisp *ndpDispatcher) {
						ndpDisp.OnAutoGenAddress(nicID1, testProtocolAddr1.AddressWithPrefix)
						ndpDisp.dynamicAddressSourceObs.hasEvents()
					},
					want: []cobalt.CobaltEvent{
						{
							MetricId:   networking_metrics.AvailableDynamicIpv6AddressConfigMetricId,
							EventCodes: []uint32{uint32(networking_metrics.GlobalSlaacOnly), nicID1},
							Payload:    cobalt.EventPayloadWithEventCount(cobalt.CountEvent{Count: 1}),
						},
					},
				},
				{
					run: func(ndpDisp *ndpDispatcher) {
						// Link local addresses should not decrement global SLAAC address
						// count.
						ndpDisp.OnAutoGenAddressInvalidated(nicID1, linkLocalAddr)
						ndpDisp.dynamicAddressSourceObs.hasEvents()
					},
					want: []cobalt.CobaltEvent{
						{
							MetricId:   networking_metrics.AvailableDynamicIpv6AddressConfigMetricId,
							EventCodes: []uint32{uint32(networking_metrics.GlobalSlaacOnly), nicID1},
							Payload:    cobalt.EventPayloadWithEventCount(cobalt.CountEvent{Count: 1}),
						},
					},
				},
				{
					run: func(ndpDisp *ndpDispatcher) {
						ndpDisp.OnAutoGenAddress(nicID1, testProtocolAddr2.AddressWithPrefix)
						ndpDisp.dynamicAddressSourceObs.hasEvents()
					},
					want: []cobalt.CobaltEvent{
						{
							MetricId:   networking_metrics.AvailableDynamicIpv6AddressConfigMetricId,
							EventCodes: []uint32{uint32(networking_metrics.GlobalSlaacOnly), nicID1},
							Payload:    cobalt.EventPayloadWithEventCount(cobalt.CountEvent{Count: 1}),
						},
					},
				},
				{
					run: func(ndpDisp *ndpDispatcher) {
						ndpDisp.OnAutoGenAddressInvalidated(nicID1, testProtocolAddr2.AddressWithPrefix)
						ndpDisp.dynamicAddressSourceObs.hasEvents()
					},
					want: []cobalt.CobaltEvent{
						{
							MetricId:   networking_metrics.AvailableDynamicIpv6AddressConfigMetricId,
							EventCodes: []uint32{uint32(networking_metrics.GlobalSlaacOnly), nicID1},
							Payload:    cobalt.EventPayloadWithEventCount(cobalt.CountEvent{Count: 1}),
						},
					},
				},
			},
		},
		{
			name: "dynamic address config with dhcpv6 only",
			init: availableDynamicAddressConfigsInit,
			steps: []step{
				{
					run: func(ndpDisp *ndpDispatcher) {
						ndpDisp.OnDHCPv6Configuration(nicID1, stack.DHCPv6ManagedAddress)
						ndpDisp.dynamicAddressSourceObs.hasEvents()
					},
					want: []cobalt.CobaltEvent{
						{
							MetricId:   networking_metrics.AvailableDynamicIpv6AddressConfigMetricId,
							EventCodes: []uint32{uint32(networking_metrics.Dhcpv6ManagedAddressOnly), nicID1},
							Payload:    cobalt.EventPayloadWithEventCount(cobalt.CountEvent{Count: 1}),
						},
					},
				},
				// Only the last configuration should be used.
				{
					run: func(ndpDisp *ndpDispatcher) {
						ndpDisp.OnDHCPv6Configuration(nicID1, stack.DHCPv6NoConfiguration)
						ndpDisp.OnDHCPv6Configuration(nicID1, stack.DHCPv6ManagedAddress)
						ndpDisp.dynamicAddressSourceObs.hasEvents()
					},
					want: []cobalt.CobaltEvent{
						{
							MetricId:   networking_metrics.AvailableDynamicIpv6AddressConfigMetricId,
							EventCodes: []uint32{uint32(networking_metrics.Dhcpv6ManagedAddressOnly), nicID1},
							Payload:    cobalt.EventPayloadWithEventCount(cobalt.CountEvent{Count: 1}),
						},
					},
				},
			},
		},
		{
			name: "dynamic address config with dhcpv6 and slaac",
			init: availableDynamicAddressConfigsInit,
			steps: []step{
				{
					run: func(ndpDisp *ndpDispatcher) {
						ndpDisp.OnDHCPv6Configuration(nicID1, stack.DHCPv6ManagedAddress)
						ndpDisp.OnAutoGenAddress(nicID1, testProtocolAddr2.AddressWithPrefix)
						ndpDisp.dynamicAddressSourceObs.hasEvents()
					},
					want: []cobalt.CobaltEvent{
						{
							MetricId:   networking_metrics.AvailableDynamicIpv6AddressConfigMetricId,
							EventCodes: []uint32{uint32(networking_metrics.GlobalSlaacAndDhcpv6ManagedAddress), nicID1},
							Payload:    cobalt.EventPayloadWithEventCount(cobalt.CountEvent{Count: 1}),
						},
					},
				},
			},
		},
		{
			name: "dynamic address config with dhcpv6 and slaac on different NICs",
			init: availableDynamicAddressConfigsInit,
			steps: []step{
				{
					run: func(ndpDisp *ndpDispatcher) {
						ndpDisp.OnDHCPv6Configuration(nicID1, stack.DHCPv6ManagedAddress)
						ndpDisp.OnAutoGenAddress(nicID2, testProtocolAddr2.AddressWithPrefix)
						ndpDisp.dynamicAddressSourceObs.hasEvents()
					},
					want: []cobalt.CobaltEvent{
						{
							MetricId:   networking_metrics.AvailableDynamicIpv6AddressConfigMetricId,
							EventCodes: []uint32{uint32(networking_metrics.Dhcpv6ManagedAddressOnly), nicID1},
							Payload:    cobalt.EventPayloadWithEventCount(cobalt.CountEvent{Count: 1}),
						},
						{
							MetricId:   networking_metrics.AvailableDynamicIpv6AddressConfigMetricId,
							EventCodes: []uint32{uint32(networking_metrics.GlobalSlaacOnly), nicID2},
							Payload:    cobalt.EventPayloadWithEventCount(cobalt.CountEvent{Count: 1}),
						},
					},
				},
				{
					run: func(ndpDisp *ndpDispatcher) {
						ndpDisp.dynamicAddressSourceObs.removedNIC(nicID1)
						ndpDisp.dynamicAddressSourceObs.hasEvents()
					},
					want: []cobalt.CobaltEvent{
						{
							MetricId:   networking_metrics.AvailableDynamicIpv6AddressConfigMetricId,
							EventCodes: []uint32{uint32(networking_metrics.GlobalSlaacOnly), nicID2},
							Payload:    cobalt.EventPayloadWithEventCount(cobalt.CountEvent{Count: 1}),
						},
					},
				},
				{
					run: func(ndpDisp *ndpDispatcher) {
						ndpDisp.dynamicAddressSourceObs.removedNIC(nicID2)
						ndpDisp.dynamicAddressSourceObs.hasEvents()
					},
					want: nil,
				},
			},
		},
	}

	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			cobaltClient := NewCobaltClient()
			ndpDisp := newNDPDispatcher()
			test.init(ndpDisp, cobaltClient)

			for i, step := range test.steps {
				step.run(ndpDisp)
				got := cobaltClient.Collect()
				if diff := cmp.Diff(step.want, got,
					// There's exactly one event code per event.
					cmpopts.SortSlices(func(a, b cobalt.CobaltEvent) bool {
						return a.EventCodes[0] > b.EventCodes[0]
					}),
					cmpopts.IgnoreTypes(struct{}{})); diff != "" {
					t.Errorf("%d-th step: observations mismatch (-want +got):\n%s", i, diff)
				}
			}
		})
	}
}

func TestAvailableDynamicIPv6AddressConfigEventsRegistration(t *testing.T) {
	savedInitialTimeout := availableDynamicIPv6AddressConfigDelayInitialEvents
	savedDelayBetweenEvents := availableDynamicIPv6AddressConfigDelayBetweenEvents
	availableDynamicIPv6AddressConfigDelayInitialEvents = time.Second
	availableDynamicIPv6AddressConfigDelayBetweenEvents = 2 * time.Second
	defer func() {
		availableDynamicIPv6AddressConfigDelayInitialEvents = savedInitialTimeout
		availableDynamicIPv6AddressConfigDelayBetweenEvents = savedDelayBetweenEvents
	}()

	const extraTimeout = 10 * time.Second

	ch := make(chan struct{}, 1)

	ndpDisp := newNDPDispatcher()
	ndpDisp.dynamicAddressSourceObs.init(func() {
		ch <- struct{}{}
	})

	select {
	case <-ch:
	case <-time.After(availableDynamicIPv6AddressConfigDelayInitialEvents + extraTimeout):
		t.Fatalf("timed out waiting for first event's registration")
	}

	select {
	case <-ch:
	case <-time.After(availableDynamicIPv6AddressConfigDelayBetweenEvents + extraTimeout):
		t.Fatalf("timed out waiting for next event's registration")
	}
}
