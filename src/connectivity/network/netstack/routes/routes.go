// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//go:build !build_with_native_toolchain

package routes

import (
	"errors"
	"fmt"
	"net"
	"sort"
	"strings"

	syslog "go.fuchsia.dev/fuchsia/src/lib/syslog/go"

	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/sync"
	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/util"

	"gvisor.dev/gvisor/pkg/tcpip"
	"gvisor.dev/gvisor/pkg/tcpip/header"
	"gvisor.dev/gvisor/pkg/tcpip/stack"
)

type Action uint32

const (
	ActionDeleteAll Action = iota
	ActionDeleteDynamic
	ActionDisableStatic
	ActionEnableStatic
)

const tag = "routes"

var (
	ErrNoSuchRoute = errors.New("no such route")
	ErrNoSuchNIC   = errors.New("no such NIC")
)

// Metric is the metric used for sorting the route table. It acts as a
// priority with a lower value being better.
type Metric uint32

// Preference is the preference for a route.
type Preference int

const (
	// LowPreference indicates that a route has a low preference.
	LowPreference Preference = iota

	// MediumPreference indicates that a route has a medium (default)
	// preference.
	MediumPreference

	// HighPreference indicates that a route has a high preference.
	HighPreference
)

// ExtendedRoute is a single route that contains the standard tcpip.Route plus
// additional attributes.
type ExtendedRoute struct {
	// Route used to build the route table to be fed into the
	// gvisor.dev/gvisor/pkg lib.
	Route tcpip.Route

	// Prf is the preference of the route when comparing routes to the same
	// destination.
	Prf Preference

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
	sync.Mutex
	routes ExtendedRouteTable
}

// For debugging.
func (rt *RouteTable) dumpLocked() {
	if rt == nil {
		syslog.VLogTf(syslog.TraceVerbosity, tag, "Current Route Table:<nil>")
	} else {
		syslog.VLogTf(syslog.TraceVerbosity, tag, "Current Route Table:\n%s", rt.routes)
	}
}

// HasDefaultRoutes returns whether an interface has default IPv4/IPv6 routes.
func (rt *RouteTable) HasDefaultRouteLocked(nicid tcpip.NICID) (bool, bool) {
	var v4, v6 bool
	for _, er := range rt.routes {
		if er.Route.NIC == nicid && er.Enabled {
			if er.Route.Destination.Equal(header.IPv4EmptySubnet) {
				v4 = true
			} else if er.Route.Destination.Equal(header.IPv6EmptySubnet) {
				v6 = true
			}
		}
	}
	return v4, v6
}

// For testing.
func (rt *RouteTable) Set(r []ExtendedRoute) {
	rt.Lock()
	defer rt.Unlock()
	rt.routes = append([]ExtendedRoute(nil), r...)
}

func (rt *RouteTable) AddRouteLocked(route tcpip.Route, prf Preference, metric Metric, tracksInterface bool, dynamic bool, enabled bool) {
	syslog.VLogTf(syslog.DebugVerbosity, tag, "RouteTable:Adding route %s with prf=%d metric=%d, trackIf=%t, dynamic=%t, enabled=%t", route, prf, metric, tracksInterface, dynamic, enabled)

	// First check if the route already exists, and remove it.
	for i, er := range rt.routes {
		if er.Route == route {
			rt.routes = append(rt.routes[:i], rt.routes[i+1:]...)
			break
		}
	}

	newEr := ExtendedRoute{
		Route:                 route,
		Prf:                   prf,
		Metric:                metric,
		MetricTracksInterface: tracksInterface,
		Dynamic:               dynamic,
		Enabled:               enabled,
	}

	// Find the target position for the new route in the table so it remains
	// sorted.
	targetIdx := sort.Search(len(rt.routes), func(i int) bool {
		return Less(&newEr, &rt.routes[i])
	})
	// Extend the table by adding the new route at the end, then move it into its
	// proper place.
	rt.routes = append(rt.routes, newEr)
	if targetIdx < len(rt.routes)-1 {
		copy(rt.routes[targetIdx+1:], rt.routes[targetIdx:])
		rt.routes[targetIdx] = newEr
	}

	rt.dumpLocked()
}

// AddRoute inserts the given route to the table in a sorted fashion. If the
// route already exists, it simply updates that route's preference, metric,
// dynamic, and enabled fields.
func (rt *RouteTable) AddRoute(route tcpip.Route, prf Preference, metric Metric, tracksInterface bool, dynamic bool, enabled bool) {
	rt.Lock()
	defer rt.Unlock()

	rt.AddRouteLocked(route, prf, metric, tracksInterface, dynamic, enabled)
}

