// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package netstack

import (
	"context"
	"fmt"
	"net"
	"sync"
	"sync/atomic"
	"syscall/zx"
	"syscall/zx/fidl"
	"time"

	"syslog"

	"netstack/connectivity"
	"netstack/dhcp"
	"netstack/dns"
	"netstack/filter"
	"netstack/link"
	"netstack/link/bridge"
	"netstack/link/eth"
	"netstack/routes"
	"netstack/util"
	networking_metrics "networking_metrics_golib"

	"fidl/fuchsia/cobalt"
	"fidl/fuchsia/device"
	"fidl/fuchsia/hardware/ethernet"
	"fidl/fuchsia/netstack"

	"gvisor.dev/gvisor/pkg/tcpip"
	"gvisor.dev/gvisor/pkg/tcpip/header"
	"gvisor.dev/gvisor/pkg/tcpip/link/loopback"
	"gvisor.dev/gvisor/pkg/tcpip/link/sniffer"
	"gvisor.dev/gvisor/pkg/tcpip/network/arp"
	"gvisor.dev/gvisor/pkg/tcpip/network/ipv4"
	"gvisor.dev/gvisor/pkg/tcpip/network/ipv6"
	"gvisor.dev/gvisor/pkg/tcpip/stack"
)

const (
	defaultInterfaceMetric routes.Metric = 100

	metricNotSet routes.Metric = 0

	lowPriorityRoute routes.Metric = 99999

	ipv4Loopback tcpip.Address = "\x7f\x00\x00\x01"
	ipv6Loopback tcpip.Address = "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01"

	dhcpAcquisition    = 60 * time.Second
	dhcpBackoff        = 1 * time.Second
	dhcpRetransmission = 4 * time.Second
)

var ipv4LoopbackBytes = func() [4]byte {
	var b [4]uint8
	copy(b[:], ipv4Loopback)
	return b
}()
var ipv6LoopbackBytes = func() [16]byte {
	var b [16]uint8
	copy(b[:], ipv6Loopback)
	return b
}()

func ipv6LinkLocalOnLinkRoute(nicID tcpip.NICID) tcpip.Route {
	return onLinkV6Route(nicID, header.IPv6LinkLocalPrefix.Subnet())
}

type stats struct {
	tcpip.Stats
	SocketCount      bindingSetCounterStat
	SocketsCreated   tcpip.StatCounter
	SocketsDestroyed tcpip.StatCounter
}

// Map from Cobalt metric ID to metric value.
type nicStats map[uint32]uint64

