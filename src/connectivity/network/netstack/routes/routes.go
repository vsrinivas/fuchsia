// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package routes

import (
	"fmt"
	"net"
	"sort"
	"strings"
	"sync"

	"syslog"

	"netstack/util"

	"gvisor.dev/gvisor/pkg/tcpip"
	"gvisor.dev/gvisor/pkg/tcpip/header"
)

type Action uint32

const (
	ActionDeleteAll Action = iota
	ActionDeleteDynamic
	ActionDisableStatic
	ActionEnableStatic
)

const tag = "routes"

// Metric is the metric used for sorting the route table. It acts as a
// priority with a lower value being better.
type Metric uint32

// ExtendedRoute is a single route that contains the standard tcpip.Route plus
// additional attributes.
type ExtendedRoute struct {
	// Route used to build the route table to be fed into the
	// gvisor.dev/gvisor/pkg lib.
	Route tcpip.Route

	// Metric acts as a tie-breaker when comparing otherwise identical routes.
	Metric Metric

	// MetricTracksInterface is true when the metric tracks the metric of the
	// interface for this route. This means when the interface metric changes, so
	// will this route's metric. If false, the metric is static and only changed
	// explicitly by API.
	MetricTracksInterface bool

	// Dynamic marks a route as being obtained via DHCP. Such routes are removed
	// from the table when the interface goes down, vs. just being disabled.
	Dynamic bool

	// Enabled marks a route as inactive, i.e., its interface is down and packets
	// must not use this route.
	// Disabled routes are omitted when building the route table for the
	// Netstack lib.
	// This flag is used with non-dynamic routes (i.e., statically added routes)
	// to keep them in the table while their interface is down.
	Enabled bool
}

// Match matches the given address against this route.
func (er *ExtendedRoute) Match(addr tcpip.Address) bool {
	return er.Route.Destination.Contains(addr)
}

func (er *ExtendedRoute) String() string {
	var out strings.Builder
	fmt.Fprintf(&out, "%s", er.Route)
	if er.MetricTracksInterface {
		fmt.Fprintf(&out, " metric[if] %d", er.Metric)
	} else {
		fmt.Fprintf(&out, " metric[static] %d", er.Metric)
	}
	if er.Dynamic {
		fmt.Fprintf(&out, " (dynamic)")
	} else {
		fmt.Fprintf(&out, " (static)")
	}
	if !er.Enabled {
		fmt.Fprintf(&out, " (disabled)")
	}
	return out.String()
}

type ExtendedRouteTable []ExtendedRoute

func (rt ExtendedRouteTable) String() string {
	var out strings.Builder
	for _, r := range rt {
		fmt.Fprintf(&out, "%s\n", &r)
	}
	return out.String()
}

// RouteTable implements a sorted list of extended routes that is used to build
// the Netstack lib route table.
type RouteTable struct {
	mu struct {
		sync.Mutex
		routes ExtendedRouteTable
	}
}

// For debugging.
func (rt *RouteTable) dumpLocked() {
	if rt == nil {
		syslog.VLogTf(syslog.TraceVerbosity, tag, "Current Route Table:<nil>")
	} else {
		syslog.VLogTf(syslog.TraceVerbosity, tag, "Current Route Table:\n%s", rt.mu.routes)
	}
}

// For testing.
func (rt *RouteTable) Set(r []ExtendedRoute) {
	rt.mu.Lock()
	defer rt.mu.Unlock()
	rt.mu.routes = append([]ExtendedRoute(nil), r...)
}

// AddRoute inserts the given route to the table in a sorted fashion. If the
// route already exists, it simply updates that route's metric, dynamic and
// enabled fields.
func (rt *RouteTable) AddRoute(route tcpip.Route, metric Metric, tracksInterface bool, dynamic bool, enabled bool) {
	syslog.VLogTf(syslog.DebugVerbosity, tag, "RouteTable:Adding route %s with metric:%d, trackIf=%t, dynamic=%t, enabled=%t", route, metric, tracksInterface, dynamic, enabled)

	rt.mu.Lock()
	defer rt.mu.Unlock()

	// First check if the route already exists, and remove it.
	for i, er := range rt.mu.routes {
		if er.Route == route {
			rt.mu.routes = append(rt.mu.routes[:i], rt.mu.routes[i+1:]...)
			break
		}
	}

	newEr := ExtendedRoute{
		Route:                 route,
		Metric:                metric,
		MetricTracksInterface: tracksInterface,
		Dynamic:               dynamic,
		Enabled:               enabled,
	}

	// Find the target position for the new route in the table so it remains
	// sorted. Initialized to point to the end of the table.
	targetIdx := len(rt.mu.routes)
	for i, er := range rt.mu.routes {
		if Less(&newEr, &er) {
			targetIdx = i
			break
		}
	}
	// Extend the table by adding the new route at the end, then move it into its
	// proper place.
	rt.mu.routes = append(rt.mu.routes, newEr)
	if targetIdx < len(rt.mu.routes)-1 {
		copy(rt.mu.routes[targetIdx+1:], rt.mu.routes[targetIdx:])
		rt.mu.routes[targetIdx] = newEr
	}

	rt.dumpLocked()
}

// DelRoute removes the given route from the route table.
func (rt *RouteTable) DelRoute(route tcpip.Route) error {
	syslog.VLogTf(syslog.DebugVerbosity, tag, "RouteTable:Deleting route %s", route)

	rt.mu.Lock()
	defer rt.mu.Unlock()

	routeDeleted := false
	oldTable := rt.mu.routes
	rt.mu.routes = oldTable[:0]
	for _, er := range oldTable {
		// Match all fields that are non-zero.
		if er.Route.Destination == route.Destination {
			if route.NIC == 0 || route.NIC == er.Route.NIC {
				if len(route.Gateway) == 0 || route.Gateway == er.Route.Gateway {
					routeDeleted = true
					continue
				}
			}
		}
		// Not matched, remains in the route table.
		rt.mu.routes = append(rt.mu.routes, er)
	}

	if !routeDeleted {
		return fmt.Errorf("no such route")
	}

	rt.dumpLocked()
	return nil
}

