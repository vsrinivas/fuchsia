// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//go:build !build_with_native_toolchain
// +build !build_with_native_toolchain

package netstack

import (
	"context"
	"errors"
	"fmt"
	"net"
	"syscall/zx"
	"testing"

	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/dhcp"
	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/fidlconv"
	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/sync"
	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/time"
	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/util"

	fidlnet "fidl/fuchsia/net"
	"fidl/fuchsia/net/interfaces"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
	"gvisor.dev/gvisor/pkg/tcpip"
	"gvisor.dev/gvisor/pkg/tcpip/header"
	"gvisor.dev/gvisor/pkg/tcpip/stack"
)

func assertWatchResult(gotEvent interfaces.Event, gotErr error, wantEvent interfaces.Event) error {
	if gotErr != nil {
		return fmt.Errorf("Watch failed: %w", gotErr)
	}
	if diff := cmp.Diff(wantEvent, gotEvent, cmpopts.IgnoreTypes(struct{}{}), cmpopts.EquateEmpty()); diff != "" {
		return fmt.Errorf("(-want +got)\n%s", diff)
	}
	return nil
}

type watchResult struct {
	event interfaces.Event
	err   error
}

type watcherHelper struct {
	*interfaces.WatcherWithCtxInterface
}

func initWatcher(t *testing.T, si *interfaceStateImpl) watcherHelper {
	request, watcher, err := interfaces.NewWatcherWithCtxInterfaceRequest()
	if err != nil {
		t.Fatalf("failed to create Watcher protocol channel pair: %s", err)
	}
	if err := si.GetWatcher(context.Background(), interfaces.WatcherOptions{}, request); err != nil {
		t.Fatalf("failed to call GetWatcher: %s", err)
	}
	return watcherHelper{
		WatcherWithCtxInterface: watcher,
	}
}

func (w *watcherHelper) expectIdleEvent(t *testing.T) {
	w.expectEvent(t, interfaces.EventWithIdle(interfaces.Empty{}))
}

func (w *watcherHelper) expectEvent(t *testing.T, want interfaces.Event) {
	event, err := w.Watch(context.Background())
	if err := assertWatchResult(event, err, want); err != nil {
		t.Fatal(err)
	}
}

func (w *watcherHelper) blockingWatch(t *testing.T, ch chan watchResult) {
	go func() {
		event, err := w.Watch(context.Background())
		ch <- watchResult{
			event: event,
			err:   err,
		}
	}()
	select {
	case got := <-ch:
		t.Fatalf("Watch did not block and completed with: %#v", got)
	case <-time.After(50 * time.Millisecond):
	}
}

func TestInterfacesWatcherDisallowMultiplePending(t *testing.T) {
	eventChan := make(chan interfaceEvent)
	watcherChan := make(chan interfaces.WatcherWithCtxInterfaceRequest)

	go interfaceWatcherEventLoop(eventChan, watcherChan)
	si := &interfaceStateImpl{watcherChan: watcherChan}

	watcher := initWatcher(t, si)
	watcher.expectIdleEvent(t)

	var wg sync.WaitGroup
	defer wg.Wait()

	for i := 0; i < 2; i++ {
		wg.Add(1)
		go func() {
			defer wg.Done()

			_, err := watcher.Watch(context.Background())
			var gotErr *zx.Error
			if !(errors.As(err, &gotErr) && gotErr.Status == zx.ErrPeerClosed) {
				t.Errorf("got watcher.Watch() = (_, %s), want %s", err, zx.ErrPeerClosed)
			}
		}()
	}
}

func TestInterfacesWatcherExisting(t *testing.T) {
	eventChan := make(chan interfaceEvent)
	watcherChan := make(chan interfaces.WatcherWithCtxInterfaceRequest)

	go interfaceWatcherEventLoop(eventChan, watcherChan)
	ns, _ := newNetstack(t, netstackTestOptions{interfaceEventChan: eventChan})
	si := &interfaceStateImpl{watcherChan: watcherChan}

	ifs := addNoopEndpoint(t, ns, "")

	watcher := initWatcher(t, si)
	defer func() {
		if err := watcher.Close(); err != nil {
			t.Fatalf("failed to close watcher: %s", err)
		}
	}()

	watcher.expectEvent(t, interfaces.EventWithExisting(initialProperties(ifs, ns.name(ifs.nicid))))
	watcher.expectIdleEvent(t)
}

