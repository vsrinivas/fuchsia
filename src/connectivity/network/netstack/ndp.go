// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package netstack

import (
	"context"
	"fmt"
	"sync"
	"syslog"
	"time"

	"gvisor.dev/gvisor/pkg/tcpip"
	"gvisor.dev/gvisor/pkg/tcpip/header"
	"gvisor.dev/gvisor/pkg/tcpip/stack"
)

const (
	// staticRouteAvoidingLifeCycleHooks is the dynamic flag when adding a
	// new route in response to an NDP discovery event.
	//
	// routes are added as a 'static' route because stack.Stack does not yet
	// support bringing a NIC down. 'dynamic' routes are deleted when the
	// integrator (Netstack) brings an interface down locally, but
	// stack.Stack will continue to consider the routers and prefixes as
	// valid and not send any discovered events for it after the interface
	// is brought back up. The notions of static/dynamic are defined only
	// with respect to Netstack's interface lifecycle hooks which isn't
	// implemented by stack.Stack; thus such routes must be 'static' to
	// escape Netstack's lifecycle management hooks that 'dynamic' routes
	// will be affected by.
	//
	// https://gvisor.dev/issue/1491 tracks the issue of stack.Stack not
	// supporting NIC lifecycle management.
	//
	// TODO(fxb/43503): Instead of adding routes as static, support a type
	// of dynamic route that can be batch disabled (separate from dynamic
	// routes that should be batch deleted).
	staticRouteAvoidingLifeCycleHooks = false
)

// ndpEvent is a marker interface used to improve type safety in ndpDispatcher.
type ndpEvent interface {
	isNDPEvent()
}

// ndpRouterEventCommon holds the common fields for all events related to NDP
// default router discovery and invalidation.
type ndpRouterEventCommon struct {
	nicID tcpip.NICID
	addr  tcpip.Address
}

// isNDPEvent implements ndpEvent.isNDPEvent.
func (*ndpRouterEventCommon) isNDPEvent() {}

type ndpDiscoveredRouterEvent struct {
	ndpRouterEventCommon
}