// GetExtendedRouteTable returns a copy of the current extended route table.
func (rt *RouteTable) GetExtendedRouteTable() ExtendedRouteTable {
	rt.mu.Lock()
	defer rt.mu.Unlock()

	rt.dumpLocked()

	return append([]ExtendedRoute(nil), rt.mu.routes...)
}

// GetNetstackTable returns the route table to be fed into the
// gvisor.dev/gvisor/pkg lib. It contains all routes except for disabled
// ones.
func (rt *RouteTable) GetNetstackTable() []tcpip.Route {
	rt.mu.Lock()
	defer rt.mu.Unlock()

	t := make([]tcpip.Route, 0, len(rt.mu.routes))
	for _, er := range rt.mu.routes {
		if er.Enabled {
			t = append(t, er.Route)
		}
	}

	syslog.VLogTf(syslog.DebugVerbosity, tag, "RouteTable:Netstack route table: %+v", t)

	return t
}

// UpdateMetricByInterface changes the metric for all routes that track a
// given interface.
func (rt *RouteTable) UpdateMetricByInterface(nicid tcpip.NICID, metric Metric) {
	syslog.VLogf(syslog.DebugVerbosity, "RouteTable:Update route table on nic-%d metric change to %d", nicid, metric)

	rt.mu.Lock()
	defer rt.mu.Unlock()

	for i, er := range rt.mu.routes {
		if er.Route.NIC == nicid && er.MetricTracksInterface {
			rt.mu.routes[i].Metric = metric
		}
	}

	rt.sortRouteTableLocked()

	rt.dumpLocked()
}

// UpdateRoutesByInterface applies an action to the routes pointing to an interface.
func (rt *RouteTable) UpdateRoutesByInterface(nicid tcpip.NICID, action Action) {
	syslog.VLogTf(syslog.DebugVerbosity, tag, "RouteTable:Update route table for routes to nic-%d with action:%d", nicid, action)

	rt.mu.Lock()
	defer rt.mu.Unlock()

	oldTable := rt.mu.routes
	rt.mu.routes = oldTable[:0]
	for _, er := range oldTable {
		if er.Route.NIC == nicid {
			switch action {
			case ActionDeleteAll:
				continue // delete
			case ActionDeleteDynamic:
				if er.Dynamic {
					continue // delete
				}
			case ActionDisableStatic:
				if !er.Dynamic {
					er.Enabled = false
				}
			case ActionEnableStatic:
				if !er.Dynamic {
					er.Enabled = true
				}
			}
		}
		// Keep.
		rt.mu.routes = append(rt.mu.routes, er)
	}

	rt.sortRouteTableLocked()

	rt.dumpLocked()
}

// FindNIC returns the NIC-ID that the given address is routed on. This requires
// an exact route match, i.e. no default route.
func (rt *RouteTable) FindNIC(addr tcpip.Address) (tcpip.NICID, error) {
	rt.mu.Lock()
	defer rt.mu.Unlock()

	for _, er := range rt.mu.routes {
		// Ignore default routes.
		if util.IsAny(er.Route.Destination.ID()) {
			continue
		}
		if er.Match(addr) && er.Route.NIC > 0 {
			return er.Route.NIC, nil
		}
	}
	return 0, fmt.Errorf("cannot find NIC with valid destination route to %s", addr)
}

func (rt *RouteTable) sortRouteTableLocked() {
	sort.SliceStable(rt.mu.routes, func(i, j int) bool {
		return Less(&rt.mu.routes[i], &rt.mu.routes[j])
	})
}

// Less compares two routes and returns which one should appear earlier in the
// route table.
func Less(ei, ej *ExtendedRoute) bool {
	ri, rj := ei.Route, ej.Route
	riDest, rjDest := ri.Destination.ID(), rj.Destination.ID()

	// Loopback routes before non-loopback ones.
	// (as a workaround for github.com/google/gvisor/issues/1169).
	if riIsLoop, rjIsLoop := net.IP(riDest).IsLoopback(), net.IP(rjDest).IsLoopback(); riIsLoop != rjIsLoop {
		return riIsLoop
	}

	// Non-default before default one.
	if riAny, rjAny := util.IsAny(riDest), util.IsAny(rjDest); riAny != rjAny {
		return !riAny
	}

	// IPv4 before IPv6 (arbitrary choice).
	if riLen, rjLen := len(riDest), len(rjDest); riLen != rjLen {
		return riLen == header.IPv4AddressSize
	}

	// Longer prefix wins.
	if riPrefix, rjPrefix := ri.Destination.Prefix(), rj.Destination.Prefix(); riPrefix != rjPrefix {
		return riPrefix > rjPrefix
	}

	// Lower metrics wins.
	if ei.Metric != ej.Metric {
		return ei.Metric < ej.Metric
	}

	// Everything that matters is the same. At this point we still need a
	// deterministic way to tie-break. First go by destination IPs (lower wins),
	// finally use the NIC.
	for i := 0; i < len(riDest); i++ {
		if riDest[i] != rjDest[i] {
			return riDest[i] < rjDest[i]
		}
	}

	// Same prefix and destination IPs (e.g. loopback IPs), use NIC as final
	// tie-breaker.
	return ri.NIC < rj.NIC
}