func TestInterfacesWatcher(t *testing.T) {
	eventChan := make(chan interfaceEvent)
	watcherChan := make(chan interfaces.WatcherWithCtxInterfaceRequest)

	go interfaceWatcherEventLoop(eventChan, watcherChan)
	ns, _ := newNetstack(t, netstackTestOptions{interfaceEventChan: eventChan})
	si := &interfaceStateImpl{watcherChan: watcherChan}

	// The first watcher will always block, while the second watcher should never block.
	blockingWatcher, nonBlockingWatcher := initWatcher(t, si), initWatcher(t, si)
	ch := make(chan watchResult)
	defer func() {
		// NB: The blocking watcher closed at the end of the test instead of deferred as
		// additional assertions are made with it.
		if err := nonBlockingWatcher.Close(); err != nil {
			t.Fatalf("failed to close non-blocking watcher: %s", err)
		}
		close(ch)
	}()

	blockingWatcher.expectIdleEvent(t)
	nonBlockingWatcher.expectIdleEvent(t)

	blockingWatcher.blockingWatch(t, ch)

	// Add an interface.
	ifs := addNoopEndpoint(t, ns, "")

	verifyWatchResults := func(t *testing.T, wantEvent interfaces.Event) {
		t.Helper()

		nonBlockingWatcher.expectEvent(t, wantEvent)

		got := <-ch
		if err := assertWatchResult(got.event, got.err, wantEvent); err != nil {
			t.Fatalf("blocked watch failed: %s", err)
		}
	}
	verifyWatchResults(t, interfaces.EventWithAdded(initialProperties(ifs, ns.name(ifs.nicid))))

	// Set interface up.
	blockingWatcher.blockingWatch(t, ch)
	if err := ifs.Up(); err != nil {
		t.Fatalf("failed to set interface up: %s", err)
	}
	var id interfaces.Properties
	id.SetId(uint64(ifs.nicid))
	online := id
	online.SetOnline(true)
	verifyWatchResults(t, interfaces.EventWithChanged(online))

	// Add an address.
	blockingWatcher.blockingWatch(t, ch)
	protocolAddr := tcpip.ProtocolAddress{
		Protocol: header.IPv4ProtocolNumber,
		AddressWithPrefix: tcpip.AddressWithPrefix{
			Address:   tcpip.Address(net.IPv4(192, 168, 0, 1).To4()),
			PrefixLen: 16,
		},
	}
	if ok, reason := ifs.addAddress(protocolAddr, stack.AddressProperties{}); !ok {
		t.Fatalf("ifs.addAddress(%s, {}): %s", protocolAddr.AddressWithPrefix, reason)
	}
	{
		addressAdded := id
		var address interfaces.Address
		address.SetAddr(fidlconv.ToNetSubnet(protocolAddr.AddressWithPrefix))
		address.SetValidUntil(int64(zx.TimensecInfinite))
		addressAdded.SetAddresses([]interfaces.Address{address})
		verifyWatchResults(t, interfaces.EventWithChanged(addressAdded))
	}

	// Add a default route.
	blockingWatcher.blockingWatch(t, ch)
	r := defaultV4Route(ifs.nicid, "\x01\x02\x03\x05")
	if err := ns.AddRoute(r, metricNotSet, false); err != nil {
		t.Fatalf("failed to add default route: %s", err)
	}
	defaultIpv4RouteAdded := id
	defaultIpv4RouteAdded.SetHasDefaultIpv4Route(true)
	verifyWatchResults(t, interfaces.EventWithChanged(defaultIpv4RouteAdded))

	// Remove the default route.
	blockingWatcher.blockingWatch(t, ch)
	_ = ns.DelRoute(r)
	defaultIpv4RouteRemoved := id
	defaultIpv4RouteRemoved.SetHasDefaultIpv4Route(false)
	verifyWatchResults(t, interfaces.EventWithChanged(defaultIpv4RouteRemoved))

	// Remove an address.
	blockingWatcher.blockingWatch(t, ch)
	if zxStatus := ifs.removeAddress(protocolAddr); zxStatus != zx.ErrOk {
		t.Fatalf("ifs.removeAddress(%s): %s", protocolAddr.AddressWithPrefix, zxStatus)
	}
	addressRemoved := id
	addressRemoved.SetAddresses([]interfaces.Address{})
	verifyWatchResults(t, interfaces.EventWithChanged(addressRemoved))

	// DHCP Acquired on the interface.
	blockingWatcher.blockingWatch(t, ch)
	addr := fidlnet.Ipv4Address{Addr: [4]uint8{192, 168, 0, 4}}
	acquiredAddr := tcpip.AddressWithPrefix{Address: tcpip.Address(addr.Addr[:]), PrefixLen: 24}
	leaseLength := dhcp.Seconds(10)
	initUpdatedAt := time.Monotonic(42)
	ifs.dhcpAcquired(context.Background(), tcpip.AddressWithPrefix{}, acquiredAddr, dhcp.Config{UpdatedAt: initUpdatedAt, LeaseLength: leaseLength})
	dhcpAddressAdded := id
	var address interfaces.Address
	address.SetAddr(fidlnet.Subnet{
		Addr:      fidlnet.IpAddressWithIpv4(addr),
		PrefixLen: uint8(acquiredAddr.PrefixLen),
	})
	address.SetValidUntil(int64(zx.TimensecInfinite))
	dhcpAddressAdded.SetAddresses([]interfaces.Address{address})
	verifyWatchResults(t, interfaces.EventWithChanged(dhcpAddressAdded))

	address.SetValidUntil(initUpdatedAt.Add(leaseLength.Duration()).MonotonicNano())
	dhcpValidUntil := id
	dhcpValidUntil.SetAddresses([]interfaces.Address{address})
	blockingWatcher.expectEvent(t, interfaces.EventWithChanged(dhcpValidUntil))
	nonBlockingWatcher.expectEvent(t, interfaces.EventWithChanged(dhcpValidUntil))

	// DHCP Acquired with same valid_until does not produce event.
	ifs.dhcpAcquired(context.Background(), acquiredAddr, acquiredAddr, dhcp.Config{UpdatedAt: initUpdatedAt, LeaseLength: leaseLength})
	blockingWatcher.blockingWatch(t, ch)

	// DHCP Acquired with different valid_until.
	updatedAt := time.Monotonic(100)
	ifs.dhcpAcquired(context.Background(), acquiredAddr, acquiredAddr, dhcp.Config{UpdatedAt: updatedAt, LeaseLength: leaseLength})
	dhcpAddressRenewed := id
	address.SetValidUntil(updatedAt.Add(leaseLength.Duration()).MonotonicNano())
	dhcpAddressRenewed.SetAddresses([]interfaces.Address{address})
	verifyWatchResults(t, interfaces.EventWithChanged(dhcpAddressRenewed))

	// DHCP Acquired on empty address signaling end of lease.
	blockingWatcher.blockingWatch(t, ch)
	ifs.dhcpAcquired(context.Background(), acquiredAddr, tcpip.AddressWithPrefix{}, dhcp.Config{})
	dhcpExpired := id
	dhcpExpired.SetAddresses([]interfaces.Address{})
	verifyWatchResults(t, interfaces.EventWithChanged(dhcpExpired))

	// Set interface down.
	blockingWatcher.blockingWatch(t, ch)
	if err := ifs.Down(); err != nil {
		t.Fatalf("failed to set interface down: %s", err)
	}
	offline := id
	offline.SetOnline(false)
	verifyWatchResults(t, interfaces.EventWithChanged(offline))

	// Remove the interface.
	blockingWatcher.blockingWatch(t, ch)
	ifs.RemoveByUser()
	verifyWatchResults(t, interfaces.EventWithRemoved(uint64(ifs.nicid)))

	blockingWatcher.blockingWatch(t, ch)
	if err := blockingWatcher.Close(); err != nil {
		t.Errorf("failed to close blocking Watcher client proxy: %s", err)
	}

	got, want := <-ch, zx.ErrCanceled
	switch err := got.err.(type) {
	case *zx.Error:
		if err.Status != want {
			t.Fatalf("unexpected zxStatus; got: %s, want: %s", err.Status, want)
		}
	default:
		t.Fatalf("unexpected return value from Watch resolved due to channel closure; got: %#v, want: %s", got, want)
	}
}