func runCobaltClient(ctx context.Context, cobaltLogger *cobalt.LoggerWithCtxInterface, stats *stats, stk *stack.Stack) error {
	// Metric                         | Sampling Interval | Aggregation Strategy
	// SocketCountMax                 | socket creation   | max
	// SocketsCreated                 | 1 minute          | delta
	// SocketsDestroyed               | 1 minute          | delta
	// PacketsSent                    | 1 minute          | delta
	// PacketsReceived                | 1 minute          | delta
	// BytesSent                      | 1 minute          | delta
	// BytesReceived                  | 1 minute          | delta
	// TCPConnectionsEstablishedTotal | 1 minute          | max
	// TCPConnectionsClosed           | 1 minute          | delta
	// TCPConnectionsReset            | 1 minute          | delta
	// TCPConnectionsTimedout         | 1 minute          | delta
	ticker := time.NewTicker(time.Minute)
	defer ticker.Stop()

	var lastCreated, lastDestroyed uint64
	var lastTcpConnectionsClosed, lastTcpConnectionsReset, lastTcpConnectionsTimedOut uint64
	previousTime := time.Now()

	lastNICStats := make(map[tcpip.NICID]nicStats)
	for {
		select {
		case <-ctx.Done():
			return ctx.Err()
		case ts := <-ticker.C:
			created := stats.SocketsCreated.Value()
			destroyed := stats.SocketsDestroyed.Value()
			tcpConnectionsClosed := stats.TCP.EstablishedClosed.Value()
			tcpConnectionsReset := stats.TCP.EstablishedResets.Value()
			tcpConnectionsTimedOut := stats.TCP.EstablishedTimedout.Value()

			period := ts.Sub(previousTime).Microseconds()
			previousTime = ts
			events := []cobalt.CobaltEvent{
				{
					MetricId: networking_metrics.SocketCountMaxMetricId,
					Payload:  eventCount(period, stats.SocketCount.Value()),
				},
				{
					MetricId: networking_metrics.SocketsCreatedMetricId,
					Payload:  eventCount(period, created-lastCreated),
				},
				{
					MetricId: networking_metrics.SocketsDestroyedMetricId,
					Payload:  eventCount(period, destroyed-lastDestroyed),
				},
				{
					MetricId: networking_metrics.TcpConnectionsEstablishedTotalMetricId,
					Payload:  eventCount(period, stats.TCP.CurrentEstablished.Value()),
				},
				{
					MetricId: networking_metrics.TcpConnectionsClosedMetricId,
					Payload:  eventCount(period, tcpConnectionsClosed-lastTcpConnectionsClosed),
				},
				{
					MetricId: networking_metrics.TcpConnectionsResetMetricId,
					Payload:  eventCount(period, tcpConnectionsReset-lastTcpConnectionsReset),
				},
				{
					MetricId: networking_metrics.TcpConnectionsTimedOutMetricId,
					Payload:  eventCount(period, tcpConnectionsTimedOut-lastTcpConnectionsTimedOut),
				},
			}

			nicInfos := stk.NICInfo()
			for nicid, info := range nicInfos {
				packetsSent, packetsReceived := info.Stats.Tx.Packets.Value(), info.Stats.Rx.Packets.Value()
				bytesSent, bytesReceived := info.Stats.Tx.Bytes.Value(), info.Stats.Rx.Bytes.Value()

				lastStats, ok := lastNICStats[nicid]
				if !ok {
					lastStats = make(nicStats)
					lastNICStats[nicid] = lastStats
				}
				deltaPacketsSent := packetsSent - lastStats[networking_metrics.PacketsSentMetricId]
				deltaPacketsReceived := packetsReceived - lastStats[networking_metrics.PacketsReceivedMetricId]
				deltaBytesSent := bytesSent - lastStats[networking_metrics.BytesSentMetricId]
				deltaBytesReceived := bytesReceived - lastStats[networking_metrics.BytesReceivedMetricId]

				lastStats[networking_metrics.PacketsSentMetricId] = packetsSent
				lastStats[networking_metrics.PacketsReceivedMetricId] = packetsReceived
				lastStats[networking_metrics.BytesSentMetricId] = bytesSent
				lastStats[networking_metrics.BytesReceivedMetricId] = bytesReceived

				// TODO(43237): log the NIC features (eth, WLAN, bridge) associated with each datapoint
				events = append(
					events,
					cobalt.CobaltEvent{
						MetricId: networking_metrics.PacketsSentMetricId,
						Payload:  eventCount(period, deltaPacketsSent),
					},
					cobalt.CobaltEvent{
						MetricId: networking_metrics.PacketsReceivedMetricId,
						Payload:  eventCount(period, deltaPacketsReceived),
					},
					cobalt.CobaltEvent{
						MetricId: networking_metrics.BytesSentMetricId,
						Payload:  eventCount(period, deltaBytesSent),
					},
					cobalt.CobaltEvent{
						MetricId: networking_metrics.BytesReceivedMetricId,
						Payload:  eventCount(period, deltaBytesReceived),
					},
				)
			}
			cobaltLogger.LogCobaltEvents(fidl.Background(), events)
			lastCreated = created
			lastDestroyed = destroyed
			lastTcpConnectionsClosed = tcpConnectionsClosed
			lastTcpConnectionsReset = tcpConnectionsReset
			lastTcpConnectionsTimedOut = tcpConnectionsTimedOut
		}
	}
}

func eventCount(period int64, count uint64) cobalt.EventPayload {
	return cobalt.EventPayloadWithEventCount(cobalt.CountEvent{PeriodDurationMicros: period, Count: int64(count)})
}

// endpointsMap is a map from zx.Handle to tcpip.Endpoint.
//
// It is a typesafe wrapper around sync.Map.
type endpointsMap struct {
	inner sync.Map
}

func (m *endpointsMap) Load(key zx.Handle) (tcpip.Endpoint, bool) {
	if value, ok := m.inner.Load(key); ok {
		return value.(tcpip.Endpoint), true
	}
	return nil, false
}

func (m *endpointsMap) Store(key zx.Handle, value tcpip.Endpoint) {
	m.inner.Store(key, value)
}

func (m *endpointsMap) LoadOrStore(key zx.Handle, value tcpip.Endpoint) (tcpip.Endpoint, bool) {
	// Create a scope to allow `value` to be shadowed below.
	{
		value, ok := m.inner.LoadOrStore(key, value)
		return value.(tcpip.Endpoint), ok
	}
}

func (m *endpointsMap) Delete(key zx.Handle) {
	m.inner.Delete(key)
}

func (m *endpointsMap) Range(f func(key zx.Handle, value tcpip.Endpoint) bool) {
	m.inner.Range(func(key, value interface{}) bool {
		return f(key.(zx.Handle), value.(tcpip.Endpoint))
	})
}

// A Netstack tracks all of the running state of the network stack.
type Netstack struct {
	dnsClient       *dns.Client
	nameProvider    *device.NameProviderWithCtxInterface
	netstackService netstack.NetstackService

	stack      *stack.Stack
	routeTable routes.RouteTable

	mu struct {
		sync.Mutex
		transactionRequest *netstack.RouteTableTransactionWithCtxInterfaceRequest
		countNIC           tcpip.NICID
	}

	stats stats

	filter *filter.Filter

	endpoints endpointsMap
}

