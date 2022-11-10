// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//go:build !build_with_native_toolchain

package netstack

import (
	"context"
	"fmt"
	"time"

	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/routes"
	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/sync"
	syslog "go.fuchsia.dev/fuchsia/src/lib/syslog/go"

	"gvisor.dev/gvisor/pkg/tcpip"
	"gvisor.dev/gvisor/pkg/tcpip/header"
	"gvisor.dev/gvisor/pkg/tcpip/network/ipv6"
	"gvisor.dev/gvisor/pkg/tcpip/stack"
)

const (
	// staticRouteAvoidingLifeCycleHooks is the dynamic flag when adding a
	// new route in response to an NDP discovery event.
	//
	// routes are added as a 'static' route because the integrator (Netstack)
	// removes all dynamic routes on DHCPv4 related changes. Routes must be
	// 'static' to escape Netstack's DHCP-learned routes' lifecycle management
	// hooks that 'dynamic' routes will be affected by.
	//
	// TODO(fxbug.dev/43503): Instead of adding routes as static, support a type
	// of dynamic route specifically for NDP.
	staticRouteAvoidingLifeCycleHooks = false

	ndpSyslogTagName = "ndp"
)

// ndpEvent is a marker interface used to improve type safety in ndpDispatcher.
type ndpEvent interface {
	isNDPEvent()
}

type ndpDuplicateAddressDetectionEvent struct {
	nicID  tcpip.NICID
	addr   tcpip.Address
	result stack.DADResult
}

func (*ndpDuplicateAddressDetectionEvent) isNDPEvent() {}

type ndpOffLinkRouteEventCommon struct {
	nicID  tcpip.NICID
	dest   tcpip.Subnet
	router tcpip.Address
}

func (*ndpOffLinkRouteEventCommon) isNDPEvent() {}

type ndpDiscoveredOffLinkRouteEvent struct {
	ndpOffLinkRouteEventCommon
	prf header.NDPRoutePreference
}

type ndpInvalidatedOffLinkRouteEvent struct {
	ndpOffLinkRouteEventCommon
}

type ndpPrefixEventCommon struct {
	nicID  tcpip.NICID
	prefix tcpip.Subnet
}

func (*ndpPrefixEventCommon) isNDPEvent() {}

type ndpDiscoveredPrefixEvent struct {
	ndpPrefixEventCommon
}
type ndpInvalidatedPrefixEvent struct {
	ndpPrefixEventCommon
}

type ndpAutoGenAddrEventCommon struct {
	nicID          tcpip.NICID
	addrWithPrefix tcpip.AddressWithPrefix
}

func (*ndpAutoGenAddrEventCommon) isNDPEvent() {}

type ndpGeneratedAutoGenAddrEvent struct {
	ndpAutoGenAddrEventCommon
}
type ndpInvalidatedAutoGenAddrEvent struct {
	ndpAutoGenAddrEventCommon
}

type ndpRecursiveDNSServerEvent struct {
	nicID    tcpip.NICID
	addrs    []tcpip.Address
	lifetime time.Duration
}

// isNDPEvent implements ndpEvent.isNDPEvent.
func (*ndpRecursiveDNSServerEvent) isNDPEvent() {}

var _ ipv6.NDPDispatcher = (*ndpDispatcher)(nil)