func TestInterfacesWatcherDuplicateAddress(t *testing.T) {
	eventChan := make(chan interfaceEvent)
	watcherChan := make(chan interfaces.WatcherWithCtxInterfaceRequest)
	go interfaceWatcherEventLoop(eventChan, watcherChan)

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	protocolAddr := tcpip.ProtocolAddress{
		Protocol: header.IPv6ProtocolNumber,
		AddressWithPrefix: tcpip.AddressWithPrefix{
			Address:   util.Parse("abcd::1"),
			PrefixLen: 64,
		},
	}
	ndpDisp := newNDPDispatcherForTest()
	ndpDisp.getAddressPrefix = func(_ *stack.NICInfo, addr tcpip.Address) (int, bool) {
		if want := protocolAddr.AddressWithPrefix.Address; addr != want {
			t.Fatalf("getAddressPrefix got addr=%s, want addr=%s", addr, want)
			return 0, false
		}
		return protocolAddr.AddressWithPrefix.PrefixLen, true
	}
	ns, _ := newNetstack(t, netstackTestOptions{interfaceEventChan: eventChan, ndpDisp: ndpDisp})
	ndpDisp.start(ctx)

	si := &interfaceStateImpl{watcherChan: watcherChan}

	ifs := addNoopEndpoint(t, ns, "")

	watcher := initWatcher(t, si)
	defer func() {
		if err := watcher.Close(); err != nil {
			t.Fatalf("failed to close watcher: %s", err)
		}
	}()

	watcher.expectEvent(t, interfaces.EventWithExisting(initialProperties(ifs, ns.name(ifs.nicid))))
	watcher.expectIdleEvent(t)

	// Add an IPv6 address, should not observe the address until DAD success.
	ifs.addAddress(protocolAddr, stack.AddressProperties{})
	resultCh := make(chan watchResult, 1)
	watcher.blockingWatch(t, resultCh)

	// Fake a DAD succeeded event and observe the address.
	ndpDisp.OnDuplicateAddressDetectionResult(ifs.nicid, protocolAddr.AddressWithPrefix.Address, &stack.DADSucceeded{})
	var wantAddress interfaces.Address
	wantAddress.SetAddr(fidlconv.ToNetSubnet(protocolAddr.AddressWithPrefix))
	wantAddress.SetValidUntil(int64(zx.TimensecInfinite))
	{
		var wantProperties interfaces.Properties
		wantProperties.SetId(uint64(ifs.nicid))
		wantProperties.SetAddresses([]interfaces.Address{wantAddress})
		watchResult := <-resultCh
		if err := assertWatchResult(watchResult.event, watchResult.err, interfaces.EventWithChanged(wantProperties)); err != nil {
			t.Fatal(err)
		}
	}

	// Fake another DAD completion event. Note that DAD is normally only re-run
	// after an interface goes down and back up, but since that is not relevant
	// to this test we forego that step. Expect nothing on the watcher.
	ndpDisp.OnDuplicateAddressDetectionResult(ifs.nicid, protocolAddr.AddressWithPrefix.Address, &stack.DADSucceeded{})
	watcher.blockingWatch(t, resultCh)

	// Remove the address and observe removal.
	if status := ifs.removeAddress(protocolAddr); status != zx.ErrOk {
		t.Fatalf("ifs.removeAddress(%#v) = %s", protocolAddr, status)
	}
	{
		var wantProperties interfaces.Properties
		wantProperties.SetId(uint64(ifs.nicid))
		wantProperties.SetAddresses([]interfaces.Address{})
		watchResult := <-resultCh
		if err := assertWatchResult(watchResult.event, watchResult.err, interfaces.EventWithChanged(wantProperties)); err != nil {
			t.Fatal(err)
		}
	}
}