// Each ifState tracks the state of a network interface.
type ifState struct {
	ns         *Netstack
	controller link.Controller
	nicid      tcpip.NICID
	mu         struct {
		sync.Mutex
		state link.State
		// metric is used by default for routes that originate from this NIC.
		metric     routes.Metric
		dnsServers []tcpip.Address
		dhcp       struct {
			*dhcp.Client
			// running must not be nil.
			running func() bool
			// cancel must not be nil.
			cancel context.CancelFunc
			// Used to restart the DHCP client when we go from link.StateDown to
			// link.StateStarted.
			enabled bool
		}
	}

	// The "outermost" LinkEndpoint implementation (the composition of link
	// endpoint functionality happens by wrapping other link endpoints).
	endpoint stack.LinkEndpoint

	bridgeable *bridge.BridgeableEndpoint

	filterEndpoint *filter.Endpoint
}

// defaultV4Route returns a default IPv4 route through gateway on the specified
// NIC.
func defaultV4Route(nicid tcpip.NICID, gateway tcpip.Address) tcpip.Route {
	return tcpip.Route{
		Destination: header.IPv4EmptySubnet,
		Gateway:     gateway,
		NIC:         nicid,
	}
}

// defaultV6Route returns a default IPv6 route through gateway on the specified
// NIC.
func defaultV6Route(nicid tcpip.NICID, gateway tcpip.Address) tcpip.Route {
	return tcpip.Route{
		Destination: header.IPv6EmptySubnet,
		Gateway:     gateway,
		NIC:         nicid,
	}
}

// onLinkV6Route returns an on-link route to dest through the specified NIC.
//
// dest must be a subnet that is directly reachable by the specified NIC as
// an on-link route is a route to a subnet that a NIC is directly connected to.
func onLinkV6Route(nicID tcpip.NICID, dest tcpip.Subnet) tcpip.Route {
	return tcpip.Route{
		Destination: dest,
		NIC:         nicID,
	}
}

func addressWithPrefixRoute(nicid tcpip.NICID, addr tcpip.AddressWithPrefix) tcpip.Route {
	mask := net.CIDRMask(addr.PrefixLen, len(addr.Address)*8)
	destination, err := tcpip.NewSubnet(tcpip.Address(net.IP(addr.Address).Mask(mask)), tcpip.AddressMask(mask))
	if err != nil {
		panic(err)
	}

	return tcpip.Route{
		Destination: destination,
		NIC:         nicid,
	}
}

func (ns *Netstack) name(nicid tcpip.NICID) string {
	if nicInfo, ok := ns.stack.NICInfo()[nicid]; ok {
		return nicInfo.Name
	}
	return fmt.Sprintf("stack.NICInfo()[%d]: %s", nicid, tcpip.ErrUnknownNICID)
}

func (ns *Netstack) onInterfacesChanged() {
	interfaces2 := ns.getNetInterfaces2()
	connectivity.InferAndNotify(interfaces2)
	// TODO(NET-2078): Switch to the new NetInterface struct once Chromium stops
	// using netstack.fidl.
	interfaces := interfaces2ListToInterfacesList(interfaces2)
	for _, key := range ns.netstackService.BindingKeys() {
		if p, ok := ns.netstackService.EventProxyFor(key); ok {
			if err := p.OnInterfacesChanged(interfaces); err != nil {
				syslog.Warnf("OnInterfacesChanged failed: %v", err)
			}
		}
	}
}

// AddRoute adds a single route to the route table in a sorted fashion.
func (ns *Netstack) AddRoute(r tcpip.Route, metric routes.Metric, dynamic bool) error {
	syslog.Infof("adding route %+v metric:%d dynamic=%v", r, metric, dynamic)
	return ns.AddRoutes([]tcpip.Route{r}, metric, dynamic)
}

// AddRoutes adds one or more routes to the route table in a sorted
// fashion.
func (ns *Netstack) AddRoutes(rs []tcpip.Route, metric routes.Metric, dynamic bool) error {
	metricTracksInterface := false
	if metric == metricNotSet {
		metricTracksInterface = true
	}

	for _, r := range rs {
		// If we don't have an interface set, find it using the gateway address.
		if r.NIC == 0 {
			nic, err := ns.routeTable.FindNIC(r.Gateway)
			if err != nil {
				return fmt.Errorf("error finding NIC for gateway %v: %s", r.Gateway, err)
			}
			r.NIC = nic
		}

		nicInfo, ok := ns.stack.NICInfo()[r.NIC]
		if !ok {
			return fmt.Errorf("error getting nicInfo for NIC %d, not in map", r.NIC)
		}

		ifs := nicInfo.Context.(*ifState)

		ifs.mu.Lock()
		enabled := ifs.mu.state == link.StateStarted
		ifs.mu.Unlock()

		if metricTracksInterface {
			metric = ifs.mu.metric
		}

		ns.routeTable.AddRoute(r, metric, metricTracksInterface, dynamic, enabled)
	}
	ns.stack.SetRouteTable(ns.routeTable.GetNetstackTable())
	return nil
}

