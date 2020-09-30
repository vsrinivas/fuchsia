// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package netstack

import (
	"context"
	"errors"
	"flag"
	"fmt"
	"net"
	"os"
	"sort"
	"syscall/zx"
	"testing"
	"time"

	"fidl/fuchsia/hardware/ethernet"
	"fidl/fuchsia/io"
	fidlnet "fidl/fuchsia/net"
	"fidl/fuchsia/net/interfaces"
	"fidl/fuchsia/net/stack"
	"fidl/fuchsia/netstack"

	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/dhcp"
	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/dns"
	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/fidlconv"
	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/link/fifo/testutil"
	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/routes"
	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/util"
	"go.fuchsia.dev/fuchsia/src/lib/component"
	syslog "go.fuchsia.dev/fuchsia/src/lib/syslog/go"

	"github.com/google/go-cmp/cmp"
	"gvisor.dev/gvisor/pkg/tcpip"
	"gvisor.dev/gvisor/pkg/tcpip/header"
	"gvisor.dev/gvisor/pkg/tcpip/network/arp"
	"gvisor.dev/gvisor/pkg/tcpip/network/ipv4"
	"gvisor.dev/gvisor/pkg/tcpip/network/ipv6"
	tcpipstack "gvisor.dev/gvisor/pkg/tcpip/stack"
	"gvisor.dev/gvisor/pkg/tcpip/transport/tcp"
	"gvisor.dev/gvisor/pkg/tcpip/transport/udp"
	"gvisor.dev/gvisor/pkg/waiter"
)

const (
	testTopoPath         string        = "/fake/ethernet/device"
	testV4Address        tcpip.Address = "\xc0\xa8\x2a\x10"
	testV6Address        tcpip.Address = "\xc0\xa8\x2a\x10\xc0\xa8\x2a\x10\xc0\xa8\x2a\x10\xc0\xa8\x2a\x10"
	testLinkLocalV6Addr1 tcpip.Address = "\xfe\x80\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01"
	testLinkLocalV6Addr2 tcpip.Address = "\xfe\x80\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02"
	dadResolutionTimeout               = dadRetransmitTimer*dadTransmits + time.Second
)

func TestMain(m *testing.M) {
	flag.Parse()
	if testing.Verbose() {
		appCtx := component.NewContextFromStartupInfo()
		s, err := syslog.ConnectToLogger(appCtx.Connector())
		if err != nil {
			panic(fmt.Sprintf("syslog.ConnectToLogger() = %s", err))
		}
		options := syslog.LogInitOptions{
			LogLevel:                      syslog.AllLevel,
			MinSeverityForFileAndLineInfo: syslog.AllLevel,
			Socket:                        s,
		}
		l, err := syslog.NewLogger(options)
		if err != nil {
			panic(fmt.Sprintf("syslog.NewLogger(%#v) = %s", options, err))
		}
		syslog.SetDefaultLogger(l)
	}

	os.Exit(m.Run())
}

func TestDelRouteErrors(t *testing.T) {
	ns := newNetstack(t)

	ifs, err := addNoopEndpoint(ns, t.Name())
	if err != nil {
		t.Fatal(err)
	}

	rt := tcpip.Route{
		Destination: header.IPv4EmptySubnet,
		Gateway:     "\x01\x02\x03\x04",
		NIC:         ifs.nicid,
	}

	// Deleting a route we never added should result in an error.
	if err := ns.DelRoute(rt); err != routes.ErrNoSuchRoute {
		t.Errorf("got DelRoute(%s) = %v, want = %s", rt, err, routes.ErrNoSuchRoute)
	}

	if err := ns.AddRoute(rt, metricNotSet, false); err != nil {
		t.Fatalf("AddRoute(%s, metricNotSet, false): %s", rt, err)
	}
	// Deleting a route we added should not result in an error.
	if err := ns.DelRoute(rt); err != nil {
		t.Fatalf("got DelRoute(%s) = %s, want = nil", rt, err)
	}
	// Deleting a route we just deleted should result in an error.
	if err := ns.DelRoute(rt); err != routes.ErrNoSuchRoute {
		t.Errorf("got DelRoute(%s) = %v, want = %s", rt, err, routes.ErrNoSuchRoute)
	}
}

// TestStackNICEnableDisable tests that the NIC in stack.Stack is enabled or
// disabled when the underlying link is brought up or down, respectively.
func TestStackNICEnableDisable(t *testing.T) {
	ns := newNetstack(t)
	ifs, err := addNoopEndpoint(ns, t.Name())
	if err != nil {
		t.Fatal(err)
	}

	// The NIC should initially be disabled in stack.Stack.
	if enabled := ns.stack.CheckNIC(ifs.nicid); enabled {
		t.Fatalf("got ns.stack.CheckNIC(%d) = true, want = false", ifs.nicid)
	}

	isUp := func() bool {
		ifs.mu.Lock()
		defer ifs.mu.Unlock()
		return ifs.IsUpLocked()
	}

	if isUp() {
		t.Fatal("got initial link state up, want down")
	}

	// Bringing the link up should enable the NIC in stack.Stack.
	if err := ifs.Up(); err != nil {
		t.Fatal("ifs.Up(): ", err)
	}
	if enabled := ns.stack.CheckNIC(ifs.nicid); !enabled {
		t.Fatalf("got ns.stack.CheckNIC(%d) = false, want = true", ifs.nicid)
	}

	if !isUp() {
		t.Fatal("got post-up link state down, want up")
	}

	// Bringing the link down should disable the NIC in stack.Stack.
	if err := ifs.Down(); err != nil {
		t.Fatal("ifs.Down(): ", err)
	}
	if enabled := ns.stack.CheckNIC(ifs.nicid); enabled {
		t.Fatalf("got ns.stack.CheckNIC(%d) = true, want = false", ifs.nicid)
	}

	if isUp() {
		t.Fatal("got post-down link state up, want down")
	}
}

var _ nicRemovedHandler = (*testNicRemovedHandler)(nil)

type testNicRemovedHandler struct {
	removedNICID tcpip.NICID
}

func (h *testNicRemovedHandler) removedNIC(nicID tcpip.NICID) {
	h.removedNICID = nicID
}