// ndpDispatcher is a type that implements ipv6.NDPDispatcher to handle the
// discovery and invaldiation of default routers, on-link prefixes and
// auto-generated addresses; and reception of recursive DNS server lists for
// IPv6.
//
// ndpDispatcher employs a worker goroutine (see ndpDispatcher.start), that
// will handle the events. The event handlers themselves will add the events to
// a queue to be handled by the goroutine. This is done so that ordering can be
// guaranteed between events that share some relationship (e.g. a router's
// invalidation must happen after its discovery). Without this enforcement, we
// could (in theory) complete handling an invalidation event before its
// respective discovery event (even though we will receive the discovery event
// before the invalidation event (this is enforced by the Stack)).
type ndpDispatcher struct {
	// ns MUST be assigned before calling ndpDispatcher.start and it must
	// never be modified after being assigned.
	ns *Netstack

	// Used to ensure that only a finite number of goroutines may be
	// permitted to run at a time.
	sem chan struct{}

	// notifyCh is used to signal the worker goroutine that a new event is
	// available.
	notifyCh chan struct{}

	// testNotifyCh is used to signal tests when events is empty.
	//
	// testNotifyCh should only be set by tests.
	testNotifyCh chan struct{}

	// dynamicAddressSourceTracker tracks the most recent dynamic address
	// configuration options available for an interface.
	dynamicAddressSourceTracker ipv6AddressConfigTracker

	// getAddressPrefix is a hook to retrieve a prefix for an address from the
	// NICInfo. It is abstracted out for testing purposes.
	getAddressPrefix func(info *stack.NICInfo, addr tcpip.Address) (int, bool)

	mu struct {
		sync.Mutex

		// events holds a queue of events that need to be handled by the
		// worker goroutine. We use a slice instead of a channel so that
		// we can guarantee that event handlers do not block trying to
		// write to events if it is full.
		// TODO(ghanan): use the ilist pkg from gvisor/pkg/ilist
		events []ndpEvent
	}
}

// OnDuplicateAddressDetectionResult implements ipv6.NDPDispatcher.
func (n *ndpDispatcher) OnDuplicateAddressDetectionResult(nicID tcpip.NICID, addr tcpip.Address, result stack.DADResult) {
	_ = syslog.VLogTf(syslog.DebugVerbosity, ndpSyslogTagName, "OnDuplicateAddressDetectionStatus(%d, %s, %#v)", nicID, addr, result)
	n.addEvent(&ndpDuplicateAddressDetectionEvent{
		nicID:  nicID,
		addr:   addr,
		result: result,
	})
}

// OnOffLinkRouteUpdated implements ipv6.NDPDispatcher.
func (n *ndpDispatcher) OnOffLinkRouteUpdated(nicID tcpip.NICID, dest tcpip.Subnet, router tcpip.Address, prf header.NDPRoutePreference) {
	_ = syslog.VLogTf(syslog.DebugVerbosity, ndpSyslogTagName, "OnOffLinkRouteUpdated(%d, %s, %s, %s)", nicID, dest, router, prf)
	n.addEvent(&ndpDiscoveredOffLinkRouteEvent{
		ndpOffLinkRouteEventCommon: ndpOffLinkRouteEventCommon{
			nicID:  nicID,
			dest:   dest,
			router: router,
		},
		prf: prf,
	})
}

// OnOffLinkRouteInvalidated implements ipv6.NDPDispatcher.
func (n *ndpDispatcher) OnOffLinkRouteInvalidated(nicID tcpip.NICID, dest tcpip.Subnet, router tcpip.Address) {
	_ = syslog.VLogTf(syslog.DebugVerbosity, ndpSyslogTagName, "OnOffLinkRouteInvalidated(%d, %s, %s)", nicID, dest, router)
	n.addEvent(&ndpInvalidatedOffLinkRouteEvent{ndpOffLinkRouteEventCommon: ndpOffLinkRouteEventCommon{
		nicID:  nicID,
		dest:   dest,
		router: router,
	}})
}

// OnOnLinkPrefixDiscovered implements ipv6.NDPDispatcher.
func (n *ndpDispatcher) OnOnLinkPrefixDiscovered(nicID tcpip.NICID, prefix tcpip.Subnet) {
	_ = syslog.VLogTf(syslog.DebugVerbosity, ndpSyslogTagName, "OnOnLinkPrefixDiscovered(%d, %s)", nicID, prefix)
	n.addEvent(&ndpDiscoveredPrefixEvent{ndpPrefixEventCommon: ndpPrefixEventCommon{nicID: nicID, prefix: prefix}})
}