// DelRoute deletes a single route from the route table.
func (ns *Netstack) DelRoute(r tcpip.Route) error {
	syslog.Infof("deleting route %+v", r)
	if err := ns.routeTable.DelRoute(r); err != nil {
		return err
	}

	ns.stack.SetRouteTable(ns.routeTable.GetNetstackTable())
	return nil
}

// GetExtendedRouteTable returns a copy of the current extended route table.
func (ns *Netstack) GetExtendedRouteTable() []routes.ExtendedRoute {
	return ns.routeTable.GetExtendedRouteTable()
}

// UpdateRoutesByInterface applies update actions to the routes for a
// given interface.
func (ns *Netstack) UpdateRoutesByInterface(nicid tcpip.NICID, action routes.Action) {
	ns.routeTable.UpdateRoutesByInterface(nicid, action)
	ns.stack.SetRouteTable(ns.routeTable.GetNetstackTable())
}

func (ns *Netstack) removeInterfaceAddress(nic tcpip.NICID, addr tcpip.ProtocolAddress) (bool, error) {
	route := addressWithPrefixRoute(nic, addr.AddressWithPrefix)
	syslog.Infof("removing static IP %s from NIC %d, deleting subnet route %+v", addr.AddressWithPrefix, nic, route)

	nicInfo, ok := ns.stack.NICInfo()[nic]
	if !ok {
		return false, nil
	}

	if _, foundAddr := findAddress(nicInfo.ProtocolAddresses, addr); !foundAddr {
		return false, fmt.Errorf("address %s doesn't exist on NIC ID %d", addr.AddressWithPrefix, nic)
	}

	if err := ns.DelRoute(route); err == routes.ErrNoSuchRoute {
		// The route might have been removed by user action. Continue.
	} else if err != nil {
		panic(fmt.Sprintf("unexpected error deleting route: %s", err))
	}

	if err := ns.stack.RemoveAddress(nic, addr.AddressWithPrefix.Address); err == tcpip.ErrUnknownNICID {
		panic(fmt.Sprintf("stack.RemoveAddress(_): NIC [%d] not found", nic))
	} else if err != nil {
		return false, fmt.Errorf("error removing address %s from NIC ID %d: %s", addr.AddressWithPrefix, nic, err)
	}

	ns.onInterfacesChanged()
	return true, nil
}

// addInterfaceAddress returns whether the NIC corresponding to the supplied
// tcpip.NICID was found or false and an error.
func (ns *Netstack) addInterfaceAddress(nic tcpip.NICID, addr tcpip.ProtocolAddress) (bool, error) {
	route := addressWithPrefixRoute(nic, addr.AddressWithPrefix)
	syslog.Infof("adding static IP %s to NIC %d, creating subnet route %+v with metric=<not-set>, dynamic=false", addr.AddressWithPrefix, nic, route)

	info, ok := ns.stack.NICInfo()[nic]
	if !ok {
		return false, nil
	}

	if a, addrFound := findAddress(info.ProtocolAddresses, addr); addrFound {
		if a.AddressWithPrefix.PrefixLen == addr.AddressWithPrefix.PrefixLen {
			return false, fmt.Errorf("address %s already exists on NIC ID %d", addr.AddressWithPrefix, nic)
		}
		// Same address but different prefix. Remove the address and re-add it
		// with the new prefix (below).
		if err := ns.stack.RemoveAddress(nic, addr.AddressWithPrefix.Address); err != nil {
			return false, fmt.Errorf("NIC %d: failed to remove address %s: %s", nic, addr.AddressWithPrefix, err)
		}
	}

	if err := ns.stack.AddProtocolAddress(nic, addr); err != nil {
		return false, fmt.Errorf("error adding address %s to NIC ID %d: %s", addr.AddressWithPrefix, nic, err)
	}

	if err := ns.AddRoute(route, metricNotSet, false); err != nil {
		return false, fmt.Errorf("error adding subnet route %v to NIC ID %d: %s", route, nic, err)
	}

	ns.onInterfacesChanged()
	return true, nil
}

func (ifs *ifState) updateMetric(metric routes.Metric) {
	ifs.mu.Lock()
	ifs.mu.metric = metric
	ifs.mu.Unlock()
}