type ndpInvalidatedRouterEvent struct {
	ndpRouterEventCommon
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
func (*ndpDispatcher) OnDuplicateAddressDetectionStatus(tcpip.NICID, tcpip.Address, bool, *tcpip.Error) {
}

// OnDefaultRouterDiscovered implements stack.NDPDispatcher.OnDefaultRouterDiscovered.
//
// Adds the event to the event queue and returns true so Stack remembers the
// discovered default router.
func (n *ndpDispatcher) OnDefaultRouterDiscovered(nicID tcpip.NICID, addr tcpip.Address) bool {
	syslog.Infof("ndp: OnDefaultRouterDiscovered(%d, %s)", nicID, addr)
	n.addEvent(&ndpDiscoveredRouterEvent{ndpRouterEventCommon: ndpRouterEventCommon{nicID: nicID, addr: addr}})
	return true
}

// OnDefaultRouterInvalidated implements stack.NDPDispatcher.OnDefaultRouterInvalidated.
func (n *ndpDispatcher) OnDefaultRouterInvalidated(nicID tcpip.NICID, addr tcpip.Address) {
	syslog.Infof("ndp: OnDefaultRouterInvalidated(%d, %s)", nicID, addr)
	n.addEvent(&ndpInvalidatedRouterEvent{ndpRouterEventCommon: ndpRouterEventCommon{nicID: nicID, addr: addr}})
}

// OnOnLinkPrefixDiscovered implements stack.NDPDispatcher.OnOnLinkPrefixDiscovered.
//
// Adds the event to the event queue and returns true so Stack remembers the
// discovered on-link prefix.
func (n *ndpDispatcher) OnOnLinkPrefixDiscovered(nicID tcpip.NICID, prefix tcpip.Subnet) bool {
	syslog.Infof("ndp: OnOnLinkPrefixDiscovered(%d, %s)", nicID, prefix)
	n.addEvent(&ndpDiscoveredPrefixEvent{ndpPrefixEventCommon: ndpPrefixEventCommon{nicID: nicID, prefix: prefix}})
	return true
}

// OnOnLinkPrefixInvalidated implements stack.NDPDispatcher.OnOnLinkPrefixInvalidated.
func (n *ndpDispatcher) OnOnLinkPrefixInvalidated(nicID tcpip.NICID, prefix tcpip.Subnet) {
	syslog.Infof("ndp: OnOnLinkPrefixInvalidated(%d, %s)", nicID, prefix)
	n.addEvent(&ndpInvalidatedPrefixEvent{ndpPrefixEventCommon: ndpPrefixEventCommon{nicID: nicID, prefix: prefix}})
}

// OnAutoGenAddress implements stack.NDPDispatcher.OnAutoGenAddress.
//
// Adds the event to the event queue and returns true so Stack adds the
// auto-generated address.
func (n *ndpDispatcher) OnAutoGenAddress(nicID tcpip.NICID, addrWithPrefix tcpip.AddressWithPrefix) bool {
	syslog.Infof("ndp: OnAutoGenAddress(%d, %s)", nicID, addrWithPrefix)
	n.addEvent(&ndpGeneratedAutoGenAddrEvent{ndpAutoGenAddrEventCommon: ndpAutoGenAddrEventCommon{nicID: nicID, addrWithPrefix: addrWithPrefix}})
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
	syslog.Infof("ndp: OnAutoGenAddressInvalidated(%d, %s)", nicID, addrWithPrefix)
	n.addEvent(&ndpInvalidatedAutoGenAddrEvent{ndpAutoGenAddrEventCommon: ndpAutoGenAddrEventCommon{nicID: nicID, addrWithPrefix: addrWithPrefix}})
}

// OnRecursiveDNSServerOption implements stack.NDPDispatcher.OnRecursiveDNSServerOption.
func (n *ndpDispatcher) OnRecursiveDNSServerOption(nicID tcpip.NICID, addrs []tcpip.Address, lifetime time.Duration) {
	syslog.Infof("ndp: OnRecursiveDNSServerOption(%d, %s, %s)", nicID, addrs, lifetime)
	n.addEvent(&ndpRecursiveDNSServerEvent{nicID: nicID, addrs: addrs, lifetime: lifetime})
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
	syslog.Infof("ndp: starting worker goroutine...")

	if n.ns == nil {
		panic(fmt.Sprintf("ndp: ndpDispatcher (%p) does not have an associated Netstack", n))
	}

	go func() {
		n.sem <- struct{}{}
		defer func() { <-n.sem }()
		done := ctx.Done()

		syslog.Infof("ndp: started worker goroutine")

		for {
			var event ndpEvent
			for {
				// Has ctx been cancelled?
				if err := ctx.Err(); err != nil {
					syslog.Infof("ndp: stopping worker goroutine; ctx.Err(): %s", err)
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
					syslog.Infof("ndp: stopping worker goroutine; ctx.Err(): %s", ctx.Err())
					return
				case <-n.notifyCh:
					continue
				}
			}

			// Handle the event.
			switch event := event.(type) {
			case *ndpDiscoveredRouterEvent:
				nicID, addr := event.nicID, event.addr
				rt := defaultV6Route(nicID, addr)
				syslog.Infof("ndp: discovered a default router (%s) on nicID (%d), adding a default route to it: [%s]", addr, nicID, rt)
				// rt is added as a 'static' route because stack.Stack does not
				// yet support bringing a NIC down. See
				// staticRouteAvoidingLifeCycleHooks for more details.
				if err := n.ns.AddRoute(rt, metricNotSet, staticRouteAvoidingLifeCycleHooks); err != nil {
					syslog.Errorf("ndp: failed to add the default route [%s] for the discovered router (%s) on nicID (%d): %s", rt, addr, nicID, err)
				}

			case *ndpInvalidatedRouterEvent:
				nicID, addr := event.nicID, event.addr
				rt := defaultV6Route(nicID, addr)
				syslog.Infof("ndp: invalidating a default router (%s) from nicID (%d), removing the default route to it: [%s]", addr, nicID, rt)
				if err := n.ns.DelRoute(rt); err != nil {
					syslog.Errorf("ndp: failed to remove the default route [%s] for the invalidated router (%s) on nicID (%d): %s", rt, addr, nicID, err)
				}

			case *ndpDiscoveredPrefixEvent:
				nicID, prefix := event.nicID, event.prefix
				rt := onLinkV6Route(nicID, prefix)
				syslog.Infof("ndp: discovered an on-link prefix (%s) on nicID (%d), adding an on-link route to it: [%s]", prefix, nicID, rt)
				// rt is added as a 'static' route because stack.Stack does not
				// yet support bringing a NIC down. See
				// staticRouteAvoidingLifeCycleHooks for more details.
				if err := n.ns.AddRoute(rt, metricNotSet, staticRouteAvoidingLifeCycleHooks); err != nil {
					syslog.Errorf("ndp: failed to add the on-link route [%s] for the discovered on-link prefix (%s) on nicID (%d): %s", rt, prefix, nicID, err)
				}

			case *ndpInvalidatedPrefixEvent:
				nicID, prefix := event.nicID, event.prefix
				rt := onLinkV6Route(nicID, prefix)
				syslog.Infof("ndp: invalidating an on-link prefix (%s) from nicID (%d), removing the on-link route to it: [%s]", prefix, nicID, rt)
				if err := n.ns.DelRoute(rt); err != nil {
					syslog.Errorf("ndp: failed to remove the on-link route [%s] for the invalidated on-link prefix (%s) on nicID (%d): %s", rt, prefix, nicID, err)
				}

			case *ndpGeneratedAutoGenAddrEvent:
				nicID, addrWithPrefix := event.nicID, event.addrWithPrefix
				syslog.Infof("ndp: added an auto-generated address (%s) on nicID (%d), remembering it locally...", addrWithPrefix, nicID)
				if err := n.ns.rememberSLAACAddress(nicID, addrWithPrefix); err != nil {
					syslog.Errorf("ndp: failed to remember auto-generated address (%s) on nicID (%d): %s", addrWithPrefix, nicID, err)
				}

			case *ndpInvalidatedAutoGenAddrEvent:
				nicID, addrWithPrefix := event.nicID, event.addrWithPrefix
				syslog.Infof("ndp: invalidated an auto-generated address (%s) on nicID (%d), forgetting it locally...", addrWithPrefix, nicID)
				if err := n.ns.forgetSLAACAddress(nicID, addrWithPrefix); err != nil {
					syslog.Errorf("ndp: failed to forget auto-generated address (%s) on nicID (%d): %s", addrWithPrefix, nicID, err)
				}

			case *ndpRecursiveDNSServerEvent:
				nicID, addrs, lifetime := event.nicID, event.addrs, event.lifetime
				syslog.Infof("ndp: updating expiring DNS servers (%s) on nicID (%d) with lifetime (%s)...", addrs, nicID, lifetime)
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

				n.ns.dnsClient.UpdateExpiringServers(servers, lifetime)

			default:
				panic(fmt.Sprintf("unrecognized event type: %T", event))
			}

			// Remove the event we just handled from the queue. If the queue is empty
			// after popping, then we know that all events in the queue (before taking
			// the lock) have been handled.
			n.mu.Lock()
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