// TestStackNICRemove tests that the NIC in stack.Stack is removed when the
// underlying link is closed.
func TestStackNICRemove(t *testing.T) {
	nicRemovedHandler := testNicRemovedHandler{}
	ns := newNetstackWithNicRemovedHandler(t, &nicRemovedHandler)
	var obs noopObserver

	ifs, err := ns.addEndpoint(
		func(tcpip.NICID) string { return t.Name() },
		&noopEndpoint{},
		&noopController{},
		&obs,
		true, /* doFilter */
		0,    /* metric */
	)
	if err != nil {
		t.Fatal(err)
	}

	// The NIC should initially be disabled in stack.Stack.
	if enabled := ns.stack.CheckNIC(ifs.nicid); enabled {
		t.Errorf("got ns.stack.CheckNIC(%d) = true, want = false", ifs.nicid)
	}
	if _, ok := ns.stack.NICInfo()[ifs.nicid]; !ok {
		t.Errorf("missing NICInfo for NIC %d", ifs.nicid)
	}
	if _, err := ns.stack.GetMainNICAddress(ifs.nicid, header.IPv6ProtocolNumber); err != nil {
		t.Errorf("GetMainNICAddress(%d, %d): %s", ifs.nicid, header.IPv6ProtocolNumber, err)
	}

	if t.Failed() {
		t.FailNow()
	}

	// Closing the link should remove the NIC from stack.Stack.
	obs.onLinkClosed()

	if enabled := ns.stack.CheckNIC(ifs.nicid); enabled {
		t.Errorf("got ns.stack.CheckNIC(%d) = false, want = true", ifs.nicid)
	}
	if nicInfo, ok := ns.stack.NICInfo()[ifs.nicid]; ok {
		t.Errorf("unexpected NICInfo found for NIC %d = %+v", ifs.nicid, nicInfo)
	}
	if addr, err := ns.stack.GetMainNICAddress(ifs.nicid, header.IPv6ProtocolNumber); err != tcpip.ErrUnknownNICID {
		t.Errorf("got GetMainNICAddress(%d, %d) = (%s, %v), want = (_, %s)", ifs.nicid, header.IPv6ProtocolNumber, addr, err, tcpip.ErrUnknownNICID)
	}
	if nicRemovedHandler.removedNICID != ifs.nicid {
		t.Errorf("got nicRemovedHandler.removedNICID = %d, want = %d", nicRemovedHandler.removedNICID, ifs.nicid)
	}

	// Wait for the controller to stop and free up its resources.
	ifs.endpoint.Wait()
}

func containsRoute(rs []tcpip.Route, r tcpip.Route) bool {
	for _, i := range rs {
		if i == r {
			return true
		}
	}

	return false
}

func TestEndpoint_Close(t *testing.T) {
	ns := newNetstack(t)
	wq := &waiter.Queue{}
	// Avoid polluting everything with err of type *tcpip.Error.
	ep := func() tcpip.Endpoint {
		ep, err := ns.stack.NewEndpoint(tcp.ProtocolNumber, ipv6.ProtocolNumber, wq)
		if err != nil {
			t.Fatalf("NewEndpoint() = %s", err)
		}
		return ep
	}()
	defer ep.Close()

	eps, err := newEndpointWithSocket(ep, wq, tcp.ProtocolNumber, ipv6.ProtocolNumber, ns)
	if err != nil {
		t.Fatal(err)
	}
	defer eps.close()

	// By-value copy since Close will mutate its receiver.
	key := zx.Handle(eps.local)

	channels := []struct {
		ch   <-chan struct{}
		name string
	}{
		{ch: eps.closing, name: "closing"},
		{ch: eps.loopReadDone, name: "loopReadDone"},
		{ch: eps.loopWriteDone, name: "loopWriteDone"},
	}

	// Check starting conditions.
	for _, ch := range channels {
		select {
		case <-ch.ch:
			t.Errorf("%s cleaned up prematurely", ch.name)
		default:
		}
	}

	if _, ok := eps.ns.endpoints.Load(key); !ok {
		var keys []zx.Handle
		eps.ns.endpoints.Range(func(key zx.Handle, value tcpip.Endpoint) bool {
			keys = append(keys, key)
			return true
		})
		t.Errorf("got endpoints map = %v at creation, want %d", keys, key)
	}

	if t.Failed() {
		t.FailNow()
	}

	// Create a referent.
	s, err := newStreamSocket(eps)
	if err != nil {
		t.Fatalf("newStreamSocket() = %s", err)
	}
	defer func() {
		func() {
			status, err := s.Close(context.Background())
			if err, ok := err.(*zx.Error); ok && err.Status == zx.ErrPeerClosed {
				return
			}
			t.Errorf("s.Close() = (%s, %v)", zx.Status(status), err)
		}()
		if err := s.Channel.Close(); err != nil {
			t.Errorf("s.Channel.Close() = %s", err)
		}
	}()

	// Create another referent.
	localC, peerC, err := zx.NewChannel(0)
	if err != nil {
		t.Fatalf("zx.NewChannel() = %s", err)
	}
	defer func() {
		// Already closed below.
		if err := localC.Close(); err != nil {
			t.Errorf("localC.Close() = %s", err)
		}

		// By-value copy already closed by the server when we closed the peer.
		err := peerC.Close()
		if err, ok := err.(*zx.Error); ok && err.Status == zx.ErrBadHandle {
			return
		}
		t.Errorf("peerC.Close() = %v", err)
	}()

	if err := s.Clone(context.Background(), 0, io.NodeWithCtxInterfaceRequest{Channel: peerC}); err != nil {
		t.Fatalf("s.Clone() = %s", err)
	}

	// Close the original referent.
	if status, err := s.Close(context.Background()); err != nil {
		t.Fatalf("s.Close() = %s", err)
	} else if status := zx.Status(status); status != zx.ErrOk {
		t.Fatalf("s.Close() = %s", status)
	}

	// There's still a referent.
	for _, ch := range channels {
		select {
		case <-ch.ch:
			t.Errorf("%s cleaned up prematurely", ch.name)
		default:
		}
	}

	if _, ok := eps.ns.endpoints.Load(key); !ok {
		var keys []zx.Handle
		eps.ns.endpoints.Range(func(key zx.Handle, value tcpip.Endpoint) bool {
			keys = append(keys, key)
			return true
		})
		t.Errorf("got endpoints map prematurely = %v, want %d", keys, key)
	}

	if t.Failed() {
		t.FailNow()
	}

	// Close the last reference.
	if err := localC.Close(); err != nil {
		t.Fatalf("localC.Close() = %s", err)
	}

	// Give a generous timeout for the closed channel to be detected.
	timeout := make(chan struct{})
	time.AfterFunc(5*time.Second, func() { close(timeout) })
	for _, ch := range channels {
		select {
		case <-ch.ch:
		case <-timeout:
			t.Errorf("%s not cleaned up", ch.name)
		}
	}

	for {
		select {
		case <-timeout:
			var keys []zx.Handle
			eps.ns.endpoints.Range(func(key zx.Handle, value tcpip.Endpoint) bool {
				keys = append(keys, key)
				return true
			})
			t.Errorf("got endpoints map = %v after closure, want *not* %d", keys, key)
		default:
			if _, ok := eps.ns.endpoints.Load(key); ok {
				continue
			}
		}
		break
	}

	if t.Failed() {
		t.FailNow()
	}
}

