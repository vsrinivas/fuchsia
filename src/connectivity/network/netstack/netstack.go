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
	"time"

	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/dhcp"
	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/dns"
	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/filter"
	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/link"
	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/link/bridge"
	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/link/eth"
	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/routes"
	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/util"
	syslog "go.fuchsia.dev/fuchsia/src/lib/syslog/go"

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

func ipv6LinkLocalOnLinkRoute(nicID tcpip.NICID) tcpip.Route {
	return onLinkV6Route(nicID, header.IPv6LinkLocalPrefix.Subnet())
}

type stats struct {
	tcpip.Stats
	SocketCount      tcpip.StatCounter
	SocketsCreated   tcpip.StatCounter
	SocketsDestroyed tcpip.StatCounter
}

// Map from Cobalt metric ID to metric value.
type nicStats map[uint32]uint64

type cobaltClient struct {
	mu struct {
		sync.Mutex
		observations map[cobaltEventProducer]struct{}
	}
}

type cobaltEventProducer interface {
	events() []cobalt.CobaltEvent
}

func NewCobaltClient() *cobaltClient {
	var c cobaltClient
	c.mu.observations = make(map[cobaltEventProducer]struct{})
	return &c
}

func (c *cobaltClient) Register(o cobaltEventProducer) {
	c.mu.Lock()
	defer c.mu.Unlock()
	c.mu.observations[o] = struct{}{}
}

func (c *cobaltClient) Collect() []cobalt.CobaltEvent {
	c.mu.Lock()
	os := c.mu.observations
	c.mu.observations = make(map[cobaltEventProducer]struct{})
	c.mu.Unlock()

	var events []cobalt.CobaltEvent
	for o := range os {
		events = append(events, o.events()...)
	}
	return events
}

func (c *cobaltClient) Run(ctx context.Context, cobaltLogger *cobalt.LoggerWithCtxInterface) error {
	ticker := time.NewTicker(time.Minute)
	defer ticker.Stop()

	ids := func(events []cobalt.CobaltEvent) []uint32 {
		ids := make([]uint32, 0, len(events))
		for _, event := range events {
			ids = append(ids, event.MetricId)
		}
		return ids
	}

	for {
		select {
		case <-ctx.Done():
			return ctx.Err()
		case <-ticker.C:
			events := c.Collect()
			if status, err := cobaltLogger.LogCobaltEvents(context.Background(), events); err != nil {
				_ = syslog.Warnf("cobaltLogger.LogCobaltEvents(_, MetricId: %d) failed: %s", ids(events), err)
			} else if status != cobalt.StatusOk {
				_ = syslog.Warnf("cobaltLogger.LogCobaltEvents(_, MetricId: %d) rejected: %s", ids(events), status)
			}
		}
	}
}

var _ cobaltEventProducer = (*statsObserver)(nil)

type statsObserver struct {
	mu struct {
		sync.Mutex
		cobaltEvents []cobalt.CobaltEvent
		hasEvents    func()
	}
}

func (o *statsObserver) run(ctx context.Context, interval time.Duration, stats *stats, stk *stack.Stack) error {
	ticker := time.NewTicker(interval)
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
			o.mu.Lock()
			o.mu.cobaltEvents = append(o.mu.cobaltEvents, events...)
			hasEvents := o.mu.hasEvents
			o.mu.Unlock()
			if hasEvents == nil {
				panic("statsObserver: hasEvents callback unspecified (ensure init has been called)")
			}
			hasEvents()
			lastCreated = created
			lastDestroyed = destroyed
			lastTcpConnectionsClosed = tcpConnectionsClosed
			lastTcpConnectionsReset = tcpConnectionsReset
			lastTcpConnectionsTimedOut = tcpConnectionsTimedOut
		}
	}
}

func (o *statsObserver) init(hasEvents func()) {
	o.mu.Lock()
	o.mu.hasEvents = hasEvents
	o.mu.Unlock()
}

