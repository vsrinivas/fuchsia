// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package routes_test

import (
	"fmt"
	"net"
	"testing"

	"netstack/routes"

	"gvisor.dev/gvisor/pkg/tcpip"
)

var testRouteTable = routes.ExtendedRouteTable{
	// nic, subnet, gateway, metric, tracksInterface, dynamic, enabled
	createExtendedRoute(1, "127.0.0.1/32", "", 100, true, false, true),                 // loopback
	createExtendedRoute(1, "::1/128", "", 100, true, false, true),                      // loopback
	createExtendedRoute(4, "192.168.1.0/24", "", 100, true, true, true),                // DHCP IP (eth)
	createExtendedRoute(2, "192.168.100.0/24", "", 200, true, true, true),              // DHCP IP (wlan)
	createExtendedRoute(3, "10.1.2.0/23", "", 100, true, false, true),                  // static IP
	createExtendedRoute(2, "110.34.0.0/16", "192.168.100.10", 500, false, false, true), // static route
	// static route (eth)
	createExtendedRoute(4, "2610:1:22:123:34:faed::/96", "fe80::250:a8ff:9:79", 100, false, false, true),
	createExtendedRoute(4, "2622:0:2200:10::/64", "", 100, true, true, true),           // RA (eth)
	createExtendedRoute(4, "0.0.0.0/0", "192.168.1.1", 100, true, true, true),          // default (eth)
	createExtendedRoute(2, "0.0.0.0/0", "192.168.100.10", 200, true, true, true),       // default (wlan)
	createExtendedRoute(4, "::/0", "fe80::210:6e00:fe11:3265", 100, true, false, true), // default (eth)
}

func createRoute(nicid tcpip.NICID, subnet string, gateway string) tcpip.Route {
	_, s, err := net.ParseCIDR(subnet)
	if err != nil {
		panic(err)
	}
	sn, err := tcpip.NewSubnet(tcpip.Address(s.IP), tcpip.AddressMask(s.Mask))
	if err != nil {
		panic(err)
	}
	return tcpip.Route{
		Destination: sn,
		Gateway:     ipStringToAddress(gateway),
		NIC:         nicid,
	}
}

func createExtendedRoute(nicid tcpip.NICID, subnet string, gateway string, metric routes.Metric, tracksInterface bool, dynamic bool, enabled bool) routes.ExtendedRoute {
	return routes.ExtendedRoute{
		Route:                 createRoute(nicid, subnet, gateway),
		Metric:                metric,
		MetricTracksInterface: tracksInterface,
		Dynamic:               dynamic,
		Enabled:               enabled,
	}
}

func ipStringToAddress(ipStr string) tcpip.Address {
	return ipToAddress(net.ParseIP(ipStr))
}

func ipToAddress(ip net.IP) tcpip.Address {
	if v4 := ip.To4(); v4 != nil {
		return tcpip.Address(v4)
	}
	return tcpip.Address(ip)
}

func TestExtendedRouteMatch(t *testing.T) {
	for _, tc := range []struct {
		subnet string
		addr   string
		want   bool
	}{
		{"192.168.10.0/24", "192.168.10.0", true},
		{"192.168.10.0/24", "192.168.10.1", true},
		{"192.168.10.0/24", "192.168.10.10", true},
		{"192.168.10.0/24", "192.168.10.255", true},
		{"192.168.10.0/24", "192.168.11.1", false},
		{"192.168.10.0/24", "192.168.0.1", false},
		{"192.168.10.0/24", "192.167.10.1", false},
		{"192.168.10.0/24", "193.168.10.1", false},
		{"192.168.10.0/24", "0.0.0.0", false},

		{"123.220.0.0/14", "123.220.11.22", true},
		{"123.220.0.0/14", "123.221.11.22", true},
		{"123.220.0.0/14", "123.222.11.22", true},
		{"123.220.0.0/14", "123.223.11.22", true},
		{"123.220.0.0/14", "123.224.11.22", false},

		{"0.0.0.0/0", "0.0.0.0", true},
		{"0.0.0.0/0", "1.1.1.1", true},
		{"0.0.0.0/0", "255.255.255.255", true},

		{"2402:f000:5:8401::/64", "2402:f000:5:8401::", true},
		{"2402:f000:5:8401::/64", "2402:f000:5:8401::1", true},
		{"2402:f000:5:8401::/64", "2402:f000:5:8401:1:12:123::", true},
		{"2402:f000:5:8401::/64", "2402:f000:5:8402::", false},
		{"2402:f000:5:8401::/64", "2402:f000:15:8401::", false},
		{"2402:f000:5:8401::/64", "2402::5:8401::", false},
		{"2402:f000:5:8401::/64", "2400:f000:5:8401::", false},

		{"::/0", "::", true},
		{"::/0", "1:1:1:1:1:1:1:1", true},
		{"::/0", "ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff", true},
	} {
		t.Run("RouteMatch", func(t *testing.T) {
			er := createExtendedRoute(1, tc.subnet, "", 100, true, true, true)
			addr := ipStringToAddress(tc.addr)
			if got := er.Match(addr); got != tc.want {
				t.Errorf("got match addr %s in subnet %s = %t, want = %t", tc.addr, tc.subnet, got, tc.want)
			}
		})
	}
}