// OnOnLinkPrefixInvalidated implements ipv6.NDPDispatcher.
func (n *ndpDispatcher) OnOnLinkPrefixInvalidated(nicID tcpip.NICID, prefix tcpip.Subnet) {
	_ = syslog.VLogTf(syslog.DebugVerbosity, ndpSyslogTagName, "OnOnLinkPrefixInvalidated(%d, %s)", nicID, prefix)
	n.addEvent(&ndpInvalidatedPrefixEvent{ndpPrefixEventCommon: ndpPrefixEventCommon{nicID: nicID, prefix: prefix}})
}

// OnAutoGenAddress implements ipv6.NDPDispatcher.
func (n *ndpDispatcher) OnAutoGenAddress(nicID tcpip.NICID, addrWithPrefix tcpip.AddressWithPrefix) stack.AddressDispatcher {
	_ = syslog.VLogTf(syslog.DebugVerbosity, ndpSyslogTagName, "OnAutoGenAddress(%d, %s)", nicID, addrWithPrefix)
	n.addEvent(&ndpGeneratedAutoGenAddrEvent{ndpAutoGenAddrEventCommon: ndpAutoGenAddrEventCommon{nicID: nicID, addrWithPrefix: addrWithPrefix}})

	// Metrics only care about dynamic global address configuration options so
	// only increase the counter if we generated a global SLAAC address.
	if !header.IsV6LinkLocalUnicastAddress(addrWithPrefix.Address) {
		n.dynamicAddressSourceTracker.incGlobalSLAAC(nicID)
	}

	return &watcherAddressDispatcher{
		nicid: nicID,
		protocolAddr: tcpip.ProtocolAddress{
			Protocol:          header.IPv6ProtocolNumber,
			AddressWithPrefix: addrWithPrefix,
		},
		ch: n.ns.interfaceEventChan,
	}
}

// OnAutoGenAddressDeprecated implements ipv6.NDPDispatcher.
func (*ndpDispatcher) OnAutoGenAddressDeprecated(tcpip.NICID, tcpip.AddressWithPrefix) {
	// No need to do anything with this as deprecated addresses are still usable.
	// stack.Stack will handle not returning deprecated addresses if more
	// preferred addresses exist.
}

// OnAutoGenAddressInvalidated implements ipv6.NDPDispatcher.
func (n *ndpDispatcher) OnAutoGenAddressInvalidated(nicID tcpip.NICID, addrWithPrefix tcpip.AddressWithPrefix) {
	_ = syslog.VLogTf(syslog.DebugVerbosity, ndpSyslogTagName, "OnAutoGenAddressInvalidated(%d, %s)", nicID, addrWithPrefix)
	n.addEvent(&ndpInvalidatedAutoGenAddrEvent{ndpAutoGenAddrEventCommon: ndpAutoGenAddrEventCommon{nicID: nicID, addrWithPrefix: addrWithPrefix}})

	// Metrics only care about dynamic global address configuration options so
	// only decrease the counter if we invalidated a global SLAAC address.
	if !header.IsV6LinkLocalUnicastAddress(addrWithPrefix.Address) {
		n.dynamicAddressSourceTracker.decGlobalSLAAC(nicID)
	}
}

// OnRecursiveDNSServerOption implements ipv6.NDPDispatcher.
func (n *ndpDispatcher) OnRecursiveDNSServerOption(nicID tcpip.NICID, addrs []tcpip.Address, lifetime time.Duration) {
	_ = syslog.VLogTf(syslog.DebugVerbosity, ndpSyslogTagName, "OnRecursiveDNSServerOption(%d, %s, %s)", nicID, addrs, lifetime)
	n.addEvent(&ndpRecursiveDNSServerEvent{nicID: nicID, addrs: addrs, lifetime: lifetime})
}