func TestNICName(t *testing.T) {
	ns := newNetstack(t)

	if want, got := "unknown NIC(id=0)", ns.name(0); got != want {
		t.Fatalf("got ns.name(0) = %q, want %q", got, want)
	}

	ifs, err := addNoopEndpoint(ns, t.Name())
	if err != nil {
		t.Fatal(err)
	}
	if got, want := ifs.ns.name(ifs.nicid), t.Name(); got != want {
		t.Fatalf("got ifs.mu.name = %q, want = %q", got, want)
	}
}

func TestNotStartedByDefault(t *testing.T) {
	ns := newNetstack(t)

	startCalled := false
	controller := noopController{
		onUp: func() { startCalled = true },
	}
	if _, err := ns.addEndpoint(
		func(tcpip.NICID) string { return t.Name() },
		&noopEndpoint{},
		&controller,
		nil,  /* observer */
		true, /* doFilter */
		0,    /* metric */
	); err != nil {
		t.Fatal(err)
	}

	if startCalled {
		t.Error("unexpected call to Controller.Up")
	}
}

type ndpDADEvent struct {
	nicID    tcpip.NICID
	addr     tcpip.Address
	resolved bool
	err      *tcpip.Error
}

// testNDPDispatcher is a tcpip.NDPDispatcher that sends an NDP DAD event on
// dadC when OnDuplicateAddressDetectionStatus gets called.
type testNDPDispatcher struct {
	dadC chan ndpDADEvent
}

// OnDuplicateAddressDetectionStatus implements
// stack.NDPDispatcher.OnDuplicateAddressDetectionStatus.
func (n *testNDPDispatcher) OnDuplicateAddressDetectionStatus(nicID tcpip.NICID, addr tcpip.Address, resolved bool, err *tcpip.Error) {
	if c := n.dadC; c != nil {
		c <- ndpDADEvent{
			nicID:    nicID,
			addr:     addr,
			resolved: resolved,
			err:      err,
		}
	}
}

// OnDefaultRouterDiscovered implements stack.NDPDispatcher.OnDefaultRouterDiscovered.
//
// Adds the event to the event queue and returns true so Stack remembers the
// discovered default router.
func (*testNDPDispatcher) OnDefaultRouterDiscovered(tcpip.NICID, tcpip.Address) bool {
	return false
}

// OnDefaultRouterInvalidated implements stack.NDPDispatcher.OnDefaultRouterInvalidated.
func (*testNDPDispatcher) OnDefaultRouterInvalidated(tcpip.NICID, tcpip.Address) {
}

// OnOnLinkPrefixDiscovered implements stack.NDPDispatcher.OnOnLinkPrefixDiscovered.
func (*testNDPDispatcher) OnOnLinkPrefixDiscovered(tcpip.NICID, tcpip.Subnet) bool {
	return false
}

// OnOnLinkPrefixInvalidated implements stack.NDPDispatcher.OnOnLinkPrefixInvalidated.
func (*testNDPDispatcher) OnOnLinkPrefixInvalidated(tcpip.NICID, tcpip.Subnet) {
}

// OnAutoGenAddress implements stack.NDPDispatcher.OnAutoGenAddress.
func (*testNDPDispatcher) OnAutoGenAddress(tcpip.NICID, tcpip.AddressWithPrefix) bool {
	return false
}

// OnAutoGenAddressDeprecated implements
// stack.NDPDispatcher.OnAutoGenAddressDeprecated.
func (*testNDPDispatcher) OnAutoGenAddressDeprecated(tcpip.NICID, tcpip.AddressWithPrefix) {
}

// OnAutoGenAddressInvalidated implements stack.NDPDispatcher.OnAutoGenAddressInvalidated.
func (*testNDPDispatcher) OnAutoGenAddressInvalidated(tcpip.NICID, tcpip.AddressWithPrefix) {
}

// OnRecursiveDNSServerOption implements stack.NDPDispatcher.OnRecursiveDNSServerOption.
func (*testNDPDispatcher) OnRecursiveDNSServerOption(tcpip.NICID, []tcpip.Address, time.Duration) {
}

// OnDNSSearchListOption implements stack.NDPDispatcher.OnDNSSearchListOption.
func (*testNDPDispatcher) OnDNSSearchListOption(tcpip.NICID, []string, time.Duration) {
}

// OnDHCPv6Configuration implements stack.NDPDispatcher.OnDHCPv6Configuration.
func (*testNDPDispatcher) OnDHCPv6Configuration(tcpip.NICID, ipv6.DHCPv6ConfigurationFromNDPRA) {
}