func (o *statsObserver) events() []cobalt.CobaltEvent {
	o.mu.Lock()
	res := o.mu.cobaltEvents
	o.mu.cobaltEvents = nil
	o.mu.Unlock()
	return res
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

// nicRemovedHandler is an interface implemented by types that are interested
// in NICs that have been removed.
type nicRemovedHandler interface {
	// removedNIC informs the receiver that the specified NIC has been removed.
	removedNIC(tcpip.NICID)
}

// A Netstack tracks all of the running state of the network stack.
type Netstack struct {
	dnsConfig    dns.ServersConfig
	nameProvider *device.NameProviderWithCtxInterface

	netstackService struct {
		mu struct {
			sync.Mutex
			proxies map[*netstack.NetstackEventProxy]struct{}
		}
	}

	interfaceWatchers interfaceWatcherCollection

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

	nicRemovedHandler nicRemovedHandler
}

// Each ifState tracks the state of a network interface.
type ifState struct {
	ns *Netstack
	// Implements administrative control of link status.
	//
	// Non-nil iff the underlying link status can be toggled.
	controller link.Controller
	// Implements observation of link status.
	//
	// Non-nil iff the underlying link status can be observed.
	observer link.Observer
	nicid    tcpip.NICID
	mu       struct {
		sync.Mutex
		adminUp, linkOnline bool
		// metric is used by default for routes that originate from this NIC.
		metric routes.Metric
		dhcp   struct {
			*dhcp.Client
			// running must not be nil.
			running func() bool
			// cancel must not be nil.
			cancel context.CancelFunc
			// Used to restart the DHCP client when we go from down to up.
			enabled bool
		}
	}

	dns struct {
		mu struct {
			sync.Mutex
			servers []tcpip.Address
		}
	}

	// The "outermost" LinkEndpoint implementation (the composition of link
	// endpoint functionality happens by wrapping other link endpoints).
	endpoint stack.LinkEndpoint

	bridgeable *bridge.BridgeableEndpoint

	filterEndpoint *filter.Endpoint
}

func (ifs *ifState) LinkOnlineLocked() bool {
	return ifs.observer == nil || ifs.mu.linkOnline
}

func (ifs *ifState) IsUpLocked() bool {
	return ifs.mu.adminUp && ifs.LinkOnlineLocked()
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
	name := ns.stack.FindNICNameFromID(nicid)
	if len(name) == 0 {
		name = fmt.Sprintf("unknown NIC(id=%d)", nicid)
	}
	return name
}

func (ns *Netstack) onInterfacesChanged() {
	// We must hold a lock through the entire process of preparing the event so
	// we guarantee events cannot be reordered.
	ns.netstackService.mu.Lock()
	// TODO(fxbug.dev/21079): Switch to the new NetInterface struct once Chromium stops
	// using netstack.fidl.
	interfaces := interfaces2ListToInterfacesList(ns.getNetInterfaces2())
	for pxy := range ns.netstackService.mu.proxies {
		if err := pxy.OnInterfacesChanged(interfaces); err != nil {
			_ = syslog.Warnf("OnInterfacesChanged failed: %s", err)
		}
	}
	ns.netstackService.mu.Unlock()
}

// AddRoute adds a single route to the route table in a sorted fashion.
func (ns *Netstack) AddRoute(r tcpip.Route, metric routes.Metric, dynamic bool) error {
	syslog.Infof("adding route %s metric=%d dynamic=%t", r, metric, dynamic)
	return ns.AddRoutes([]tcpip.Route{r}, metric, dynamic)
}

// AddRoutes adds one or more routes to the route table in a sorted
// fashion.
func (ns *Netstack) AddRoutes(rs []tcpip.Route, metric routes.Metric, dynamic bool) error {
	metricTracksInterface := false
	if metric == metricNotSet {
		metricTracksInterface = true
	}

	var defaultRouteAdded bool
	for _, r := range rs {
		// If we don't have an interface set, find it using the gateway address.
		if r.NIC == 0 {
			nic, err := ns.routeTable.FindNIC(r.Gateway)
			if err != nil {
				return fmt.Errorf("error finding NIC for gateway %s: %w", r.Gateway, err)
			}
			r.NIC = nic
		}

		nicInfo, ok := ns.stack.NICInfo()[r.NIC]
		if !ok {
			return fmt.Errorf("error getting nicInfo for NIC %d, not in map: %w", r.NIC, routes.ErrNoSuchNIC)
		}

		ifs := nicInfo.Context.(*ifState)

		ifs.mu.Lock()
		enabled := ifs.IsUpLocked()

		if metricTracksInterface {
			metric = ifs.mu.metric
		}

		ns.routeTable.AddRoute(r, metric, metricTracksInterface, dynamic, enabled)
		ifs.mu.Unlock()

		if util.IsAny(r.Destination.ID()) && enabled {
			defaultRouteAdded = true
		}
	}
	ns.routeTable.UpdateStack(ns.stack)
	if defaultRouteAdded {
		ns.onDefaultRouteChange()
	}
	return nil
}

// DelRoute deletes a single route from the route table.
func (ns *Netstack) DelRoute(r tcpip.Route) error {
	syslog.Infof("deleting route %s", r)
	if err := ns.routeTable.DelRoute(r); err != nil {
		return err
	}

	ns.routeTable.UpdateStack(ns.stack)
	if util.IsAny(r.Destination.ID()) {
		ns.onDefaultRouteChange()
	}
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
	ns.routeTable.UpdateStack(ns.stack)
	// ifState may be locked here, so run the default route change handler in a
	// goroutine to prevent deadlock.
	go ns.onDefaultRouteChange()
}

func (ns *Netstack) removeInterfaceAddress(nic tcpip.NICID, addr tcpip.ProtocolAddress) (bool, error) {
	route := addressWithPrefixRoute(nic, addr.AddressWithPrefix)
	syslog.Infof("removing static IP %s from NIC %d, deleting subnet route %s", addr.AddressWithPrefix, nic, route)

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
	ns.onPropertiesChange(nic)
	return true, nil
}

// addInterfaceAddress returns whether the NIC corresponding to the supplied
// tcpip.NICID was found or false and an error.
func (ns *Netstack) addInterfaceAddress(nic tcpip.NICID, addr tcpip.ProtocolAddress) (bool, error) {
	route := addressWithPrefixRoute(nic, addr.AddressWithPrefix)
	syslog.Infof("adding static IP %s to NIC %d, creating subnet route %s with metric=<not-set>, dynamic=false", addr.AddressWithPrefix, nic, route)

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
		return false, fmt.Errorf("error adding subnet route %s to NIC ID %d: %w", route, nic, err)
	}

	ns.onInterfacesChanged()
	ns.onPropertiesChange(nic)
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
				syslog.Infof("adding routes %s with metric=<not-set> dynamic=true", rs)

				if err := ifs.ns.AddRoutes(rs, metricNotSet, true /* dynamic */); err != nil {
					syslog.Infof("error adding routes for DHCP address/gateway: %s", err)
				}
			}
		}
		// Dispatch interface change handlers on another goroutine to prevent a
		// deadlock while holding ifState.mu since dhcpAcquired is called on
		// cancellation.
		go func() {
			ifs.ns.onInterfacesChanged()
			ifs.ns.onPropertiesChange(ifs.nicid)
		}()
	}

	if updated := ifs.setDNSServers(config.DNS); updated {
		syslog.Infof("NIC %s: setting DNS servers: %s", name, config.DNS)
	}
}

