// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//go:build !build_with_native_toolchain

package netstack

import (
	"bytes"
	"context"
	"errors"
	"flag"
	"fmt"
	"math"
	"net"
	"runtime"
	"sort"
	"syscall/zx"
	"testing"
	"time"

	"fidl/fuchsia/hardware/ethernet"
	"fidl/fuchsia/logger"
	fidlnet "fidl/fuchsia/net"
	"fidl/fuchsia/net/stack"
	"fidl/fuchsia/netstack"
	"fidl/fuchsia/unknown"

	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/dhcp"
	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/dns"
	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/fidlconv"
	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/link/eth/testutil"
	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/routes"
	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/sync"
	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/util"
	"go.fuchsia.dev/fuchsia/src/lib/component"
	syslog "go.fuchsia.dev/fuchsia/src/lib/syslog/go"

	"github.com/google/go-cmp/cmp"
	"go.uber.org/goleak"
	"gvisor.dev/gvisor/pkg/tcpip"
	"gvisor.dev/gvisor/pkg/tcpip/faketime"
	"gvisor.dev/gvisor/pkg/tcpip/header"
	"gvisor.dev/gvisor/pkg/tcpip/link/sniffer"
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
		componentCtx := component.NewContextFromStartupInfo()
		req, logSink, err := logger.NewLogSinkWithCtxInterfaceRequest()
		if err != nil {
			panic(err)
		}
		componentCtx.ConnectToEnvService(req)
		options := syslog.LogInitOptions{
			LogLevel: math.MinInt8,
		}
		options.LogSink = logSink
		options.MinSeverityForFileAndLineInfo = math.MinInt8
		l, err := syslog.NewLogger(options)
		if err != nil {
			panic(fmt.Sprintf("syslog.NewLogger(%#v) = %s", options, err))
		}
		syslog.SetDefaultLogger(l)

		// As of this writing we set this value to 0 in netstack/main.go.
		sniffer.LogPackets.Store(1)
	}

	// Verifies that none of the tests in this package leak any goroutines.
	goleak.VerifyTestMain(
		m,
		append(goroutineTopFunctionsToIgnore(), goleak.IgnoreCurrent())...,
	)
}

func TestDelRouteErrors(t *testing.T) {
	addGoleakCheck(t)
	ns, _ := newNetstack(t, netstackTestOptions{})

	ifs := addNoopEndpoint(t, ns, "")
	t.Cleanup(ifs.RemoveByUser)

	rt := tcpip.Route{
		Destination: header.IPv4EmptySubnet,
		Gateway:     "\x01\x02\x03\x04",
		NIC:         ifs.nicid,
	}

	// Deleting a route we never added should return no routes deleted.
	if got := ns.DelRoute(rt); len(got) != 0 {
		t.Errorf("DelRoute(%s) = %#v not empty", rt, got)
	}

	if err := ns.AddRoute(rt, metricNotSet, false); err != nil {
		t.Fatalf("AddRoute(%s, metricNotSet, false): %s", rt, err)
	}
	// Deleting a route we added should not result in an error.
	want := routes.ExtendedRoute{
		Route:                 rt,
		Prf:                   routes.MediumPreference,
		Metric:                defaultInterfaceMetric,
		MetricTracksInterface: true,
	}
	if diff := cmp.Diff(ns.DelRoute(rt), []routes.ExtendedRoute{want}); diff != "" {
		t.Fatalf("DelRoute(%s): -got +want %s", rt, diff)
	}
	// Deleting a route we just deleted should result in no routes actually deleted.
	if got := ns.DelRoute(rt); len(got) != 0 {
		t.Errorf("DelRoute(%s) = %#v not empty", rt, got)
	}
}

func (ifs *ifState) IsUp() bool {
	ifs.mu.Lock()
	defer ifs.mu.Unlock()
	return ifs.IsUpLocked()
}

func installAndValidateIface(t *testing.T, ns *Netstack, addEndpoint func(*testing.T, *Netstack, string) *ifState) *ifState {
	ifs := addEndpoint(t, ns, "")
	t.Cleanup(ifs.RemoveByUser)

	// The NIC should initially be disabled in stack.Stack.
	if enabled := ns.stack.CheckNIC(ifs.nicid); enabled {
		t.Fatalf("got ns.stack.CheckNIC(%d) = true, want = false", ifs.nicid)
	}

	if ifs.IsUp() {
		t.Fatal("got initial link state up, want down")
	}

	// Bringing the link up should enable the NIC in stack.Stack.
	if err := ifs.Up(); err != nil {
		t.Fatal("ifs.Up(): ", err)
	}
	if enabled := ns.stack.CheckNIC(ifs.nicid); !enabled {
		t.Fatalf("got ns.stack.CheckNIC(%d) = false, want = true", ifs.nicid)
	}

	if !ifs.IsUp() {
		t.Fatal("got post-up link state down, want up")
	}
	return ifs
}

// TestStackNICEnableDisable tests that the NIC in stack.Stack is enabled or
// disabled when the underlying link is brought up or down, respectively.
func TestStackNICEnableDisable(t *testing.T) {
	addGoleakCheck(t)
	ns, _ := newNetstack(t, netstackTestOptions{})
	ifs := installAndValidateIface(t, ns, addNoopEndpoint)

	// Bringing the link down should disable the NIC in stack.Stack.
	if err := ifs.Down(); err != nil {
		t.Fatal("ifs.Down(): ", err)
	}
	if enabled := ns.stack.CheckNIC(ifs.nicid); enabled {
		t.Fatalf("got ns.stack.CheckNIC(%d) = true, want = false", ifs.nicid)
	}

	if ifs.IsUp() {
		t.Fatal("got post-down link state up, want down")
	}
}

var _ NICRemovedHandler = (*testNicRemovedHandler)(nil)

type testNicRemovedHandler struct {
	removedNICID tcpip.NICID
}

func (h *testNicRemovedHandler) RemovedNIC(nicID tcpip.NICID) {
	h.removedNICID = nicID
}