// Test that NICs get an IPv6 link-local address using the same algorithm that
// netsvc uses. It does not matter whether the address is generated
// automatically by the netstack or manually by the bindings (Netstack).
func TestIpv6LinkLocalAddr(t *testing.T) {
	t.Parallel()

	ndpDisp := testNDPDispatcher{
		dadC: make(chan ndpDADEvent, 1),
	}
	ns := newNetstackWithStackNDPDispatcher(t, &ndpDisp)

	ep := noopEndpoint{
		linkAddress: tcpip.LinkAddress([]byte{2, 3, 4, 5, 6, 7}),
	}
	ifs, err := ns.addEndpoint(
		func(tcpip.NICID) string { return t.Name() },
		&ep,
		&noopController{},
		nil,  /* observer */
		true, /* doFilter */
		0,    /* metric */
	)
	if err != nil {
		t.Fatal(err)
	}
	if err := ifs.Up(); err != nil {
		t.Fatal("ifs.Up(): ", err)
	}

	want := tcpip.ProtocolAddress{
		Protocol: header.IPv6ProtocolNumber,
		AddressWithPrefix: tcpip.AddressWithPrefix{
			Address: "\xfe\x80\x00\x00\x00\x00\x00\x00\x00\x03\x04\xff\xfe\x05\x06\x07",
		},
	}

	select {
	case d := <-ndpDisp.dadC:
		if diff := cmp.Diff(ndpDADEvent{nicID: ifs.nicid, addr: want.AddressWithPrefix.Address, resolved: true, err: nil}, d, cmp.AllowUnexported(d)); diff != "" {
			t.Fatalf("ndp DAD event mismatch (-want +got):\n%s", diff)
		}
	case <-time.After(dadResolutionTimeout):
		t.Fatal("timed out waiting for DAD event")
	}

	nicInfos := ns.stack.NICInfo()
	nicInfo, ok := nicInfos[ifs.nicid]
	if !ok {
		t.Fatalf("stack.NICInfo()[%d]: %s", ifs.nicid, tcpip.ErrUnknownNICID)
	}

	if _, found := findAddress(nicInfo.ProtocolAddresses, want); !found {
		t.Fatalf("got NIC addrs = %+v, want = %+v", nicInfo.ProtocolAddresses, want)
	}
}

func TestIpv6LinkLocalOnLinkRoute(t *testing.T) {
	if got, want := ipv6LinkLocalOnLinkRoute(6), (tcpip.Route{Destination: header.IPv6LinkLocalPrefix.Subnet(), NIC: 6}); got != want {
		t.Fatalf("got ipv6LinkLocalOnLinkRoute(6) = %s, want = %s", got, want)
	}
}

// Test that NICs get an on-link route to the IPv6 link-local subnet when it is
// brought up.
func TestIpv6LinkLocalOnLinkRouteOnUp(t *testing.T) {
	ns := newNetstack(t)

	ep := noopEndpoint{
		linkAddress: tcpip.LinkAddress([]byte{2, 3, 4, 5, 6, 7}),
	}
	ifs, err := ns.addEndpoint(
		func(tcpip.NICID) string { return t.Name() },
		&ep,
		&noopController{},
		nil,  /* observer */
		true, /* doFilter */
		0,    /* metric */
	)
	if err != nil {
		t.Fatal(err)
	}

	linkLocalRoute := ipv6LinkLocalOnLinkRoute(ifs.nicid)

	// Initially should not have the link-local route.
	rt := ns.stack.GetRouteTable()
	if containsRoute(rt, linkLocalRoute) {
		t.Fatalf("got GetRouteTable() = %+v, don't want = %s", rt, linkLocalRoute)
	}

	// Bringing the ethernet device up should result in the link-local
	// route being added.
	if err := ifs.Up(); err != nil {
		t.Fatalf("eth.Up(): %s", err)
	}
	rt = ns.stack.GetRouteTable()
	if !containsRoute(rt, linkLocalRoute) {
		t.Fatalf("got GetRouteTable() = %+v, want = %s", rt, linkLocalRoute)
	}

	// Bringing the ethernet device down should result in the link-local
	// route being removed.
	if err := ifs.Down(); err != nil {
		t.Fatalf("eth.Down(): %s", err)
	}
	rt = ns.stack.GetRouteTable()
	if containsRoute(rt, linkLocalRoute) {
		t.Fatalf("got GetRouteTable() = %+v, don't want = %s", rt, linkLocalRoute)
	}
}

func TestDefaultV6Route(t *testing.T) {
	if got, want := defaultV6Route(6, testLinkLocalV6Addr1), (tcpip.Route{Destination: header.IPv6EmptySubnet, Gateway: testLinkLocalV6Addr1, NIC: 6}); got != want {
		t.Fatalf("got defaultV6Route(6, %s) = %s, want = %s", testLinkLocalV6Addr1, got, want)
	}
}

func TestOnLinkV6Route(t *testing.T) {
	subAddr := util.Parse("abcd:1234::")
	subMask := tcpip.AddressMask(util.Parse("ffff:ffff::"))
	subnet, err := tcpip.NewSubnet(subAddr, subMask)
	if err != nil {
		t.Fatalf("NewSubnet(%s, %s): %s", subAddr, subMask, err)
	}

	if got, want := onLinkV6Route(6, subnet), (tcpip.Route{Destination: subnet, NIC: 6}); got != want {
		t.Fatalf("got onLinkV6Route(6, %s) = %s, want = %s", subnet, got, want)
	}
}

func TestMulticastPromiscuousModeEnabledByDefault(t *testing.T) {
	ns := newNetstack(t)

	multicastPromiscuousModeEnabled := false
	eth, _ := testutil.MakeEthernetDevice(t, ethernet.Info{}, 1)
	eth.ConfigMulticastSetPromiscuousModeImpl = func(enabled bool) (int32, error) {
		multicastPromiscuousModeEnabled = enabled
		return int32(zx.ErrOk), nil
	}

	if _, err := ns.addEth(testTopoPath, netstack.InterfaceConfig{Name: t.Name()}, &eth); err != nil {
		t.Fatal(err)
	}

	if !multicastPromiscuousModeEnabled {
		t.Error("expected a call to ConfigMulticastSetPromiscuousMode(true) by addEth")
	}
}

func TestUniqueFallbackNICNames(t *testing.T) {
	ns := newNetstack(t)

	ifs1, err := addNoopEndpoint(ns, "")
	if err != nil {
		t.Fatal(err)
	}
	ifs2, err := addNoopEndpoint(ns, "")
	if err != nil {
		t.Fatal(err)
	}

	nicInfos := ns.stack.NICInfo()

	nicInfo1, ok := nicInfos[ifs1.nicid]
	if !ok {
		t.Fatalf("stack.NICInfo()[%d]: %s", ifs1.nicid, tcpip.ErrUnknownNICID)
	}
	nicInfo2, ok := nicInfos[ifs2.nicid]
	if !ok {
		t.Fatalf("stack.NICInfo()[%d]: %s", ifs2.nicid, tcpip.ErrUnknownNICID)
	}

	if nicInfo1.Name == nicInfo2.Name {
		t.Fatalf("got (%+v).Name == (%+v).Name, want non-equal", nicInfo1, nicInfo2)
	}
}