func (ifs *ifState) dhcpAcquired(oldAddr, newAddr tcpip.AddressWithPrefix, config dhcp.Config) {
	name := ifs.ns.name(ifs.nicid)

	if oldAddr == newAddr {
		syslog.Infof("NIC %s: DHCP renewed address %s for %s", name, newAddr, config.LeaseLength)
	} else {
		if oldAddr != (tcpip.AddressWithPrefix{}) {
			if err := ifs.ns.stack.RemoveAddress(ifs.nicid, oldAddr.Address); err != nil {
				syslog.Infof("NIC %s: failed to remove DHCP address %s: %s", name, oldAddr, err)
			} else {
				syslog.Infof("NIC %s: removed DHCP address %s", name, oldAddr)
			}

			// Remove the dynamic routes for this interface.
			ifs.ns.UpdateRoutesByInterface(ifs.nicid, routes.ActionDeleteDynamic)
		}

		if newAddr != (tcpip.AddressWithPrefix{}) {
			if err := ifs.ns.stack.AddProtocolAddressWithOptions(ifs.nicid, tcpip.ProtocolAddress{
				Protocol:          ipv4.ProtocolNumber,
				AddressWithPrefix: newAddr,
			}, stack.FirstPrimaryEndpoint); err != nil {
				syslog.Infof("NIC %s: failed to add DHCP acquired address %s: %s", name, newAddr, err)
			} else {
				syslog.Infof("NIC %s: DHCP acquired address %s for %s", name, newAddr, config.LeaseLength)

				// Add a default route and a route for the local subnet.
				rs := []tcpip.Route{
					defaultV4Route(ifs.nicid, config.Gateway),
					addressWithPrefixRoute(ifs.nicid, newAddr),
				}
				syslog.Infof("adding routes %+v with metric=<not-set> dynamic=true", rs)

				if err := ifs.ns.AddRoutes(rs, metricNotSet, true /* dynamic */); err != nil {
					syslog.Infof("error adding routes for DHCP address/gateway: %s", err)
				}
			}
		}
		ifs.ns.onInterfacesChanged()
	}

	if updated := ifs.setDNSServers(config.DNS); updated {
		syslog.Infof("NIC %s: setting DNS servers: %s", name, config.DNS)
		ifs.ns.dnsClient.SetRuntimeServers(ifs.ns.getRuntimeDNSServerRefs())
	}
}

// setDNSServers updates the receiver's dnsServers if necessary and returns
// whether they were updated.
func (ifs *ifState) setDNSServers(servers []tcpip.Address) bool {
	ifs.mu.Lock()
	sameDNS := len(ifs.mu.dnsServers) == len(servers)
	if sameDNS {
		for i := range ifs.mu.dnsServers {
			sameDNS = ifs.mu.dnsServers[i] == servers[i]
			if !sameDNS {
				break
			}
		}
	}
	if !sameDNS {
		ifs.mu.dnsServers = servers
	}
	ifs.mu.Unlock()

	return !sameDNS
}

// setDHCPStatus updates the DHCP status on an interface and runs the DHCP
// client if it should be enabled.
//
// Takes the ifState lock.
func (ifs *ifState) setDHCPStatus(name string, enabled bool) {
	ifs.mu.Lock()
	defer ifs.mu.Unlock()
	ifs.mu.dhcp.enabled = enabled
	ifs.mu.dhcp.cancel()
	if ifs.mu.dhcp.enabled && ifs.mu.state == link.StateStarted {
		ifs.runDHCPLocked(name)
	}
}

// Runs the DHCP client with a fresh context and initializes ifs.mu.dhcp.cancel.
// Call the old cancel function before calling this function.
func (ifs *ifState) runDHCPLocked(name string) {
	ctx, cancel := context.WithCancel(context.Background())
	ifs.mu.dhcp.cancel = cancel
	ifs.mu.dhcp.running = func() bool {
		return ctx.Err() == nil
	}
	if c := ifs.mu.dhcp.Client; c != nil {
		c.Run(ctx)
	} else {
		panic(fmt.Sprintf("nil DHCP client on interface %s", name))
	}
}

func (ifs *ifState) dhcpEnabled() bool {
	ifs.mu.Lock()
	defer ifs.mu.Unlock()
	return ifs.mu.dhcp.enabled
}

