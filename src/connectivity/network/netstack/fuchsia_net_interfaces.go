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

	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/fidlconv"
	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/sync"
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

func hasAllSecondaryProperties(addr interfaces.Address) bool {
	return addr.HasValidUntil() && addr.HasPreferredLifetimeInfo()
}

var _ interfaces.WatcherWithCtx = (*interfaceWatcherImpl)(nil)

type interfaceWatcherImpl struct {
	cancelServe  context.CancelFunc
	ready        chan struct{}
	addrInterest interfaces.AddressPropertiesInterest
	mu           struct {
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

// filterAddressProperties returns a list of addresses with disinterested
// properties cleared.
//
// Does not modify addresses and returns a new slice.
func (wi *interfaceWatcherImpl) filterAddressProperties(addresses []interfaces.Address) []interfaces.Address {
	rtn := append([]interfaces.Address(nil), addresses...)
	clearValidUntil := !wi.addrInterest.HasBits(interfaces.AddressPropertiesInterestValidUntil)
	clearPreferredLifetimeInfo := !wi.addrInterest.HasBits(interfaces.AddressPropertiesInterestPreferredLifetimeInfo)
	if !clearValidUntil && !clearPreferredLifetimeInfo {
		return rtn
	}
	for i := range rtn {
		addr := &rtn[i]
		if clearValidUntil {
			addr.ClearValidUntil()
		}
		if clearPreferredLifetimeInfo {
			addr.ClearPreferredLifetimeInfo()
		}
	}
	return rtn
}

// onAddressesChanged handles an address for this watcher client.
//
// Does not modify addresses.
func (wi *interfaceWatcherImpl) onAddressesChanged(nicid tcpip.NICID, addresses []interfaces.Address, removedOrAdded bool, propertiesChanged interfaces.AddressPropertiesInterest) {
	if !removedOrAdded && (propertiesChanged&wi.addrInterest == 0) {
		return
	}
	var changed interfaces.Properties
	changed.SetId(uint64(nicid))
	changed.SetAddresses(wi.filterAddressProperties(addresses))
	wi.onEvent(interfaces.EventWithChanged(changed))
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
			wi.mu.queue[0] = interfaces.Event{}
			wi.mu.queue = wi.mu.queue[1:]
			if len(wi.mu.queue) == 0 {
				// Drop the whole slice so that the backing array can be garbage
				// collected. Otherwise, the now-inaccessible front of wi.mu.queue could
				// be retained in memory forever.
				wi.mu.queue = nil
			}
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

type interfaceWatcherRequest struct {
	req     interfaces.WatcherWithCtxInterfaceRequest
	options interfaces.WatcherOptions
}

var _ interfaces.StateWithCtx = (*interfaceStateImpl)(nil)

type interfaceStateImpl struct {
	watcherChan chan<- interfaceWatcherRequest
}

func (si *interfaceStateImpl) GetWatcher(_ fidl.Context, options interfaces.WatcherOptions, watcher interfaces.WatcherWithCtxInterfaceRequest) error {
	si.watcherChan <- interfaceWatcherRequest{
		req:     watcher,
		options: options,
	}
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

func diffAddressProperties(properties1, properties2 addressProperties) interfaces.AddressPropertiesInterest {
	var bitflag interfaces.AddressPropertiesInterest
	if properties1.lifetimes.ValidUntil != properties2.lifetimes.ValidUntil {
		bitflag |= interfaces.AddressPropertiesInterestValidUntil
	}
	if properties1.lifetimes.Deprecated != properties2.lifetimes.Deprecated ||
		properties1.lifetimes.PreferredUntil != properties2.lifetimes.PreferredUntil {
		bitflag |= interfaces.AddressPropertiesInterestPreferredLifetimeInfo
	}
	return bitflag
}

// isAddressVisible returns whether an address should be visible to clients.
//
// Returns true iff the address is IPv4 or the address is IPv6 and assigned.
// In particular, IPv6 addresses are not visible to clients while the interface
// they are assigned to is offline, or they are tentative (yet to pass DAD).
//
// TODO(https://fxbug.dev/113056): Expose the address assignment state via
// the API directly instead of hiding addresses in disabled/tentative state.
func isAddressVisible(state stack.AddressAssignmentState) bool {
	switch state {
	case stack.AddressAssigned:
		return true
	case stack.AddressDisabled, stack.AddressTentative:
		return false
	default:
		panic(fmt.Sprintf("unknown address assignment state: %d", state))
	}
}

type interfaceProperties struct {
	interfaces.Properties
	// addresses stores address properties that come from the gVisor stack.
	//
	// It is necessary to track these properties separately because IPv6
	// addresses in disabled or tentative state are hidden from clients
	// so such addresses are not present in the embedded Properties and
	// need to have their properties stored here.
	addresses map[tcpip.ProtocolAddress]addressProperties
}

func addressMapToSlice(addressMap map[tcpip.ProtocolAddress]addressProperties) []interfaces.Address {
	var addressSlice []interfaces.Address
	for protocolAddr, properties := range addressMap {
		if isAddressVisible(properties.state) {
			var addr interfaces.Address
			addr.SetAddr(fidlconv.ToNetSubnet(protocolAddr.AddressWithPrefix))
			addr.SetValidUntil(int64(toZxTimeInfiniteIfZero(properties.lifetimes.ValidUntil)))
			info := func() interfaces.PreferredLifetimeInfo {
				if properties.lifetimes.Deprecated {
					return interfaces.PreferredLifetimeInfoWithDeprecated(interfaces.Empty{})
				} else {
					return interfaces.PreferredLifetimeInfoWithPreferredUntil(int64(toZxTimeInfiniteIfZero(properties.lifetimes.PreferredUntil)))
				}
			}()
			addr.SetPreferredLifetimeInfo(info)

			addressSlice = append(addressSlice, addr)
		}
	}

	sort.Slice(addressSlice, func(i, j int) bool {
		return cmpSubnet(addressSlice[i].GetAddr(), addressSlice[j].GetAddr()) <= 0
	})
	return addressSlice
}

func toZxTimeInfiniteIfZero(t tcpip.MonotonicTime) zx.Time {
	if t == (tcpip.MonotonicTime{}) {
		return zx.TimensecInfinite
	}
	return fidlconv.ToZxTime(t)
}

type fidlInterfaceWatcherStats struct {
	count atomic.Int64
}

func interfaceWatcherEventLoop(
	ctx context.Context,
	eventChan <-chan interfaceEvent,
	watcherChan <-chan interfaceWatcherRequest,
	fidlInterfaceWatcherStats *fidlInterfaceWatcherStats,
) {
	if eventChan == nil {
		panic("cannot start interface watcher event loop with nil interface event channel")
	}
	if watcherChan == nil {
		panic("cannot start interface watcher event loop with nil watcher channel")
	}

	watchers := make(map[*interfaceWatcherImpl]struct{})
	propertiesMap := make(map[tcpip.NICID]interfaceProperties)
	watcherClosedChan := make(chan *interfaceWatcherImpl)
	watcherClosedFn := func(closedWatcher *interfaceWatcherImpl) {
		delete(watchers, closedWatcher)
		fidlInterfaceWatcherStats.count.Add(-1)
	}

	for {
		select {
		case <-ctx.Done():
			_ = syslog.InfoTf(watcherProtocolName, "stopping interface watcher event loop")

			// Wait for all watchers to close so that it is guaranteed that no
			// goroutines serving the interface watcher API are still running once
			// this function returns.
			for len(watchers) > 0 {
				watcherClosedFn(<-watcherClosedChan)
			}
			return
		case e := <-eventChan:
			switch event := e.(type) {
			case interfaceAdded:
				added := interfaces.Properties(event)
				if !added.HasId() {
					panic(fmt.Sprintf("interface added event with no ID: %#v", event))
				}
				if len(added.GetAddresses()) > 0 {
					// This panic enforces that interfaces are never added
					// with addresses present, which enables the event loop to
					// not have to worry about address properties/assignment
					// state when handling interface-added events.
					panic(fmt.Sprintf("interface added event contains addresses: %#v", event))
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
					properties := added
					// Since added interfaces must not have any addresses, explicitly set
					// the addresses field to nil instead of potentially copying a slice
					// of length 0.
					properties.SetAddresses(nil)
					w.onEvent(interfaces.EventWithAdded(properties))
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
				if !ok {
					panic(fmt.Sprintf("address changed event for unknown interface: %#v", event))
				}
				nextProperties := addressProperties{state: event.state, lifetimes: event.lifetimes}
				prevProperties, found := properties.addresses[event.protocolAddr]
				properties.addresses[event.protocolAddr] = nextProperties
				addresses := addressMapToSlice(properties.addresses)
				properties.SetAddresses(addresses)
				propertiesMap[event.nicid] = properties

				propertyChangedBitflag, addedOrRemoved := func() (interfaces.AddressPropertiesInterest, bool) {
					nextVisible := isAddressVisible(event.state)
					if found {
						if prevProperties == nextProperties {
							return interfaces.AddressPropertiesInterest(0), false
						}
						if prevVisible := isAddressVisible(prevProperties.state); prevVisible != nextVisible {
							return interfaces.AddressPropertiesInterest(0), true
						} else {
							return diffAddressProperties(prevProperties, nextProperties), false
						}
					} else {
						return interfaces.AddressPropertiesInterest(0), isAddressVisible(event.state)
					}
				}()
				if propertyChangedBitflag == 0 && !addedOrRemoved {
					break
				}
				for w := range watchers {
					w.onAddressesChanged(event.nicid, addresses, addedOrRemoved, propertyChangedBitflag)
				}
			case addressRemoved:
				properties, ok := propertiesMap[event.nicid]
				if !ok {
					panic(fmt.Sprintf("address removed event for unknown interface: %#v", event))
				}
				addrProperties, ok := properties.addresses[event.protocolAddr]
				if !ok {
					panic(fmt.Sprintf("address removed event for unknown address: %#v", event))
				}
				delete(properties.addresses, event.protocolAddr)
				addresses := addressMapToSlice(properties.addresses)
				properties.SetAddresses(addresses)
				propertiesMap[event.nicid] = properties

				if !isAddressVisible(addrProperties.state) {
					break
				}
				for w := range watchers {
					w.onAddressesChanged(event.nicid, addresses, true /* addedOrRemoved */, interfaces.AddressPropertiesInterest(0))
				}
			}
		case watcher := <-watcherChan:
			watcherCtx, cancel := context.WithCancel(ctx)
			impl := interfaceWatcherImpl{
				ready:        make(chan struct{}, 1),
				cancelServe:  cancel,
				addrInterest: watcher.options.GetAddressPropertiesInterestWithDefault(0),
			}
			impl.mu.queue = make([]interfaces.Event, 0, maxInterfaceWatcherQueueLen)

			for _, properties := range propertiesMap {
				// Filtering address properties returns a deep copy of the
				// addresses so that updates to the current interface state
				// don't accidentally change enqueued events.
				properties := properties.Properties
				properties.SetAddresses(impl.filterAddressProperties(properties.GetAddresses()))
				impl.onEvent(interfaces.EventWithExisting(properties))
			}
			impl.mu.queue = append(impl.mu.queue, interfaces.EventWithIdle(interfaces.Empty{}))

			watchers[&impl] = struct{}{}
			fidlInterfaceWatcherStats.count.Add(1)

			go func() {
				defer cancel()
				component.Serve(watcherCtx, &interfaces.WatcherWithCtxStub{Impl: &impl}, watcher.req.Channel, component.ServeOptions{
					Concurrent: true,
					OnError: func(err error) {
						_ = syslog.WarnTf(watcherProtocolName, "%s", err)
					},
				})

				watcherClosedChan <- &impl
			}()
		case watcherClosed := <-watcherClosedChan:
			watcherClosedFn(watcherClosed)
		}
	}
}