func TestStaticIPConfiguration(t *testing.T) {
	ns := newNetstack(t)

	addr := fidlconv.ToNetIpAddress(testV4Address)
	ifAddr := fidlnet.Subnet{Addr: addr, PrefixLen: 32}
	for _, test := range []struct {
		name     string
		features ethernet.Features
	}{
		{name: "default"},
		{name: "wlan", features: ethernet.FeaturesWlan},
	} {
		t.Run(test.name, func(t *testing.T) {
			d, _ := testutil.MakeEthernetDevice(t, ethernet.Info{
				Features: test.features,
			}, 1)
			ifs, err := ns.addEth(testTopoPath, netstack.InterfaceConfig{Name: t.Name()}, &d)
			if err != nil {
				t.Fatal(err)
			}
			defer ifs.Remove()

			onlineChanged := make(chan bool, 1)
			ifs.observer.SetOnLinkOnlineChanged(func(linkOnline bool) {
				ifs.onLinkOnlineChanged(linkOnline)
				onlineChanged <- linkOnline
			})

			name := ifs.ns.name(ifs.nicid)
			result := ns.addInterfaceAddr(uint64(ifs.nicid), ifAddr)
			if result != stack.StackAddInterfaceAddressResultWithResponse(stack.StackAddInterfaceAddressResponse{}) {
				t.Fatalf("got ns.addInterfaceAddr(%d, %#v) = %#v, want = Response()", ifs.nicid, ifAddr, result)
			}

			if mainAddr, err := ns.stack.GetMainNICAddress(ifs.nicid, ipv4.ProtocolNumber); err != nil {
				t.Errorf("stack.GetMainNICAddress(%d, %d): %s", ifs.nicid, ipv4.ProtocolNumber, err)
			} else if got := mainAddr.Address; got != testV4Address {
				t.Errorf("got stack.GetMainNICAddress(%d, %d).Addr = %#v, want = %#v", ifs.nicid, ipv4.ProtocolNumber, got, testV4Address)
			}

			ifs.mu.Lock()
			if ifs.mu.dhcp.enabled {
				t.Error("expected dhcp state to be disabled initially")
			}
			ifs.mu.Unlock()

			if err := ifs.Down(); err != nil {
				t.Fatal(err)
			}

			ifs.mu.Lock()
			if ifs.mu.dhcp.enabled {
				t.Error("expected dhcp state to remain disabled after bringing interface down")
			}
			if ifs.mu.dhcp.running() {
				t.Error("expected dhcp state to remain stopped after bringing interface down")
			}
			ifs.mu.Unlock()

			if err := ifs.Up(); err != nil {
				t.Fatal(err)
			}
			if got, want := <-onlineChanged; got != want {
				t.Errorf("got state = %t, want %t", got, want)
			}

			ifs.mu.Lock()
			if ifs.mu.dhcp.enabled {
				t.Error("expected dhcp state to remain disabled after restarting interface")
			}
			ifs.mu.Unlock()

			ifs.setDHCPStatus(name, true)

			ifs.mu.Lock()
			if !ifs.mu.dhcp.enabled {
				t.Error("expected dhcp state to become enabled after manually enabling it")
			}
			if !ifs.mu.dhcp.running() {
				t.Error("expected dhcp state running")
			}
			ifs.mu.Unlock()
		})
	}
}

var _ nicRemovedHandler = (*noopNicRemovedHandler)(nil)

type noopNicRemovedHandler struct{}

func (*noopNicRemovedHandler) removedNIC(tcpip.NICID) {}

func newNetstack(t *testing.T) *Netstack {
	t.Helper()
	return newNetstackWithNDPDispatcher(t, nil)
}

func newNetstackWithNicRemovedHandler(t *testing.T, h nicRemovedHandler) *Netstack {
	t.Helper()
	return newNetstackWithStackNDPDispatcherAndNICRemovedHandler(t, nil, h)
}

func newNetstackWithNDPDispatcher(t *testing.T, ndpDisp *ndpDispatcher) *Netstack {
	t.Helper()

	// ndpDispatcher should never be called with a nil receiver.
	//
	// From https://golang.org/doc/faq#nil_error:
	//
	// Under the covers, interfaces are implemented as two elements, a type T and
	// a value V.
	//
	// An interface value is nil only if the V and T are both unset, (T=nil, V is
	// not set), In particular, a nil interface will always hold a nil type. If we
	// store a nil pointer of type *int inside an interface value, the inner type
	// will be *int regardless of the value of the pointer: (T=*int, V=nil). Such
	// an interface value will therefore be non-nil even when the pointer value V
	// inside is nil.
	if ndpDisp == nil {
		return newNetstackWithStackNDPDispatcher(t, nil)
	}

	ns := newNetstackWithStackNDPDispatcher(t, ndpDisp)
	ndpDisp.ns = ns
	return ns
}

func newNetstackWithStackNDPDispatcher(t *testing.T, ndpDisp ipv6.NDPDispatcher) *Netstack {
	t.Helper()
	return newNetstackWithStackNDPDispatcherAndNICRemovedHandler(t, ndpDisp, &noopNicRemovedHandler{})
}

func newNetstackWithStackNDPDispatcherAndNICRemovedHandler(t *testing.T, ndpDisp ipv6.NDPDispatcher, h nicRemovedHandler) *Netstack {
	t.Helper()

	// TODO(fxbug.dev/57075): Use a fake clock
	stk := tcpipstack.New(tcpipstack.Options{
		NetworkProtocols: []tcpipstack.NetworkProtocolFactory{
			arp.NewProtocol,
			ipv4.NewProtocol,
			ipv6.NewProtocolWithOptions(ipv6.Options{
				NDPDisp: ndpDisp,
			}),
		},
		TransportProtocols: []tcpipstack.TransportProtocolFactory{
			tcp.NewProtocol,
			udp.NewProtocol,
		},
	})
	ns := &Netstack{
		stack: stk,
		// Required initialization because adding/removing interfaces interacts with
		// DNS configuration.
		dnsConfig:         dns.MakeServersConfig(stk.Clock()),
		nicRemovedHandler: h,
	}
	ns.interfaceWatchers.mu.watchers = make(map[*interfaceWatcherImpl]struct{})
	ns.interfaceWatchers.mu.lastObserved = make(map[tcpip.NICID]interfaces.Properties)
	t.Cleanup(func() {
		for _, nic := range ns.stack.NICInfo() {
			ifs := nic.Context.(*ifState)
			ifs.Remove()
			ifs.endpoint.Wait()
		}
	})
	return ns
}

