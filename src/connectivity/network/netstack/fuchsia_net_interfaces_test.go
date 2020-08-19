// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package netstack

import (
	"context"
	"fmt"
	"net"
	"testing"
	"time"

	fidlnet "fidl/fuchsia/net"
	"fidl/fuchsia/net/interfaces"
	"fidl/fuchsia/netstack"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
	"gvisor.dev/gvisor/pkg/tcpip"
	"gvisor.dev/gvisor/pkg/tcpip/header"
)

func TestDiffInterfaceProperties(t *testing.T) {
	var addr1 interfaces.Address
	addr1.SetAddr(fidlnet.Subnet{
		Addr:      toIpAddress(net.IPv4(192, 168, 0, 1).To4()),
		PrefixLen: 16,
	})
	var addr2 interfaces.Address
	addr2.SetAddr(fidlnet.Subnet{
		Addr:      toIpAddress(net.IPv4(192, 168, 0, 2).To4()),
		PrefixLen: 16,
	})

	var p interfaces.Properties
	p.SetId(1)
	p.SetAddresses([]interfaces.Address{addr1})
	p.SetOnline(false)
	p.SetHasDefaultIpv4Route(true)
	p.SetHasDefaultIpv6Route(false)

	var diff interfaces.Properties
	diff.SetId(1)

	online := p
	online.SetOnline(true)
	onlineDiff := diff
	onlineDiff.SetOnline(true)

	defaultRouteAdded := p
	defaultRouteAdded.SetHasDefaultIpv6Route(true)
	defaultRouteAddedDiff := diff
	defaultRouteAddedDiff.SetHasDefaultIpv6Route(true)

	defaultRouteRemoved := p
	defaultRouteRemoved.SetHasDefaultIpv4Route(false)
	defaultRouteRemovedDiff := diff
	defaultRouteRemovedDiff.SetHasDefaultIpv4Route(false)

	addressRemoved := p
	addressRemoved.SetAddresses(nil)
	addressRemovedDiff := diff
	addressRemovedDiff.SetAddresses(nil)

	addressAdded := p
	addressAdded.SetAddresses([]interfaces.Address{addr1, addr2})
	addressAddedDiff := diff
	addressAddedDiff.SetAddresses([]interfaces.Address{addr1, addr2})

	addressChanged := p
	addressChanged.SetAddresses([]interfaces.Address{addr2})
	addressChangedDiff := diff
	addressChangedDiff.SetAddresses([]interfaces.Address{addr2})

	for _, tc := range []struct {
		name     string
		before   interfaces.Properties
		after    interfaces.Properties
		wantDiff interfaces.Properties
	}{
		{"Online", p, online, onlineDiff},
		{"DefaultRouteAdded", p, defaultRouteAdded, defaultRouteAddedDiff},
		{"DefaultRouteRemoved", p, defaultRouteRemoved, defaultRouteRemovedDiff},
		{"AddressRemoved", p, addressRemoved, addressRemovedDiff},
		{"AddressAdded", p, addressAdded, addressAddedDiff},
		{"AddressChanged", p, addressChanged, addressChangedDiff},
	} {
		t.Run(tc.name, func(t *testing.T) {
			gotDiff := diffInterfaceProperties(tc.before, tc.after)
			if diff := cmp.Diff(tc.wantDiff, gotDiff, cmpopts.IgnoreTypes(struct{}{}), cmpopts.EquateEmpty()); diff != "" {
				t.Fatalf("(-want +got)\n%s", diff)
			}
		})
	}
}

type watchResult struct {
	event interfaces.Event
	err   error
}

func assertWatchResult(gotEvent interfaces.Event, gotErr error, wantEvent interfaces.Event) error {
	if gotErr != nil {
		return fmt.Errorf("Watch failed: %s", gotErr)
	}
	if diff := cmp.Diff(wantEvent, gotEvent, cmpopts.IgnoreTypes(struct{}{}), cmpopts.EquateEmpty()); diff != "" {
		return fmt.Errorf("(-want +got)\n%s", diff)
	}
	return nil
}

func wantInterfaceProperties(ns *Netstack, nicid tcpip.NICID) interfaces.Properties {
	var hasDefaultIpv4Route, hasDefaultIpv6Route bool
	for _, er := range ns.GetExtendedRouteTable() {
		if er.Enabled && er.Route.NIC == nicid {
			if er.Route.Destination.Equal(header.IPv4EmptySubnet) {
				hasDefaultIpv4Route = true
			} else if er.Route.Destination.Equal(header.IPv6EmptySubnet) {
				hasDefaultIpv6Route = true
			}
		}
	}
	return interfaceProperties(ns.stack.NICInfo()[nicid], hasDefaultIpv4Route, hasDefaultIpv6Route)
}

