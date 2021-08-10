// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//go:build !build_with_native_toolchain
// +build !build_with_native_toolchain

package netstack

import (
	"bytes"
	"context"
	"errors"
	"fmt"
	"math"
	"sort"
	"sync"
	"syscall/zx/fidl"

	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/fidlconv"
	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/time"
	"go.fuchsia.dev/fuchsia/src/lib/component"
	syslog "go.fuchsia.dev/fuchsia/src/lib/syslog/go"

	"fidl/fuchsia/net"
	"fidl/fuchsia/net/interfaces"

	"gvisor.dev/gvisor/pkg/tcpip"
	"gvisor.dev/gvisor/pkg/tcpip/header"
	"gvisor.dev/gvisor/pkg/tcpip/network/ipv4"
	"gvisor.dev/gvisor/pkg/tcpip/network/ipv6"
	tcpipstack "gvisor.dev/gvisor/pkg/tcpip/stack"
)

const watcherProtocolName = "fuchsia.net.interfaces/Watcher"

// addressPatch is a patch to the address data exposed by upstream interface
// data. This type provides a mechanism by which clients of onPropertiesChange
// can extend the data exposed by fuchsia.net.interfaces beyond what is
// available in the upstream interface representation. At the the time of this
// writing, there is only need for an extension to Address data; should more
// general extensions be needed, a properties patch type which composes this
// type may be called for.
type addressPatch struct {
	addr       tcpip.AddressWithPrefix
	validUntil time.Time
}

func interfaceProperties(nicInfo tcpipstack.NICInfo, hasDefaultIPv4Route, hasDefaultIPv6Route bool, addressPatches []addressPatch) interfaces.Properties {
	var p interfaces.Properties
	ifs := nicInfo.Context.(*ifState)
	p.SetId(uint64(ifs.nicid))
	p.SetName(nicInfo.Name)
	p.SetHasDefaultIpv4Route(hasDefaultIPv4Route)
	p.SetHasDefaultIpv6Route(hasDefaultIPv6Route)

	if ifs.endpoint.Capabilities()&tcpipstack.CapabilityLoopback != 0 {
		p.SetDeviceClass(interfaces.DeviceClassWithLoopback(interfaces.Empty{}))
	} else if ifs.controller != nil {
		p.SetDeviceClass(interfaces.DeviceClassWithDevice(ifs.controller.DeviceClass()))
	} else {
		panic(fmt.Sprintf("can't extract DeviceClass from non-loopback NIC %d(%s) with nil controller", ifs.nicid, nicInfo.Name))
	}

	ifs.mu.Lock()
	p.SetOnline(ifs.IsUpLocked())
	ifs.mu.Unlock()

	var addrs []interfaces.Address
	for _, a := range nicInfo.ProtocolAddresses {
		if a.Protocol != ipv4.ProtocolNumber && a.Protocol != ipv6.ProtocolNumber {
			continue
		}
		var addr interfaces.Address
		addr.SetAddr(net.Subnet{
			Addr:      fidlconv.ToNetIpAddress(a.AddressWithPrefix.Address),
			PrefixLen: uint8(a.AddressWithPrefix.PrefixLen),
		})
		addr.SetValidUntil(math.MaxInt64)
		for _, p := range addressPatches {
			if p.addr == a.AddressWithPrefix {
				addr.SetValidUntil(p.validUntil.MonotonicNano())
				break
			}
		}
		addrs = append(addrs, addr)
	}
	sort.Slice(addrs, func(i, j int) bool {
		return cmpSubnet(addrs[i].GetAddr(), addrs[j].GetAddr()) <= 0
	})
	p.SetAddresses(addrs)

	return p
}

var _ interfaces.WatcherWithCtx = (*interfaceWatcherImpl)(nil)

type interfaceWatcherImpl struct {
	cancelServe context.CancelFunc
	ready       chan struct{}
	mu          struct {
		sync.Mutex
		isHanging bool
		queue     []interfaces.Event
	}
}

const maxInterfaceWatcherQueueLen = 128

func (wi *interfaceWatcherImpl) onEvent(e interfaces.Event) {
	wi.mu.Lock()
	if len(wi.mu.queue) >= maxInterfaceWatcherQueueLen {
		_ = syslog.WarnTf(watcherProtocolName, "too many unconsumed events (client may not be calling Watch as frequently as possible): %d, max: %d", len(wi.mu.queue), maxInterfaceWatcherQueueLen)
		wi.cancelServe()
	} else {
		wi.mu.queue = append(wi.mu.queue, e)
	}
	queueLen := len(wi.mu.queue)
	isHanging := wi.mu.isHanging
	wi.mu.Unlock()

	if queueLen > 0 && isHanging {
		select {
		case wi.ready <- struct{}{}:
		default:
		}
	}
}