// OnDNSSearchListOption implements ipv6.NDPDispatcher.
func (n *ndpDispatcher) OnDNSSearchListOption(nicID tcpip.NICID, domainNames []string, lifetime time.Duration) {
	_ = syslog.VLogTf(syslog.DebugVerbosity, ndpSyslogTagName, "OnDNSSearchListOption(%d, %s, %s)", nicID, domainNames, lifetime)
}

var _ NICRemovedHandler = (*ipv6AddressConfigTracker)(nil)

const (
	// ipv6AddressConfigTrackerInitialDelay is the initial delay
	// before polling the first IPv6 address configs.
	//
	// The delay should be large enough to let the network configurations
	// stabilize.
	ipv6AddressConfigTrackerInitialDelay = 10 * time.Minute

	// ipv6AddressConfigTrackerInterval is the duration between polling the latest
	// IPv6 address configs.
	ipv6AddressConfigTrackerInterval = time.Hour
)

type ipv6AddressConfigByNIC map[tcpip.NICID]struct {
	globalSLAAC uint32
	lastDHCPv6  ipv6.DHCPv6ConfigurationFromNDPRA
}

type ipv6AddressConfigTracker struct {
	ns    *Netstack
	timer tcpip.Timer

	mu struct {
		sync.Mutex
		nics ipv6AddressConfigByNIC
	}
}

func (i *ipv6AddressConfigTracker) init(ns *Netstack) {
	i.ns = ns

	i.mu.Lock()
	i.mu.nics = make(ipv6AddressConfigByNIC)
	i.mu.Unlock()

	i.timer = ns.stack.Clock().AfterFunc(ipv6AddressConfigTrackerInitialDelay, func() {
		i.incrementCounter()
		i.timer.Reset(ipv6AddressConfigTrackerInterval)
	})
}

func (i *ipv6AddressConfigTracker) RemovedNIC(nicID tcpip.NICID) {
	i.mu.Lock()
	defer i.mu.Unlock()
	delete(i.mu.nics, nicID)
}

func (i *ipv6AddressConfigTracker) incGlobalSLAAC(nicID tcpip.NICID) {
	i.mu.Lock()
	defer i.mu.Unlock()

	nic := i.mu.nics[nicID]
	nic.globalSLAAC++
	i.mu.nics[nicID] = nic
}

func (i *ipv6AddressConfigTracker) decGlobalSLAAC(nicID tcpip.NICID) {
	i.mu.Lock()
	defer i.mu.Unlock()

	nic := i.mu.nics[nicID]
	if nic.globalSLAAC == 0 {
		panic(fmt.Sprintf("cannot have a negative globalSLAAC count for nicID = %d", nicID))
	}
	nic.globalSLAAC--
	i.mu.nics[nicID] = nic
}

func (i *ipv6AddressConfigTracker) setLastDHCPv6(nicID tcpip.NICID, v ipv6.DHCPv6ConfigurationFromNDPRA) {
	i.mu.Lock()
	defer i.mu.Unlock()

	nic := i.mu.nics[nicID]
	nic.lastDHCPv6 = v
	i.mu.nics[nicID] = nic
}

func (i *ipv6AddressConfigTracker) incrementCounter() {
	i.mu.Lock()
	defer i.mu.Unlock()

	for _, nic := range i.mu.nics {
		slaac := nic.globalSLAAC != 0
		dhcpv6 := nic.lastDHCPv6 == ipv6.DHCPv6ManagedAddress
		switch {
		case !slaac && !dhcpv6:
			i.ns.stats.IPv6AddressConfig.NoGlobalSLAACOrDHCPv6ManagedAddress.Increment()
		case slaac && !dhcpv6:
			i.ns.stats.IPv6AddressConfig.GlobalSLAACOnly.Increment()
		case !slaac && dhcpv6:
			i.ns.stats.IPv6AddressConfig.DHCPv6ManagedAddressOnly.Increment()
		case slaac && dhcpv6:
			i.ns.stats.IPv6AddressConfig.GlobalSLAACAndDHCPv6ManagedAddress.Increment()
		}
	}
}

