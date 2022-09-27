// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//go:build !build_with_native_toolchain

package netstack

import (
	"bytes"
	"context"
	"errors"
	"fmt"
	"sort"
	"sync/atomic"
	"syscall/zx"
	"syscall/zx/fidl"

	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/sync"
	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/time"
	"go.fuchsia.dev/fuchsia/src/lib/component"
	syslog "go.fuchsia.dev/fuchsia/src/lib/syslog/go"

	"fidl/fuchsia/net"
	"fidl/fuchsia/net/interfaces"

	"gvisor.dev/gvisor/pkg/tcpip"
	"gvisor.dev/gvisor/pkg/tcpip/stack"
)

const watcherProtocolName = "fuchsia.net.interfaces/Watcher"

func initialProperties(ifs *ifState, name string) interfaces.Properties {
	var p interfaces.Properties

	p.SetId(uint64(ifs.nicid))
	p.SetName(name)
	if ifs.endpoint.Capabilities()&stack.CapabilityLoopback != 0 {
		p.SetDeviceClass(interfaces.DeviceClassWithLoopback(interfaces.Empty{}))
	} else if ifs.controller != nil {
		p.SetDeviceClass(interfaces.DeviceClassWithDevice(ifs.controller.DeviceClass()))
	} else {
		panic(fmt.Sprintf("can't extract DeviceClass from non-loopback NIC %d(%s) with nil controller", ifs.nicid, name))
	}

	p.SetOnline(false)
	p.SetHasDefaultIpv4Route(false)
	p.SetHasDefaultIpv6Route(false)
	p.SetAddresses([]interfaces.Address{})

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
		_ = syslog.ErrorTf(watcherProtocolName, "too many unconsumed events (client may not be calling Watch as frequently as possible): %d, max: %d", len(wi.mu.queue), maxInterfaceWatcherQueueLen)
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

func cmpSubnet(ifAddr1 net.Subnet, ifAddr2 net.Subnet) int {
	switch ifAddr1.Addr.Which() {
	case net.IpAddressIpv4:
		if ifAddr2.Addr.Which() == net.IpAddressIpv6 {
			return -1
		}
		if diff := bytes.Compare(ifAddr1.Addr.Ipv4.Addr[:], ifAddr2.Addr.Ipv4.Addr[:]); diff != 0 {
			return diff
		}
	case net.IpAddressIpv6:
		if ifAddr2.Addr.Which() == net.IpAddressIpv4 {
			return 1
		}
		if diff := bytes.Compare(ifAddr1.Addr.Ipv6.Addr[:], ifAddr2.Addr.Ipv6.Addr[:]); diff != 0 {
			return diff
		}
	}
	if ifAddr1.PrefixLen < ifAddr2.PrefixLen {
		return -1
	} else if ifAddr1.PrefixLen > ifAddr2.PrefixLen {
		return 1
	}
	return 0
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

var _ interfaces.StateWithCtx = (*interfaceStateImpl)(nil)

type interfaceStateImpl struct {
	watcherChan chan<- interfaces.WatcherWithCtxInterfaceRequest
}

func (si *interfaceStateImpl) GetWatcher(_ fidl.Context, _ interfaces.WatcherOptions, watcher interfaces.WatcherWithCtxInterfaceRequest) error {
	si.watcherChan <- watcher
	return nil
}

type interfaceEvent interface {
	isInterfaceEvent()
}

type interfaceAdded interfaces.Properties

var _ interfaceEvent = (*interfaceAdded)(nil)

func (interfaceAdded) isInterfaceEvent() {}

type interfaceRemoved tcpip.NICID

var _ interfaceEvent = (*interfaceRemoved)(nil)

func (interfaceRemoved) isInterfaceEvent() {}

type onlineChanged struct {
	nicid  tcpip.NICID
	online bool
}

var _ interfaceEvent = (*onlineChanged)(nil)

func (onlineChanged) isInterfaceEvent() {}

type defaultRouteChanged struct {
	nicid               tcpip.NICID
	hasDefaultIPv4Route *bool
	hasDefaultIPv6Route *bool
}

var _ interfaceEvent = (*defaultRouteChanged)(nil)

func (defaultRouteChanged) isInterfaceEvent() {}

type addressAdded struct {
	nicid  tcpip.NICID
	subnet net.Subnet
	// TODO(https://fxbug.dev/97731): Remove this once address assignment
	// state is tracked.
	//
	// If true, receiver should panic if this event contains an address that
	// already exists.
	strict bool
}

var _ interfaceEvent = (*addressAdded)(nil)

func (addressAdded) isInterfaceEvent() {}

type addressRemoved struct {
	nicid  tcpip.NICID
	subnet net.Subnet
	// TODO(https://fxbug.dev/97731): Remove this once address assignment
	// state is tracked.
	//
	// If true, receiver should panic if this event contains an address that
	// doesn't exist.
	strict bool
}

var _ interfaceEvent = (*addressRemoved)(nil)

func (addressRemoved) isInterfaceEvent() {}

type validUntilChanged struct {
	nicid      tcpip.NICID
	subnet     net.Subnet
	validUntil time.Time
}

var _ interfaceEvent = (*validUntilChanged)(nil)

func (validUntilChanged) isInterfaceEvent() {}

type fidlInterfaceWatcherStats struct {
	count atomic.Int64
}

func interfaceWatcherEventLoop(
	eventChan <-chan interfaceEvent,
	watcherChan <-chan interfaces.WatcherWithCtxInterfaceRequest,
	fidlInterfaceWatcherStats *fidlInterfaceWatcherStats,
) {
	watchers := make(map[*interfaceWatcherImpl]struct{})
	propertiesMap := make(map[tcpip.NICID]interfaces.Properties)
	watcherClosedChan := make(chan *interfaceWatcherImpl)

	for {
		select {
		case e := <-eventChan:
			switch event := e.(type) {
			case interfaceAdded:
				added := interfaces.Properties(event)
				if !added.HasId() {
					panic(fmt.Sprintf("interface added event with no ID: %#v", event))
				}
				nicid := tcpip.NICID(added.GetId())
				if properties, ok := propertiesMap[nicid]; ok {
					panic(fmt.Sprintf("interface %#v already exists but duplicate added event received: %#v", properties, event))
				}
				propertiesMap[nicid] = added
				// Must make a deep copy of the addresses so that updates to the slice
				// don't accidentally change the event added to the queue.
				added.SetAddresses(append([]interfaces.Address(nil), added.GetAddresses()...))
				for w := range watchers {
					w.onEvent(interfaces.EventWithAdded(added))
				}
			case interfaceRemoved:
				removed := tcpip.NICID(event)
				if _, ok := propertiesMap[removed]; !ok {
					panic(fmt.Sprintf("unknown interface NIC=%d removed", removed))
					continue
				}
				delete(propertiesMap, removed)
				for w := range watchers {
					w.onEvent(interfaces.EventWithRemoved(uint64(removed)))
				}
			case defaultRouteChanged:
				properties, ok := propertiesMap[event.nicid]
				// TODO(https://fxbug.dev/95468): Change to panic once interface properties
				// are guaranteed to not change after an interface is removed.
				if !ok {
					_ = syslog.WarnTf(watcherProtocolName, "default route changed event for unknown interface: %#v", event)
					break
				}
				// TODO(https://fxbug.dev/95574): Once these events are only emitted when
				// the presence of a default route has actually changed, panic if the event
				// disagrees with our view of the world.
				var changes interfaces.Properties
				if event.hasDefaultIPv4Route != nil && properties.GetHasDefaultIpv4Route() != *event.hasDefaultIPv4Route {
					properties.SetHasDefaultIpv4Route(*event.hasDefaultIPv4Route)
					changes.SetHasDefaultIpv4Route(*event.hasDefaultIPv4Route)
				}
				if event.hasDefaultIPv6Route != nil && properties.GetHasDefaultIpv6Route() != *event.hasDefaultIPv6Route {
					properties.SetHasDefaultIpv6Route(*event.hasDefaultIPv6Route)
					changes.SetHasDefaultIpv6Route(*event.hasDefaultIPv6Route)
				}
				if changes.HasHasDefaultIpv4Route() || changes.HasHasDefaultIpv6Route() {
					propertiesMap[event.nicid] = properties
					changes.SetId(uint64(event.nicid))
					for w := range watchers {
						w.onEvent(interfaces.EventWithChanged(changes))
					}
				}
			case onlineChanged:
				properties, ok := propertiesMap[event.nicid]
				// TODO(https://fxbug.dev/95468): Change to panic once interface properties
				// are guaranteed to not change after an interface is removed.
				if !ok {
					_ = syslog.WarnTf(watcherProtocolName, "online changed event for unknown interface: %#v", event)
					break
				}
				if event.online == properties.GetOnline() {
					// This assertion is possible because the event is always emitted under a
					// lock (so cannot race against itself), and the event is only emitted when
					// there is an actual change to the boolean value.
					panic(fmt.Sprintf("online changed event for interface with properties %#v with no actual change", properties))
				}

				properties.SetOnline(event.online)
				propertiesMap[event.nicid] = properties

				var changes interfaces.Properties
				changes.SetId(uint64(event.nicid))
				changes.SetOnline(event.online)
				for w := range watchers {
					w.onEvent(interfaces.EventWithChanged(changes))
				}
			case addressAdded:
				properties, ok := propertiesMap[event.nicid]
				// TODO(https://fxbug.dev/95468): Change to panic once interface properties
				// are guaranteed to not change after an interface is removed.
				if !ok {
					_ = syslog.WarnTf(watcherProtocolName, "address added event for unknown interface: %#v", event)
					break
				}
				addresses := properties.GetAddresses()
				// Addresses are sorted by subnet.
				i := sort.Search(len(addresses), func(i int) bool {
					return cmpSubnet(event.subnet, addresses[i].GetAddr()) <= 0
				})
				if i < len(addresses) && cmpSubnet(event.subnet, addresses[i].GetAddr()) == 0 {
					// TODO(https://fxbug.dev/97731): Panic if we receive duplicate DAD success
					// within the same link once address assignment state is tracked.
					if event.strict {
						panic(fmt.Sprintf("duplicate address added event: %#v", event))
					} else {
						_ = syslog.WarnTf(watcherProtocolName, "address added event for already-assigned address: %#v", event)
						break
					}
				}
				addresses = append(addresses, interfaces.Address{})
				copy(addresses[i+1:], addresses[i:])
				newAddr := &addresses[i]
				newAddr.SetAddr(event.subnet)
				newAddr.SetValidUntil(int64(zx.TimensecInfinite))
				properties.SetAddresses(addresses)
				propertiesMap[event.nicid] = properties

				var changes interfaces.Properties
				changes.SetId(uint64(event.nicid))
				changes.SetAddresses(append([]interfaces.Address(nil), addresses...))
				for w := range watchers {
					w.onEvent(interfaces.EventWithChanged(changes))
				}
			case addressRemoved:
				properties, ok := propertiesMap[event.nicid]
				// TODO(https://fxbug.dev/95468): Change to panic once interface properties
				// are guaranteed to not change after an interface is removed.
				if !ok {
					_ = syslog.WarnTf(watcherProtocolName, "address removed event for unknown interface: %#v", event)
					break
				}
				addresses := properties.GetAddresses()
				// Addresses are sorted by subnet.
				i := sort.Search(len(addresses), func(i int) bool {
					return cmpSubnet(event.subnet, addresses[i].GetAddr()) <= 0
				})
				if i == len(addresses) || cmpSubnet(event.subnet, addresses[i].GetAddr()) != 0 {
					// TODO(https://fxbug.dev/97731): Panic when the address being removed
					// isn't assigned or tentative when `event.strict` is true.
					_ = syslog.WarnTf(watcherProtocolName, "address removed event for non-existent address: %#v", event)
					break
				}
				addresses = append(addresses[:i], addresses[i+1:]...)
				properties.SetAddresses(addresses)
				propertiesMap[event.nicid] = properties

				var changes interfaces.Properties
				changes.SetId(uint64(event.nicid))
				changes.SetAddresses(append([]interfaces.Address(nil), addresses...))
				for w := range watchers {
					w.onEvent(interfaces.EventWithChanged(changes))
				}
			case validUntilChanged:
				properties, ok := propertiesMap[event.nicid]
				// TODO(https://fxbug.dev/95468): Change to panic once interface properties
				// are guaranteed to not change after an interface is removed.
				if !ok {
					_ = syslog.WarnTf(watcherProtocolName, "address validUntil changed event for unknown interface: %#v", event)
					break
				}
				addresses := properties.GetAddresses()
				// Addresses are sorted by subnet.
				i := sort.Search(len(addresses), func(i int) bool {
					return cmpSubnet(event.subnet, addresses[i].GetAddr()) <= 0
				})
				if i == len(addresses) || cmpSubnet(event.subnet, addresses[i].GetAddr()) != 0 {
					// TODO(https://fxbug.dev/96130): Change this to panic once DHCPv4 client
					// is guaranteed to not send this event if the address is missing.
					_ = syslog.ErrorTf(watcherProtocolName, "validUntil changed event for non-existent address: %#v", event)
					break
				}

				if time.Monotonic(addresses[i].GetValidUntil()) != event.validUntil {
					addresses[i].SetValidUntil(event.validUntil.MonotonicNano())

					var changes interfaces.Properties
					changes.SetId(uint64(event.nicid))
					changes.SetAddresses(append([]interfaces.Address(nil), addresses...))
					for w := range watchers {
						w.onEvent(interfaces.EventWithChanged(changes))
					}
				}
			}
		case watcher := <-watcherChan:
			ctx, cancel := context.WithCancel(context.Background())
			impl := interfaceWatcherImpl{
				ready:       make(chan struct{}, 1),
				cancelServe: cancel,
			}
			impl.mu.queue = make([]interfaces.Event, 0, maxInterfaceWatcherQueueLen)

			for _, properties := range propertiesMap {
				// Must make a deep copy of the addresses so that updates to the slice
				// don't accidentally change the event added to the queue.
				properties.SetAddresses(append([]interfaces.Address(nil), properties.GetAddresses()...))
				impl.mu.queue = append(impl.mu.queue, interfaces.EventWithExisting(properties))
			}
			impl.mu.queue = append(impl.mu.queue, interfaces.EventWithIdle(interfaces.Empty{}))

			watchers[&impl] = struct{}{}
			fidlInterfaceWatcherStats.count.Add(1)

			go func() {
				component.Serve(ctx, &interfaces.WatcherWithCtxStub{Impl: &impl}, watcher.Channel, component.ServeOptions{
					Concurrent: true,
					OnError: func(err error) {
						_ = syslog.WarnTf(watcherProtocolName, "%s", err)
					},
				})

				watcherClosedChan <- &impl
			}()
		case watcherClosed := <-watcherClosedChan:
			delete(watchers, watcherClosed)
			fidlInterfaceWatcherStats.count.Add(-1)
		}
	}
}