func TestSortingLess(t *testing.T) {
	for _, tc := range []struct {
		subnet1 string
		metric1 routes.Metric
		nic1    tcpip.NICID

		subnet2 string
		metric2 routes.Metric
		nic2    tcpip.NICID

		want bool
	}{
		// non-default before default
		{"100.99.24.12/32", 100, 1, "0.0.0.0/0", 100, 1, true},
		{"10.144.0.0/12", 100, 1, "0.0.0.0/0", 100, 2, true},
		{"127.0.0.1/24", 100, 1, "0.0.0.0/0", 100, 1, true},
		{"2511:5f32:4:6:124::2:1/128", 100, 1, "::/0", 100, 1, true},
		{"fe80:ffff:0:1234:5:6::/96", 100, 2, "::/0", 100, 1, true},
		{"::1/128", 100, 2, "::/0", 100, 2, true},
		// IPv4 before IPv6
		{"100.99.24.12/32", 100, 1, "2511:5f32:4:6:124::2/120", 100, 1, true},
		{"100.99.24.12/32", 100, 2, "2605:32::12/128", 100, 1, true},
		{"127.0.0.1/24", 100, 1, "::1/128", 100, 1, true},
		{"0.0.0.0/0", 100, 1, "::/0", 100, 3, true},
		// longer prefix wins
		{"100.99.24.12/32", 100, 2, "100.99.24.12/31", 100, 1, true},
		{"100.99.24.12/32", 100, 1, "10.144.0.0/12", 100, 1, true},
		{"100.99.24.128/25", 100, 5, "128.0.0.1/24", 100, 3, true},
		{"2511:5f32:4:6:124::2:12/128", 100, 1, "2511:5f32:4:6:124::2:12/126", 100, 1, true},
		{"2511:5f32:4:6:124::2:12/128", 100, 1, "2605:32::12/127", 100, 1, true},
		{"fe80:ffff:0:1234:5:6::/96", 100, 2, "2511:5f32:4:6:124::/90", 100, 1, true},
		{"2511:5f32:4:6:124::2:1/128", 100, 1, "::1/120", 100, 2, true},
		// lower metric
		{"100.99.24.12/32", 100, 1, "10.1.21.31/32", 101, 1, true},
		{"100.99.24.12/32", 101, 1, "10.1.21.31/32", 100, 2, false},
		{"100.99.2.0/23", 100, 3, "10.1.22.0/23", 101, 1, true},
		{"100.99.2.0/23", 101, 1, "10.1.22.0/23", 100, 1, false},
		{"2511:5f32:4:6:124::2:1/128", 100, 1, "2605:32::12/128", 101, 1, true},
		{"2511:5f32:4:6:124::2:1/128", 101, 3, "2605:32::12/128", 100, 2, false},
		{"2511:5f32:4:6:124::2:1/96", 100, 1, "fe80:ffff:0:1234:5:6::/96", 101, 4, true},
		{"2511:5f32:4:6:124::2:1/96", 101, 1, "fe80:ffff:0:1234:5:6::/96", 100, 4, false},
		// tie-breaker: destination IPs
		{"10.1.21.31/32", 100, 2, "100.99.24.12/32", 100, 1, true},
		{"100.99.24.12/32", 100, 1, "10.1.21.31/32", 100, 3, false},
		{"2511:5f32:4:6:124::2:1/128", 100, 1, "2605:32::12/128", 100, 1, true},
		{"2605:32::12/128", 100, 1, "2511:5f32:4:6:124::2:1/128", 100, 1, false},
		// tie-breaker: NIC
		{"10.1.21.31/32", 100, 1, "10.1.21.31/32", 100, 2, true},
		{"10.1.21.31/32", 100, 2, "10.1.21.31/32", 100, 1, false},
		{"2511:5f32:4:6:124::2:1/128", 100, 1, "2511:5f32:4:6:124::2:1/128", 100, 2, true},
		{"2511:5f32:4:6:124::2:1/128", 100, 2, "2511:5f32:4:6:124::2:1/128", 100, 1, false},
	} {
		name := fmt.Sprintf("Test-%s@nic%d[m:%d]_<_%s@nic%d[m:%d]", tc.subnet1, tc.nic1, tc.metric1, tc.subnet2, tc.nic2, tc.metric2)
		t.Run(name, func(t *testing.T) {
			e1 := createExtendedRoute(tc.nic1, tc.subnet1, "", tc.metric1, true, true, true)
			e2 := createExtendedRoute(tc.nic2, tc.subnet2, "", tc.metric2, true, true, true)
			if got := routes.Less(&e1, &e2); got != tc.want {
				t.Errorf("got Less(%s, %s) = %t, want = %t", &e1, &e2, got, tc.want)
			}
			// reverse test
			reverseWant := !tc.want
			if got := routes.Less(&e2, &e1); got != reverseWant {
				t.Errorf("got Less(%s, %s) = %t, want = %t", &e2, &e1, got, reverseWant)
			}
		})
	}
}

