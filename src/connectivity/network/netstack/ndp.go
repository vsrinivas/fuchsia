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
	"gvisor.dev/gvisor/pkg/tcpip/stack"
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
func (*ndpDispatcher) OnOnLinkPrefixDiscovered(nicID tcpip.NICID, prefix tcpip.Subnet) bool {
	// TODO(ghanan): enable prefix discovery.
	return false
}

// OnOnLinkPrefixInvalidated implements stack.NDPDispatcher.OnOnLinkPrefixInvalidated.
func (*ndpDispatcher) OnOnLinkPrefixInvalidated(nicID tcpip.NICID, prefix tcpip.Subnet) {
	panic(fmt.Sprintf("should never end up here since we never remember an on-link prefix; got prefix = %s on nicID = %d", prefix, nicID))
}

// OnAutoGenAddress implements stack.NDPDispatcher.OnAutoGenAddress.
func (*ndpDispatcher) OnAutoGenAddress(tcpip.NICID, tcpip.AddressWithPrefix) bool {
	// TODO(ghanan): enable auto-generating addresses (SLAAC).
	return false
}

// OnAutoGenAddressInvalidated implements stack.NDPDispatcher.OnAutoGenAddressInvalidated.
func (*ndpDispatcher) OnAutoGenAddressInvalidated(nicID tcpip.NICID, addrWithPrefix tcpip.AddressWithPrefix) {
	panic(fmt.Sprintf("should never end up here since we never accept a SLAAC address; got addrWithPrefix = %s on nicID = %d", addrWithPrefix, nicID))
}

// OnRecursiveDNSServerOption implements stack.NDPDispatcher.OnRecursiveDNSServerOption.
func (*ndpDispatcher) OnRecursiveDNSServerOption(nicID tcpip.NICID, addrs []tcpip.Address, lifetime time.Duration) {
	// TODO(ghanan): pass the server list to the dns client.
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

				n.mu.Lock()
				if len(n.mu.events) > 0 {
					event = n.mu.events[0]
					n.mu.events = n.mu.events[1:]
				}
				n.mu.Unlock()

				if event != nil {
					break
				}

				// No NDP events to handle. Wait for an NDP or
				// ctx cancellation event to handle.
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
				syslog.Infof("ndp: discovered a default router (%s) on nic (%d), adding a default route to it: [%s]", addr, nicID, rt)
				if err := n.ns.AddRoute(rt, metricNotSet, true /* dynamic */); err != nil {
					syslog.Errorf("ndp: failed to add the default route [%s] for the discovered router (%s) on nicID (%d): %s", rt, addr, nicID, err)
				}

			case *ndpInvalidatedRouterEvent:
				nicID, addr := event.nicID, event.addr
				rt := defaultV6Route(nicID, addr)
				syslog.Infof("ndp: invalidating a default router (%s) from nicID (%d), removing the default route to it: [%s]", addr, nicID, rt)
				if err := n.ns.DelRoute(rt); err != nil {
					syslog.Errorf("ndp: failed to remove the default route [%s] for the invalidated router (%s) on nicID (%d): %s", rt, addr, nicID, err)
				}

			default:
				panic(fmt.Sprintf("unrecognized event type: %T", event))
			}

			// Signal tests that are waiting for the event queue to
			// be empty. We signal after handling the last event so
			// that when the test wakes up, it knows that all events
			// in the queue (up to this notification) have been
			// handled.
			if c := n.testNotifyCh; c != nil {
				n.mu.Lock()
				eventsLeft := len(n.mu.events)
				n.mu.Unlock()

				if eventsLeft == 0 {
					select {
					case c <- struct{}{}:
					default:
					}
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