func getInterfaceAddresses(t *testing.T, ni *stackImpl, nicid tcpip.NICID) []tcpip.AddressWithPrefix {
	t.Helper()

	interfaces, err := ni.ListInterfaces(context.Background())
	if err != nil {
		t.Fatalf("ni.ListInterfaces() failed: %s", err)
	}

	info, found := stack.InterfaceInfo{}, false
	for _, i := range interfaces {
		if tcpip.NICID(i.Id) == nicid {
			info = i
			found = true
			break
		}
	}
	if !found {
		t.Fatalf("couldn't find NICID=%d in %+v", nicid, interfaces)
	}

	addrs := make([]tcpip.AddressWithPrefix, 0, len(info.Properties.Addresses))
	for _, a := range info.Properties.Addresses {
		addrs = append(addrs, tcpip.AddressWithPrefix{
			Address:   fidlconv.ToTCPIPAddress(a.Addr),
			PrefixLen: int(a.PrefixLen),
		})
	}
	return addrs
}

func compareInterfaceAddresses(t *testing.T, got, want []tcpip.AddressWithPrefix) {
	t.Helper()
	sort.Slice(got, func(i, j int) bool { return got[i].Address < got[j].Address })
	sort.Slice(want, func(i, j int) bool { return want[i].Address < want[j].Address })
	if diff := cmp.Diff(got, want); diff != "" {
		t.Errorf("Interface addresses mismatch (-want +got):\n%s", diff)
	}
}

func TestNetstackImpl_GetInterfaces2(t *testing.T) {
	ns := newNetstack(t)
	ni := &netstackImpl{ns: ns}

	if _, err := addNoopEndpoint(ns, t.Name()); err != nil {
		t.Fatal(err)
	}

	interfaces, err := ni.GetInterfaces2(context.Background())
	if err != nil {
		t.Fatal(err)
	}

	if l := len(interfaces); l == 0 {
		t.Fatalf("got len(GetInterfaces2()) = %d, want != %d", l, l)
	}

	var expectedAddr fidlnet.IpAddress
	expectedAddr.SetIpv4(fidlnet.Ipv4Address{})
	for _, iface := range interfaces {
		if iface.Addr != expectedAddr {
			t.Errorf("got interface %+v, want Addr = %+v", iface, expectedAddr)
		}
		if iface.Netmask != expectedAddr {
			t.Errorf("got interface %+v, want NetMask = %+v", iface, expectedAddr)
		}
	}
}

// Test adding a list of both IPV4 and IPV6 addresses and then removing them
// again one-by-one.
func TestListInterfaceAddresses(t *testing.T) {
	ndpDisp := testNDPDispatcher{
		dadC: make(chan ndpDADEvent, 1),
	}
	ns := newNetstackWithStackNDPDispatcher(t, &ndpDisp)
	ni := &stackImpl{ns: ns}

	ep := noopEndpoint{
		linkAddress: tcpip.LinkAddress([]byte{2, 3, 4, 5, 6, 7}),
	}
	ifState, err := ns.addEndpoint(
		func(tcpip.NICID) string { return t.Name() },
		&ep,
		&noopController{},
		nil,  /* observer */
		true, /* doFilter */
		0,    /* metric */
	)
	if err != nil {
		t.Fatal(err)
	}
	if err := ifState.Up(); err != nil {
		t.Fatal("ifState.Up(): ", err)
	}

	waitForDAD := func(addr tcpip.Address) {
		t.Helper()

		select {
		case d := <-ndpDisp.dadC:
			if diff := cmp.Diff(ndpDADEvent{nicID: ifState.nicid, addr: addr, resolved: true, err: nil}, d, cmp.AllowUnexported(d)); diff != "" {
				t.Fatalf("ndp DAD event mismatch (-want +got):\n%s", diff)
			}
		case <-time.After(dadResolutionTimeout):
			t.Fatal("timed out waiting for DAD event")
		}
	}

	waitForDAD("\xfe\x80\x00\x00\x00\x00\x00\x00\x00\x03\x04\xff\xfe\x05\x06\x07")

	// The call to ns.addEndpoint() added addresses to the stack. Make sure we include
	// those in our want list.
	wantAddrs := getInterfaceAddresses(t, ni, ifState.nicid)

	testAddresses := []tcpip.AddressWithPrefix{
		{"\x01\x01\x01\x01", 32},
		{"\x02\x02\x02\x02", 24},
		{"\x03\x03\x03\x03", 16},
		{"\x04\x04\x04\x04", 8},
		{"\x01\x01\x01\x01\x01\x01\x01\x01\x01\x01\x01\x01\x01\x01\x01\x01", 128},
		{"\x02\x02\x02\x02\x02\x02\x02\x02\x02\x02\x02\x02\x02\x02\x02\x02", 64},
		{"\x03\x03\x03\x03\x03\x03\x03\x03\x03\x03\x03\x03\x03\x03\x03\x03", 32},
		{"\x04\x04\x04\x04\x04\x04\x04\x04\x04\x04\x04\x04\x04\x04\x04\x04", 8},
	}

	t.Run("Add", func(t *testing.T) {
		for _, addr := range testAddresses {
			t.Run(addr.String(), func(t *testing.T) {
				ifAddr := fidlnet.Subnet{
					Addr:      fidlconv.ToNetIpAddress(addr.Address),
					PrefixLen: uint8(addr.PrefixLen),
				}

				result, err := ni.AddInterfaceAddress(context.Background(), uint64(ifState.nicid), ifAddr)
				AssertNoError(t, err)
				if result != stack.StackAddInterfaceAddressResultWithResponse(stack.StackAddInterfaceAddressResponse{}) {
					t.Fatalf("got ni.AddInterfaceAddress(%d, %#v) = %#v, want = Response()", ifState.nicid, ifAddr, result)
				}
				if addr := addr.Address; header.IsV6UnicastAddress(addr) {
					waitForDAD(addr)
				}
				wantAddrs = append(wantAddrs, addr)
				gotAddrs := getInterfaceAddresses(t, ni, ifState.nicid)

				compareInterfaceAddresses(t, gotAddrs, wantAddrs)
			})
		}
	})

	t.Run("Remove", func(t *testing.T) {
		for _, addr := range testAddresses {
			t.Run(addr.String(), func(t *testing.T) {
				ifAddr := fidlnet.Subnet{
					Addr:      fidlconv.ToNetIpAddress(addr.Address),
					PrefixLen: uint8(addr.PrefixLen),
				}

				result, err := ni.DelInterfaceAddress(context.Background(), uint64(ifState.nicid), ifAddr)
				AssertNoError(t, err)
				if result != stack.StackDelInterfaceAddressResultWithResponse(stack.StackDelInterfaceAddressResponse{}) {
					t.Fatalf("got ni.DelInterfaceAddress(%d, %#v) = %#v, want = Response()", ifState.nicid, ifAddr, result)
				}

				// Remove address from list.
				for i, a := range wantAddrs {
					if a == addr {
						wantAddrs = append(wantAddrs[:i], wantAddrs[i+1:]...)
						break
					}
				}
				gotAddrs := getInterfaceAddresses(t, ni, ifState.nicid)
				compareInterfaceAddresses(t, gotAddrs, wantAddrs)
			})
		}
	})
}