func cmpSubnet(s1 net.Subnet, s2 net.Subnet) int {
	switch s1.Addr.Which() {
	case net.IpAddressIpv4:
		if s2.Addr.Which() == net.IpAddressIpv6 {
			return -1
		}
		if diff := bytes.Compare(s1.Addr.Ipv4.Addr[:], s2.Addr.Ipv4.Addr[:]); diff != 0 {
			return diff
		}
	case net.IpAddressIpv6:
		if s2.Addr.Which() == net.IpAddressIpv4 {
			return 1
		}
		if diff := bytes.Compare(s1.Addr.Ipv6.Addr[:], s2.Addr.Ipv6.Addr[:]); diff != 0 {
			return diff
		}
	}
	if s1.PrefixLen < s2.PrefixLen {
		return -1
	} else if s1.PrefixLen > s2.PrefixLen {
		return 1
	}
	return 0
}

func emptyInterfaceProperties(p interfaces.Properties) bool {
	return !(p.HasId() || p.HasAddresses() || p.HasOnline() || p.HasDeviceClass() || p.HasHasDefaultIpv4Route() || p.HasHasDefaultIpv6Route())
}

// Diff two interface properties. The return value will have no fields present
// if there is no difference. The return value may contain references to fields
// in p2.
func diffInterfaceProperties(p1, p2 interfaces.Properties) interfaces.Properties {
	var diff interfaces.Properties
	if p1.GetOnline() != p2.GetOnline() {
		diff.SetOnline(p2.GetOnline())
	}
	if p1.GetHasDefaultIpv4Route() != p2.GetHasDefaultIpv4Route() {
		diff.SetHasDefaultIpv4Route(p2.GetHasDefaultIpv4Route())
	}
	if p1.GetHasDefaultIpv6Route() != p2.GetHasDefaultIpv6Route() {
		diff.SetHasDefaultIpv6Route(p2.GetHasDefaultIpv6Route())
	}
	if func() bool {
		if len(p2.GetAddresses()) != len(p1.GetAddresses()) {
			return true
		}
		for i, addr := range p1.GetAddresses() {
			p2Addr := p2.GetAddresses()[i]
			if cmpSubnet(addr.GetAddr(), p2Addr.GetAddr()) != 0 {
				return true
			}
			if addr.GetValidUntil() != p2Addr.GetValidUntil() {
				return true
			}
		}
		return false
	}() {
		diff.SetAddresses(p2.GetAddresses())
	}
	if !emptyInterfaceProperties(diff) {
		diff.SetId(p2.GetId())
	}
	return diff
}

func (wi *interfaceWatcherImpl) Watch(ctx fidl.Context) (interfaces.Event, error) {
	wi.mu.Lock()
	defer wi.mu.Unlock()

	if wi.mu.isHanging {
		wi.cancelServe()
		return interfaces.Event{}, errors.New("not allowed to call Watcher.Watch when a call is already pending")
	}

	for {
		if len(wi.mu.queue) > 0 {
			event := wi.mu.queue[0]
			wi.mu.queue = wi.mu.queue[1:]
			return event, nil
		}

		wi.mu.isHanging = true
		wi.mu.Unlock()

		var err error
		select {
		case <-wi.ready:
		case <-ctx.Done():
			err = fmt.Errorf("cancelled: %w", ctx.Err())
		}

		wi.mu.Lock()
		wi.mu.isHanging = false
		if err != nil {
			return interfaces.Event{}, err
		}
	}
}

type interfaceWatcherCollection struct {
	mu struct {
		sync.Mutex
		lastObserved map[tcpip.NICID]interfaces.Properties
		watchers     map[*interfaceWatcherImpl]struct{}
	}
}

func (ns *Netstack) onPropertiesChange(nicid tcpip.NICID, addressPatches []addressPatch) {
	ns.interfaceWatchers.mu.Lock()
	defer ns.interfaceWatchers.mu.Unlock()

	nicInfo, ok := ns.stack.NICInfo()[nicid]
	if !ok {
		_ = syslog.WarnTf(watcherProtocolName, "onPropertiesChange interface %d cannot be found", nicid)
		return
	}

	if properties, ok := ns.interfaceWatchers.mu.lastObserved[nicid]; ok {
		newProperties := interfaceProperties(nicInfo, properties.GetHasDefaultIpv4Route(), properties.GetHasDefaultIpv6Route(), addressPatches)
		if diff := diffInterfaceProperties(properties, newProperties); !emptyInterfaceProperties(diff) {
			ns.interfaceWatchers.mu.lastObserved[nicid] = newProperties
			for w := range ns.interfaceWatchers.mu.watchers {
				w.onEvent(interfaces.EventWithChanged(diff))
			}
		}
	} else {
		_ = syslog.WarnTf(watcherProtocolName, "onPropertiesChange called regarding unknown interface %d", nicid)
	}
}

