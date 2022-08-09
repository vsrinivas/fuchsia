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
	"sort"
	"syscall/zx"
	"syscall/zx/fidl"

	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/fidlconv"
	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/sync"
	"go.fuchsia.dev/fuchsia/src/lib/component"
	syslog "go.fuchsia.dev/fuchsia/src/lib/syslog/go"

	"fidl/fuchsia/net"
	"fidl/fuchsia/net/interfaces"

	"gvisor.dev/gvisor/pkg/tcpip"
	"gvisor.dev/gvisor/pkg/tcpip/header"
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

type addressProperties struct {
	lifetimes stack.AddressLifetimes
	state     stack.AddressAssignmentState
}

// isAddressVisible returns whether an address should be visible to clients.
//
// Returns true iff the address is IPv4 or the address is IPv6 and assigned.
// In particular, IPv6 addresses are not visible to clients while the interface
// they are assigned to is offline, or they are tentative (yet to pass DAD).
func isAddressVisible(protocol tcpip.NetworkProtocolNumber, state stack.AddressAssignmentState) bool {
	switch protocol {
	case header.IPv4ProtocolNumber:
		return true
	case header.IPv6ProtocolNumber:
		switch state {
		case stack.AddressAssigned:
			return true
		case stack.AddressDisabled, stack.AddressTentative:
			return false
		default:
			panic(fmt.Sprintf("unknown address assignment state: %d", state))
		}
	default:
		panic(fmt.Sprintf("unknown protocol number: %d", protocol))
	}
}

type interfaceProperties struct {
	interfaces.Properties
	addresses map[tcpip.ProtocolAddress]addressProperties
}

func infiniteIfZero(t tcpip.MonotonicTime) zx.Time {
	if t == (tcpip.MonotonicTime{}) {
		return zx.TimensecInfinite
	}
	return fidlconv.ToZXTime(t)
}