func (ifs *ifState) stateChange(s link.State) {
	name := ifs.ns.name(ifs.nicid)

	ifs.mu.Lock()
	switch s {
	case link.StateClosed:
		syslog.Infof("NIC %s: link.StateClosed", name)
		fallthrough
	case link.StateDown:
		syslog.Infof("NIC %s: link.StateDown", name)

		// Stop DHCP, this triggers the removal of all dynamically obtained configuration (IP, routes,
		// DNS servers).
		ifs.mu.dhcp.cancel()

		// Remove DNS servers through ifs.
		ifs.ns.dnsClient.RemoveAllServersWithNIC(ifs.nicid)

		// TODO(crawshaw): more cleanup to be done here:
		// 	- remove link endpoint
		//	- reclaim NICID?

		if s == link.StateClosed {
			// The interface is removed, force all of its routes to be removed.
			ifs.ns.UpdateRoutesByInterface(ifs.nicid, routes.ActionDeleteAll)
		} else {
			// The interface is down, disable static routes (dynamic ones are handled
			// by the cancelled DHCP server).
			ifs.ns.UpdateRoutesByInterface(ifs.nicid, routes.ActionDisableStatic)
		}

		if err := ifs.ns.DelRoute(ipv6LinkLocalOnLinkRoute(ifs.nicid)); err != nil && err != routes.ErrNoSuchRoute {
			syslog.Errorf("error deleting link-local on-link route for nicID (%d): %s", ifs.nicid, err)
		}

		if err := ifs.ns.stack.DisableNIC(ifs.nicid); err != nil {
			syslog.Errorf("error disabling NIC %s in stack.Stack: %s", name, err)
		}

	case link.StateStarted:
		syslog.Infof("NIC %s: link.StateStarted", name)

		if err := ifs.ns.stack.EnableNIC(ifs.nicid); err != nil {
			syslog.Errorf("error enabling NIC %s in stack.Stack: %s", name, err)
		}

		// DHCPv4 sends packets to the IPv4 broadcast address so make sure there is
		// a valid route to it. This route is only needed for the initial DHCPv4
		// transaction. Marking the route as dynamic will result in it being removed
		// when configurations are acquired via DHCPv4, which is okay as following
		// DHCPv4 requests will be sent directly to the DHCPv4 server instead of
		// broadcasting it to the whole link.
		ifs.ns.routeTable.AddRoute(
			tcpip.Route{Destination: util.PointSubnet(header.IPv4Broadcast), NIC: ifs.nicid},
			lowPriorityRoute,
			false, /* metricTracksInterface */
			true,  /* dynamic */
			true,  /* enabled */
		)

		// Re-enable static routes out this interface.
		ifs.ns.UpdateRoutesByInterface(ifs.nicid, routes.ActionEnableStatic)
		if ifs.mu.dhcp.enabled {
			ifs.mu.dhcp.cancel()
			ifs.runDHCPLocked(name)
		}

		// Add an on-link route for the IPv6 link-local subnet. The route is added
		// as a 'static' route because Netstack will remove dynamic routes on DHCPv4
		// changes. See staticRouteAvoidingLifeCycleHooks for more details.
		ifs.ns.routeTable.AddRoute(
			ipv6LinkLocalOnLinkRoute(ifs.nicid),
			metricNotSet,
			true, /* metricTracksInterface */
			staticRouteAvoidingLifeCycleHooks,
			true, /* enabled */
		)
		ifs.ns.stack.SetRouteTable(ifs.ns.routeTable.GetNetstackTable())
	}

	ifs.mu.state = s
	ifs.mu.Unlock()

	ifs.ns.dnsClient.SetRuntimeServers(ifs.ns.getRuntimeDNSServerRefs())
	ifs.ns.onInterfacesChanged()
}

// Return a slice of references to each NIC's DNS servers.
// The caller takes ownership of the returned slice.
func (ns *Netstack) getRuntimeDNSServerRefs() []*[]tcpip.Address {
	nicInfos := ns.stack.NICInfo()
	refs := make([]*[]tcpip.Address, 0, len(nicInfos))
	for _, nicInfo := range nicInfos {
		ifs := nicInfo.Context.(*ifState)
		ifs.mu.Lock()
		refs = append(refs, &ifs.mu.dnsServers)
		ifs.mu.Unlock()
	}
	return refs
}

func (ns *Netstack) getdnsServers() []tcpip.Address {
	defaultServers := ns.dnsClient.GetDefaultServers()
	uniqServers := make(map[tcpip.Address]struct{})

	nicInfos := ns.stack.NICInfo()
	for _, nicInfo := range nicInfos {
		ifs := nicInfo.Context.(*ifState)
		ifs.mu.Lock()
		for _, server := range ifs.mu.dnsServers {
			uniqServers[server] = struct{}{}
		}
		ifs.mu.Unlock()
	}

	out := make([]tcpip.Address, 0, len(defaultServers)+len(uniqServers))
	out = append(out, defaultServers...)
	for server := range uniqServers {
		out = append(out, server)
	}
	return out
}

var nameProviderErrorLogged uint32 = 0

func (ns *Netstack) getDeviceName() string {
	result, err := ns.nameProvider.GetDeviceName(fidl.Background())
	if err != nil {
		if atomic.CompareAndSwapUint32(&nameProviderErrorLogged, 0, 1) {
			syslog.Warnf("getDeviceName: error accessing device name provider: %s", err)
		}
		return device.DefaultDeviceName
	}

	switch tag := result.Which(); tag {
	case device.NameProviderGetDeviceNameResultResponse:
		atomic.StoreUint32(&nameProviderErrorLogged, 0)
		return result.Response.Name
	case device.NameProviderGetDeviceNameResultErr:
		if atomic.CompareAndSwapUint32(&nameProviderErrorLogged, 0, 1) {
			syslog.Warnf("getDeviceName: nameProvider.GetdeviceName() = %s", zx.Status(result.Err))
		}
		return device.DefaultDeviceName
	default:
		panic(fmt.Sprintf("unknown tag: GetDeviceName().Which() = %d", tag))
	}
}