func isSameRouteTableImpl(rt1, rt2 []routes.ExtendedRoute, checkAttributes bool) bool {
	if len(rt1) != len(rt2) {
		return false
	}
	for i, r1 := range rt1 {
		r2 := rt2[i]
		if r1.Route != r2.Route {
			return false
		}
		if checkAttributes && (r1.Metric != r2.Metric || r1.MetricTracksInterface != r2.MetricTracksInterface || r1.Dynamic != r2.Dynamic || r1.Enabled != r2.Enabled) {
			return false
		}
	}
	return true
}

func isSameRouteTable(rt1, rt2 []routes.ExtendedRoute) bool {
	return isSameRouteTableImpl(rt1, rt2, true /* checkAttributes */)
}

func isSameRouteTableSkippingAttributes(rt1, rt2 []routes.ExtendedRoute) bool {
	return isSameRouteTableImpl(rt1, rt2, false /* checkAttributes */)
}

func TestAddRoute(t *testing.T) {
	for _, tc := range []struct {
		name  string
		order []int
	}{
		// different orders
		{"Add1", []int{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10}},
		{"Add2", []int{10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0}},
		{"Add3", []int{5, 3, 9, 2, 6, 0, 7, 10, 1, 4, 8}},
		{"Add4", []int{6, 5, 7, 4, 8, 3, 9, 2, 10, 1, 0}},

		// different orders and duplicates
		{"Add5", []int{0, 0, 1, 2, 3, 4, 2, 5, 6, 7, 1, 8, 9, 10, 0}},
		{"Add6", []int{10, 9, 8, 4, 7, 6, 5, 2, 4, 3, 2, 1, 0, 5, 5}},
		{"Add7", []int{5, 3, 9, 2, 6, 0, 7, 10, 10, 1, 3, 4, 8}},
		{"Add8", []int{6, 6, 6, 5, 7, 4, 8, 3, 9, 9, 2, 7, 10, 1, 0}},
	} {
		t.Run(tc.name, func(t *testing.T) {
			// Create route table by adding all routes in the given order.
			tb := routes.RouteTable{}
			for _, j := range tc.order {
				r := testRouteTable[j]
				tb.AddRoute(r.Route, r.Metric, r.MetricTracksInterface, r.Dynamic, r.Enabled)
			}
			tableWanted := testRouteTable
			tableGot := tb.GetExtendedRouteTable()
			if len(tableGot) != len(tableWanted) {
				t.Fatalf("got len(table) = %d, want len(table) = %d", len(tableGot), len(tableWanted))
			}
			if !isSameRouteTable(tableGot, tableWanted) {
				t.Errorf("got\n%s, want\n%s", tableGot, tableWanted)
			}
		})
	}

	// Adding a route that already exists but with different dynamic/enabled
	// attributes will just overwrite the entry with the new attributes. The
	// position in the table stays the same.
	t.Run("Changing dynamic/enabled", func(t *testing.T) {
		tb := routes.RouteTable{}
		tb.Set(testRouteTable)
		for i, r := range testRouteTable {
			r.Dynamic = !r.Dynamic
			tb.AddRoute(r.Route, r.Metric, r.MetricTracksInterface, r.Dynamic, r.Enabled)
			tableWanted := testRouteTable
			tableGot := tb.GetExtendedRouteTable()
			if tableGot[i].Dynamic != r.Dynamic {
				t.Errorf("got tableGot[%d].Dynamic = %t, want %t", i, tableGot[i].Dynamic, r.Dynamic)
			}
			if !isSameRouteTableSkippingAttributes(tableGot, tableWanted) {
				t.Errorf("got\n%s, want\n%s", tableGot, tableWanted)
			}

			r.Enabled = !r.Enabled
			tb.AddRoute(r.Route, r.Metric, r.MetricTracksInterface, r.Dynamic, r.Enabled)
			tableGot = tb.GetExtendedRouteTable()
			if tableGot[i].Enabled != r.Enabled {
				t.Errorf("got tableGot[%d].Enabled = %t, want %t", i, tableGot[i].Enabled, r.Enabled)
			}
			if !isSameRouteTableSkippingAttributes(tableGot, tableWanted) {
				t.Errorf("got\n%s, want\n%s", tableGot, tableWanted)
			}
		}
	})

	// The metric is used as a tie-breaker when routes have the same prefix length
	t.Run("Changing metric", func(t *testing.T) {
		r0 := createRoute(1, "0.0.0.0/0", "192.168.1.1")
		r1 := createRoute(2, "0.0.0.0/0", "192.168.100.10")

		// 1.test - r0 gets lower metric.
		{
			var tb routes.RouteTable
			tb.AddRoute(r0, 100, true, true, true)
			tb.AddRoute(r1, 200, true, true, true)
			tableGot := tb.GetExtendedRouteTable()
			if got, want := tableGot[0].Route, r0; got != want {
				t.Errorf("got = %s, want = %s", got, want)
			}
			if got, want := tableGot[1].Route, r1; got != want {
				t.Errorf("got = %s, want = %s", got, want)
			}
		}

		// 2.test - r1 gets lower metric.
		{
			var tb routes.RouteTable
			tb.AddRoute(r0, 200, true, true, true)
			tb.AddRoute(r1, 100, true, true, true)
			tableGot := tb.GetExtendedRouteTable()
			if got, want := tableGot[0].Route, r1; got != want {
				t.Errorf("got = %s, want = %s", got, want)
			}
			if got, want := tableGot[1].Route, r0; got != want {
				t.Errorf("got = %s, want = %s", got, want)
			}
		}
	})
}

