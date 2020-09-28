// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package netstack

import (
	"context"
	"errors"
	"fmt"
	"sync"
	"time"

	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/routes"
	syslog "go.fuchsia.dev/fuchsia/src/lib/syslog/go"

	networking_metrics "networking_metrics_golib"

	"fidl/fuchsia/cobalt"

	"gvisor.dev/gvisor/pkg/tcpip"
	"gvisor.dev/gvisor/pkg/tcpip/header"
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

// ndpRouterAndDADEventCommon holds the common fields for NDP default router
// discovery and invalidation, and Duplicate Address Detection events.
type ndpRouterAndDADEventCommon struct {
	nicID tcpip.NICID
	addr  tcpip.Address
}

// isNDPEvent implements ndpEvent.isNDPEvent.
func (*ndpRouterAndDADEventCommon) isNDPEvent() {}

type ndpDuplicateAddressDetectionEvent struct {
	ndpRouterAndDADEventCommon
	resolved bool
	err      *tcpip.Error
}

type ndpDiscoveredRouterEvent struct {
	ndpRouterAndDADEventCommon
}

type ndpInvalidatedRouterEvent struct {
	ndpRouterAndDADEventCommon
}

// ndpPrefixEventCommon holds the common fields for all events related to NDP
// on-link prefix discovery and invalidation.
type ndpPrefixEventCommon struct {
	nicID  tcpip.NICID
	prefix tcpip.Subnet
}

// isNDPEvent implements ndpEvent.isNDPEvent.
func (*ndpPrefixEventCommon) isNDPEvent() {}

type ndpDiscoveredPrefixEvent struct {
	ndpPrefixEventCommon
}
type ndpInvalidatedPrefixEvent struct {
	ndpPrefixEventCommon
}

// ndpAutoGenAddrEventCommon holds the common fields for all events related to
// auto-generated address events.
type ndpAutoGenAddrEventCommon struct {
	nicID          tcpip.NICID
	addrWithPrefix tcpip.AddressWithPrefix
}

// isNDPEvent implements ndpEvent.isNDPEvent.
func (*ndpAutoGenAddrEventCommon) isNDPEvent() {}

type ndpGeneratedAutoGenAddrEvent struct {
	ndpAutoGenAddrEventCommon
}
type ndpInvalidatedAutoGenAddrEvent struct {
	ndpAutoGenAddrEventCommon
}

// ndpRecursiveDNSServerEvent holds the fields for an NDP Recursive DNS Server
// list event.
type ndpRecursiveDNSServerEvent struct {
	nicID    tcpip.NICID
	addrs    []tcpip.Address
	lifetime time.Duration
}

// isNDPEvent implements ndpEvent.isNDPEvent.
func (*ndpRecursiveDNSServerEvent) isNDPEvent() {}

var _ stack.NDPDispatcher = (*ndpDispatcher)(nil)

// ndpDispatcher is a type that implements stack.NDPDispatcher to handle the
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

	// dhcpv6Obs tracks unique DHCPv6 configurations since the last Cobalt pull.
	dhcpv6Obs dhcpV6Observation

	// dynamicAddressSourceObs tracks the most recent dynamic address
	// configuration options available for an interface.
	dynamicAddressSourceObs availableDynamicIPv6AddressConfigObservation

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

// OnDuplicateAddressDetectionStatus implements
// stack.NDPDispatcher.OnDuplicateAddressDetectionStatus.
func (n *ndpDispatcher) OnDuplicateAddressDetectionStatus(nicID tcpip.NICID, addr tcpip.Address, resolved bool, err *tcpip.Error) {
	_ = syslog.VLogTf(syslog.DebugVerbosity, ndpSyslogTagName, "OnDuplicateAddressDetectionStatus(%d, %s, %t, %v)", nicID, addr, resolved, err)
	n.addEvent(&ndpDuplicateAddressDetectionEvent{
		ndpRouterAndDADEventCommon: ndpRouterAndDADEventCommon{
			nicID: nicID,
			addr:  addr,
		},
		resolved: resolved,
		err:      err,
	})
}

// OnDefaultRouterDiscovered implements stack.NDPDispatcher.OnDefaultRouterDiscovered.
//
// Adds the event to the event queue and returns true so Stack remembers the
// discovered default router.
func (n *ndpDispatcher) OnDefaultRouterDiscovered(nicID tcpip.NICID, addr tcpip.Address) bool {
	_ = syslog.VLogTf(syslog.DebugVerbosity, ndpSyslogTagName, "OnDefaultRouterDiscovered(%d, %s)", nicID, addr)
	n.addEvent(&ndpDiscoveredRouterEvent{ndpRouterAndDADEventCommon: ndpRouterAndDADEventCommon{nicID: nicID, addr: addr}})
	return true
}

// OnDefaultRouterInvalidated implements stack.NDPDispatcher.OnDefaultRouterInvalidated.
func (n *ndpDispatcher) OnDefaultRouterInvalidated(nicID tcpip.NICID, addr tcpip.Address) {
	_ = syslog.VLogTf(syslog.DebugVerbosity, ndpSyslogTagName, "OnDefaultRouterInvalidated(%d, %s)", nicID, addr)
	n.addEvent(&ndpInvalidatedRouterEvent{ndpRouterAndDADEventCommon: ndpRouterAndDADEventCommon{nicID: nicID, addr: addr}})
}

// OnOnLinkPrefixDiscovered implements stack.NDPDispatcher.OnOnLinkPrefixDiscovered.
//
// Adds the event to the event queue and returns true so Stack remembers the
// discovered on-link prefix.
func (n *ndpDispatcher) OnOnLinkPrefixDiscovered(nicID tcpip.NICID, prefix tcpip.Subnet) bool {
	_ = syslog.VLogTf(syslog.DebugVerbosity, ndpSyslogTagName, "OnOnLinkPrefixDiscovered(%d, %s)", nicID, prefix)
	n.addEvent(&ndpDiscoveredPrefixEvent{ndpPrefixEventCommon: ndpPrefixEventCommon{nicID: nicID, prefix: prefix}})
	return true
}

// OnOnLinkPrefixInvalidated implements stack.NDPDispatcher.OnOnLinkPrefixInvalidated.
func (n *ndpDispatcher) OnOnLinkPrefixInvalidated(nicID tcpip.NICID, prefix tcpip.Subnet) {
	_ = syslog.VLogTf(syslog.DebugVerbosity, ndpSyslogTagName, "OnOnLinkPrefixInvalidated(%d, %s)", nicID, prefix)
	n.addEvent(&ndpInvalidatedPrefixEvent{ndpPrefixEventCommon: ndpPrefixEventCommon{nicID: nicID, prefix: prefix}})
}

// OnAutoGenAddress implements stack.NDPDispatcher.OnAutoGenAddress.
//
// Adds the event to the event queue and returns true so Stack adds the
// auto-generated address.
func (n *ndpDispatcher) OnAutoGenAddress(nicID tcpip.NICID, addrWithPrefix tcpip.AddressWithPrefix) bool {
	_ = syslog.VLogTf(syslog.DebugVerbosity, ndpSyslogTagName, "OnAutoGenAddress(%d, %s)", nicID, addrWithPrefix)
	n.addEvent(&ndpGeneratedAutoGenAddrEvent{ndpAutoGenAddrEventCommon: ndpAutoGenAddrEventCommon{nicID: nicID, addrWithPrefix: addrWithPrefix}})

	// Metrics only care about dynamic global address configuration options so
	// only increase the counter if we generated a global SLAAC address.
	if !header.IsV6LinkLocalAddress(addrWithPrefix.Address) {
		n.dynamicAddressSourceObs.incGlobalSLAAC(nicID)
	}

	return true
}

// OnAutoGenAddressDeprecated implements
// stack.NDPDispatcher.OnAutoGenAddressDeprecated.
func (*ndpDispatcher) OnAutoGenAddressDeprecated(tcpip.NICID, tcpip.AddressWithPrefix) {
	// No need to do anything with this as deprecated addresses are still usable.
	// stack.Stack will handle not returning deprecated addresses if more
	// preferred addresses exist.
}

// OnAutoGenAddressInvalidated implements stack.NDPDispatcher.OnAutoGenAddressInvalidated.
func (n *ndpDispatcher) OnAutoGenAddressInvalidated(nicID tcpip.NICID, addrWithPrefix tcpip.AddressWithPrefix) {
	_ = syslog.VLogTf(syslog.DebugVerbosity, ndpSyslogTagName, "OnAutoGenAddressInvalidated(%d, %s)", nicID, addrWithPrefix)
	n.addEvent(&ndpInvalidatedAutoGenAddrEvent{ndpAutoGenAddrEventCommon: ndpAutoGenAddrEventCommon{nicID: nicID, addrWithPrefix: addrWithPrefix}})

	// Metrics only care about dynamic global address configuration options so
	// only decrease the counter if we invalidated a global SLAAC address.
	if !header.IsV6LinkLocalAddress(addrWithPrefix.Address) {
		n.dynamicAddressSourceObs.decGlobalSLAAC(nicID)
	}
}

// OnRecursiveDNSServerOption implements stack.NDPDispatcher.OnRecursiveDNSServerOption.
func (n *ndpDispatcher) OnRecursiveDNSServerOption(nicID tcpip.NICID, addrs []tcpip.Address, lifetime time.Duration) {
	_ = syslog.VLogTf(syslog.DebugVerbosity, ndpSyslogTagName, "OnRecursiveDNSServerOption(%d, %s, %s)", nicID, addrs, lifetime)
	n.addEvent(&ndpRecursiveDNSServerEvent{nicID: nicID, addrs: addrs, lifetime: lifetime})
}

// OnDNSSearchListOption implements stack.NDPDispatcher.OnDNSSearchListOption.
func (n *ndpDispatcher) OnDNSSearchListOption(nicID tcpip.NICID, domainNames []string, lifetime time.Duration) {
	_ = syslog.VLogTf(syslog.DebugVerbosity, ndpSyslogTagName, "OnDNSSearchListOption(%d, %s, %s)", nicID, domainNames, lifetime)
}

var _ nicRemovedHandler = (*availableDynamicIPv6AddressConfigObservation)(nil)
var _ cobaltEventProducer = (*availableDynamicIPv6AddressConfigObservation)(nil)

// Using a var so that it can be overriden for tests.
//
// TODO(fxbug.dev/57075): Use a fake clock in tests so we can make these constants.
var (
	// availableDynamicIPv6AddressConfigDelayInitialEvents is the initial delay
	// before registering with the cobalt client that events are ready.
	//
	// The delay should be large enough to let the network configurations
	// stabilize.
	availableDynamicIPv6AddressConfigDelayInitialEvents = 10 * time.Minute

	// availableDynamicIPv6AddressConfigDelayBetweenEvents is the delay between
	// registering with the cobalt client that events are ready after the initial
	// registration.
	availableDynamicIPv6AddressConfigDelayBetweenEvents = time.Hour
)

type byNICAvailableDynamicIPv6AddressConfig map[tcpip.NICID]struct {
	globalSLAAC uint32
	lastDHCPv6  stack.DHCPv6ConfigurationFromNDPRA
}

type availableDynamicIPv6AddressConfigObservation struct {
	hasEvents func()

	mu struct {
		sync.Mutex
		obs byNICAvailableDynamicIPv6AddressConfig
	}
}

func (o *availableDynamicIPv6AddressConfigObservation) initWithoutTimer(hasEvents func()) {
	o.hasEvents = hasEvents

	o.mu.Lock()
	o.mu.obs = make(byNICAvailableDynamicIPv6AddressConfig)
	o.mu.Unlock()
}

// init sets the events registration callback and starts the timer to register
// events.
func (o *availableDynamicIPv6AddressConfigObservation) init(hasEvents func()) {
	o.initWithoutTimer(hasEvents)

	var mu sync.Mutex
	var t *time.Timer
	mu.Lock()
	defer mu.Unlock()
	t = time.AfterFunc(availableDynamicIPv6AddressConfigDelayInitialEvents, func() {
		o.hasEvents()

		mu.Lock()
		defer mu.Unlock()
		t.Reset(availableDynamicIPv6AddressConfigDelayBetweenEvents)
	})
}

func (o *availableDynamicIPv6AddressConfigObservation) removedNIC(nicID tcpip.NICID) {
	o.mu.Lock()
	defer o.mu.Unlock()
	delete(o.mu.obs, nicID)
}

func (o *availableDynamicIPv6AddressConfigObservation) incGlobalSLAAC(nicID tcpip.NICID) {
	o.mu.Lock()
	defer o.mu.Unlock()
	nic := o.mu.obs[nicID]
	nic.globalSLAAC++
	o.mu.obs[nicID] = nic
}

func (o *availableDynamicIPv6AddressConfigObservation) decGlobalSLAAC(nicID tcpip.NICID) {
	o.mu.Lock()
	defer o.mu.Unlock()

	nic := o.mu.obs[nicID]
	if nic.globalSLAAC == 0 {
		panic(fmt.Sprintf("cannot have a negative globalSLAAC count for nicID = %d", nicID))
	}

	nic.globalSLAAC--
	o.mu.obs[nicID] = nic
}

func (o *availableDynamicIPv6AddressConfigObservation) setLastDHCPv6(nicID tcpip.NICID, v stack.DHCPv6ConfigurationFromNDPRA) {
	o.mu.Lock()
	defer o.mu.Unlock()
	nic := o.mu.obs[nicID]
	nic.lastDHCPv6 = v
	o.mu.obs[nicID] = nic
}

func (o *availableDynamicIPv6AddressConfigObservation) events() []cobalt.CobaltEvent {
	o.mu.Lock()
	defer o.mu.Unlock()

	events := make([]cobalt.CobaltEvent, 0, len(o.mu.obs))
	for nicID, nic := range o.mu.obs {
		var v uint8

		if nic.globalSLAAC != 0 {
			v |= 1
		}

		if nic.lastDHCPv6 == stack.DHCPv6ManagedAddress {
			v |= 2
		}

		var code networking_metrics.NetworkingMetricDimensionDynamicIpv6AddressSource
		switch v {
		case 0:
			code = networking_metrics.NoGlobalSlaacOrDhcpv6ManagedAddress
		case 1:
			code = networking_metrics.GlobalSlaacOnly
		case 2:
			code = networking_metrics.Dhcpv6ManagedAddressOnly
		case 3:
			code = networking_metrics.GlobalSlaacAndDhcpv6ManagedAddress
		default:
			panic(fmt.Sprintf("unrecognized v = %d", v))
		}

		events = append(events, cobalt.CobaltEvent{
			MetricId: networking_metrics.AvailableDynamicIpv6AddressConfigMetricId,
			EventCodes: networking_metrics.AvailableDynamicIpv6AddressConfigEventCodes{
				DynamicIpv6AddressSource: code,
				InterfaceId:              networking_metrics.AvailableDynamicIpv6AddressConfigMetricDimensionInterfaceId(nicID),
			}.ToArray(),
			Payload: cobalt.EventPayloadWithEventCount(cobalt.CountEvent{
				Count: 1,
			}),
		})
	}

	return events
}

var _ cobaltEventProducer = (*dhcpV6Observation)(nil)

type dhcpV6Observation struct {
	mu struct {
		sync.Mutex
		seen      map[stack.DHCPv6ConfigurationFromNDPRA]int
		hasEvents func()
	}
}

func (o *dhcpV6Observation) init(hasEvents func()) {
	o.mu.Lock()
	defer o.mu.Unlock()
	o.mu.hasEvents = hasEvents
}

func (o *dhcpV6Observation) events() []cobalt.CobaltEvent {
	o.mu.Lock()
	defer o.mu.Unlock()

	var res []cobalt.CobaltEvent
	for c, count := range o.mu.seen {
		var code networking_metrics.NetworkingMetricDimensionConfigurationFromNdpra
		switch c {
		case stack.DHCPv6NoConfiguration:
			code = networking_metrics.NoConfiguration
		case stack.DHCPv6ManagedAddress:
			code = networking_metrics.ManagedAddress
		case stack.DHCPv6OtherConfigurations:
			code = networking_metrics.OtherConfigurations
		default:
			_ = syslog.Warnf("ndp: unknown stack.DHCPv6ConfigurationFromNDPRA: %s", c)
		}
		for i := 0; i < count; i++ {
			res = append(res, cobalt.CobaltEvent{
				MetricId:   networking_metrics.DhcpV6ConfigurationMetricId,
				EventCodes: []uint32{uint32(code)},
				Payload:    cobalt.EventPayloadWithEvent(cobalt.Event{}),
			})
		}
	}
	o.mu.seen = nil
	return res
}

// OnDHCPv6Configuration implements stack.NDPDispatcher.OnDHCPv6Configuration.
func (n *ndpDispatcher) OnDHCPv6Configuration(nicID tcpip.NICID, configuration stack.DHCPv6ConfigurationFromNDPRA) {
	_ = syslog.VLogTf(syslog.DebugVerbosity, ndpSyslogTagName, "OnDHCPv6Configuration(%d, %s)", nicID, configuration)

	n.dhcpv6Obs.mu.Lock()
	if n.dhcpv6Obs.mu.seen == nil {
		n.dhcpv6Obs.mu.seen = make(map[stack.DHCPv6ConfigurationFromNDPRA]int)
	}
	n.dhcpv6Obs.mu.seen[configuration] += 1
	hasEvents := n.dhcpv6Obs.mu.hasEvents
	n.dhcpv6Obs.mu.Unlock()
	if hasEvents == nil {
		panic("ndp dispatcher: dhcpV6Observation: hasEvents callback unspecified (ensure init has been called)")
	}
	hasEvents()

	n.dynamicAddressSourceObs.setLastDHCPv6(nicID, configuration)
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
		done := ctx.Done()

		_ = syslog.InfoTf(ndpSyslogTagName, "started worker goroutine")

		for {
			var event ndpEvent
			for {
				// Has ctx been cancelled?
				if err := ctx.Err(); err != nil {
					_ = syslog.InfoTf(ndpSyslogTagName, "stopping worker goroutine; ctx.Err(): %s", err)
					return
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
				n.mu.Lock()
				if len(n.mu.events) > 0 {
					event = n.mu.events[0]
				}
				n.mu.Unlock()

				if event != nil {
					break
				}

				// No NDP events to handle. Wait for an NDP or ctx cancellation event to
				// handle.
				select {
				case <-done:
					_ = syslog.InfoTf(ndpSyslogTagName, "stopping worker goroutine; ctx.Err(): %s", ctx.Err())
					return
				case <-n.notifyCh:
					continue
				}
			}

			// Handle the event.
			switch event := event.(type) {
			case *ndpDuplicateAddressDetectionEvent:
				if event.resolved {
					_ = syslog.InfoTf(ndpSyslogTagName, "DAD resolved for %s on nicID (%d), sending interface changed event...", event.addr, event.nicID)
				} else if err := event.err; err != nil {
					_ = syslog.ErrorTf(ndpSyslogTagName, "DAD for %s on nicID (%d) encountered error = %s, sending interface changed event...", event.addr, event.nicID, err)
				} else {
					_ = syslog.WarnTf(ndpSyslogTagName, "duplicate address detected during DAD for %s on nicID (%d), sending interface changed event...", event.addr, event.nicID)
				}

				n.ns.onInterfacesChanged()
				n.ns.onPropertiesChange(event.nicID)

			case *ndpDiscoveredRouterEvent:
				nicID, addr := event.nicID, event.addr
				rt := defaultV6Route(nicID, addr)
				_ = syslog.InfoTf(ndpSyslogTagName, "discovered a default router (%s) on nicID (%d), adding a default route to it: [%s]", addr, nicID, rt)
				// rt is added as a 'static' route because Netstack will remove dynamic
				// routes on DHCPv4 changes. See
				// staticRouteAvoidingLifeCycleHooks for more details.
				if err := n.ns.AddRoute(rt, metricNotSet, staticRouteAvoidingLifeCycleHooks); err != nil {
					_ = syslog.ErrorTf(ndpSyslogTagName, "failed to add the default route [%s] for the discovered router (%s) on nicID (%d): %s", rt, addr, nicID, err)
				}

			case *ndpInvalidatedRouterEvent:
				nicID, addr := event.nicID, event.addr
				rt := defaultV6Route(nicID, addr)
				_ = syslog.InfoTf(ndpSyslogTagName, "invalidating a default router (%s) from nicID (%d), removing the default route to it: [%s]", addr, nicID, rt)
				// If the route does not exist, we do not consider that an error as it
				// may have been removed by the user.
				if err := n.ns.DelRoute(rt); err != nil && !errors.Is(err, routes.ErrNoSuchRoute) {
					_ = syslog.ErrorTf(ndpSyslogTagName, "failed to remove the default route [%s] for the invalidated router (%s) on nicID (%d): %s", rt, addr, nicID, err)
				}

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
				if err := n.ns.DelRoute(rt); err != nil && !errors.Is(err, routes.ErrNoSuchRoute) {
					_ = syslog.ErrorTf(ndpSyslogTagName, "failed to remove the on-link route [%s] for the invalidated on-link prefix (%s) on nicID (%d): %s", rt, prefix, nicID, err)
				}

			case *ndpGeneratedAutoGenAddrEvent:
				nicID, addrWithPrefix := event.nicID, event.addrWithPrefix
				_ = syslog.InfoTf(ndpSyslogTagName, "added an auto-generated address (%s) on nicID (%d)", addrWithPrefix, nicID)

			case *ndpInvalidatedAutoGenAddrEvent:
				nicID, addrWithPrefix := event.nicID, event.addrWithPrefix
				_ = syslog.InfoTf(ndpSyslogTagName, "invalidated an auto-generated address (%s) on nicID (%d), sending interface changed event...", addrWithPrefix, nicID)
				n.ns.onInterfacesChanged()
				n.ns.onPropertiesChange(event.nicID)

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
	}()
}

// newNDPDispatcher returns a new ndpDispatcher that allows 1 worker goroutine
// to be employed.
func newNDPDispatcher() *ndpDispatcher {
	return &ndpDispatcher{
		// This is set to 1 to guarantee ordering between events that
		// share some relationship. See ndpDispatcher for more details.
		sem:      make(chan struct{}, 1),
		notifyCh: make(chan struct{}, 1),
	}
}