// OnDHCPv6Configuration implements ipv6.NDPDispatcher.
func (n *ndpDispatcher) OnDHCPv6Configuration(nicID tcpip.NICID, configuration ipv6.DHCPv6ConfigurationFromNDPRA) {
	_ = syslog.VLogTf(syslog.DebugVerbosity, ndpSyslogTagName, "OnDHCPv6Configuration(%d, %s)", nicID, configuration)
	switch configuration {
	case ipv6.DHCPv6NoConfiguration:
		n.ns.stats.DHCPv6.NoConfiguration.Increment()
	case ipv6.DHCPv6ManagedAddress:
		n.ns.stats.DHCPv6.ManagedAddress.Increment()
	case ipv6.DHCPv6OtherConfigurations:
		n.ns.stats.DHCPv6.OtherConfiguration.Increment()
	default:
		panic(fmt.Sprintf("unknown ipv6.DHCPv6ConfigurationFromNDPRA: %s", configuration))
	}

	n.dynamicAddressSourceTracker.setLastDHCPv6(nicID, configuration)
}

// addEvent adds an event to be handled by the ndpDispatcher goroutine.
func (n *ndpDispatcher) addEvent(e ndpEvent) {
	n.mu.Lock()
	n.mu.events = append(n.mu.events, e)
	n.mu.Unlock()
	select {
	case n.notifyCh <- struct{}{}:
	default:
		// If we are unable to send to notifyCh, then we know that the
		// worker goroutine has already been signalled to wake up and
		// handle pending events.
	}
}

// start starts the ndpDispatcher goroutine which will handle the NDP events.
//
// The worker goroutine will be stopped if ctx is cancelled.
//
// Panics if n does not have an associated Netstack.
func (n *ndpDispatcher) start(ctx context.Context) {
	_ = syslog.InfoTf(ndpSyslogTagName, "starting worker goroutine...")

	if n.ns == nil {
		panic(fmt.Sprintf("ndp: ndpDispatcher (%p) does not have an associated Netstack", n))
	}

	go func() {
		n.sem <- struct{}{}
		defer func() { <-n.sem }()

		_ = syslog.InfoTf(ndpSyslogTagName, "started worker goroutine")

		for {
			event := n.readEvent(ctx)
			if event == nil {
				return
			}
			n.handleEvent(event)
		}
	}()
}

// readEvent reads an NDP event.
func (n *ndpDispatcher) readEvent(ctx context.Context) ndpEvent {
	for {
		// Has ctx been cancelled?
		if err := ctx.Err(); err != nil {
			_ = syslog.InfoTf(ndpSyslogTagName, "stopping worker goroutine; ctx.Err(): %s", err)
			return nil
		}

		// Get the next event from the queue, but do not remove the event from
		// the queue yet. The event will be removed from the queue once it has
		// been handled. This is to avoid a race condition in tests where
		// waiting for the queue to empty can block indefinitely if the queue is
		// already empty.
		//
		// This is safe because the worker goroutine will be the only goroutine
		// handling events and popping from the queue. Other goroutines will
		// only push to the queue.
		if event := func() ndpEvent {
			n.mu.Lock()
			defer n.mu.Unlock()

			if len(n.mu.events) == 0 {
				return nil
			}
			return n.mu.events[0]
		}(); event != nil {
			return event
		}

		// No NDP events to handle. Wait for an NDP or ctx cancellation event to
		// handle.
		select {
		case <-ctx.Done():
			_ = syslog.InfoTf(ndpSyslogTagName, "stopping worker goroutine; ctx.Err(): %s", ctx.Err())
			return nil
		case <-n.notifyCh:
			continue
		}
	}
}