func (ns *Netstack) onDefaultRouteChange() {
	ns.interfaceWatchers.mu.Lock()
	defer ns.interfaceWatchers.mu.Unlock()

	v4DefaultRoute := make(map[tcpip.NICID]struct{})
	v6DefaultRoute := make(map[tcpip.NICID]struct{})
	for _, er := range ns.GetExtendedRouteTable() {
		if er.Enabled {
			if er.Route.Destination.Equal(header.IPv4EmptySubnet) {
				v4DefaultRoute[er.Route.NIC] = struct{}{}
			} else if er.Route.Destination.Equal(header.IPv6EmptySubnet) {
				v6DefaultRoute[er.Route.NIC] = struct{}{}
			}
		}
	}

	for nicid, properties := range ns.interfaceWatchers.mu.lastObserved {
		var diff interfaces.Properties
		diff.SetId(uint64(nicid))
		if _, ok := v4DefaultRoute[nicid]; ok != properties.GetHasDefaultIpv4Route() {
			properties.SetHasDefaultIpv4Route(ok)
			diff.SetHasDefaultIpv4Route(ok)
		}
		if _, ok := v6DefaultRoute[nicid]; ok != properties.GetHasDefaultIpv6Route() {
			properties.SetHasDefaultIpv6Route(ok)
			diff.SetHasDefaultIpv6Route(ok)
		}
		if diff.HasHasDefaultIpv4Route() || diff.HasHasDefaultIpv6Route() {
			ns.interfaceWatchers.mu.lastObserved[nicid] = properties
			for w := range ns.interfaceWatchers.mu.watchers {
				w.onEvent(interfaces.EventWithChanged(diff))
			}
		}
	}
}

func (ns *Netstack) onInterfaceAdd(nicid tcpip.NICID) {
	ns.interfaceWatchers.mu.Lock()
	defer ns.interfaceWatchers.mu.Unlock()

	if properties, ok := ns.interfaceWatchers.mu.lastObserved[nicid]; ok {
		_ = syslog.WarnTf(watcherProtocolName, "interface added but already known: %+v", properties)
		return
	}

	nicInfo, ok := ns.stack.NICInfo()[nicid]
	if !ok {
		_ = syslog.WarnTf(watcherProtocolName, "interface %d added but not in NICInfo map", nicid)
		return
	}

	var hasDefaultIpv4Route, hasDefaultIpv6Route bool
	for _, er := range ns.GetExtendedRouteTable() {
		if er.Enabled && er.Route.NIC == nicid {
			hasDefaultIpv4Route = hasDefaultIpv4Route || er.Route.Destination.Equal(header.IPv4EmptySubnet)
			hasDefaultIpv6Route = hasDefaultIpv6Route || er.Route.Destination.Equal(header.IPv6EmptySubnet)
		}
	}

	properties := interfaceProperties(nicInfo, hasDefaultIpv4Route, hasDefaultIpv6Route, nil)
	ns.interfaceWatchers.mu.lastObserved[nicid] = properties
	for w := range ns.interfaceWatchers.mu.watchers {
		w.onEvent(interfaces.EventWithAdded(properties))
	}
}

func (c *interfaceWatcherCollection) onInterfaceRemove(nicid tcpip.NICID) {
	c.mu.Lock()
	defer c.mu.Unlock()

	if _, ok := c.mu.lastObserved[nicid]; !ok {
		_ = syslog.WarnTf(watcherProtocolName, "unknown interface removed")
		return
	}
	delete(c.mu.lastObserved, nicid)
	for w := range c.mu.watchers {
		w.onEvent(interfaces.EventWithRemoved(uint64(nicid)))
	}
}

var _ interfaces.StateWithCtx = (*interfaceStateImpl)(nil)

type interfaceStateImpl struct {
	ns *Netstack
}

func (si *interfaceStateImpl) GetWatcher(_ fidl.Context, _ interfaces.WatcherOptions, watcher interfaces.WatcherWithCtxInterfaceRequest) error {
	ctx, cancel := context.WithCancel(context.Background())
	impl := interfaceWatcherImpl{
		ready:       make(chan struct{}, 1),
		cancelServe: cancel,
	}
	impl.mu.queue = make([]interfaces.Event, 0, maxInterfaceWatcherQueueLen)

	si.ns.interfaceWatchers.mu.Lock()

	for _, properties := range si.ns.interfaceWatchers.mu.lastObserved {
		impl.mu.queue = append(impl.mu.queue, interfaces.EventWithExisting(properties))
	}
	impl.mu.queue = append(impl.mu.queue, interfaces.EventWithIdle(interfaces.Empty{}))

	si.ns.interfaceWatchers.mu.watchers[&impl] = struct{}{}
	si.ns.interfaceWatchers.mu.Unlock()

	go func() {
		component.ServeExclusiveConcurrent(ctx, &interfaces.WatcherWithCtxStub{Impl: &impl}, watcher.Channel, func(err error) {
			_ = syslog.WarnTf(watcherProtocolName, "%s", err)
		})

		si.ns.interfaceWatchers.mu.Lock()
		delete(si.ns.interfaceWatchers.mu.watchers, &impl)
		si.ns.interfaceWatchers.mu.Unlock()
	}()

	return nil
}