func TestDelRoute(t *testing.T) {
	for _, tc := range []struct {
		name  string
		order []int
	}{
		{"Del1", []int{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10}},
		{"Del2", []int{10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0}},
		{"Del3", []int{6, 5, 7, 4, 8, 3, 9, 2, 10, 1, 0}},
		{"Del4", []int{0}},
		{"Del5", []int{1}},
		{"Del6", []int{9}},
		{"Del7", []int{0, 0, 1, 1, 5, 5, 9, 9, 10, 10}},
		{"Del8", []int{5, 8, 5, 0, 1, 9, 1, 5}},
	} {
		t.Run(tc.name, func(t *testing.T) {
			tb := routes.RouteTable{}
			tb.Set(testRouteTable)
			validRoutes := make([]bool, len(testRouteTable))
			for i := range validRoutes {
				validRoutes[i] = true
			}
			for _, d := range tc.order {
				toDel := testRouteTable[d]
				tb.DelRoute(toDel.Route)
				validRoutes[d] = false
			}
			tableGot := tb.GetExtendedRouteTable()
			tableWant := routes.ExtendedRouteTable{}
			for i, valid := range validRoutes {
				if valid {
					tableWant = append(tableWant, testRouteTable[i])
				}
			}
			if !isSameRouteTable(tableGot, tableWant) {
				t.Errorf("got\n%s, want\n%s", tableGot, tableWant)
			}
		})
	}
}