func interfaceWatcherEventLoop(ctx context.Context, eventChan <-chan interfaceEvent, watcherChan <-chan interfaces.WatcherWithCtxInterfaceRequest) {
	if eventChan == nil {
		panic("cannot start interface watcher event loop with nil interface event channel")
	}
	if watcherChan == nil {
		panic("cannot start interface watcher event loop with nil watcher channel")
	}

	watchers := make(map[*interfaceWatcherImpl]struct{})
	propertiesMap := make(map[tcpip.NICID]interfaceProperties)
	watcherClosedChan := make(chan *interfaceWatcherImpl)

	var cancelled bool
	for {
		select {
		case <-ctx.Done():
			if len(watchers) == 0 {
				return
			}
			cancelled = true
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
				propertiesMap[nicid] = interfaceProperties{
					Properties: added,
					addresses:  make(map[tcpip.ProtocolAddress]addressProperties),
				}
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
			case addressChanged:
				properties, ok := propertiesMap[event.nicid]
				// NB: Address changed events are not ordered wrt interface removal,
				// so this cannot panic unlike some other events which are ordered. Best
				// we can do is emit a warning.
				if !ok {
					_ = syslog.WarnTf(watcherProtocolName, "address changed event for unknown interface: %#v", event)
					break
				}
				newProperties := addressProperties{state: event.state, lifetimes: event.lifetimes}
				if addrProperties, ok := properties.addresses[event.protocolAddr]; ok && addrProperties == newProperties {
					continue
				}
				properties.addresses[event.protocolAddr] = newProperties

				addr := interfaces.Address{}
				addr.SetAddr(fidlconv.ToNetSubnet(event.protocolAddr.AddressWithPrefix))
				addr.SetValidUntil(int64(infiniteIfZero(event.lifetimes.ValidUntil)))
				info := func() interfaces.PreferredLifetimeInfo {
					if event.lifetimes.Deprecated {
						return interfaces.PreferredLifetimeInfoWithDeprecated(interfaces.Empty{})
					} else {
						return interfaces.PreferredLifetimeInfoWithPreferredUntil(int64(infiniteIfZero(event.lifetimes.PreferredUntil)))
					}
				}()
				addr.SetPreferredLifetimeInfo(info)

				// Addresses are sorted by subnet.
				addresses := properties.GetAddresses()
				i := sort.Search(len(addresses), func(i int) bool {
					return cmpSubnet(addr.GetAddr(), addresses[i].GetAddr()) <= 0
				})
				found := i < len(addresses) && cmpSubnet(addr.GetAddr(), addresses[i].GetAddr()) == 0
				var changed bool
				if isAddressVisible(event.protocolAddr.Protocol, event.state) {
					if found {
						if addresses[i] != addr {
							changed = true
							addresses[i] = addr
						}
					} else {
						changed = true
						addresses = append(addresses, interfaces.Address{})
						copy(addresses[i+1:], addresses[i:])
						addresses[i] = addr
					}
				} else {
					if found {
						changed = true
						addresses = append(addresses[:i], addresses[i+1:]...)
					}
				}
				if changed {
					properties.SetAddresses(addresses)

					var changes interfaces.Properties
					changes.SetId(uint64(event.nicid))
					changes.SetAddresses(append([]interfaces.Address(nil), addresses...))
					for w := range watchers {
						w.onEvent(interfaces.EventWithChanged(changes))
					}
				}
				propertiesMap[event.nicid] = properties
			case addressRemoved:
				properties, ok := propertiesMap[event.nicid]
				// NB: Address removed events are not ordered wrt interface removal,
				// so this cannot panic unlike some other events which are ordered. Best
				// we can do is emit a warning.
				if !ok {
					_ = syslog.WarnTf(watcherProtocolName, "address removed event for unknown interface: %#v", event)
					break
				}
				addrProperties, ok := properties.addresses[event.protocolAddr]
				if !ok {
					panic(fmt.Sprintf("address removed event for unknown address: %#v", event))
				}
				delete(properties.addresses, event.protocolAddr)

				if isAddressVisible(event.protocolAddr.Protocol, addrProperties.state) {
					// Addresses are sorted by subnet.
					addresses := properties.GetAddresses()
					subnet := fidlconv.ToNetSubnet(event.protocolAddr.AddressWithPrefix)
					i := sort.Search(len(addresses), func(i int) bool {
						return cmpSubnet(subnet, addresses[i].GetAddr()) <= 0
					})
					if i == len(addresses) || cmpSubnet(subnet, addresses[i].GetAddr()) != 0 {
						panic(fmt.Sprintf("address visible to clients removed but cannot be found: %#v", event))
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
				}
			}
		case watcher := <-watcherChan:
			watcherCtx, cancel := context.WithCancel(ctx)
			impl := interfaceWatcherImpl{
				ready:       make(chan struct{}, 1),
				cancelServe: cancel,
			}
			impl.mu.queue = make([]interfaces.Event, 0, maxInterfaceWatcherQueueLen)

			for _, properties := range propertiesMap {
				properties := properties.Properties
				// Must make a deep copy of the addresses so that updates to the slice
				// don't accidentally change the event added to the queue.
				properties.SetAddresses(append([]interfaces.Address(nil), properties.GetAddresses()...))
				impl.mu.queue = append(impl.mu.queue, interfaces.EventWithExisting(properties))
			}
			impl.mu.queue = append(impl.mu.queue, interfaces.EventWithIdle(interfaces.Empty{}))

			watchers[&impl] = struct{}{}

			go func() {
				component.Serve(watcherCtx, &interfaces.WatcherWithCtxStub{Impl: &impl}, watcher.Channel, component.ServeOptions{
					Concurrent: true,
					OnError: func(err error) {
						_ = syslog.WarnTf(watcherProtocolName, "%s", err)
					},
				})

				watcherClosedChan <- &impl
			}()
		case watcherClosed := <-watcherClosedChan:
			delete(watchers, watcherClosed)
			if len(watchers) == 0 && cancelled {
				return
			}
		}
	}
}