func (rt *RouteTable) DelRouteLocked(route tcpip.Route) []ExtendedRoute {
	syslog.VLogTf(syslog.DebugVerbosity, tag, "RouteTable:Deleting route %s", route)

	var routesDeleted []ExtendedRoute
	oldTable := rt.routes
	rt.routes = oldTable[:0]
	for _, er := range oldTable {
		if er.Route.Destination == route.Destination && er.Route.NIC == route.NIC {
			// Match any route if Gateway is empty.
			if len(route.Gateway) == 0 || er.Route.Gateway == route.Gateway {
				routesDeleted = append(routesDeleted, er)
				continue
			}
		}
		// Not matched, remains in the route table.
		rt.routes = append(rt.routes, er)
	}

	if len(routesDeleted) == 0 {
		return nil
	}

	rt.dumpLocked()
	return routesDeleted
}

// DelRoute removes matching routes from the route table, returning them.
func (rt *RouteTable) DelRoute(route tcpip.Route) []ExtendedRoute {
	rt.Lock()
	defer rt.Unlock()

	return rt.DelRouteLocked(route)
}

// GetExtendedRouteTable returns a copy of the current extended route table.
func (rt *RouteTable) GetExtendedRouteTable() ExtendedRouteTable {
	rt.Lock()
	defer rt.Unlock()

	rt.dumpLocked()

	return append([]ExtendedRoute(nil), rt.routes...)
}

// UpdateStack updates stack with the current route table.
func (rt *RouteTable) UpdateStackLocked(stack *stack.Stack, onUpdateSucceeded func()) {
	t := make([]tcpip.Route, 0, len(rt.routes))
	for _, er := range rt.routes {
		if er.Enabled {
			t = append(t, er.Route)
		}
	}
	stack.SetRouteTable(t)

	_ = syslog.VLogTf(syslog.DebugVerbosity, tag, "UpdateStack route table: %+v", t)
	onUpdateSucceeded()
}

// UpdateStack updates stack with the current route table.
func (rt *RouteTable) UpdateStack(stack *stack.Stack, onUpdateSucceeded func()) {
	rt.Lock()
	defer rt.Unlock()

	rt.UpdateStackLocked(stack, onUpdateSucceeded)
}

// UpdateMetricByInterface changes the metric for all routes that track a
// given interface.
func (rt *RouteTable) UpdateMetricByInterface(nicid tcpip.NICID, metric Metric) {
	syslog.VLogf(syslog.DebugVerbosity, "RouteTable:Update route table on nic-%d metric change to %d", nicid, metric)

	rt.Lock()
	defer rt.Unlock()

	for i, er := range rt.routes {
		if er.Route.NIC == nicid && er.MetricTracksInterface {
			rt.routes[i].Metric = metric
		}
	}

	rt.sortRouteTableLocked()

	rt.dumpLocked()
}

func (rt *RouteTable) UpdateRoutesByInterfaceLocked(nicid tcpip.NICID, action Action) {
	syslog.VLogTf(syslog.DebugVerbosity, tag, "RouteTable:Update route table for routes to nic-%d with action:%d", nicid, action)

	oldTable := rt.routes
	rt.routes = oldTable[:0]
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
		rt.routes = append(rt.routes, er)
	}

	rt.sortRouteTableLocked()

	rt.dumpLocked()
}

// UpdateRoutesByInterface applies an action to the routes pointing to an interface.
func (rt *RouteTable) UpdateRoutesByInterface(nicid tcpip.NICID, action Action) {
	rt.Lock()
	defer rt.Unlock()

	rt.UpdateRoutesByInterfaceLocked(nicid, action)
}

// FindNIC returns the NIC-ID that the given address is routed on. This requires
// an exact route match, i.e. no default route.
func (rt *RouteTable) FindNIC(addr tcpip.Address) (tcpip.NICID, error) {
	rt.Lock()
	defer rt.Unlock()

	for _, er := range rt.routes {
		// Ignore default routes.
		if util.IsAny(er.Route.Destination.ID()) {
			continue
		}
		if er.Match(addr) && er.Route.NIC > 0 {
			return er.Route.NIC, nil
		}
	}
	return 0, ErrNoSuchNIC
}

func (rt *RouteTable) sortRouteTableLocked() {
	sort.SliceStable(rt.routes, func(i, j int) bool {
		return Less(&rt.routes[i], &rt.routes[j])
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

	// On-link wins.
	if riOnLink, rjOnLink := len(ri.Gateway) == 0, len(rj.Gateway) == 0; riOnLink != rjOnLink {
		return riOnLink
	}

	// Higher preference wins.
	if ei.Prf != ej.Prf {
		return ei.Prf > ej.Prf
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