func TestUpdateMetricByInterface(t *testing.T) {
	// Updating the metric for the an interface updates the metric for all routes
	// that track that interface.
	for nicid := tcpip.NICID(1); nicid <= 4; nicid++ {
		t.Run(fmt.Sprintf("Change-NIC-%d-metric-updates-route-metric", nicid), func(t *testing.T) {
			tb := routes.RouteTable{}
			tb.Set(testRouteTable)
			newMetric := routes.Metric(1000 + nicid)
			// Verify existing table does not use the new metric yet.
			tableGot := tb.GetExtendedRouteTable()
			for _, r := range tableGot {
				if r.Metric == newMetric {
					t.Fatalf("Error: r.Metric said to invalid value before test start")
				}
			}

			tb.UpdateMetricByInterface(nicid, newMetric)

			tableGot = tb.GetExtendedRouteTable()
			for _, r := range tableGot {
				if r.Route.NIC != nicid {
					continue
				}
				if !r.MetricTracksInterface && r.Metric == newMetric {
					t.Errorf("got MetricTracksInterface==false && Metric=%d, want metric!=%d", r.Metric, newMetric)
				}
				if r.MetricTracksInterface && r.Metric != newMetric {
					t.Errorf("got MetricTracksInterface==true && Metric=%d, want metric=%d", r.Metric, newMetric)
				}
			}
		})
	}

	// A metric change should trigger a re-sort of the routing table.
	t.Run(fmt.Sprint("Change-metric-resorts-table"), func(t *testing.T) {
		r0 := createRoute(0, "0.0.0.0/0", "192.168.1.1")
		r1 := createRoute(1, "0.0.0.0/0", "192.168.100.10")

		// Initially r0 has lower metric and is ahead in the table.
		tb := routes.RouteTable{}
		tb.AddRoute(r0, 100, true, true, true)
		tb.AddRoute(r1, 200, true, true, true)
		{
			tableGot := tb.GetExtendedRouteTable()
			if got, want := tableGot[0].Route, r0; got != want {
				t.Errorf("got = %s, want = %s", got, want)
			}
			if got, want := tableGot[1].Route, r1; got != want {
				t.Errorf("got = %s, want = %s", got, want)
			}
		}

		// Lowering nic1's metric should be reflected in r1's metric and promote it
		// in the route table.
		tb.UpdateMetricByInterface(1, 50)
		{
			tableGot := tb.GetExtendedRouteTable()
			if got, want := tableGot[0].Route, r1; got != want {
				t.Errorf("got = %s, want = %s", got, want)
			}
			if got, want := tableGot[1].Route, r0; got != want {
				t.Errorf("got = %s, want = %s", got, want)
			}
		}
	})
}

func TestUpdateRoutesByInterface(t *testing.T) {
	for nicid := tcpip.NICID(1); nicid <= 4; nicid++ {
		// Test the normal case where on DOWN netstack removes dynamic routes and
		// disables static ones (ActionDeleteDynamicDisableStatic), and on up
		// it re-enables static routes (ActionEnableStatic).
		t.Run(fmt.Sprintf("Down-Up_NIC-%d", nicid), func(t *testing.T) {
			tb := routes.RouteTable{}
			tb.Set(testRouteTable)
			tableGot := tb.GetExtendedRouteTable()
			for _, r := range tableGot {
				if got, want := r.Enabled, true; got != want {
					t.Errorf("got Enabled = %t, want = %t, route = %s", got, want, &r)
				}
			}

			// Down -> 1.Remove dynamic routes.
			tb.UpdateRoutesByInterface(nicid, routes.ActionDeleteDynamic)

			// Verify all dynamic routes to this NIC are gone, and static ones are
			// still enabled.
			tableGot = tb.GetExtendedRouteTable()
			for _, r := range tableGot {
				if r.Route.NIC != nicid {
					continue
				}
				if got, want := r.Enabled, true; got != want {
					t.Errorf("got Enabled = %t, want = %t, route = %s", got, want, &r)
				}
				if got, want := r.Dynamic, false; got != want {
					t.Errorf("got Dynamic = %t, want = %t, route = %s", got, want, &r)
				}
			}

			// Down -> 2.Disable static ones.
			tb.UpdateRoutesByInterface(nicid, routes.ActionDisableStatic)
			// Verify all dynamic routes to this NIC are gone, and static ones are
			// disabled.
			tableGot = tb.GetExtendedRouteTable()
			for _, r := range tableGot {
				if r.Route.NIC != nicid {
					continue
				}
				if got, want := r.Enabled, false; got != want {
					t.Errorf("got Enabled = %t, want = %t, route = %s", got, want, &r)
				}
				if got, want := r.Dynamic, false; got != want {
					t.Errorf("got Dynamic = %t, want = %t, route = %s", got, want, &r)
				}
			}

			// Up -> Re-enable static routes.
			tb.UpdateRoutesByInterface(nicid, routes.ActionEnableStatic)

			// Verify dynamic routes to this NIC are still gone, and static ones are
			// enabled.
			tableGot = tb.GetExtendedRouteTable()
			for _, r := range tableGot {
				if r.Route.NIC != nicid {
					continue
				}
				if got, want := r.Enabled, true; got != want {
					t.Errorf("got Enabled = %t, want = %t, route = %s", got, want, &r)
				}
				if got, want := r.Dynamic, false; got != want {
					t.Errorf("got Dynamic = %t, want = %t, route = %s", got, want, &r)
				}
			}
		})

		// Test the special case where the interface is removed and in response all
		// routes to this interface are removed (ActionDeleteAll).
		t.Run(fmt.Sprintf("Remove_NIC-%d", nicid), func(t *testing.T) {
			tb := routes.RouteTable{}
			tb.Set(testRouteTable)
			tableGot := tb.GetExtendedRouteTable()
			for _, r := range tableGot {
				if got, want := r.Enabled, true; got != want {
					t.Errorf("got Enabled = %t, want = %t, route = %s", got, want, &r)
				}
			}

			// Remove all routes to nicid.
			tb.UpdateRoutesByInterface(nicid, routes.ActionDeleteAll)

			// Verify all routes to this NIC are gone.
			tableGot = tb.GetExtendedRouteTable()
			for _, r := range tableGot {
				if r.Route.NIC == nicid {
					t.Errorf("got route pointing to NIC-%d, want none, route = %s", nicid, &r)
				}
			}
		})
	}
}