// TestStackNICRemove tests that the NIC in stack.Stack is removed when the
// underlying link is closed.
func TestStackNICRemove(t *testing.T) {
	addGoleakCheck(t)
	nicRemovedHandler := testNicRemovedHandler{}
	ns, _ := newNetstack(t, netstackTestOptions{nicRemovedHandler: &nicRemovedHandler})
	var obs noopObserver

	ifs, err := ns.addEndpoint(
		func(tcpip.NICID) string { return t.Name() },
		&noopEndpoint{},
		&noopController{},
		&obs,
		defaultInterfaceMetric, /* metric */
		qdiscConfig{},
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
		t.Errorf("GetMainNICAddress(%d, header.IPv6ProtocolNumber): %s", ifs.nicid, err)
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
	{
		addr, err := ns.stack.GetMainNICAddress(ifs.nicid, header.IPv6ProtocolNumber)
		if _, ok := err.(*tcpip.ErrUnknownNICID); !ok {
			t.Errorf("got GetMainNICAddress(%d, header.IPv6ProtocolNumber) = (%s, %T), want = (_, *tcpip.ErrUnknownNICID)", ifs.nicid, addr, err)
		}
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
	addGoleakCheck(t)
	ns, _ := newNetstack(t, netstackTestOptions{})
	var wq waiter.Queue
	// Avoid polluting everything with err of type tcpip.Error.
	ep := func() tcpip.Endpoint {
		ep, err := ns.stack.NewEndpoint(tcp.ProtocolNumber, ipv6.ProtocolNumber, &wq)
		if err != nil {
			t.Fatalf("NewEndpoint(tcp.ProtocolNumber, ipv6.ProtocolNumber, _) = %s", err)
		}
		return ep
	}()
	defer ep.Close()

	eps, err := newEndpointWithSocket(ep, &wq, tcp.ProtocolNumber, ipv6.ProtocolNumber, ns, zx.SocketStream)
	if err != nil {
		t.Fatal(err)
	}
	defer eps.close()

	eps.mu.Lock()
	channels := []struct {
		ch   <-chan struct{}
		name string
	}{
		{ch: eps.closing, name: "closing"},
		{ch: eps.mu.loopReadDone, name: "loopReadDone"},
		{ch: eps.mu.loopWriteDone, name: "loopWriteDone"},
	}
	eps.mu.Unlock()

	// Check starting conditions.
	for _, ch := range channels {
		select {
		case <-ch.ch:
			t.Errorf("%s cleaned up prematurely", ch.name)
		default:
		}
	}

	if _, ok := eps.ns.endpoints.Load(eps.endpoint.key); !ok {
		var keys []uint64
		eps.ns.endpoints.Range(func(key uint64, _ tcpip.Endpoint) bool {
			keys = append(keys, key)
			return true
		})
		t.Errorf("got endpoints map = %d at creation, want %d", keys, eps.endpoint.key)
	}

	if t.Failed() {
		t.FailNow()
	}

	// Create a referent.
	s, err := newStreamSocket(makeStreamSocketImpl(eps))
	if err != nil {
		t.Fatalf("newStreamSocket() = %s", err)
	}
	defer func() {
		func() {
			result, err := s.Close(context.Background())
			if err, ok := err.(*zx.Error); ok && err.Status == zx.ErrPeerClosed {
				return
			}
			t.Errorf("s.Close() = (%#v, %v)", result, err)
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

	if err := s.Clone2(context.Background(), unknown.CloneableWithCtxInterfaceRequest{Channel: peerC}); err != nil {
		t.Fatalf("s.Clone() = %s", err)
	}

	// Close the original referent.
	if result, err := s.Close(context.Background()); err != nil {
		t.Fatalf("s.Close() = %s", err)
	} else if result.Which() != unknown.CloseableCloseResultResponse {
		t.Fatalf("s.Close() = %s", zx.Status(result.Err))
	}

	// There's still a referent.
	for _, ch := range channels {
		select {
		case <-ch.ch:
			t.Errorf("%s cleaned up prematurely", ch.name)
		default:
		}
	}

	if _, ok := eps.ns.endpoints.Load(eps.endpoint.key); !ok {
		var keys []uint64
		eps.ns.endpoints.Range(func(key uint64, _ tcpip.Endpoint) bool {
			keys = append(keys, key)
			return true
		})
		t.Errorf("got endpoints map prematurely = %d, want %d", keys, eps.endpoint.key)
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
		if ch.ch != nil {
			select {
			case <-ch.ch:
			case <-timeout:
				t.Errorf("%s not cleaned up", ch.name)
			}
		}
	}

	for {
		if _, ok := eps.ns.endpoints.Load(eps.endpoint.key); ok {
			select {
			case <-timeout:
				var keys []uint64
				eps.ns.endpoints.Range(func(key uint64, _ tcpip.Endpoint) bool {
					keys = append(keys, key)
					return true
				})
				t.Errorf("got endpoints map = %d after closure, want *not* %d", keys, eps.endpoint.key)
			default:
				continue
			}
		}
		break
	}

	if t.Failed() {
		t.FailNow()
	}
}

// TestTCPEndpointMapAcceptAfterReset tests that an already-reset endpoint
// isn't added to the endpoints map, since such an endpoint wouldn't receive a
// hangup notification and its reference in the map would leak.
func TestTCPEndpointMapAcceptAfterReset(t *testing.T) {
	addGoleakCheck(t)
	ns, _ := newNetstack(t, netstackTestOptions{})
	if err := ns.addLoopback(); err != nil {
		t.Fatalf("ns.addLoopback() = %s", err)
	}

	listener := makeStreamSocketImpl(createEP(t, ns, new(waiter.Queue)))

	if err := listener.ep.Bind(tcpip.FullAddress{}); err != nil {
		t.Fatalf("Bind({}) = %s", err)
	}
	if err := listener.ep.Listen(1); err != nil {
		t.Fatalf("Listen(1) = %s", err)
	}

	client := createEP(t, ns, new(waiter.Queue))

	// Connect and wait for the incoming connection.
	func() {
		connectAddr, err := listener.ep.GetLocalAddress()
		if err != nil {
			t.Fatalf("GetLocalAddress() = %s", err)
		}

		waitEntry, notifyCh := waiter.NewChannelEntry(waiter.EventIn)
		listener.wq.EventRegister(&waitEntry)
		defer listener.wq.EventUnregister(&waitEntry)

		switch err := client.ep.Connect(connectAddr); err.(type) {
		case *tcpip.ErrConnectStarted:
		default:
			t.Fatalf("Connect(%#v) = %s", connectAddr, err)
		}
		<-notifyCh
	}()

	// Initiate a RST of the connection sitting in the accept queue.
	client.ep.SocketOptions().SetLinger(tcpip.LingerOption{
		Enabled: true,
		Timeout: 0,
	})
	client.ep.Close()

	// Wait for the RST to be processed by the stack.
	time.Sleep(100 * time.Millisecond)

	_, _, s, err := listener.accept(false)
	if err != nil {
		t.Fatalf("ep.accept(false) = %s", err)
	}
	defer s.endpointWithSocket.close()

	// Expect the `Accept` to have removed the endpoint from the map.
	if _, ok := ns.endpoints.Load(s.endpoint.key); ok {
		t.Fatalf("got endpoints.Load(%d) = (_, true)", s.endpoint.key)
	}

	s.mu.Lock()
	channels := []struct {
		ch   <-chan struct{}
		name string
	}{
		{ch: s.closing, name: "closing"},
		{ch: s.mu.loopReadDone, name: "loopReadDone"},
		{ch: s.mu.loopWriteDone, name: "loopWriteDone"},
	}
	s.mu.Unlock()

	// Give a generous timeout for the closed channel to be detected.
	timeout := make(chan struct{})
	time.AfterFunc(5*time.Second, func() { close(timeout) })
	for _, ch := range channels {
		if ch.ch != nil {
			select {
			case <-ch.ch:
			case <-timeout:
				t.Errorf("%s not cleaned up", ch.name)
			}
		}
	}
}

func createEP(t *testing.T, ns *Netstack, wq *waiter.Queue) *endpointWithSocket {
	// Avoid polluting the scope with err of type tcpip.Error.
	ep := func() tcpip.Endpoint {
		ep, err := ns.stack.NewEndpoint(tcp.ProtocolNumber, ipv4.ProtocolNumber, wq)
		if err != nil {
			t.Fatalf("NewEndpoint(tcp.ProtocolNumber, ipv4.ProtocolNumber, _) = %s", err)
		}
		return ep
	}()
	t.Cleanup(ep.Close)
	eps, err := newEndpointWithSocket(ep, wq, tcp.ProtocolNumber, ipv4.ProtocolNumber, ns, zx.SocketStream)
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(eps.close)
	return eps
}

func TestTCPEndpointMapClose(t *testing.T) {
	addGoleakCheck(t)
	ns, _ := newNetstack(t, netstackTestOptions{})
	eps := createEP(t, ns, new(waiter.Queue))

	// Closing the endpoint should remove it from the endpoints map.
	if _, ok := ns.endpoints.Load(eps.endpoint.key); !ok {
		t.Fatalf("got endpoints.Load(%d) = (_, false)", eps.endpoint.key)
	}
	eps.close()
	if _, ok := ns.endpoints.Load(eps.endpoint.key); ok {
		t.Fatalf("got endpoints.Load(%d) = (_, true)", eps.endpoint.key)
	}
}

func TestTCPEndpointMapConnect(t *testing.T) {
	addGoleakCheck(t)
	ns, clock := newNetstack(t, netstackTestOptions{})

	var linkEP tcpipstack.LinkEndpoint = &noopEndpoint{
		capabilities: tcpipstack.CapabilityResolutionRequired,
	}
	if testing.Verbose() {
		linkEP = sniffer.New(linkEP)
	}
	ifs, err := ns.addEndpoint(
		func(tcpip.NICID) string { return t.Name() },
		linkEP,
		&noopController{},
		nil,
		defaultInterfaceMetric, /* metric */
		qdiscConfig{},
	)
	if err != nil {
		t.Fatal(err)
	}
	if err := ns.stack.EnableNIC(ifs.nicid); err != nil {
		t.Fatal(err)
	}

	address := tcpip.Address([]byte{1, 2, 3, 4})
	destination := tcpip.FullAddress{
		Addr: address,
		Port: 1,
	}
	source := tcpip.Address([]byte{5, 6, 7, 8})
	protocolAddress := tcpip.ProtocolAddress{
		Protocol:          ipv4.ProtocolNumber,
		AddressWithPrefix: source.WithPrefix(),
	}
	if err := ns.stack.AddProtocolAddress(ifs.nicid, protocolAddress, tcpipstack.AddressProperties{}); err != nil {
		t.Fatalf("AddProtocolAddress(%d, %#v, {}) = %s", ifs.nicid, protocolAddress, err)
	}

	ns.stack.SetRouteTable([]tcpip.Route{
		{
			Destination: address.WithPrefix().Subnet(),
			NIC:         ifs.nicid,
		},
	})

	var wq waiter.Queue
	eps := createEP(t, ns, &wq)

	// We trigger link resolution with a TCP segment being sent, and then send
	// another segment after 1 second, and another after 2 seconds. This means we
	// could send up to 3 TCP segments, meaning the waiter can get notified up to
	// 3 times when link resolution fails. We only consume 1 waiter event in this
	// test, so we need to leave room in the buffer for two events in order for
	// the test not to deadlock.
	events := make(chan waiter.EventMask, 2)
	waitEntry := waiter.NewFunctionEntry(math.MaxUint64, func(m waiter.EventMask) {
		events <- m
	})
	wq.EventRegister(&waitEntry)
	defer wq.EventUnregister(&waitEntry)

	switch err := eps.ep.Connect(destination); err.(type) {
	case *tcpip.ErrConnectStarted:
	default:
		t.Fatalf("got Connect(%#v) = %v, want %s", destination, err, &tcpip.ErrConnectStarted{})
	}

	{
		nudConfig, err := ns.stack.NUDConfigurations(ifs.nicid, ipv4.ProtocolNumber)
		if err != nil {
			t.Fatalf("stack.NUDConfigurations(): %s", err)
		}
		clock.Advance(time.Duration(nudConfig.MaxMulticastProbes) * nudConfig.RetransmitTimer)
	}

	if got, want := <-events, waiter.ReadableEvents|waiter.WritableEvents|waiter.EventErr|waiter.EventHUp; got != want {
		t.Fatalf("got event = %b, want %b", got, want)
	}

	// The callback on HUp should have removed the endpoint from the map.
	if _, ok := ns.endpoints.Load(eps.endpoint.key); ok {
		t.Fatalf("got endpoints.Load(%d) = (_, true)", eps.endpoint.key)
	}
}

// TestTCPEndpointMapClosing validates the endpoint in a closing state like
// FIN_WAIT2 to be present in the endpoints map and is deleted when the
// endpoint transitions to CLOSED state.
func TestTCPEndpointMapClosing(t *testing.T) {
	addGoleakCheck(t)
	ns, _ := newNetstack(t, netstackTestOptions{})
	if err := ns.addLoopback(); err != nil {
		t.Fatalf("ns.addLoopback() = %s", err)
	}
	listener := createEP(t, ns, new(waiter.Queue))

	if err := listener.ep.Bind(tcpip.FullAddress{}); err != nil {
		t.Fatalf("ep.Bind({}) = %s", err)
	}
	if err := listener.ep.Listen(1); err != nil {
		t.Fatalf("ep.Listen(1) = %s", err)
	}
	connectAddr, err := listener.ep.GetLocalAddress()
	if err != nil {
		t.Fatalf("ep.GetLocalAddress() = %s", err)
	}
	client := createEP(t, ns, new(waiter.Queue))

	waitEntry, inCh := waiter.NewChannelEntry(waiter.EventIn)
	listener.wq.EventRegister(&waitEntry)
	defer listener.wq.EventUnregister(&waitEntry)

	switch err := client.ep.Connect(connectAddr); err.(type) {
	case *tcpip.ErrConnectStarted:
	default:
		t.Fatalf("ep.Connect(%#v) = %s", connectAddr, err)
	}
	// Wait for the newly established connection to show up as acceptable by
	// the peer.
	<-inCh

	server, _, err := listener.ep.Accept(nil)
	if err != nil {
		t.Fatalf("ep.Accept(nil) = %s", err)
	}

	// Ensure that the client endpoint is present in our internal map.
	if _, ok := ns.endpoints.Load(client.endpoint.key); !ok {
		t.Fatalf("got endpoints.Load(%d) = (_,false)", client.endpoint.key)
	}

	// Trigger an active close from the client.
	client.close()

	// The client endpoint should not be removed from endpoints map even after
	// an endpoint close.
	if _, ok := ns.endpoints.Load(client.endpoint.key); !ok {
		t.Fatalf("got endpoints.Load(%d) = (_,false)", client.endpoint.key)
	}

	ticker := time.NewTicker(10 * time.Millisecond)
	defer ticker.Stop()
	// Wait and check for the client active close to reach FIN_WAIT2 state.
	for {
		if tcp.EndpointState(client.ep.State()) == tcp.StateFinWait2 {
			break
		}
		<-ticker.C
	}

	// Lookup for the client once more in the endpoints map, it should still not
	// be removed.
	if _, ok := ns.endpoints.Load(client.endpoint.key); !ok {
		t.Fatalf("got endpoints.Load(%d) = (_,false)", client.endpoint.key)
	}

	timeWaitOpt := tcpip.TCPTimeWaitTimeoutOption(time.Duration(0))
	if err := ns.stack.SetTransportProtocolOption(tcp.ProtocolNumber, &timeWaitOpt); err != nil {
		t.Fatalf("SetTransportProtocolOption(%d, &%T(%d)) = %s", tcp.ProtocolNumber, timeWaitOpt, timeWaitOpt, err)
	}

	// Trigger server close, so that client enters TIME_WAIT.
	server.Close()

	// gVisor stack notifies EventHUp on entering TIME_WAIT. Wait for some time
	// for the EventHUp to be processed by netstack.
	for {
		// The client endpoint would be removed from the endpoints map as a result
		// of processing EventHUp.
		if _, ok := ns.endpoints.Load(client.endpoint.key); !ok {
			break
		}
		<-ticker.C
	}
}

func TestEndpointsMapKey(t *testing.T) {
	addGoleakCheck(t)
	ns, _ := newNetstack(t, netstackTestOptions{})
	if ns.endpoints.nextKey != 0 {
		t.Fatalf("got ns.endpoints.nextKey = %d, want 0", ns.endpoints.nextKey)
	}

	tcpipEP := func() (*waiter.Queue, tcpip.Endpoint) {
		var wq waiter.Queue
		ep, err := ns.stack.NewEndpoint(tcp.ProtocolNumber, ipv6.ProtocolNumber, &wq)
		if err != nil {
			t.Fatalf("NewEndpoint(tcp.ProtocolNumber, ipv6.ProtocolNumber, _) = %s", err)
		}
		t.Cleanup(ep.Close)
		return &wq, ep
	}
	// Test if we always skip key value 0 while adding to the map.
	for _, key := range []uint64{0, math.MaxUint64} {
		wq, ep := tcpipEP()

		// Set a test value to nextKey which is used to compute the endpoint key.
		ns.endpoints.nextKey = key
		eps, err := newEndpointWithSocket(ep, wq, tcp.ProtocolNumber, ipv6.ProtocolNumber, ns, zx.SocketStream)
		if err != nil {
			t.Fatal(err)
		}
		t.Cleanup(eps.close)
		if ns.endpoints.nextKey != 1 {
			t.Fatalf("got ns.endpoints.nextKey = %d, want 1", ns.endpoints.nextKey)
		}
		if eps.endpoint.key != 1 {
			t.Fatalf("got eps.endpoint.key = %d, want 1", eps.endpoint.key)
		}
		if _, ok := ns.endpoints.Load(eps.endpoint.key); !ok {
			t.Fatalf("got endpoints.Load(%d) = (_,false)", eps.endpoint.key)
		}
		// Closing the endpoint should remove the endpoint with key value 1
		// from the endpoints map. This lets the subsequent iteration to reuse
		// key value 1 to add a new endpoint to the map.
		eps.close()
	}

	// Key value 0 is not expected to be removed from the map.
	_, ep := tcpipEP()
	ns.endpoints.Store(0, ep)
	if ns.onRemoveEndpoint(0) {
		t.Errorf("got ns.onRemoveEndpoint(0) = true, want false")
	}
	if _, ok := ns.endpoints.Load(0); !ok {
		t.Fatal("got endpoints.Load(0) = (_,false)")
	}
}

func TestNICName(t *testing.T) {
	addGoleakCheck(t)
	ns, _ := newNetstack(t, netstackTestOptions{})

	if want, got := "unknown(NICID=0)", ns.name(0); got != want {
		t.Fatalf("got ns.name(0) = %q, want %q", got, want)
	}

	{
		ifs := addNoopEndpoint(t, ns, "")
		t.Cleanup(ifs.RemoveByUser)
		if got, want := ifs.ns.name(ifs.nicid), t.Name()+"1"; got != want {
			t.Fatalf("got ifs.mu.name = %q, want = %q", got, want)
		}
	}

	{
		const name = "VerySpecialName"
		ifs := addNoopEndpoint(t, ns, name)
		t.Cleanup(ifs.RemoveByUser)
		if got, want := ifs.ns.name(ifs.nicid), name; got != want {
			t.Fatalf("got ifs.mu.name = %q, want = %q", got, want)
		}
	}
}

func TestNotStartedByDefault(t *testing.T) {
	addGoleakCheck(t)
	ns, _ := newNetstack(t, netstackTestOptions{})

	startCalled := false
	controller := noopController{
		onUp: func() { startCalled = true },
	}
	if _, err := ns.addEndpoint(
		func(tcpip.NICID) string { return t.Name() },
		&noopEndpoint{},
		&controller,
		nil,                    /* observer */
		defaultInterfaceMetric, /* metric */
		qdiscConfig{},
	); err != nil {
		t.Fatal(err)
	}

	if startCalled {
		t.Error("unexpected call to Controller.Up")
	}
}

type ndpDADEvent struct {
	nicID  tcpip.NICID
	addr   tcpip.Address
	result tcpipstack.DADResult
}

var _ ipv6.NDPDispatcher = (*testNDPDispatcher)(nil)

// testNDPDispatcher is a tcpip.NDPDispatcher that sends an NDP DAD event on
// dadC when OnDuplicateAddressDetectionResult gets called.
type testNDPDispatcher struct {
	dadC chan ndpDADEvent
}

// OnDuplicateAddressDetectionResult implements ipv6.NDPDispatcher.
func (n *testNDPDispatcher) OnDuplicateAddressDetectionResult(nicID tcpip.NICID, addr tcpip.Address, result tcpipstack.DADResult) {
	if c := n.dadC; c != nil {
		c <- ndpDADEvent{
			nicID:  nicID,
			addr:   addr,
			result: result,
		}
	}
}

// OnDefaultRouterDiscovered implements ipv6.NDPDispatcher.
func (*testNDPDispatcher) OnOffLinkRouteUpdated(tcpip.NICID, tcpip.Subnet, tcpip.Address, header.NDPRoutePreference) {
}

// OnDefaultRouterInvalidated implements ipv6.NDPDispatcher.
func (*testNDPDispatcher) OnOffLinkRouteInvalidated(tcpip.NICID, tcpip.Subnet, tcpip.Address) {
}

// OnOnLinkPrefixDiscovered implements ipv6.NDPDispatcher.
func (*testNDPDispatcher) OnOnLinkPrefixDiscovered(tcpip.NICID, tcpip.Subnet) {
}

// OnOnLinkPrefixInvalidated implements ipv6.NDPDispatcher.
func (*testNDPDispatcher) OnOnLinkPrefixInvalidated(tcpip.NICID, tcpip.Subnet) {
}

// OnAutoGenAddress implements ipv6.NDPDispatcher.
func (*testNDPDispatcher) OnAutoGenAddress(tcpip.NICID, tcpip.AddressWithPrefix) tcpipstack.AddressDispatcher {
	return nil
}

// OnAutoGenAddressDeprecated implements ipv6.NDPDispatcher.
func (*testNDPDispatcher) OnAutoGenAddressDeprecated(tcpip.NICID, tcpip.AddressWithPrefix) {
}

// OnAutoGenAddressInvalidated implements ipv6.NDPDispatcher.
func (*testNDPDispatcher) OnAutoGenAddressInvalidated(tcpip.NICID, tcpip.AddressWithPrefix) {
}

// OnRecursiveDNSServerOption implements ipv6.NDPDispatcher.
func (*testNDPDispatcher) OnRecursiveDNSServerOption(tcpip.NICID, []tcpip.Address, time.Duration) {
}

// OnDNSSearchListOption implements ipv6.NDPDispatcher.
func (*testNDPDispatcher) OnDNSSearchListOption(tcpip.NICID, []string, time.Duration) {
}

// OnDHCPv6Configuration implements ipv6.NDPDispatcher.
func (*testNDPDispatcher) OnDHCPv6Configuration(tcpip.NICID, ipv6.DHCPv6ConfigurationFromNDPRA) {
}

func TestIpv6LinkLocalOnLinkRoute(t *testing.T) {
	addGoleakCheck(t)
	if got, want := ipv6LinkLocalOnLinkRoute(6), (tcpip.Route{Destination: header.IPv6LinkLocalPrefix.Subnet(), NIC: 6}); got != want {
		t.Fatalf("got ipv6LinkLocalOnLinkRoute(6) = %s, want = %s", got, want)
	}
}

// Test that NICs get an on-link route to the IPv6 link-local subnet when it is
// brought up.
func TestIpv6LinkLocalOnLinkRouteOnUp(t *testing.T) {
	addGoleakCheck(t)
	ns, _ := newNetstack(t, netstackTestOptions{})

	ep := noopEndpoint{
		linkAddress: tcpip.LinkAddress([]byte{2, 3, 4, 5, 6, 7}),
	}
	ifs, err := ns.addEndpoint(
		func(tcpip.NICID) string { return t.Name() },
		&ep,
		&noopController{},
		nil,                    /* observer */
		defaultInterfaceMetric, /* metric */
		qdiscConfig{},
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

func TestOnLinkV6Route(t *testing.T) {
	addGoleakCheck(t)
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
	addGoleakCheck(t)
	ns, _ := newNetstack(t, netstackTestOptions{})

	multicastPromiscuousModeEnabled := false
	eth, _ := testutil.MakeEthernetDevice(t, ethernet.Info{}, 1)
	eth.ConfigMulticastSetPromiscuousModeImpl = func(enabled bool) (int32, error) {
		multicastPromiscuousModeEnabled = enabled
		return int32(zx.ErrOk), nil
	}

	ifs, err := ns.addEth(testTopoPath, netstack.InterfaceConfig{Name: t.Name()}, &eth)
	if err != nil {
		t.Fatal(err)
	}
	defer ifs.RemoveByUser()

	if !multicastPromiscuousModeEnabled {
		t.Error("expected a call to ConfigMulticastSetPromiscuousMode(true) by addEth")
	}
}

func TestUniqueFallbackNICNames(t *testing.T) {
	addGoleakCheck(t)
	ns, _ := newNetstack(t, netstackTestOptions{})

	ifs1 := addNoopEndpoint(t, ns, "")
	t.Cleanup(ifs1.RemoveByUser)
	ifs2 := addNoopEndpoint(t, ns, "")
	t.Cleanup(ifs2.RemoveByUser)

	nicInfos := ns.stack.NICInfo()

	nicInfo1, ok := nicInfos[ifs1.nicid]
	if !ok {
		t.Fatalf("stack.NICInfo()[%d] = (_, false)", ifs1.nicid)
	}
	nicInfo2, ok := nicInfos[ifs2.nicid]
	if !ok {
		t.Fatalf("stack.NICInfo()[%d]: (_, false)", ifs2.nicid)
	}

	if nicInfo1.Name == nicInfo2.Name {
		t.Fatalf("got (%+v).Name == (%+v).Name, want non-equal", nicInfo1, nicInfo2)
	}
}

func TestStaticIPConfiguration(t *testing.T) {
	addGoleakCheck(t)
	ns, _ := newNetstack(t, netstackTestOptions{})

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
				Mtu:      1400,
			}, 1)
			ifs, err := ns.addEth(testTopoPath, netstack.InterfaceConfig{Name: t.Name()}, &d)
			if err != nil {
				t.Fatal(err)
			}
			defer ifs.RemoveByUser()

			onlineChanged := make(chan bool, 1)
			ifs.observer.SetOnLinkOnlineChanged(func(linkOnline bool) {
				ifs.onLinkOnlineChanged(linkOnline)
				onlineChanged <- linkOnline
			})

			name := ifs.ns.name(ifs.nicid)
			result := ns.addInterfaceAddr(uint64(ifs.nicid), ifAddr)
			if result != stack.StackAddInterfaceAddressDeprecatedResultWithResponse(stack.StackAddInterfaceAddressDeprecatedResponse{}) {
				t.Fatalf("got ns.addInterfaceAddr(%d, %#v) = %#v, want = Response()", ifs.nicid, ifAddr, result)
			}

			if mainAddr, err := ns.stack.GetMainNICAddress(ifs.nicid, ipv4.ProtocolNumber); err != nil {
				t.Errorf("stack.GetMainNICAddress(%d, ipv4.ProtocolNumber): %s", ifs.nicid, err)
			} else if got := mainAddr.Address; got != testV4Address {
				t.Errorf("got stack.GetMainNICAddress(%d, ipv4.ProtocolNumber).Addr = %s, want = %s", ifs.nicid, got, testV4Address)
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

var _ NICRemovedHandler = (*noopNicRemovedHandler)(nil)

type noopNicRemovedHandler struct{}

func (*noopNicRemovedHandler) RemovedNIC(tcpip.NICID) {}

type netstackTestOptions struct {
	nicRemovedHandler NICRemovedHandler
	// TODO(https://fxbug.dev/104820): Tests which pass a NDPDispatcher impl
	// and runs a goroutine should have the goroutine joined in addition to
	// being cancelled.
	ndpDisp            ipv6.NDPDispatcher
	interfaceEventChan chan<- interfaceEvent
}

func newNetstack(t *testing.T, options netstackTestOptions) (*Netstack, *faketime.ManualClock) {
	t.Helper()

	clock := faketime.NewManualClock()

	stk := tcpipstack.New(tcpipstack.Options{
		NetworkProtocols: []tcpipstack.NetworkProtocolFactory{
			arp.NewProtocol,
			ipv4.NewProtocol,
			ipv6.NewProtocolWithOptions(ipv6.Options{
				NDPDisp: options.ndpDisp,
			}),
		},
		TransportProtocols: []tcpipstack.TransportProtocolFactory{
			tcp.NewProtocol,
			udp.NewProtocol,
		},
		Clock: clock,
	})
	t.Cleanup(func() {
		stk.Close()
		stk.Wait()
	})

	ns := &Netstack{
		stack: stk,
		// Required initialization because adding/removing interfaces interacts with
		// DNS configuration.
		dnsConfig: dns.MakeServersConfig(stk.Clock()),
		nicRemovedHandlers: []NICRemovedHandler{func() NICRemovedHandler {
			if h := options.nicRemovedHandler; h != nil {
				return h
			}
			return &noopNicRemovedHandler{}

		}()},
		interfaceEventChan: options.interfaceEventChan,
	}
	if ndpDisp, ok := options.ndpDisp.(*ndpDispatcher); ok {
		ndpDisp.ns = ns
		ndpDisp.dynamicAddressSourceTracker.init(ns)
	}

	return ns, clock
}

func goroutineTopFunctionsToIgnore() []goleak.Option {
	return []goleak.Option{
		// Only one interfaceWatcherEventLoop is ever started per netstack,
		// so it's not worth adding go test-level teardown just for this.
		goleak.IgnoreTopFunction(
			"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack.interfaceWatcherEventLoop",
		),
		// The only usage of this is when the system-level sysWaiter is initialized,
		// but we have to ignore it since this initialization can race with IgnoreCurrent().
		goleak.IgnoreTopFunction(
			"syscall/zx.(*Port).Wait",
		),
	}
}

// addGoleakCheck adds an assertion that no goroutines are leaked by the current
// test. Each test should begin with addGoleakCheck(t). While the invocation of
// goleak.VerifyTestMain in TestMain will ensure that no goroutines are leaked
// even if addGoleakCheck(t) is omitted from a test, the inclusion of
// addGoleakCheck in each test produces more useful failure messages for
// narrowing down which test is leaking a goroutine.
func addGoleakCheck(t *testing.T) {
	opts := append(goroutineTopFunctionsToIgnore(), goleak.IgnoreCurrent())

	t.Helper()
	t.Cleanup(func() {
		// If the goleak check is run at a particularly unlucky time, it's possible
		// to hit an error path in runtime.Stack() [1] that prevents goleak from
		// parsing the runtime stack [2]. (goleak.VerifyNone() transitively calls
		// runtime.Stack [3].)
		//
		// Fundamentally, this issue arises because goleak can't directly hook into
		// the Go runtime and thus has to rely on well-formatted runtime debug
		// output, for which there is no guarantee.
		//
		// In order to minimize this possibility, we wait until the runtime is in a
		// less "interesting" state before trying to introspect it. We do so by
		// yielding execution, which may allow other test goroutines to complete and
		// make us less likely to hit the panic. The justification is only empirical
		// -- we ran the tests many times and observed fewer panics when adding the
		// yield.
		//
		// [1] https://cs.opensource.google/go/go/+/master:src/runtime/traceback.go;l=113;drc=f2656f20ea420ada5f15ef06ddf18d2797e18841
		// [2] https://github.com/uber-go/goleak/blob/89d54f0adef2491e157717f756bf7f918943f3cc/internal/stack/stacks.go#L135
		// [3] https://github.com/uber-go/goleak/blob/89d54f0adef2491e157717f756bf7f918943f3cc/internal/stack/stacks.go#L124
		//
		// In the event that goleak does panic within VerifyNone, we expect that it
		// was due to this bad timing, so we should just recover and try to run the
		// goleak check again.
		const maxTries = 5
		for try := 1; try <= maxTries; try++ {
			panicked := func() (panicked bool) {
				defer func() {
					if r := recover(); r != nil {
						t.Logf("recovered from panic in goleak check attempt %d: %s", try, r)
						panicked = true
					}
				}()
				runtime.Gosched()
				goleak.VerifyNone(t, opts...)
				return panicked
			}()
			if !panicked {
				return
			}
		}

		t.Errorf("panicked in all %d attempts at running a goleak check", maxTries)
	})
}

func getInterfaceAddresses(
	t *testing.T,
	ni *stackImpl,
	nicid tcpip.NICID,
) []tcpip.AddressWithPrefix {
	t.Helper()
	nicInfos := ni.ns.stack.NICInfo()
	nicInfo, ok := nicInfos[nicid]
	if !ok {
		t.Fatalf("couldn't find NICID=%d in %#v", nicid, nicInfos)
	}
	addrs := make([]tcpip.AddressWithPrefix, len(nicInfo.ProtocolAddresses))
	for i := range nicInfo.ProtocolAddresses {
		addrs[i] = nicInfo.ProtocolAddresses[i].AddressWithPrefix
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

// Test adding a list of both IPV4 and IPV6 addresses and then removing them
// again one-by-one.
func TestListInterfaceAddresses(t *testing.T) {
	addGoleakCheck(t)
	ndpDisp := testNDPDispatcher{
		dadC: make(chan ndpDADEvent, 1),
	}
	ns, clock := newNetstack(t, netstackTestOptions{ndpDisp: &ndpDisp})
	ni := &stackImpl{ns: ns}

	ep := noopEndpoint{
		linkAddress: tcpip.LinkAddress([]byte{2, 3, 4, 5, 6, 7}),
	}
	ifState, err := ns.addEndpoint(
		func(tcpip.NICID) string { return t.Name() },
		&ep,
		&noopController{},
		nil,                    /* observer */
		defaultInterfaceMetric, /* metric */
		qdiscConfig{},
	)
	if err != nil {
		t.Fatal(err)
	}
	if err := ifState.Up(); err != nil {
		t.Fatal("ifState.Up(): ", err)
	}

	// Wait for and account for any addresses added automatically.
	clock.Advance(dadResolutionTimeout)
	for {
		select {
		case d := <-ndpDisp.dadC:
			t.Logf("startup DAD event: %#v", d)
			continue
		default:
		}
		break
	}

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

				result, err := ni.AddInterfaceAddressDeprecated(context.Background(), uint64(ifState.nicid), ifAddr)
				if err != nil {
					t.Fatalf("ni.AddInterfaceAddressDeprecated(%d, %#v): %s", ifState.nicid, ifAddr, err)
				}
				if result != stack.StackAddInterfaceAddressDeprecatedResultWithResponse(stack.StackAddInterfaceAddressDeprecatedResponse{}) {
					t.Fatalf("got ni.AddInterfaceAddressDeprecated(%d, %#v) = %#v, want = Response()", ifState.nicid, ifAddr, result)
				}
				expectDad := header.IsV6UnicastAddress(addr.Address)
				clock.Advance(dadResolutionTimeout)
				select {
				case d := <-ndpDisp.dadC:
					if !expectDad {
						t.Fatalf("unexpected DAD event: %#v", d)
					}
					if diff := cmp.Diff(ndpDADEvent{nicID: ifState.nicid, addr: addr.Address, result: &tcpipstack.DADSucceeded{}}, d, cmp.AllowUnexported(d)); diff != "" {
						t.Fatalf("ndp DAD event mismatch (-want +got):\n%s", diff)
					}
				default:
					if expectDad {
						t.Fatal("timed out waiting for DAD event")
					}
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

				result, err := ni.DelInterfaceAddressDeprecated(context.Background(), uint64(ifState.nicid), ifAddr)
				if err != nil {
					t.Fatalf("ni.DelInterfaceAddressDeprecated(%d, %#v): %s", ifState.nicid, ifAddr, err)
				}
				if result != stack.StackDelInterfaceAddressDeprecatedResultWithResponse(stack.StackDelInterfaceAddressDeprecatedResponse{}) {
					t.Fatalf("got ni.DelInterfaceAddressDeprecated(%d, %#v) = %#v, want = Response()", ifState.nicid, ifAddr, result)
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
	addGoleakCheck(t)
	ns, _ := newNetstack(t, netstackTestOptions{})
	ni := &stackImpl{ns: ns}
	ifState := addNoopEndpoint(t, ns, "")
	t.Cleanup(ifState.RemoveByUser)

	// The call to ns.addEndpoint() added addresses to the stack. Make sure we include
	// those in our want list.
	initialAddrs := getInterfaceAddresses(t, ni, ifState.nicid)

	// Add address.
	addr := tcpip.AddressWithPrefix{Address: "\x01\x01\x01\x01", PrefixLen: 8}
	ifAddr := fidlnet.Subnet{
		Addr:      fidlconv.ToNetIpAddress(addr.Address),
		PrefixLen: uint8(addr.PrefixLen),
	}

	result, err := ni.AddInterfaceAddressDeprecated(context.Background(), uint64(ifState.nicid), ifAddr)
	if err != nil {
		t.Fatalf("ni.AddInterfaceAddressDeprecated(%d, %#v): %s", ifState.nicid, ifAddr, err)
	}
	if result != stack.StackAddInterfaceAddressDeprecatedResultWithResponse(stack.StackAddInterfaceAddressDeprecatedResponse{}) {
		t.Fatalf("got ni.AddInterfaceAddressDeprecated(%d, %#v) = %#v, want = Response()", ifState.nicid, ifAddr, result)
	}

	wantAddrs := append(initialAddrs, addr)
	gotAddrs := getInterfaceAddresses(t, ni, ifState.nicid)
	compareInterfaceAddresses(t, gotAddrs, wantAddrs)

	// Add the same address with a different prefix.
	addr.PrefixLen *= 2
	ifAddr.PrefixLen *= 2

	result, err = ni.AddInterfaceAddressDeprecated(context.Background(), uint64(ifState.nicid), ifAddr)
	if err != nil {
		t.Fatalf("ni.AddInterfaceAddressDeprecated(%d, %#v): %s", ifState.nicid, ifAddr, err)
	}
	if result != stack.StackAddInterfaceAddressDeprecatedResultWithResponse(stack.StackAddInterfaceAddressDeprecatedResponse{}) {
		t.Fatalf("got ni.AddInterfaceAddressDeprecated(%d, %#v) = %#v, want = Response()", ifState.nicid, ifAddr, result)
	}

	wantAddrs = append(initialAddrs, addr)
	gotAddrs = getInterfaceAddresses(t, ni, ifState.nicid)

	compareInterfaceAddresses(t, gotAddrs, wantAddrs)
}

func addAddressAndRoute(t *testing.T, ns *Netstack, ifState *ifState, addr tcpip.ProtocolAddress) {
	if ok, reason := ifState.addAddress(addr, tcpipstack.AddressProperties{}); !ok {
		t.Fatalf("ifState.addAddress(%s, {}): %s", addr.AddressWithPrefix, reason)
	}
	route := addressWithPrefixRoute(ifState.nicid, addr.AddressWithPrefix)
	if err := ns.AddRoute(route, metricNotSet /* dynamic */, false); err != nil {
		t.Fatalf("ns.AddRoute(%s, 0, false): %s", route, err)
	}
}

func TestAddRouteParameterValidation(t *testing.T) {
	addGoleakCheck(t)
	ns, _ := newNetstack(t, netstackTestOptions{})
	addr := tcpip.ProtocolAddress{
		Protocol: ipv4.ProtocolNumber,
		AddressWithPrefix: tcpip.AddressWithPrefix{
			Address:   tcpip.Address("\xf0\xf0\xf0\xf0"),
			PrefixLen: 24,
		},
	}
	subnetLocalAddress := tcpip.Address("\xf0\xf0\xf0\xf1")
	ifState := addNoopEndpoint(t, ns, "")
	t.Cleanup(ifState.RemoveByUser)

	addAddressAndRoute(t, ns, ifState, addr)

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
	addGoleakCheck(t)
	ns, _ := newNetstack(t, netstackTestOptions{})
	ifState := addNoopEndpoint(t, ns, "")
	t.Cleanup(ifState.RemoveByUser)

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
					Prf:                   routes.MediumPreference,
					Metric:                defaultInterfaceMetric,
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
					Prf:                   routes.MediumPreference,
					Metric:                defaultInterfaceMetric,
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
					Prf:                   routes.MediumPreference,
					Metric:                defaultInterfaceMetric,
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
					Prf:                   routes.MediumPreference,
					Metric:                defaultInterfaceMetric,
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
			ifState.dhcpAcquired(context.Background(), test.oldAddr, test.newAddr, test.config)

			if diff := cmp.Diff(test.config.DNS, ifState.dns.mu.servers); diff != "" {
				t.Errorf("ifState.mu.dnsServers mismatch (-want +got):\n%s", diff)
			}

			if diff := cmp.Diff(test.expectedRouteTable, ifState.ns.GetExtendedRouteTable(), cmp.AllowUnexported(tcpip.Subnet{})); diff != "" {
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
			ifState.dhcpAcquired(context.Background(), remAddr, tcpip.AddressWithPrefix{}, dhcp.Config{})

			if diff := cmp.Diff([]tcpip.Address(nil), ifState.dns.mu.servers); diff != "" {
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

func TestInFlightPacketsConsumeUDPSendBuffer(t *testing.T) {
	addGoleakCheck(t)
	ns, _ := newNetstack(t, netstackTestOptions{})
	linkEp := &sentinelEndpoint{}
	linkEp.SetBlocking(true)
	ifState := installAndValidateIface(t, ns, func(t *testing.T, ns *Netstack, name string) *ifState {
		return addLinkEndpoint(t, ns, name, linkEp)
	})
	t.Cleanup(ifState.RemoveByUser)

	addr := tcpip.ProtocolAddress{
		Protocol: ipv4.ProtocolNumber,
		AddressWithPrefix: tcpip.AddressWithPrefix{
			Address:   tcpip.Address("\xf0\xf0\xf0\xf0"),
			PrefixLen: 24,
		},
	}
	addAddressAndRoute(t, ns, ifState, addr)

	wq := new(waiter.Queue)
	var eventOut struct {
		mu       sync.Mutex
		returned bool
	}
	eventOutReturned := func() bool {
		eventOut.mu.Lock()
		defer eventOut.mu.Unlock()
		return eventOut.returned
	}
	cb := func(m waiter.EventMask) {
		eventOut.mu.Lock()
		defer eventOut.mu.Unlock()
		if m&waiter.EventOut != 0 {
			eventOut.returned = true
		}
	}
	entry := waiter.NewFunctionEntry(waiter.EventOut, cb)
	wq.EventRegister(&entry)

	ep, err := ns.stack.NewEndpoint(header.UDPProtocolNumber, header.IPv4ProtocolNumber, wq)
	if err != nil {
		t.Fatalf("ns.stack.NewEndpoint(%d, %d, _): %s", header.UDPProtocolNumber, header.IPv4ProtocolNumber, err)
	}

	fullAddress := &tcpip.FullAddress{
		NIC:  ifState.nicid,
		Addr: addr.AddressWithPrefix.Address,
		Port: 42,
	}
	var r bytes.Reader
	data := []byte{0, 1, 2, 3, 4}
	validateWrite := func(
		expectedCount int64,
		expectedErr tcpip.Error,
	) error {
		r.Reset(data)
		writeOpts := tcpip.WriteOptions{
			To: fullAddress,
		}
		n, err := ep.Write(&r, writeOpts)
		if diff := cmp.Diff(expectedErr, err); diff != "" {
			return fmt.Errorf("got ep.Write(_, %#v) = (_, %#v), want (_, %#v)", writeOpts, err, expectedErr)
		}
		if n != expectedCount {
			return fmt.Errorf("got ep.Write(_, %#v) = (%d, _), want (%d, _)", writeOpts, n, expectedCount)
		}
		return nil
	}

	// Write() succeeds when length(data) > sizeof(send buf) > 0; thereafter,
	// the send buffer is exhausted.
	ep.SocketOptions().SetSendBufferSize(int64(len(data))-1, false)
	if err := validateWrite(int64(len(data)), nil); err != nil {
		t.Fatal(err)
	}

	// Expect Write() blocks when the send buffer is exhausted.
	if err := validateWrite(0, &tcpip.ErrWouldBlock{}); err != nil {
		t.Fatal(err)
	}

	if eventOutReturned() {
		t.Fatalf("got waiter.EventOut before endpoint was unblocked")
	}
	linkEp.Drain()
	if !eventOutReturned() {
		t.Fatalf("expect waiter.EventOut signaled when endpoint is unblocked")
	}

	// Expect Write() succeeds now that the send buffer has space.
	if err := validateWrite(int64(len(data)), nil); err != nil {
		t.Fatal(err)
	}
}