// setDNSServers updates the receiver's dnsServers if necessary and returns
// whether they were updated.
func (ifs *ifState) setDNSServers(servers []tcpip.Address) bool {
	ifs.dns.mu.Lock()
	sameDNS := len(ifs.dns.mu.servers) == len(servers)
	if sameDNS {
		for i := range ifs.dns.mu.servers {
			sameDNS = ifs.dns.mu.servers[i] == servers[i]
			if !sameDNS {
				break
			}
		}
	}
	if !sameDNS {
		ifs.dns.mu.servers = servers
		ifs.ns.dnsConfig.UpdateDhcpServers(ifs.nicid, &ifs.dns.mu.servers)
	}
	ifs.dns.mu.Unlock()
	return !sameDNS
}

// setDHCPStatus updates the DHCP status on an interface and runs the DHCP
// client if it should be enabled.
//
// Takes the ifState lock.
func (ifs *ifState) setDHCPStatus(name string, enabled bool) {
	_ = syslog.VLogf(syslog.DebugVerbosity, "NIC %s: setDHCPStatus = %t", name, enabled)
	ifs.mu.Lock()
	defer ifs.mu.Unlock()
	ifs.mu.dhcp.enabled = enabled
	ifs.mu.dhcp.cancel()
	if ifs.mu.dhcp.enabled && ifs.IsUpLocked() {
		ifs.runDHCPLocked(name)
	}
}

// Runs the DHCP client with a fresh context and initializes ifs.mu.dhcp.cancel.
// Call the old cancel function before calling this function.
func (ifs *ifState) runDHCPLocked(name string) {
	_ = syslog.Infof("NIC %s: run DHCP", name)
	ctx, cancel := context.WithCancel(context.Background())
	var wg sync.WaitGroup
	ifs.mu.dhcp.cancel = func() {
		cancel()
		wg.Wait()
	}
	ifs.mu.dhcp.running = func() bool {
		return ctx.Err() == nil
	}
	if c := ifs.mu.dhcp.Client; c != nil {
		wg.Add(1)
		go func() {
			c.Run(ctx)
			wg.Done()
		}()
	} else {
		panic(fmt.Sprintf("nil DHCP client on interface %s", name))
	}
}

