// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package netstack

import (
	"net"
	"sort"
	"syscall/zx"
	"syscall/zx/fidl"
	"testing"
	"time"

	"fidl/fuchsia/hardware/ethernet"
	fidlnet "fidl/fuchsia/net"
	"fidl/fuchsia/net/stack"
	"fidl/fuchsia/netstack"
	ethernetext "fidlext/fuchsia/hardware/ethernet"

	"netstack/dhcp"
	"netstack/dns"
	"netstack/fidlconv"
	"netstack/link/eth"
	"netstack/routes"
	"netstack/util"

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
	testDeviceName       string        = "testdevice"
	testTopoPath         string        = "/fake/ethernet/device"
	testV4Address        tcpip.Address = tcpip.Address("\xc0\xa8\x2a\x10")
	testV6Address        tcpip.Address = tcpip.Address("\xc0\xa8\x2a\x10\xc0\xa8\x2a\x10\xc0\xa8\x2a\x10\xc0\xa8\x2a\x10")
	testLinkLocalV6Addr1 tcpip.Address = "\xfe\x80\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01"
	testLinkLocalV6Addr2 tcpip.Address = "\xfe\x80\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02"
	dadResolutionTimeout               = dadRetransmitTimer*dadTransmits + time.Second
)

func TestDelRouteErrors(t *testing.T) {
	ns := newNetstack(t)

	eth := deviceForAddEth(ethernet.Info{}, t)
	ifs, err := ns.addEth(testTopoPath, netstack.InterfaceConfig{Name: testDeviceName}, &eth)
	if err != nil {
		t.Fatalf("addEth(%q, _): %s", testTopoPath, err)
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
	eth := deviceForAddEth(ethernet.Info{}, t)
	eth.StopImpl = func() error { return nil }
	config := netstack.InterfaceConfig{Name: testDeviceName}
	ifs, err := ns.addEth(testTopoPath, config, &eth)
	if err != nil {
		t.Fatalf("ns.addEth(%q, %+v, _): %s", testTopoPath, config, err)
	}

	// The NIC should initially be disabled in stack.Stack.
	if enabled := ns.stack.CheckNIC(ifs.nicid); enabled {
		t.Fatalf("got ns.stack.CheckNIC(%d) = true, want = false", ifs.nicid)
	}

	// Bringing the link up should enable the NIC in stack.Stack.
	if err := ifs.controller.Up(); err != nil {
		t.Fatal("ifs.controller.Up(): ", err)
	}
	if enabled := ns.stack.CheckNIC(ifs.nicid); !enabled {
		t.Fatalf("got ns.stack.CheckNIC(%d) = false, want = true", ifs.nicid)
	}

	// Bringing the link down should disable the NIC in stack.Stack.
	if err := ifs.controller.Down(); err != nil {
		t.Fatal("ifs.controller.Down(): ", err)
	}
	if enabled := ns.stack.CheckNIC(ifs.nicid); enabled {
		t.Fatalf("got ns.stack.CheckNIC(%d) = true, want = false", ifs.nicid)
	}
}

func TestBindingSetCounterStat_Value(t *testing.T) {
	s := bindingSetCounterStat{bindingSets: []*fidl.BindingSet{
		new(fidl.BindingSet),
		new(fidl.BindingSet),
	}}
	defer func() {
		for _, b := range s.bindingSets {
			b.Close()
		}
	}()

	// Create a pair of channels for each call to (*fidl.BindingSet).Add;
	// (*fidl.BindingSet).Remove closes the channel being removed, which causes
	// its peer to return ZX_ERR_PEER_CLOSED, which in turn would cause the peer
	// to be removed from any fidl.BindingSet to which it has been added, which
	// would cause this test to flake (if some other test has started the FIDL
	// dispatcher).
	type Pair struct {
		ch, peer zx.Channel
	}
	var pairs []Pair
	defer func() {
		for _, pair := range pairs {
			// Explicitly ignore the errors; if the test runs past the
			// (*fidl.BindingSet).Remove calls below, these channels will have been
			// closed, and these Close calls will return ErrBadHandle. If the test
			// does not, then these Close calls will return nil. Either way, that's
			// not what we're testing here.
			_ = pair.ch.Close()
			_ = pair.peer.Close()
		}
	}()

	var want uint64
	if got := s.Value(); got != want {
		t.Errorf("got s.Value() = %d want = %d", got, want)
	}
	for _, b := range s.bindingSets {
		for i := 0; i < 2; i++ {
			ch, peer, err := zx.NewChannel(0)
			if err != nil {
				t.Fatal("zx.NewChannel(...) failed:", err)
			}
			pairs = append(pairs, Pair{ch: ch, peer: peer})

			if _, err := b.Add(nil, ch, nil); err != nil {
				t.Fatalf("%T.Add(...) failed: %s", b, err)
			}
			want++

			if got := s.Value(); got != want {
				t.Errorf("got s.Value() = %d want = %d", got, want)
			}
		}
	}

	for _, b := range s.bindingSets {
		for _, key := range b.BindingKeys() {
			if !b.Remove(key) {
				t.Fatalf("got %T.Remove(...) = false want = true", b)
			}
			want--

			if got := s.Value(); got != want {
				t.Errorf("got s.Value() = %d want = %d", got, want)
			}
		}
	}
	if got := s.Value(); got != want {
		t.Errorf("got s.Value() = %d want = %d", got, want)
	}

	// Ensure the test is self-consistent.
	if want != 0 {
		t.Errorf("got `want` = %d want = 0", want)
	}
}

func containsRoute(rs []tcpip.Route, r tcpip.Route) bool {
	for _, i := range rs {
		if i == r {
			return true
		}
	}

	return false
}

// TestEndpointDoubleClose tests that closing an endpoint that has already been
// closed once will not panic. This is in response to a bug where a socketImpl
// (whose endpoint had already been closed) was cloned, resulting in a second
// socketImpl with a reference to the already closed endpoint. When this second
// socketImpl closes, it will attempt to close the endpoint (which is already
// closed) resulting in a panic.
func TestEndpointDoubleClose(t *testing.T) {
	ns := newNetstack(t)
	wq := &waiter.Queue{}
	ep, err := ns.stack.NewEndpoint(tcp.ProtocolNumber, ipv6.ProtocolNumber, wq)
	if err != nil {
		t.Fatalf("NewEndpoint = %s", err)
	}

	ios := endpointWithSocket{
		endpoint: endpoint{
			netProto:   ipv6.ProtocolNumber,
			transProto: tcp.ProtocolNumber,
			wq:         wq,
			ep:         ep,
			ns:         ns,
		},
		loopReadDone:  make(chan struct{}),
		loopWriteDone: make(chan struct{}),
		closing:       make(chan struct{}),
	}

	{
		localS, peerS, err := zx.NewSocket(uint32(zx.SocketStream))
		if err != nil {
			t.Fatalf("zx.NewSocket = %s", err)
		}

		ios.local = localS
		ios.peer = peerS
	}

	// We set clones to 2 initially to make sure that resources only get
	// cleaned up when the ref count drops from 1 to 0.
	ios.clones = 2
	if refcount := ios.close(); refcount != 1 {
		t.Fatalf("got refcount = %d, want = 1", refcount)
	}
	select {
	case <-ios.closing:
		t.Fatal("ios.closing is closed")
	default:
	}

	// Ref count is now 1 so when we call close again, it will cleanup
	// associated resources.
	if refcount := ios.close(); refcount != 0 {
		t.Fatalf("got refcount = %d, want = 0", refcount)
	}
	select {
	case <-ios.closing:
	default:
		t.Fatal("ios.closing is not closed")
	}

	// Set ref count to 1 so it drops to 0 and make sure we do not
	// do the work of closing again, and therefore should not panic.
	ios.clones = 1
	if refcount := ios.close(); refcount != 0 {
		t.Fatalf("got refcount = %d, want = 0", refcount)
	}
}

// Test whether ios.close will delete the reference to the
// endpoint created here from the netstack`s endpoints.
func TestCloseEndpointsMap(t *testing.T) {
	ns := newNetstack(t)
	wq := &waiter.Queue{}
	ep, err := ns.stack.NewEndpoint(tcp.ProtocolNumber, ipv6.ProtocolNumber, wq)
	if err != nil {
		t.Fatalf("NewEndpoint = %s", err)
	}

	ios := endpointWithSocket{
		endpoint: endpoint{
			netProto:   ipv6.ProtocolNumber,
			transProto: tcp.ProtocolNumber,
			wq:         wq,
			ep:         ep,
			ns:         ns,
		},
		loopReadDone:  make(chan struct{}),
		loopWriteDone: make(chan struct{}),
		closing:       make(chan struct{}),
	}
	{
		localS, peerS, err := zx.NewSocket(uint32(zx.SocketStream))
		if err != nil {
			t.Fatalf("zx.NewSocket = %s", err)
		}

		ios.local = localS
		ios.peer = peerS
	}
	if _, loaded := ns.endpoints.LoadOrStore(zx.Handle(ios.local), ios.ep); loaded {
		t.Fatalf("endpoint map store error, key %d exists", ios.local)
	}

	ios.clones = 1
	if refcount := ios.close(); refcount != 0 {
		t.Fatalf("got refcount = %d, want = 0", refcount)
	}
	select {
	case <-ios.closing:
	default:
		t.Fatal("ios.closing is not closed")
	}

	// Check if the reference to the endpoint is deleted from endpoints.
	if _, ok := ios.ns.endpoints.Load(zx.Handle(ios.local)); ok {
		t.Fatal("endpoint map not updated on ios.close")
	}
}

func TestNicName(t *testing.T) {
	ns := newNetstack(t)

	eth := deviceForAddEth(ethernet.Info{}, t)
	ifs, err := ns.addEth(testTopoPath, netstack.InterfaceConfig{Name: testDeviceName}, &eth)
	if err != nil {
		t.Fatal(err)
	}
	name := ifs.ns.name(ifs.nicid)
	if name != testDeviceName {
		t.Fatalf("ifs.mu.name = %v, want = %v", name, testDeviceName)
	}
}

func TestNotStartedByDefault(t *testing.T) {
	ns := newNetstack(t)

	startCalled := false
	eth := deviceForAddEth(ethernet.Info{}, t)
	eth.StartImpl = func() (int32, error) {
		startCalled = true
		return int32(zx.ErrOk), nil
	}

	if _, err := ns.addEth(testTopoPath, netstack.InterfaceConfig{Name: testDeviceName}, &eth); err != nil {
		t.Fatal(err)
	}

	if startCalled {
		t.Error("expected no calls to ethernet.Device.Start by addEth")
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

// OnDHCPv6Configuration implements stack.NDPDispatcher.OnDHCPv6Configuration.
func (*testNDPDispatcher) OnDHCPv6Configuration(tcpip.NICID, tcpipstack.DHCPv6ConfigurationFromNDPRA) {
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

	eth := deviceForAddEth(ethernet.Info{
		Mac: ethernet.MacAddress{
			Octets: [6]byte{2, 3, 4, 5, 6, 7},
		},
	}, t)
	ifs, err := ns.addEth(testTopoPath, netstack.InterfaceConfig{Name: testDeviceName}, &eth)
	if err != nil {
		t.Fatalf("addEth(_, _, _): %s", err)
	}
	if err := ifs.controller.Up(); err != nil {
		t.Fatal("ifs.controller.Up(): ", err)
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

	eth := deviceForAddEth(ethernet.Info{
		Mac: ethernet.MacAddress{
			Octets: [6]byte{2, 3, 4, 5, 6, 7},
		},
	}, t)
	eth.StopImpl = func() error { return nil }
	ifs, err := ns.addEth(testTopoPath, netstack.InterfaceConfig{Name: testDeviceName}, &eth)
	if err != nil {
		t.Fatalf("addEth(_, _, _): %s", err)
	}

	linkLocalRoute := ipv6LinkLocalOnLinkRoute(ifs.nicid)

	// Initially should not have the link-local route.
	rt := ns.stack.GetRouteTable()
	if containsRoute(rt, linkLocalRoute) {
		t.Fatalf("got GetRouteTable() = %+v, don't want = %s", rt, linkLocalRoute)
	}

	// Bringing the ethernet device up should result in the link-local
	// route being added.
	if err := ifs.controller.Up(); err != nil {
		t.Fatalf("eth.Up(): %s", err)
	}
	rt = ns.stack.GetRouteTable()
	if !containsRoute(rt, linkLocalRoute) {
		t.Fatalf("got GetRouteTable() = %+v, want = %s", rt, linkLocalRoute)
	}

	// Bringing the ethernet device down should result in the link-local
	// route being removed.
	if err := ifs.controller.Down(); err != nil {
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
	eth := deviceForAddEth(ethernet.Info{}, t)
	eth.ConfigMulticastSetPromiscuousModeImpl = func(enabled bool) (int32, error) {
		multicastPromiscuousModeEnabled = enabled
		return int32(zx.ErrOk), nil
	}

	if _, err := ns.addEth(testTopoPath, netstack.InterfaceConfig{Name: testDeviceName}, &eth); err != nil {
		t.Fatal(err)
	}

	if !multicastPromiscuousModeEnabled {
		t.Error("expected a call to ConfigMulticastSetPromiscuousMode(true) by addEth")
	}
}

func TestDhcpConfiguration(t *testing.T) {
	ns := newNetstack(t)

	ipAddressConfig := netstack.IpAddressConfig{}
	ipAddressConfig.SetDhcp(true)

	d := deviceForAddEth(ethernet.Info{}, t)
	d.StopImpl = func() error { return nil }
	ifs, err := ns.addEth(testTopoPath, netstack.InterfaceConfig{Name: testDeviceName, IpAddressConfig: ipAddressConfig}, &d)
	if err != nil {
		t.Fatal(err)
	}

	name := ifs.ns.name(ifs.nicid)

	ifs.mu.Lock()
	if ifs.mu.dhcp.Client == nil {
		t.Error("no dhcp client")
	}

	if ifs.mu.dhcp.enabled {
		t.Error("expected dhcp to be disabled")
	}

	if ifs.mu.dhcp.running() {
		t.Error("expected dhcp client to be stopped initially")
	}
	ifs.mu.Unlock()

	ifs.setDHCPStatus(name, true)

	ifs.controller.Up()

	ifs.mu.Lock()
	if !ifs.mu.dhcp.enabled {
		t.Error("expected dhcp to be enabled")
	}

	if !ifs.mu.dhcp.running() {
		t.Error("expected dhcp client to be running")
	}
	ifs.mu.Unlock()

	ifs.controller.Down()

	ifs.mu.Lock()
	if ifs.mu.dhcp.running() {
		t.Error("expected dhcp client to be stopped on eth down")
	}
	if !ifs.mu.dhcp.enabled {
		t.Error("expected dhcp configuration to be preserved on eth down")
	}
	ifs.mu.Unlock()

	ifs.controller.Up()

	ifs.mu.Lock()
	if !ifs.mu.dhcp.running() {
		t.Error("expected dhcp client to be running on eth restart")
	}
	if !ifs.mu.dhcp.enabled {
		t.Error("expected dhcp configuration to be preserved on eth restart")
	}
	ifs.mu.Unlock()
}

func TestUniqueFallbackNICNames(t *testing.T) {
	ns := newNetstack(t)

	d1 := deviceForAddEth(ethernet.Info{}, t)
	ifs1, err := ns.addEth(testTopoPath, netstack.InterfaceConfig{}, &d1)
	if err != nil {
		t.Fatal(err)
	}

	d2 := deviceForAddEth(ethernet.Info{}, t)
	ifs2, err := ns.addEth(testTopoPath, netstack.InterfaceConfig{}, &d2)
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
	ifAddr := stack.InterfaceAddress{IpAddress: addr, PrefixLen: 32}
	d := deviceForAddEth(ethernet.Info{}, t)
	d.StopImpl = func() error { return nil }
	ifs, err := ns.addEth(testTopoPath, netstack.InterfaceConfig{Name: testDeviceName}, &d)
	if err != nil {
		t.Fatal(err)
	}

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

	ifs.controller.Down()

	ifs.mu.Lock()
	if ifs.mu.dhcp.enabled {
		t.Error("expected dhcp state to remain disabled after bringing interface down")
	}
	if ifs.mu.dhcp.running() {
		t.Error("expected dhcp state to remain stopped after bringing interface down")
	}
	ifs.mu.Unlock()

	ifs.controller.Up()

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
}

func TestWLANStaticIPConfiguration(t *testing.T) {
	ns := Netstack{
		stack: tcpipstack.New(tcpipstack.Options{
			NetworkProtocols: []tcpipstack.NetworkProtocol{
				arp.NewProtocol(),
				ipv4.NewProtocol(),
				ipv6.NewProtocol(),
			},
		}),
	}

	addr := fidlconv.ToNetIpAddress(testV4Address)
	ifAddr := stack.InterfaceAddress{IpAddress: addr, PrefixLen: 32}
	d := deviceForAddEth(ethernet.Info{Features: ethernet.InfoFeatureWlan}, t)
	ifs, err := ns.addEth(testTopoPath, netstack.InterfaceConfig{Name: testDeviceName}, &d)
	if err != nil {
		t.Fatal(err)
	}

	result := ns.addInterfaceAddr(uint64(ifs.nicid), ifAddr)
	if result != stack.StackAddInterfaceAddressResultWithResponse(stack.StackAddInterfaceAddressResponse{}) {
		t.Fatalf("got ns.addInterfaceAddr(%d, %#v) = %#v, want = Response()", ifs.nicid, ifAddr, result)
	}

	if mainAddr, err := ns.stack.GetMainNICAddress(ifs.nicid, ipv4.ProtocolNumber); err != nil {
		t.Errorf("stack.GetMainNICAddress(%d, %d): %s", ifs.nicid, ipv4.ProtocolNumber, err)
	} else if got := mainAddr.Address; got != testV4Address {
		t.Errorf("got stack.GetMainNICAddress(%d, %d).Addr = %#v, want = %#v", ifs.nicid, ipv4.ProtocolNumber, got, testV4Address)
	}
}

func newNetstack(t *testing.T) *Netstack {
	t.Helper()
	return newNetstackWithNDPDispatcher(t, nil)
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
	var ndpDispImpl tcpipstack.NDPDispatcher
	if ndpDisp != nil {
		ndpDispImpl = ndpDispImpl
	}
	ns := newNetstackWithStackNDPDispatcher(t, ndpDispImpl)
	if ndpDisp != nil {
		ndpDisp.ns = ns
	}
	return ns
}

func newNetstackWithStackNDPDispatcher(t *testing.T, ndpDisp tcpipstack.NDPDispatcher) *Netstack {
	t.Helper()

	stk := tcpipstack.New(tcpipstack.Options{
		NetworkProtocols: []tcpipstack.NetworkProtocol{
			arp.NewProtocol(),
			ipv4.NewProtocol(),
			ipv6.NewProtocol(),
		},
		TransportProtocols: []tcpipstack.TransportProtocol{
			tcp.NewProtocol(),
			udp.NewProtocol(),
		},
		NDPDisp: ndpDisp,
	})
	return &Netstack{
		stack: stk,
		// We need to initialize the DNS client, since adding/removing interfaces
		// sets the DNS servers on that interface, which requires that dnsClient
		// exist.
		dnsClient: dns.NewClient(stk),
	}
}

func getInterfaceAddresses(t *testing.T, ni *stackImpl, nicid tcpip.NICID) []tcpip.AddressWithPrefix {
	t.Helper()

	interfaces, err := ni.ListInterfaces(fidl.Background())
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
			Address:   fidlconv.ToTCPIPAddress(a.IpAddress),
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

	d := deviceForAddEth(ethernet.Info{}, t)
	if _, err := ns.addEth(testTopoPath, netstack.InterfaceConfig{}, &d); err != nil {
		t.Fatal(err)
	}

	interfaces, err := ni.GetInterfaces2(fidl.Background())
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

	d := deviceForAddEth(ethernet.Info{
		Mac: ethernet.MacAddress{
			Octets: [6]byte{2, 3, 4, 5, 6, 7},
		},
	}, t)
	ifState, err := ns.addEth(testTopoPath, netstack.InterfaceConfig{}, &d)
	if err != nil {
		t.Fatal(err)
	}
	if err := ifState.controller.Up(); err != nil {
		t.Fatal("ifState.controller.Up(): ", err)
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

	// The call to ns.addEth() added addresses to the stack. Make sure we include
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
				ifAddr := stack.InterfaceAddress{
					IpAddress: fidlconv.ToNetIpAddress(addr.Address),
					PrefixLen: uint8(addr.PrefixLen),
				}

				result, err := ni.AddInterfaceAddress(fidl.Background(), uint64(ifState.nicid), ifAddr)
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
				ifAddr := stack.InterfaceAddress{
					IpAddress: fidlconv.ToNetIpAddress(addr.Address),
					PrefixLen: uint8(addr.PrefixLen),
				}

				result, err := ni.DelInterfaceAddress(fidl.Background(), uint64(ifState.nicid), ifAddr)
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
	d := deviceForAddEth(ethernet.Info{}, t)
	ifState, err := ns.addEth(testTopoPath, netstack.InterfaceConfig{}, &d)
	if err != nil {
		t.Fatal(err)
	}

	// The call to ns.addEth() added addresses to the stack. Make sure we include
	// those in our want list.
	initialAddrs := getInterfaceAddresses(t, ni, ifState.nicid)

	// Add address.
	addr := tcpip.AddressWithPrefix{"\x01\x01\x01\x01", 8}
	ifAddr := stack.InterfaceAddress{
		IpAddress: fidlconv.ToNetIpAddress(addr.Address),
		PrefixLen: uint8(addr.PrefixLen),
	}

	result, err := ni.AddInterfaceAddress(fidl.Background(), uint64(ifState.nicid), ifAddr)
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

	result, err = ni.AddInterfaceAddress(fidl.Background(), uint64(ifState.nicid), ifAddr)
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
	d := deviceForAddEth(ethernet.Info{}, t)
	addr := tcpip.ProtocolAddress{
		Protocol: ipv4.ProtocolNumber,
		AddressWithPrefix: tcpip.AddressWithPrefix{
			Address:   tcpip.Address("\xf0\xf0\xf0\xf0"),
			PrefixLen: 24,
		},
	}
	subnetLocalAddress := tcpip.Address("\xf0\xf0\xf0\xf1")
	ifState, err := ns.addEth(testTopoPath, netstack.InterfaceConfig{}, &d)
	if err != nil {
		t.Fatalf("got ns.addEth(_) = _, %s want = _, nil", err)
	}

	found, err := ns.addInterfaceAddress(ifState.nicid, addr)
	if err != nil {
		t.Fatalf("ns.addInterfaceAddress(%d, %s) = _, %s", ifState.nicid, addr.AddressWithPrefix, err)
	}
	if !found {
		t.Fatalf("ns.addInterfaceAddress(%d, %s) = %t, _", ifState.nicid, addr.AddressWithPrefix, found)
	}

	tests := []struct {
		name        string
		route       tcpip.Route
		metric      routes.Metric
		dynamic     bool
		shouldError bool
	}{
		{
			name: "IPv4 destination no NIC invalid gateway",
			route: tcpip.Route{
				Destination: util.PointSubnet(testV4Address),
				Gateway:     testV4Address,
				NIC:         0,
			},
			metric:      routes.Metric(0),
			shouldError: true,
		},
		{
			name: "IPv6 destination no NIC invalid gateway",
			route: tcpip.Route{
				Destination: util.PointSubnet(testV6Address),
				Gateway:     testV6Address,
				NIC:         0,
			},
			metric:      routes.Metric(0),
			shouldError: true,
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
			err := ns.AddRoute(test.route, test.metric, test.dynamic)
			if got := err != nil; got != test.shouldError {
				t.Logf("err = %v", err)
				t.Errorf("got (ns.AddRoute(_) != nil) = %t, want = %t", got, test.shouldError)
			}
		})
	}
}

func TestDHCPAcquired(t *testing.T) {
	ns := newNetstack(t)
	d := deviceForAddEth(ethernet.Info{}, t)
	ifState, err := ns.addEth(testTopoPath, netstack.InterfaceConfig{}, &d)
	if err != nil {
		t.Fatal(err)
	}

	serverAddress := []byte(testV4Address)
	serverAddress[len(serverAddress)-1]++
	gatewayAddress := serverAddress
	gatewayAddress[len(gatewayAddress)-1]++

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
				ServerAddress: tcpip.Address(serverAddress),
				Gateway:       tcpip.Address(serverAddress),
				SubnetMask:    tcpip.AddressMask(defaultMask),
				DNS:           []tcpip.Address{tcpip.Address(gatewayAddress)},
				LeaseLength:   dhcp.Seconds(60),
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

			if diff := cmp.Diff(ifState.mu.dnsServers, test.config.DNS); diff != "" {
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

			if diff := cmp.Diff(ifState.mu.dnsServers, ifState.mu.dnsServers[:0]); diff != "" {
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

// Returns an ethernetext.Device struct that implements
// ethernet.Device and can be started and stopped.
//
// Reports the passed in ethernet.Info when Device#GetInfo is called.
func deviceForAddEth(info ethernet.Info, t *testing.T) ethernetext.Device {
	return ethernetext.Device{
		TB:                t,
		GetInfoImpl:       func() (ethernet.Info, error) { return info, nil },
		SetClientNameImpl: func(string) (int32, error) { return 0, nil },
		GetStatusImpl: func() (uint32, error) {
			return uint32(eth.LinkUp), nil
		},
		GetFifosImpl: func() (int32, *ethernet.Fifos, error) {
			return int32(zx.ErrOk), &ethernet.Fifos{
				TxDepth: 1,
			}, nil
		},
		SetIoBufferImpl: func(zx.VMO) (int32, error) {
			return int32(zx.ErrOk), nil
		},
		StartImpl: func() (int32, error) {
			return int32(zx.ErrOk), nil
		},
		ConfigMulticastSetPromiscuousModeImpl: func(bool) (int32, error) {
			return int32(zx.ErrOk), nil
		},
	}
}