// handleEvent handles an NDP event.
func (n *ndpDispatcher) handleEvent(event ndpEvent) {
	// Handle the event.
	switch event := event.(type) {
	case *ndpDuplicateAddressDetectionEvent:
		switch result := event.result.(type) {
		case *stack.DADSucceeded:
			_ = syslog.InfoTf(ndpSyslogTagName, "DAD resolved for %s on nicID (%d)", event.addr, event.nicID)
		case *stack.DADError:
			logFn := syslog.ErrorTf
			if _, ok := result.Err.(*tcpip.ErrClosedForSend); ok {
				logFn = syslog.WarnTf
			}
			_ = logFn(ndpSyslogTagName, "DAD for %s on nicID (%d) encountered error = %s", event.addr, event.nicID, result.Err)
		case *stack.DADAborted:
			_ = syslog.WarnTf(ndpSyslogTagName, "DAD for %s on nicID (%d) aborted", event.addr, event.nicID)
			// Do not trigger on DAD complete because DAD was actually aborted.
			// The link online change handler will update the address assignment
			// state accordingly.
		case *stack.DADDupAddrDetected:
			_ = syslog.WarnTf(ndpSyslogTagName, "DAD found %s holding %s on nicID (%d)", result.HolderLinkAddress, event.addr, event.nicID)
		default:
			panic(fmt.Sprintf("unhandled DAD result variant %#v", result))
		}

	case *ndpDiscoveredOffLinkRouteEvent:
		rt := tcpip.Route{Destination: event.dest, Gateway: event.router, NIC: event.nicID}
		_ = syslog.InfoTf(ndpSyslogTagName, "discovered an off-link route to [%s] through [%s] on nicID (%d) with preference=%s: [%s]", event.dest, event.router, event.nicID, event.prf, rt)

		var prf routes.Preference
		switch event.prf {
		case header.LowRoutePreference:
			prf = routes.LowPreference
		case header.MediumRoutePreference:
			prf = routes.MediumPreference
		case header.HighRoutePreference:
			prf = routes.HighPreference
		default:
			panic(fmt.Sprintf("unhandled NDP route preference = %s", event.prf))
		}

		// rt is added as a 'static' route because Netstack will remove dynamic
		// routes on DHCPv4 changes. See
		// staticRouteAvoidingLifeCycleHooks for more details.
		if err := n.ns.addRouteWithPreference(rt, prf, metricNotSet, staticRouteAvoidingLifeCycleHooks); err != nil {
			_ = syslog.ErrorTf(ndpSyslogTagName, "failed to add the route [%s] with preference=%s for the discovered off-link route to [%s] through [%s] on nicID (%d): %s", rt, event.prf, event.dest, event.router, event.nicID, err)
		}

	case *ndpInvalidatedOffLinkRouteEvent:
		rt := tcpip.Route{Destination: event.dest, Gateway: event.router, NIC: event.nicID}
		_ = syslog.InfoTf(ndpSyslogTagName, "invalidating an off-link route to [%s] through [%s] on nicID (%d), removing the default route to it: [%s]", event.dest, event.router, event.nicID, rt)
		// If the route does not exist, we do not consider that an error as it
		// may have been removed by the user.
		_ = n.ns.DelRoute(rt)

	case *ndpDiscoveredPrefixEvent:
		nicID, prefix := event.nicID, event.prefix
		rt := onLinkV6Route(nicID, prefix)
		_ = syslog.InfoTf(ndpSyslogTagName, "discovered an on-link prefix (%s) on nicID (%d), adding an on-link route to it: [%s]", prefix, nicID, rt)
		// rt is added as a 'static' route because Netstack will remove dynamic
		// routes on DHCPv4 changes. See
		// staticRouteAvoidingLifeCycleHooks for more details.
		if err := n.ns.AddRoute(rt, metricNotSet, staticRouteAvoidingLifeCycleHooks); err != nil {
			_ = syslog.ErrorTf(ndpSyslogTagName, "failed to add the on-link route [%s] for the discovered on-link prefix (%s) on nicID (%d): %s", rt, prefix, nicID, err)
		}

	case *ndpInvalidatedPrefixEvent:
		nicID, prefix := event.nicID, event.prefix
		rt := onLinkV6Route(nicID, prefix)
		_ = syslog.InfoTf(ndpSyslogTagName, "invalidating an on-link prefix (%s) from nicID (%d), removing the on-link route to it: [%s]", prefix, nicID, rt)
		// If the route does not exist, we do not consider that an error as it
		// may have been removed by the user.
		_ = n.ns.DelRoute(rt)

	case *ndpGeneratedAutoGenAddrEvent:
		nicID, addrWithPrefix := event.nicID, event.addrWithPrefix
		_ = syslog.InfoTf(ndpSyslogTagName, "added an auto-generated address (%s) on nicID (%d)", addrWithPrefix, nicID)

	case *ndpInvalidatedAutoGenAddrEvent:
		nicID, addrWithPrefix := event.nicID, event.addrWithPrefix
		_ = syslog.InfoTf(ndpSyslogTagName, "invalidated an auto-generated address (%s) on nicID (%d)", addrWithPrefix, nicID)

	case *ndpRecursiveDNSServerEvent:
		nicID, addrs, lifetime := event.nicID, event.addrs, event.lifetime
		_ = syslog.VLogTf(syslog.DebugVerbosity, ndpSyslogTagName, "updating expiring DNS servers (%s) on nicID (%d) with lifetime (%s)...", addrs, nicID, lifetime)
		servers := make([]tcpip.FullAddress, 0, len(addrs))
		for _, a := range addrs {
			// The default DNS port will be used since the Port field is
			// unspecified here.
			servers = append(servers, tcpip.FullAddress{Addr: a, NIC: nicID})
		}

		// lifetime should never be greater than header.NDPInfiniteLifetime.
		if lifetime > header.NDPInfiniteLifetime {
			panic(fmt.Sprintf("ndp: got recursive DNS server event with lifetime (%s) greater than infinite lifetime (%s) on nicID (%d) with addrs (%s)", lifetime, header.NDPInfiniteLifetime, nicID, addrs))
		}

		if lifetime == header.NDPInfiniteLifetime {
			// A lifetime value less than 0 implies infinite lifetime to the DNS
			// client.
			lifetime = -1
		}

		n.ns.dnsConfig.UpdateNdpServers(servers, lifetime)

	default:
		panic(fmt.Sprintf("unrecognized event type: %T", event))
	}

	// Remove the event we just handled from the queue. If the queue is empty
	// after popping, then we know that all events in the queue (before taking
	// the lock) have been handled.
	n.mu.Lock()
	n.mu.events[0] = nil
	n.mu.events = n.mu.events[1:]
	eventsLeft := len(n.mu.events)
	if eventsLeft == 0 {
		// Nil the event slice so that excess capacity can be deallocated.
		n.mu.events = nil
	}
	n.mu.Unlock()

	// Signal tests that are waiting for the event queue to be empty. We
	// signal after handling the last event so that when the test wakes up,
	// the test can safely assume that all events in the queue (up to this
	// notification) have been handled.
	if eventsLeft == 0 {
		select {
		case n.testNotifyCh <- struct{}{}:
		default:
		}
	}
}

// newNDPDispatcher returns a new ndpDispatcher that allows 1 worker goroutine
// to be employed.
func newNDPDispatcher() *ndpDispatcher {
	return &ndpDispatcher{
		// This is set to 1 to guarantee ordering between events that
		// share some relationship. See ndpDispatcher for more details.
		sem:      make(chan struct{}, 1),
		notifyCh: make(chan struct{}, 1),
		getAddressPrefix: func(nicInfo *stack.NICInfo, a tcpip.Address) (int, bool) {
			for _, addr := range nicInfo.ProtocolAddresses {
				if addr.AddressWithPrefix.Address == a {
					return addr.AddressWithPrefix.PrefixLen, true
				}
			}
			return 0, false
		},
	}
}