func (ifs *ifState) dhcpEnabled() bool {
	ifs.mu.Lock()
	defer ifs.mu.Unlock()
	return ifs.mu.dhcp.enabled
}

func (ifs *ifState) onDownLocked(name string, closed bool) {
	// Stop DHCP, this triggers the removal of all dynamically obtained configuration (IP, routes,
	// DNS servers).
	ifs.mu.dhcp.cancel()

	// Remove DNS servers through ifs.
	ifs.ns.dnsConfig.RemoveAllServersWithNIC(ifs.nicid)
	ifs.setDNSServers(nil)

	if closed {
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

	if closed {
		if err := ifs.ns.stack.RemoveNIC(ifs.nicid); err != nil && err != tcpip.ErrUnknownNICID {
			syslog.Errorf("error removing NIC %s in stack.Stack: %s", name, err)
		}

		ifs.ns.nicRemovedHandler.removedNIC(ifs.nicid)
	} else {
		if err := ifs.ns.stack.DisableNIC(ifs.nicid); err != nil {
			syslog.Errorf("error disabling NIC %s in stack.Stack: %s", name, err)
		}
	}
}

func (ifs *ifState) stateChangeLocked(name string, adminUp, linkOnline bool) bool {
	before := ifs.IsUpLocked()
	after := adminUp && linkOnline

	if after != before {
		if after {
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
			ifs.ns.routeTable.UpdateStack(ifs.ns.stack)
		} else {
			ifs.onDownLocked(name, false)
		}
	}

	ifs.mu.adminUp = adminUp
	ifs.mu.linkOnline = linkOnline

	return after != before
}

func (ifs *ifState) onLinkOnlineChanged(linkOnline bool) {
	name := ifs.ns.name(ifs.nicid)

	ifs.mu.Lock()
	changed := ifs.stateChangeLocked(name, ifs.mu.adminUp, linkOnline)
	ifs.mu.Unlock()
	_ = syslog.Infof("NIC %s: observed linkOnline=%t interfacesChanged=%t", name, linkOnline, changed)
	if changed {
		ifs.ns.onInterfacesChanged()
		ifs.ns.onPropertiesChange(ifs.nicid)
	}
}

func (ifs *ifState) setState(enabled bool) error {
	name := ifs.ns.name(ifs.nicid)

	changed, err := func() (bool, error) {
		ifs.mu.Lock()
		defer ifs.mu.Unlock()

		if ifs.mu.adminUp == enabled {
			return false, nil
		}

		if controller := ifs.controller; controller != nil {
			fn := controller.Down
			if enabled {
				fn = controller.Up
			}
			if err := fn(); err != nil {
				return false, err
			}
		}

		return ifs.stateChangeLocked(name, enabled, ifs.LinkOnlineLocked()), nil
	}()
	if err != nil {
		_ = syslog.Infof("NIC %s: setting adminUp=%t failed: %s", name, enabled, err)
		return err
	}
	_ = syslog.Infof("NIC %s: set adminUp=%t interfacesChanged=%t", name, enabled, changed)

	if changed {
		ifs.ns.onInterfacesChanged()
		ifs.ns.onPropertiesChange(ifs.nicid)
	}

	return nil
}

func (ifs *ifState) Up() error {
	return ifs.setState(true)
}

func (ifs *ifState) Down() error {
	return ifs.setState(false)
}

func (ifs *ifState) Remove() {
	name := ifs.ns.name(ifs.nicid)

	_ = syslog.Infof("NIC %s: removing...", name)

	ifs.mu.Lock()
	ifs.onDownLocked(name, true)
	ifs.mu.Unlock()

	_ = syslog.Infof("NIC %s: waiting for endpoint cleanup...", name)

	ifs.endpoint.Wait()

	_ = syslog.Infof("NIC %s: removed", name)

	ifs.ns.onInterfacesChanged()
	ifs.ns.interfaceWatchers.onInterfaceRemove(ifs.nicid)
}

var nameProviderErrorLogged uint32 = 0

func (ns *Netstack) getDeviceName() string {
	result, err := ns.nameProvider.GetDeviceName(context.Background())
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
	ifs, err := ns.addEndpoint(
		func(tcpip.NICID) string {
			return "lo"
		},
		loopback.New(),
		nil,   /* controller */
		nil,   /* observer */
		false, /* doFilter */
		defaultInterfaceMetric,
	)
	if err != nil {
		return err
	}

	ifs.mu.Lock()
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
		return fmt.Errorf("loopback: adding routes failed: %w", err)
	}

	if err := ifs.Up(); err != nil {
		return err
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
		if controller := ifs.controller; controller != nil {
			if err := controller.SetPromiscuousMode(true); err != nil {
				return nil, err
			}
		}
		links = append(links, ifs.bridgeable)
	}

	b := bridge.New(links)
	return ns.addEndpoint(
		func(nicid tcpip.NICID) string {
			return fmt.Sprintf("br%d", nicid)
		},
		b,
		b,
		nil,   /* observer */
		false, /* doFilter */
		defaultInterfaceMetric,
	)
}