// Test that adding an address with one prefix and then adding the same address
// but with a different prefix will simply replace the first address.
func TestAddAddressesThenChangePrefix(t *testing.T) {
	ns := newNetstack(t)
	ni := &stackImpl{ns: ns}
	ifState, err := addNoopEndpoint(ns, t.Name())
	if err != nil {
		t.Fatal(err)
	}

	// The call to ns.addEndpoint() added addresses to the stack. Make sure we include
	// those in our want list.
	initialAddrs := getInterfaceAddresses(t, ni, ifState.nicid)

	// Add address.
	addr := tcpip.AddressWithPrefix{Address: "\x01\x01\x01\x01", PrefixLen: 8}
	ifAddr := fidlnet.Subnet{
		Addr:      fidlconv.ToNetIpAddress(addr.Address),
		PrefixLen: uint8(addr.PrefixLen),
	}

	result, err := ni.AddInterfaceAddress(context.Background(), uint64(ifState.nicid), ifAddr)
	AssertNoError(t, err)
	if result != stack.StackAddInterfaceAddressResultWithResponse(stack.StackAddInterfaceAddressResponse{}) {
		t.Fatalf("got ni.AddInterfaceAddress(%d, %#v) = %#v, want = Response()", ifState.nicid, ifAddr, result)
	}

	wantAddrs := append(initialAddrs, addr)
	gotAddrs := getInterfaceAddresses(t, ni, ifState.nicid)
	compareInterfaceAddresses(t, gotAddrs, wantAddrs)

	// Add the same address with a different prefix.
	addr.PrefixLen *= 2
	ifAddr.PrefixLen *= 2

	result, err = ni.AddInterfaceAddress(context.Background(), uint64(ifState.nicid), ifAddr)
	AssertNoError(t, err)
	if result != stack.StackAddInterfaceAddressResultWithResponse(stack.StackAddInterfaceAddressResponse{}) {
		t.Fatalf("got ni.AddInterfaceAddress(%d, %#v) = %#v, want = Response()", ifState.nicid, ifAddr, result)
	}

	wantAddrs = append(initialAddrs, addr)
	gotAddrs = getInterfaceAddresses(t, ni, ifState.nicid)

	compareInterfaceAddresses(t, gotAddrs, wantAddrs)
}

func TestAddRouteParameterValidation(t *testing.T) {
	ns := newNetstack(t)
	addr := tcpip.ProtocolAddress{
		Protocol: ipv4.ProtocolNumber,
		AddressWithPrefix: tcpip.AddressWithPrefix{
			Address:   tcpip.Address("\xf0\xf0\xf0\xf0"),
			PrefixLen: 24,
		},
	}
	subnetLocalAddress := tcpip.Address("\xf0\xf0\xf0\xf1")
	ifState, err := addNoopEndpoint(ns, t.Name())
	if err != nil {
		t.Fatal(err)
	}

	found, err := ns.addInterfaceAddress(ifState.nicid, addr)
	if err != nil {
		t.Fatalf("ns.addInterfaceAddress(%d, %s) = _, %s", ifState.nicid, addr.AddressWithPrefix, err)
	}
	if !found {
		t.Fatalf("ns.addInterfaceAddress(%d, %s) = %t, _", ifState.nicid, addr.AddressWithPrefix, found)
	}

	tests := []struct {
		name    string
		route   tcpip.Route
		metric  routes.Metric
		dynamic bool
		err     error
	}{
		{
			name: "IPv4 destination no NIC invalid gateway",
			route: tcpip.Route{
				Destination: util.PointSubnet(testV4Address),
				Gateway:     testV4Address,
				NIC:         0,
			},
			metric: routes.Metric(0),
			err:    routes.ErrNoSuchNIC,
		},
		{
			name: "IPv6 destination no NIC invalid gateway",
			route: tcpip.Route{
				Destination: util.PointSubnet(testV6Address),
				Gateway:     testV6Address,
				NIC:         0,
			},
			metric: routes.Metric(0),
			err:    routes.ErrNoSuchNIC,
		},
		{
			name: "IPv4 destination no NIC valid gateway",
			route: tcpip.Route{
				Destination: util.PointSubnet(testV4Address),
				Gateway:     subnetLocalAddress,
				NIC:         0,
			},
		},
		{
			name: "zero length gateway",
			route: tcpip.Route{
				Destination: util.PointSubnet(testV4Address),
				NIC:         ifState.nicid,
			},
		},
	}

	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			if err := ns.AddRoute(test.route, test.metric, test.dynamic); !errors.Is(err, test.err) {
				t.Errorf("got ns.AddRoute(...) = %v, want %v", err, test.err)
			}
		})
	}
}