// TODO(stijlist): figure out a way to make it impossible to accidentally
// enable DHCP on loopback interfaces.
func (ns *Netstack) addLoopback() error {
	ifs, err := ns.addEndpoint(func(tcpip.NICID) string {
		return "lo"
	}, loopback.New(), link.NewLoopbackController(), false, defaultInterfaceMetric, true /* enabled */)
	if err != nil {
		return err
	}

	ifs.mu.Lock()
	ifs.mu.state = link.StateStarted
	nicid := ifs.nicid
	ifs.mu.Unlock()

	// Loopback interfaces do not need NDP.
	if err := ns.stack.SetNDPConfigurations(nicid, stack.NDPConfigurations{}); err != nil {
		return fmt.Errorf("error setting NDP configurations to NIC ID %d: %s", nicid, err)
	}

	ipv4LoopbackPrefix := tcpip.AddressMask(net.IP(ipv4Loopback).DefaultMask()).Prefix()
	ipv4LoopbackAddressWithPrefix := tcpip.AddressWithPrefix{
		Address:   ipv4Loopback,
		PrefixLen: ipv4LoopbackPrefix,
	}
	ipv4LoopbackRoute := addressWithPrefixRoute(nicid, ipv4LoopbackAddressWithPrefix)

	if err := ns.stack.AddProtocolAddress(nicid, tcpip.ProtocolAddress{
		Protocol:          ipv4.ProtocolNumber,
		AddressWithPrefix: ipv4LoopbackAddressWithPrefix,
	}); err != nil {
		return fmt.Errorf("error adding address %s to NIC ID %d: %s", ipv4LoopbackAddressWithPrefix, nicid, err)
	}

	if err := ns.stack.AddAddressRange(nicid, ipv4.ProtocolNumber, ipv4LoopbackRoute.Destination); err != nil {
		return fmt.Errorf("loopback: adding ipv4 subnet failed: %s", err)
	}

	if err := ns.stack.AddAddress(nicid, ipv6.ProtocolNumber, ipv6Loopback); err != nil {
		return fmt.Errorf("loopback: adding ipv6 address failed: %s", err)
	}

	if err := ns.AddRoutes(
		[]tcpip.Route{
			ipv4LoopbackRoute,
			{
				Destination: util.PointSubnet(ipv6Loopback),
				NIC:         nicid,
			},
		},
		metricNotSet, /* use interface metric */
		false,        /* dynamic */
	); err != nil {
		return fmt.Errorf("loopback: adding routes failed: %v", err)
	}

	return nil
}

func (ns *Netstack) Bridge(nics []tcpip.NICID) (*ifState, error) {
	links := make([]*bridge.BridgeableEndpoint, 0, len(nics))
	for _, nicid := range nics {
		nicInfo, ok := ns.stack.NICInfo()[nicid]
		if !ok {
			panic("NIC known by netstack not in interface table")
		}
		ifs := nicInfo.Context.(*ifState)
		if err := ifs.controller.SetPromiscuousMode(true); err != nil {
			return nil, err
		}
		links = append(links, ifs.bridgeable)
	}

	b := bridge.New(links)
	return ns.addEndpoint(func(nicid tcpip.NICID) string {
		return fmt.Sprintf("br%d", nicid)
	}, b, b, false, defaultInterfaceMetric, false /* enabled */)
}

func (ns *Netstack) addEth(topological_path string, config netstack.InterfaceConfig, device ethernet.DeviceWithCtx) (*ifState, error) {
	client, err := eth.NewClient("netstack", topological_path, config.Filepath, device)
	if err != nil {
		return nil, err
	}

	return ns.addEndpoint(func(nicid tcpip.NICID) string {
		if len(config.Name) == 0 {
			return fmt.Sprintf("eth%d", nicid)
		}
		return config.Name
	}, eth.NewLinkEndpoint(client), client, true, routes.Metric(config.Metric), false /* enabled */)
}