func makeEndpointName(prefix, config_name string) func(nicid tcpip.NICID) string {
	return func(nicid tcpip.NICID) string {
		if len(config_name) == 0 {
			return fmt.Sprintf("%s%d", prefix, nicid)
		}
		return config_name
	}
}

func (ns *Netstack) addEth(topopath string, config netstack.InterfaceConfig, device ethernet.DeviceWithCtx) (*ifState, error) {
	client, err := eth.NewClient("netstack", topopath, config.Filepath, device)
	if err != nil {
		return nil, err
	}

	return ns.addEndpoint(
		makeEndpointName("eth", config.Name),
		eth.NewLinkEndpoint(client),
		client,
		client,
		true, /* doFilter */
		routes.Metric(config.Metric),
	)
}

// addEndpoint creates a new NIC with stack.Stack.
func (ns *Netstack) addEndpoint(
	nameFn func(nicid tcpip.NICID) string,
	ep stack.LinkEndpoint,
	controller link.Controller,
	observer link.Observer,
	doFilter bool,
	metric routes.Metric,
) (*ifState, error) {
	ifs := &ifState{
		ns:         ns,
		controller: controller,
		observer:   observer,
	}
	if observer != nil {
		observer.SetOnLinkClosed(ifs.Remove)
		observer.SetOnLinkOnlineChanged(ifs.onLinkOnlineChanged)
	}

	ifs.mu.metric = metric
	ifs.mu.dhcp.running = func() bool { return false }
	ifs.mu.dhcp.cancel = func() {}

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
	if err := ns.stack.CreateNICWithOptions(ifs.nicid, ep, stack.NICOptions{Name: name, Context: ifs, Disabled: true}); err != nil {
		return nil, fmt.Errorf("NIC %s: could not create NIC: %w", name, WrapTcpIpError(err))
	}

	syslog.Infof("NIC %s added", name)

	if ep.Capabilities()&stack.CapabilityResolutionRequired > 0 {
		if err := ns.stack.AddAddress(ifs.nicid, arp.ProtocolNumber, arp.ProtocolAddress); err != nil {
			return nil, fmt.Errorf("NIC %s: adding arp address failed: %w", name, WrapTcpIpError(err))
		}
	}

	if err := func() error {
		if linkAddr := ep.LinkAddress(); len(linkAddr) > 0 {
			ifs.mu.Lock()
			defer ifs.mu.Unlock()
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
				return fmt.Errorf("NIC %s: adding link-local IPv6 %s failed: %w", name, lladdr, WrapTcpIpError(err))
			}

			syslog.Infof("NIC %s: link-local IPv6: %s", name, lladdr)
		}
		return nil
	}(); err != nil {
		return nil, err
	}

	ns.onInterfacesChanged()
	ns.onInterfaceAdd(ifs.nicid)

	return ifs, nil
}

func (ns *Netstack) getIfStateInfo(nicInfo map[tcpip.NICID]stack.NICInfo) map[tcpip.NICID]ifStateInfo {
	ifStates := make(map[tcpip.NICID]ifStateInfo)
	for id, ni := range nicInfo {
		ifs := ni.Context.(*ifState)

		ifs.dns.mu.Lock()
		dnsServers := ifs.dns.mu.servers
		ifs.dns.mu.Unlock()

		ifs.mu.Lock()
		info := ifStateInfo{
			NICInfo:     ni,
			nicid:       ifs.nicid,
			adminUp:     ifs.mu.adminUp,
			linkOnline:  ifs.LinkOnlineLocked(),
			dnsServers:  dnsServers,
			dhcpEnabled: ifs.mu.dhcp.enabled,
		}
		if ifs.mu.dhcp.enabled {
			info.dhcpInfo = ifs.mu.dhcp.Info()
			info.dhcpStats = ifs.mu.dhcp.Stats()
		}
		ifs.mu.Unlock()
		info.controller = ifs.controller
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