func TestDHCPAcquired(t *testing.T) {
	ns := newNetstack(t)
	ifState, err := addNoopEndpoint(ns, t.Name())
	if err != nil {
		t.Fatal(err)
	}

	addressBytes := []byte(testV4Address)
	nextAddress := func() tcpip.Address {
		addressBytes[len(addressBytes)-1]++
		return tcpip.Address(addressBytes)
	}

	serverAddress := nextAddress()
	router1Address := nextAddress()
	router2Address := nextAddress()
	multicastAddress := net.IPv4(0xe8, 0x2b, 0xd3, 0xea)
	if !multicastAddress.IsMulticast() {
		t.Fatalf("%s is not a multicast address", multicastAddress)
	}

	defaultMask := net.IP(testV4Address).DefaultMask()
	prefixLen, _ := defaultMask.Size()

	destination1, err := tcpip.NewSubnet(util.Parse("192.168.42.0"), tcpip.AddressMask(util.Parse("255.255.255.0")))
	if err != nil {
		t.Fatal(err)
	}
	destination2, err := tcpip.NewSubnet(util.Parse("0.0.0.0"), tcpip.AddressMask(util.Parse("0.0.0.0")))
	if err != nil {
		t.Fatal(err)
	}

	tests := []struct {
		name               string
		oldAddr, newAddr   tcpip.AddressWithPrefix
		config             dhcp.Config
		expectedRouteTable []routes.ExtendedRoute
	}{
		{
			name:    "subnet mask provided",
			oldAddr: tcpip.AddressWithPrefix{},
			newAddr: tcpip.AddressWithPrefix{
				Address:   testV4Address,
				PrefixLen: prefixLen,
			},
			config: dhcp.Config{
				ServerAddress: serverAddress,
				Router: []tcpip.Address{
					router1Address,
					router2Address,
					header.IPv4Any,
					header.IPv4Broadcast,
					tcpip.Address(multicastAddress),
				},
				SubnetMask: tcpip.AddressMask(defaultMask),
				DNS: []tcpip.Address{
					router1Address,
					router2Address,
				},
				LeaseLength: dhcp.Seconds(60),
			},
			expectedRouteTable: []routes.ExtendedRoute{
				{
					Route: tcpip.Route{
						Destination: destination1,
						NIC:         1,
					},
					Metric:                0,
					MetricTracksInterface: true,
					Dynamic:               true,
					Enabled:               false,
				},
				{
					Route: tcpip.Route{
						Destination: destination2,
						Gateway:     util.Parse("192.168.42.18"),
						NIC:         1,
					},
					Metric:                0,
					MetricTracksInterface: true,
					Dynamic:               true,
					Enabled:               false,
				},
				{
					Route: tcpip.Route{
						Destination: destination2,
						Gateway:     util.Parse("192.168.42.19"),
						NIC:         1,
					},
					Metric:                0,
					MetricTracksInterface: true,
					Dynamic:               true,
					Enabled:               false,
				},
			},
		},
		{
			name:    "no routers",
			oldAddr: tcpip.AddressWithPrefix{},
			newAddr: tcpip.AddressWithPrefix{
				Address:   testV4Address,
				PrefixLen: prefixLen,
			},
			config: dhcp.Config{},
			expectedRouteTable: []routes.ExtendedRoute{
				{
					Route: tcpip.Route{
						Destination: destination1,
						NIC:         1,
					},
					Metric:                0,
					MetricTracksInterface: true,
					Dynamic:               true,
					Enabled:               false,
				},
			},
		},
	}

	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			// save current route table for later
			originalRouteTable := ifState.ns.GetExtendedRouteTable()

			// Update the DHCP address to the given test values and verify it took
			// effect.
			ifState.dhcpAcquired(test.oldAddr, test.newAddr, test.config)

			if diff := cmp.Diff(ifState.dns.mu.servers, test.config.DNS); diff != "" {
				t.Errorf("ifState.mu.dnsServers mismatch (-want +got):\n%s", diff)
			}

			if diff := cmp.Diff(ifState.ns.GetExtendedRouteTable(), test.expectedRouteTable, cmp.AllowUnexported(tcpip.Subnet{})); diff != "" {
				t.Errorf("GetExtendedRouteTable() mismatch (-want +got):\n%s", diff)
			}

			infoMap := ns.stack.NICInfo()
			if info, ok := infoMap[ifState.nicid]; ok {
				found := false
				for _, address := range info.ProtocolAddresses {
					if address.Protocol == ipv4.ProtocolNumber {
						switch address.AddressWithPrefix {
						case test.oldAddr:
							t.Errorf("expired address %s was not removed from NIC addresses %v", test.oldAddr, info.ProtocolAddresses)
						case test.newAddr:
							found = true
						}
					}
				}

				if !found {
					t.Errorf("new address %s was not added to NIC addresses %v", test.newAddr, info.ProtocolAddresses)
				}
			} else {
				t.Errorf("NIC %d not found in %v", ifState.nicid, infoMap)
			}

			// Remove the address and verify everything is cleaned up correctly.
			remAddr := test.newAddr
			ifState.dhcpAcquired(remAddr, tcpip.AddressWithPrefix{}, dhcp.Config{})

			if diff := cmp.Diff(ifState.dns.mu.servers, ifState.dns.mu.servers[:0]); diff != "" {
				t.Errorf("ifState.mu.dnsServers mismatch (-want +got):\n%s", diff)
			}

			if diff := cmp.Diff(ifState.ns.GetExtendedRouteTable(), originalRouteTable); diff != "" {
				t.Errorf("GetExtendedRouteTable() mismatch (-want +got):\n%s", diff)
			}

			infoMap = ns.stack.NICInfo()
			if info, ok := infoMap[ifState.nicid]; ok {
				for _, address := range info.ProtocolAddresses {
					if address.Protocol == ipv4.ProtocolNumber {
						if address.AddressWithPrefix == remAddr {
							t.Errorf("address %s/%d was not removed from NIC addresses %v", remAddr.Address, remAddr.PrefixLen, info.ProtocolAddresses)
						}
					}
				}
			} else {
				t.Errorf("NIC %d not found in %v", ifState.nicid, infoMap)
			}
		})
	}
}
