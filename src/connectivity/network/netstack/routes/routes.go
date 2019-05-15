// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package routes

import (
	"fmt"
	"sort"
	"strings"
	"sync"

	"syslog"

	"netstack/util"

	"github.com/google/netstack/tcpip"
	"github.com/google/netstack/tcpip/header"
)

type Action uint32

const (
	ActionDeleteAll Action = iota
	ActionDeleteDynamicDisableStatic
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
	// github.com/google/netstack lib.
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
	r := er.Route
	if len(addr) != len(r.Destination) {
		return false
	}
	for i := 0; i < len(r.Destination); i++ {
		if (addr[i] & r.Mask[i]) != r.Destination[i] {
			return false
		}
	}
	return true
}

func (er *ExtendedRoute) String() string {
	var out strings.Builder
	out.WriteString(fmt.Sprintf("  %v/%v", er.Route.Destination, util.PrefixLength(er.Route.Mask)))
	if len(er.Route.Gateway) > 0 {
		out.WriteString(fmt.Sprintf(" via %v", er.Route.Gateway))
	}
	out.WriteString(fmt.Sprintf(" nic %v", er.Route.NIC))
	if er.MetricTracksInterface {
		out.WriteString(fmt.Sprintf(" metric[if] %v", er.Metric))
	} else {
		out.WriteString(fmt.Sprintf(" metric[static] %v", er.Metric))
	}
	if er.Dynamic {
		out.WriteString(" (dynamic)")
	} else {
		out.WriteString(" (static)")
	}
	if !er.Enabled {
		out.WriteString(" (disabled)")
	}
	out.WriteString("\n")
	return out.String()
}

// RouteTable implements a sorted list of extended routes that is used to build
// the Netstack lib route table.
type RouteTable struct {
	mu struct {
		sync.Mutex
		routes []ExtendedRoute
	}
}

func (rt *RouteTable) String() string {
	var out strings.Builder
	for _, r := range rt.mu.routes {
		out.WriteString(fmt.Sprintf("%v", &r))
	}
	return out.String()
}