func TestGetNetstackTable(t *testing.T) {
	for _, tc := range []struct {
		name     string
		disabled []int
	}{
		{"GetNsTable1", []int{}},
		{"GetNsTable2", []int{0}},
		{"GetNsTable3", []int{10}},
		{"GetNsTable4", []int{1}},
		{"GetNsTable5", []int{9}},
		{"GetNsTable6", []int{3, 5, 8}},
		{"GetNsTable7", []int{0, 1, 5, 6, 8, 9, 10}},
		{"GetNsTable8", []int{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10}},
	} {
		t.Run(tc.name, func(t *testing.T) {
			// We have no way to directly disable routes in the route table, but we
			// can use the Set() command to set a table with pre-disabled routes.
			testRouteTable := append([]routes.ExtendedRoute(nil), testRouteTable...)
			// Disable a few routes.
			for _, i := range tc.disabled {
				testRouteTable[i].Enabled = false
			}
			var tb routes.RouteTable
			tb.Set(testRouteTable)

			tableGot := tb.GetNetstackTable()

			// Verify no disabled routes are in the Netstack table we got.
			i := 0
			for _, r := range testRouteTable {
				if r.Enabled {
					if got, want := tableGot[i], r.Route; got != want {
						t.Errorf("got = %s, want = %s", got, want)
					}
					i++
				}
			}
		})
	}
}

func TestFindNIC(t *testing.T) {
	for _, tc := range []struct {
		name      string
		addr      string
		nicWanted tcpip.NICID // 0 means not found
	}{
		{"FindNic1", "127.0.0.1", 1},
		{"FindNic2", "127.0.0.0", 0},
		{"FindNic3", "192.168.1.234", 4},
		{"FindNic4", "192.168.1.1", 4},
		{"FindNic5", "192.168.2.1", 0},
		{"FindNic6", "192.168.100.1", 2},
		{"FindNic7", "192.168.100.10", 2},
		{"FindNic8", "192.168.101.10", 0},
		{"FindNic9", "10.1.2.1", 3},
		{"FindNic10", "10.1.3.1", 3},
		{"FindNic11", "10.1.4.1", 0},
	} {
		t.Run(tc.name, func(t *testing.T) {
			tb := routes.RouteTable{}
			tb.Set(testRouteTable)

			nicGot, err := tb.FindNIC(ipStringToAddress(tc.addr))
			if err != nil && tc.nicWanted > 0 {
				t.Errorf("got nic = <unknown>, want = %d", tc.nicWanted)
			} else if err == nil && tc.nicWanted != nicGot {
				t.Errorf("got nic = %d, want = %d", nicGot, tc.nicWanted)
			}
		})
	}
}