// addEndpoint creates a new NIC with stack.Stack.
//
// If enabled is false, the NIC will initially be disabled. This is desirable
// when the underlying device or the newly created NIC needs to be further
// configured (with IP addresses, routes, etc.) before it is brought up and
// starts handling packets.
func (ns *Netstack) addEndpoint(
	nameFn func(nicid tcpip.NICID) string,
	ep stack.LinkEndpoint,
	controller link.Controller,
	doFilter bool,
	metric routes.Metric,
	enabled bool,
) (*ifState, error) {
	ifs := &ifState{
		ns:         ns,
		controller: controller,
	}

	ifs.mu.state = link.StateUnknown
	ifs.mu.metric = metric
	ifs.mu.dhcp.running = func() bool { return false }
	ifs.mu.dhcp.cancel = func() {}

	ifs.controller.SetOnStateChange(ifs.stateChange)

	// LinkEndpoint chains:
	// Put sniffer as close as the NIC.
	// A wrapper LinkEndpoint should encapsulate the underlying
	// one, and manifest itself to 3rd party netstack.
	ep = sniffer.New(ep)

	if doFilter {
		ifs.filterEndpoint = filter.NewEndpoint(ns.filter, ep)
		ep = ifs.filterEndpoint
	}
	ifs.bridgeable = bridge.NewEndpoint(ep)
	ep = ifs.bridgeable
	ifs.endpoint = ep

	ns.mu.Lock()
	ifs.nicid = ns.mu.countNIC + 1
	ns.mu.countNIC++
	ns.mu.Unlock()

	name := nameFn(ifs.nicid)
	if err := ns.stack.CreateNICWithOptions(ifs.nicid, ep, stack.NICOptions{Name: name, Context: ifs, Disabled: !enabled}); err != nil {
		return nil, fmt.Errorf("NIC %s: could not create NIC: %s", name, err)
	}

	syslog.Infof("NIC %s added", name)

	if ep.Capabilities()&stack.CapabilityResolutionRequired > 0 {
		if err := ns.stack.AddAddress(ifs.nicid, arp.ProtocolNumber, arp.ProtocolAddress); err != nil {
			return nil, fmt.Errorf("NIC %s: adding arp address failed: %s", name, err)
		}
	}

	ifs.mu.Lock()
	defer ifs.mu.Unlock()

	if linkAddr := ep.LinkAddress(); len(linkAddr) > 0 {
		ifs.mu.dhcp.Client = dhcp.NewClient(ns.stack, ifs.nicid, linkAddr, dhcpAcquisition, dhcpBackoff, dhcpRetransmission, ifs.dhcpAcquired)

		// TODO(37636): remove this. netstack automatically generates a link-local
		// ipv6 address via a configuration option. However, the algorithm used to
		// generate that address may differ from the algorithm used by netsvc. This
		// matters because netsvc implements the host side of the netboot protocol
		// which provides device discovery.
		//
		// This code can be removed when:
		//
		// device discovery moves to another mechanism which is implemented by
		// something running on top of netstack (not netsvc)
		//
		// OR
		//
		// netsvc ceases to implement its own network stack and uses netstack
		// directly.
		lladdr := tcpip.Address([]byte{
			0:  0xFE,
			1:  0x80,
			8:  linkAddr[0] ^ 2,
			9:  linkAddr[1],
			10: linkAddr[2],
			11: 0xFF,
			12: 0xFE,
			13: linkAddr[3],
			14: linkAddr[4],
			15: linkAddr[5],
		})

		if err := ns.stack.AddAddress(ifs.nicid, ipv6.ProtocolNumber, lladdr); err != nil && err != tcpip.ErrDuplicateAddress {
			return nil, fmt.Errorf("NIC %s: adding link-local IPv6 %s failed: %s", name, lladdr, err)
		}

		syslog.Infof("NIC %s: link-local IPv6: %s", name, lladdr)
	}

	return ifs, nil
}

func (ns *Netstack) getIfStateInfo(nicInfo map[tcpip.NICID]stack.NICInfo) map[tcpip.NICID]ifStateInfo {
	ifStates := make(map[tcpip.NICID]ifStateInfo)
	for id, ni := range nicInfo {
		ifs := ni.Context.(*ifState)
		ifs.mu.Lock()
		info := ifStateInfo{
			NICInfo:     ni,
			nicid:       ifs.nicid,
			state:       ifs.mu.state,
			dnsServers:  ifs.mu.dnsServers,
			dhcpEnabled: ifs.mu.dhcp.enabled,
		}
		if ifs.mu.dhcp.enabled {
			info.dhcpInfo = ifs.mu.dhcp.Info()
			info.dhcpStats = ifs.mu.dhcp.Stats()
		}
		ifs.mu.Unlock()
		if client, ok := ifs.controller.(*eth.Client); ok {
			info.client = client
		}
		ifStates[id] = info
	}
	return ifStates
}

func findAddress(addrs []tcpip.ProtocolAddress, addr tcpip.ProtocolAddress) (tcpip.ProtocolAddress, bool) {
	// Ignore prefix length.
	addr.AddressWithPrefix.PrefixLen = 0
	for _, a := range addrs {
		a.AddressWithPrefix.PrefixLen = 0
		if a == addr {
			return a, true
		}
	}
	return tcpip.ProtocolAddress{}, false
}