func TestInterfacesWatcher(t *testing.T) {
	ns := newNetstack(t)
	ni := &netstackImpl{ns: ns}
	si := &interfaceStateImpl{ns: ns}

	var nicid tcpip.NICID
	{
		ifs, err := addNoopEndpoint(ns, "")
		if err != nil {
			t.Fatal(err)
		}
		defer ifs.Remove()
		nicid = ifs.nicid
	}

	// The first watcher will always block, while the second watcher should never block.
	initWatcher := func() *interfaces.WatcherWithCtxInterface {
		request, watcher, err := interfaces.NewWatcherWithCtxInterfaceRequest()
		if err != nil {
			t.Fatalf("failed to create Watcher protocol channel pair: %s", err)
		}
		if err := si.GetWatcher(context.Background(), interfaces.WatcherOptions{}, request); err != nil {
			t.Fatalf("failed to call GetWatcher: %s", err)
		}
		event, err := watcher.Watch(context.Background())
		if err := assertWatchResult(event, err, interfaces.EventWithExists(wantInterfaceProperties(ns, nicid))); err != nil {
			t.Fatal(err)
		}
		event, err = watcher.Watch(context.Background())
		if err := assertWatchResult(event, err, interfaces.EventWithIdle(interfaces.Empty{})); err != nil {
			t.Fatal(err)
		}
		return watcher
	}
	blockingWatcher, nonBlockingWatcher := initWatcher(), initWatcher()
	defer func() {
		if err := blockingWatcher.Close(); err != nil {
			t.Fatalf("failed to close blocking Watcher client proxy: %s", err)
		}
		if err := nonBlockingWatcher.Close(); err != nil {
			t.Fatalf("failed to close non-blocking Watcher client proxy: %s", err)
		}
	}()

	ch := make(chan watchResult)
	blockingWatch := func() {
		go func() {
			event, err := blockingWatcher.Watch(context.Background())
			ch <- watchResult{event, err}
		}()
		select {
		case got := <-ch:
			t.Fatalf("WatchServers did not block and completed with: %+v", got)
		case <-time.After(50 * time.Millisecond):
		}
	}
	blockingWatch()

	// Add an interface.
	ifs, err := addNoopEndpoint(ns, "")
	if err != nil {
		t.Fatal(err)
	}

	verifyWatchResults := func(wantEvent interfaces.Event) {
		got := <-ch
		event, err := nonBlockingWatcher.Watch(context.Background())
		if err := assertWatchResult(got.event, got.err, wantEvent); err != nil {
			t.Fatalf("blocked watch failed: %s", err)
		}
		if err := assertWatchResult(event, err, wantEvent); err != nil {
			t.Fatalf("non-blocked watch failed: %s", err)
		}
	}
	verifyWatchResults(interfaces.EventWithAdded(wantInterfaceProperties(ns, ifs.nicid)))

	// Set interface up.
	blockingWatch()
	if err := ifs.Up(); err != nil {
		t.Fatalf("failed to set interface up: %s", err)
	}
	var id interfaces.Properties
	id.SetId(uint64(ifs.nicid))
	online := id
	online.SetOnline(true)
	verifyWatchResults(interfaces.EventWithChanged(online))

	// Add an address.
	blockingWatch()
	addr := toIpAddress(net.IPv4(192, 168, 0, 1).To4())
	prefixLen := uint8(16)
	if netErr, err := ni.SetInterfaceAddress(context.Background(), uint32(ifs.nicid), addr, prefixLen); netErr.Status != netstack.StatusOk || err != nil {
		t.Fatalf("failed to call SetInterfaceAddress: netErr=%s, err=%s", netErr, err)
	}
	addressAdded := id
	properties := wantInterfaceProperties(ns, ifs.nicid)
	addressAdded.SetAddresses(properties.GetAddresses())
	verifyWatchResults(interfaces.EventWithChanged(addressAdded))

	// Add a default route.
	blockingWatch()
	r := defaultV4Route(ifs.nicid, tcpip.Address(net.IPv4(192, 168, 0, 2).To4()))
	if err := ns.AddRoute(r, metricNotSet, false); err != nil {
		t.Fatalf("failed to add default route: %s", err)
	}
	defaultIpv4RouteAdded := id
	defaultIpv4RouteAdded.SetHasDefaultIpv4Route(true)
	verifyWatchResults(interfaces.EventWithChanged(defaultIpv4RouteAdded))

	// Remove the default route.
	blockingWatch()
	if err := ns.DelRoute(r); err != nil {
		t.Fatalf("failed to delete default route: %s", err)
	}
	defaultIpv4RouteRemoved := id
	defaultIpv4RouteRemoved.SetHasDefaultIpv4Route(false)
	verifyWatchResults(interfaces.EventWithChanged(defaultIpv4RouteRemoved))

	// Remove an address.
	blockingWatch()
	if netErr, err := ni.RemoveInterfaceAddress(context.Background(), uint32(ifs.nicid), addr, prefixLen); netErr.Status != netstack.StatusOk || err != nil {
		t.Fatalf("failed to call RemoveInterfaceAddress: netErr=%s, err=%s", netErr, err)
	}
	addressRemoved := id
	properties = wantInterfaceProperties(ns, ifs.nicid)
	addressRemoved.SetAddresses(properties.GetAddresses())
	verifyWatchResults(interfaces.EventWithChanged(addressRemoved))

	// Set interface down.
	blockingWatch()
	if err := ifs.Down(); err != nil {
		t.Fatalf("failed to set interface down: %s", err)
	}
	offline := id
	offline.SetOnline(false)
	verifyWatchResults(interfaces.EventWithChanged(offline))

	// Remove the interface.
	blockingWatch()
	ifs.Remove()
	verifyWatchResults(interfaces.EventWithRemoved(uint64(ifs.nicid)))
}