// For debugging.
func (rt *RouteTable) Dump() {
	syslog.VLogTf(syslog.TraceVerbosity, tag, "Current Route Table:\n%v", rt)
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
	syslog.VLogTf(syslog.TraceVerbosity, tag, "RouteTable:Adding route %+v with metric:%d, trackIf=%v, dynamic=%v, enabled=%v", route, metric, tracksInterface, dynamic, enabled)

	rt.mu.Lock()
	defer rt.mu.Unlock()

	// First check if the route already exists, and remove it.
	for i, er := range rt.mu.routes {
		if IsSameRoute(route, er.Route) {
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

	rt.Dump()
}

// DelRoute removes the given route from the route table.
func (rt *RouteTable) DelRoute(route tcpip.Route) error {
	syslog.VLogTf(syslog.TraceVerbosity, tag, "RouteTable:Deleting route %+v", route)

	rt.mu.Lock()
	defer rt.mu.Unlock()

	routeDeleted := false
	oldTable := rt.mu.routes
	rt.mu.routes = oldTable[:0]
	for _, er := range oldTable {
		// Match all fields that are non-zero.
		if isSameSubnet(er.Route, route) {
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

	rt.Dump()
	return nil
}

// GetExtendedRouteTable returns a copy of the current extended route table.
func (rt *RouteTable) GetExtendedRouteTable() []ExtendedRoute {
	rt.mu.Lock()
	defer rt.mu.Unlock()

	rt.Dump()

	return append([]ExtendedRoute(nil), rt.mu.routes...)
}

// GetNetstackTable returns the route table to be fed into the
// github.com/google/netstack lib. It contains all routes except for disabled
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

	syslog.VLogTf(syslog.TraceVerbosity, tag, "RouteTable:Netstack route table: %+v", t)

	return t
}

// UpdateMetricByInterface changes the metric for all routes that track a
// given interface.
func (rt *RouteTable) UpdateMetricByInterface(nicid tcpip.NICID, metric Metric) {
	syslog.VLogf(syslog.TraceVerbosity, "RouteTable:Update route table on nic-%d metric change to %d", nicid, metric)

	rt.mu.Lock()
	defer rt.mu.Unlock()

	for i, er := range rt.mu.routes {
		if er.Route.NIC == nicid && er.MetricTracksInterface {
			rt.mu.routes[i].Metric = metric
		}
	}

	rt.sortRouteTableLocked()

	rt.Dump()
}

// UpdateRoutesByInterface applies an action to the routes pointing to an interface.
func (rt *RouteTable) UpdateRoutesByInterface(nicid tcpip.NICID, action Action) {
	syslog.VLogTf(syslog.TraceVerbosity, tag, "RouteTable:Update route table for routes to nic-%d with action:%d", nicid, action)

	rt.mu.Lock()
	defer rt.mu.Unlock()

	oldTable := rt.mu.routes
	rt.mu.routes = oldTable[:0]
	for _, er := range oldTable {
		if er.Route.NIC == nicid {
			switch action {
			case ActionDeleteAll:
				continue // delete
			case ActionDeleteDynamicDisableStatic:
				if er.Dynamic {
					continue // delete
				}
				er.Enabled = false
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

	rt.Dump()
}

// FindNIC returns the NIC-ID that the given address is routed on. This requires
// an exact route match, i.e. no default route.
func (rt *RouteTable) FindNIC(addr tcpip.Address) (tcpip.NICID, error) {
	rt.mu.Lock()
	defer rt.mu.Unlock()

	for _, er := range rt.mu.routes {
		// Ignore default routes.
		if util.IsAny(er.Route.Destination) {
			continue
		}
		if er.Match(addr) && er.Route.NIC > 0 {
			return er.Route.NIC, nil
		}
	}
	return 0, fmt.Errorf("cannot find NIC with valid destination route to %v", addr)
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
	// Non-default before default one.
	if util.IsAny(ri.Destination) != util.IsAny(rj.Destination) {
		return !util.IsAny(ri.Destination)
	}

	// IPv4 before IPv6 (arbitrary choice).
	if len(ri.Destination) != len(rj.Destination) {
		return len(ri.Destination) == header.IPv4AddressSize
	}

	// Longer prefix wins.
	li, lj := util.PrefixLength(ri.Mask), util.PrefixLength(rj.Mask)
	if len(ri.Mask) == len(rj.Mask) && li != lj {
		return li > lj
	}

	// Lower metrics wins.
	if ei.Metric != ej.Metric {
		return ei.Metric < ej.Metric
	}

	// Everything that matters is the same. At this point we still need a
	// deterministic way to tie-break. First go by destination IPs (lower wins),
	// finally use the NIC.
	riDest, rjDest := []byte(ri.Destination), []byte(rj.Destination)
	for i := 0; i < len(riDest); i++ {
		if riDest[i] != rjDest[i] {
			return riDest[i] < rjDest[i]
		}
	}

	// Same prefix and destination IPs (e.g. loopback IPs), use NIC as final
	// tie-breaker.
	return ri.NIC < rj.NIC
}

func isSameSubnet(a, b tcpip.Route) bool {
	return a.Destination == b.Destination && a.Mask == b.Mask
}

// IsSameRoute returns true if two routes are the same.
func IsSameRoute(a, b tcpip.Route) bool {
	if !isSameSubnet(a, b) || a.NIC != b.NIC {
		return false
	}

	aHasGW := len(a.Gateway) > 0 && !util.IsAny(a.Gateway)
	bHasGW := len(a.Gateway) > 0 && !util.IsAny(b.Gateway)
	if aHasGW && bHasGW {
		return a.Gateway == b.Gateway
	}
	// either one or both routes have no gateway
	return !aHasGW && !bHasGW
}
