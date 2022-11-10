// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//go:build !build_with_native_toolchain

package netstack

import (
	"context"
	"fmt"
	"math"
	"net"
	"syscall/zx"
	"time"

	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/dhcp"
	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/dns"
	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/link"
	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/link/bridge"
	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/link/eth"
	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/routes"
	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/sync"
	zxtime "go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/time"
	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/util"
	syslog "go.fuchsia.dev/fuchsia/src/lib/syslog/go"

	fidlethernet "fidl/fuchsia/hardware/ethernet"
	"fidl/fuchsia/net/interfaces/admin"
	"fidl/fuchsia/netstack"

	"gvisor.dev/gvisor/pkg/tcpip"
	"gvisor.dev/gvisor/pkg/tcpip/header"
	"gvisor.dev/gvisor/pkg/tcpip/link/ethernet"
	"gvisor.dev/gvisor/pkg/tcpip/link/loopback"
	"gvisor.dev/gvisor/pkg/tcpip/link/packetsocket"
	"gvisor.dev/gvisor/pkg/tcpip/link/qdisc/fifo"
	"gvisor.dev/gvisor/pkg/tcpip/link/sniffer"
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

	dhcpAcquisition    = 60 * zxtime.Second
	dhcpBackoff        = 1 * zxtime.Second
	dhcpRetransmission = 4 * zxtime.Second

	// Devices do not support multiple queues yet.
	numQDiscFIFOQueues = 1
	// A multiplier to apply to a device's TX Depth to calculate qdisc's queue
	// length.
	//
	// A large enough value was chosen through experimentation to handle sudden
	// bursts of traffic.
	qdiscTxDepthMultiplier = 20
)

func ipv6LinkLocalOnLinkRoute(nicID tcpip.NICID) tcpip.Route {
	return onLinkV6Route(nicID, header.IPv6LinkLocalPrefix.Subnet())
}

type stats struct {
	tcpip.Stats
	SocketCount      tcpip.StatCounter
	SocketsCreated   tcpip.StatCounter
	SocketsDestroyed tcpip.StatCounter
	DHCPv6           struct {
		NoConfiguration    tcpip.StatCounter
		ManagedAddress     tcpip.StatCounter
		OtherConfiguration tcpip.StatCounter
	}
	IPv6AddressConfig struct {
		NoGlobalSLAACOrDHCPv6ManagedAddress tcpip.StatCounter
		GlobalSLAACOnly                     tcpip.StatCounter
		DHCPv6ManagedAddressOnly            tcpip.StatCounter
		GlobalSLAACAndDHCPv6ManagedAddress  tcpip.StatCounter
	}
}

// endpointsMap is a map from a monotonically increasing uint64 value to tcpip.Endpoint.
//
// It is a typesafe wrapper around sync.Map.
type endpointsMap struct {
	nextKey uint64
	inner   sync.Map
}

func (m *endpointsMap) Load(key uint64) (tcpip.Endpoint, bool) {
	if value, ok := m.inner.Load(key); ok {
		return value.(tcpip.Endpoint), true
	}
	return nil, false
}

func (m *endpointsMap) Store(key uint64, value tcpip.Endpoint) {
	m.inner.Store(key, value)
}

func (m *endpointsMap) LoadOrStore(key uint64, value tcpip.Endpoint) (tcpip.Endpoint, bool) {
	// Create a scope to allow `value` to be shadowed below.
	{
		value, ok := m.inner.LoadOrStore(key, value)
		return value.(tcpip.Endpoint), ok
	}
}

func (m *endpointsMap) LoadAndDelete(key uint64) (tcpip.Endpoint, bool) {
	if value, ok := m.inner.LoadAndDelete(key); ok {
		return value.(tcpip.Endpoint), ok
	}
	return nil, false
}

func (m *endpointsMap) Delete(key uint64) {
	m.inner.Delete(key)
}

func (m *endpointsMap) Range(f func(key uint64, value tcpip.Endpoint) bool) {
	m.inner.Range(func(key, value interface{}) bool {
		return f(key.(uint64), value.(tcpip.Endpoint))
	})
}

// NICRemovedHandler is an interface implemented by types that are interested
// in NICs that have been removed.
type NICRemovedHandler interface {
	// RemovedNIC informs the receiver that the specified NIC has been removed.
	RemovedNIC(tcpip.NICID)
}

// A Netstack tracks all of the running state of the network stack.
type Netstack struct {
	dnsConfig dns.ServersConfig

	interfaceEventChan chan<- interfaceEvent

	stack      *stack.Stack
	routeTable routes.RouteTable

	mu struct {
		sync.Mutex
		countNIC tcpip.NICID
	}

	destinationCacheMu struct {
		sync.Mutex
		destinationCache destinationCache
	}

	stats stats

	endpoints endpointsMap

	nicRemovedHandlers []NICRemovedHandler

	featureFlags featureFlags
}

// Flags for turning on functionality in select environments.
type featureFlags struct {
	enableFastUDP bool
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
	// TODO(https://fxbug.dev/96478): This lock is unnecessary in that we would
	// rather reuse `ifState.mu` if not for the fact that mutexes cannot be used
	// with select. Consolidate and remove this lock.
	// Lock for DHCP client's access to ifState.
	//
	// Must write to this channel before acquiring ifState.mu on all paths that
	// may cancel the DHCP client. Before the DHCP client needs to hold
	// ifState.mu, it will select on this channel and its context done
	// channel, and only proceed if it is able to write to the channel.
	dhcpLock chan struct{}
	mu       struct {
		sync.RWMutex
		adminUp, linkOnline, removed bool
		dhcp                         struct {
			*dhcp.Client
			// running must not be nil.
			running func() bool
			// cancelLocked must not be nil.
			//
			// ifState.mu must be locked before calling this function.
			cancelLocked context.CancelFunc
			// Used to restart the DHCP client when we go from down to up.
			enabled bool
		}
	}

	// metric is used by default for routes that originate from this NIC.
	metric routes.Metric

	adminControls adminControlCollection

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

	// TODO(https://fxbug.dev/86665): Bridged interfaces are disabled within
	// gVisor upon creation and thus the bridge must keep track of them
	// in order to re-enable them when the bridge is removed. This is a
	// hack, and should be replaced with a proper bridging implementation.
	bridgedInterfaces []tcpip.NICID
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

func (ns *Netstack) resetDestinationCache() {
	ns.destinationCacheMu.Lock()
	defer ns.destinationCacheMu.Unlock()
	ns.destinationCacheMu.destinationCache.reset()
}

func (ns *Netstack) name(nicid tcpip.NICID) string {
	name := ns.stack.FindNICNameFromID(nicid)
	if len(name) == 0 {
		name = fmt.Sprintf("unknown(NICID=%d)", nicid)
	}
	return name
}

func (ns *Netstack) fillRouteNIC(r tcpip.Route) (tcpip.Route, error) {
	// If we don't have an interface set, find it using the gateway address.
	if r.NIC == 0 {
		nic, err := ns.routeTable.FindNIC(r.Gateway)
		if err != nil {
			return tcpip.Route{}, fmt.Errorf("error finding NIC for gateway %s: %w", r.Gateway, err)
		}
		r.NIC = nic
	}
	return r, nil
}

// AddRoute adds a single route to the route table in a sorted fashion.
func (ns *Netstack) AddRoute(r tcpip.Route, metric routes.Metric, dynamic bool) error {
	r, err := ns.fillRouteNIC(r)
	if err != nil {
		return err
	}
	return ns.AddRoutes(r.NIC, []tcpip.Route{r}, metric, dynamic)
}

func (ns *Netstack) addRouteWithPreference(r tcpip.Route, prf routes.Preference, metric routes.Metric, dynamic bool) error {
	r, err := ns.fillRouteNIC(r)
	if err != nil {
		return err
	}
	return ns.addRoutesWithPreference(r.NIC, []tcpip.Route{r}, prf, metric, dynamic)
}

// AddRoutes adds one or more routes to the route table in a sorted
// fashion.
//
// The routes will be added with the default (medium) medium preference value.
// All routes in `rs` will have their NICID rewritten to `nicid`.
func (ns *Netstack) AddRoutes(nicid tcpip.NICID, rs []tcpip.Route, metric routes.Metric, dynamic bool) error {
	return ns.addRoutesWithPreference(nicid, rs, routes.MediumPreference, metric, dynamic)
}

// addRoutesWithPreference adds routes for the same interface to the route table
// with a configurable preference value.
//
// All routes in `rs` will have their NICID rewritten to `nicid`.
func (ns *Netstack) addRoutesWithPreference(nicid tcpip.NICID, rs []tcpip.Route, prf routes.Preference, metric routes.Metric, dynamic bool) error {
	nicInfo, ok := ns.stack.NICInfo()[nicid]
	if !ok {
		return fmt.Errorf("error getting nicInfo for NIC %d, not in map: %w", nicid, routes.ErrNoSuchNIC)
	}
	ifs := nicInfo.Context.(*ifState)

	ifs.mu.Lock()
	defer ifs.mu.Unlock()

	ifs.addRoutesWithPreferenceLocked(rs, prf, metric, dynamic)
	return nil
}

func (ifs *ifState) addRoutesWithPreferenceLocked(rs []tcpip.Route, prf routes.Preference, metric routes.Metric, dynamic bool) {
	metricTracksInterface := false
	if metric == metricNotSet {
		metricTracksInterface = true
	}

	enabled := ifs.IsUpLocked()
	if metricTracksInterface {
		metric = ifs.metric
	}

	ifs.ns.routeTable.Lock()
	defer ifs.ns.routeTable.Unlock()

	for _, r := range rs {
		r.NIC = ifs.nicid

		ifs.ns.routeTable.AddRouteLocked(r, prf, metric, metricTracksInterface, dynamic, enabled)
		_ = syslog.Infof("adding route [%s] prf=%d metric=%d dynamic=%t", r, prf, metric, dynamic)

		if enabled {
			if r.Destination.Equal(header.IPv4EmptySubnet) {
				ifs.ns.onDefaultIPv4RouteChangeLocked(r.NIC, true /* hasDefaultRoute */)
			} else if r.Destination.Equal(header.IPv6EmptySubnet) {
				ifs.ns.onDefaultIPv6RouteChangeLocked(r.NIC, true /* hasDefaultRoute */)
			}
		}
	}
	ifs.ns.routeTable.UpdateStackLocked(ifs.ns.stack, ifs.ns.resetDestinationCache)
}

// delRouteLocked deletes routes from a single interface identified by `r.NIC`.
//
// The `ifState` of the interface identified by `r.NIC` must be locked for the
// duration of this function.
func (ns *Netstack) delRouteLocked(r tcpip.Route) []routes.ExtendedRoute {
	ns.routeTable.Lock()
	defer ns.routeTable.Unlock()

	routesDeleted := ns.routeTable.DelRouteLocked(r)
	if len(routesDeleted) == 0 {
		return nil
	}

	for _, er := range routesDeleted {
		if er.Enabled {
			if er.Route.Destination.Equal(header.IPv4EmptySubnet) {
				ns.onDefaultIPv4RouteChangeLocked(er.Route.NIC, false /* hasDefaultRoute */)
			} else if er.Route.Destination.Equal(header.IPv6EmptySubnet) {
				ns.onDefaultIPv6RouteChangeLocked(er.Route.NIC, false /* hasDefaultRoute */)
			}
		}
	}

	ns.routeTable.UpdateStackLocked(ns.stack, ns.resetDestinationCache)
	return routesDeleted
}

// DelRoute deletes all routes matching r from the route table.
func (ns *Netstack) DelRoute(r tcpip.Route) []routes.ExtendedRoute {
	_ = syslog.Infof("deleting route %s", r)

	nicInfoMap := ns.stack.NICInfo()

	delRoute := func(nicInfo stack.NICInfo, r tcpip.Route) []routes.ExtendedRoute {
		ifs := nicInfo.Context.(*ifState)
		ifs.mu.Lock()
		defer ifs.mu.Unlock()

		return ns.delRouteLocked(r)
	}

	if r.NIC == 0 {
		var routesDeleted []routes.ExtendedRoute
		for nicid, nicInfo := range nicInfoMap {
			r.NIC = nicid
			routesDeleted = append(routesDeleted, delRoute(nicInfo, r)...)
		}
		return routesDeleted
	} else {
		nicInfo, ok := nicInfoMap[r.NIC]
		if !ok {
			return nil
		}
		return delRoute(nicInfo, r)
	}
}

// GetExtendedRouteTable returns a copy of the current extended route table.
func (ns *Netstack) GetExtendedRouteTable() []routes.ExtendedRoute {
	return ns.routeTable.GetExtendedRouteTable()
}

// UpdateRoutesByInterface applies update actions to the routes for a
// given interface.
func (ns *Netstack) UpdateRoutesByInterfaceLocked(nicid tcpip.NICID, action routes.Action) {
	ns.routeTable.Lock()
	defer ns.routeTable.Unlock()

	ns.routeTable.UpdateRoutesByInterfaceLocked(nicid, action)

	hasDefaultIPv4Route, hasDefaultIPv6Route := ns.routeTable.HasDefaultRouteLocked(nicid)
	ns.onDefaultRouteChangeLocked(nicid, hasDefaultIPv4Route, hasDefaultIPv6Route)

	ns.routeTable.UpdateStackLocked(ns.stack, ns.resetDestinationCache)
}

func (ifs *ifState) removeAddress(protocolAddr tcpip.ProtocolAddress) zx.Status {
	_ = syslog.Infof("NIC %d: removing IP %s", ifs.nicid, protocolAddr.AddressWithPrefix)

	switch err := ifs.ns.stack.RemoveAddress(ifs.nicid, protocolAddr.AddressWithPrefix.Address); err.(type) {
	case nil:
	case *tcpip.ErrUnknownNICID:
		_ = syslog.Warnf("stack.RemoveAddress(%d, %s): NIC not found", ifs.nicid, protocolAddr.AddressWithPrefix)
		return zx.ErrBadState
	case *tcpip.ErrBadLocalAddress:
		return zx.ErrNotFound
	default:
		panic(fmt.Sprintf("stack.RemoveAddress(%d, %s) = %s", ifs.nicid, protocolAddr.AddressWithPrefix, err))
	}

	ifs.ns.resetDestinationCache()
	return zx.ErrOk
}

type addressChanged struct {
	nicid        tcpip.NICID
	protocolAddr tcpip.ProtocolAddress
	lifetimes    stack.AddressLifetimes
	state        stack.AddressAssignmentState
}

var _ interfaceEvent = (*addressChanged)(nil)

func (addressChanged) isInterfaceEvent() {}

type addressRemoved struct {
	nicid        tcpip.NICID
	protocolAddr tcpip.ProtocolAddress
	reason       stack.AddressRemovalReason
}

var _ interfaceEvent = (*addressRemoved)(nil)

func (addressRemoved) isInterfaceEvent() {}

var _ stack.AddressDispatcher = (*watcherAddressDispatcher)(nil)

type watcherAddressDispatcher struct {
	nicid        tcpip.NICID
	protocolAddr tcpip.ProtocolAddress
	ch           chan<- interfaceEvent
}

// OnChanged is called when the address this AddressDispatcher is registered
// on is assigned or changed.
//
// Note that this function is called while locked inside gVisor, so care should
// be taken to avoid deadlock.
func (ad *watcherAddressDispatcher) OnChanged(lifetimes stack.AddressLifetimes, state stack.AddressAssignmentState) {
	_ = syslog.Debugf("NIC=%d addr=%s changed lifetimes=%#v state=%s",
		ad.nicid, ad.protocolAddr.AddressWithPrefix, lifetimes, state)
	if ad.ch != nil {
		ad.ch <- addressChanged{
			nicid:        ad.nicid,
			protocolAddr: ad.protocolAddr,
			lifetimes:    lifetimes,
			state:        state,
		}
	}
}

// OnRemoved is called when the address this AddressDispatcher is registered
// on is removed.
//
// Note that this function is called while locked inside gVisor, so care should
// be taken to avoid deadlock.
func (ad *watcherAddressDispatcher) OnRemoved(reason stack.AddressRemovalReason) {
	_ = syslog.Debugf("NIC=%d addr=%s removed reason=%s", ad.nicid, ad.protocolAddr.AddressWithPrefix, reason)
	if ad.ch != nil {
		ad.ch <- addressRemoved{
			nicid:        ad.nicid,
			protocolAddr: ad.protocolAddr,
			reason:       reason,
		}
	}
}

func (ifs *ifState) addAddress(protocolAddr tcpip.ProtocolAddress, properties stack.AddressProperties) (bool, admin.AddressRemovalReason) {
	_ = syslog.Infof("NIC %d: adding address %s", ifs.nicid, protocolAddr.AddressWithPrefix)

	// properties.Disp is non-nil iff the caller is serving
	// fuchsia.net.interfaces.admin/Control.AddAddress. The
	// AddressDispatcher implementation used serves both
	// fuchsia.net.interfaces.admin/AddressStateProvider and
	// fuchsia.net.interfaces/Watcher.
	//
	// If no dispatcher is passed, register one for serving
	// fuchsia.net.interfaces/Watcher.
	if properties.Disp == nil && ifs.ns.interfaceEventChan != nil {
		properties.Disp = &watcherAddressDispatcher{
			nicid:        ifs.nicid,
			protocolAddr: protocolAddr,
			ch:           ifs.ns.interfaceEventChan,
		}
	}
	switch err := ifs.ns.stack.AddProtocolAddress(ifs.nicid, protocolAddr, properties); err.(type) {
	case nil:
		return true, 0
	case *tcpip.ErrUnknownNICID:
		return false, admin.AddressRemovalReasonInterfaceRemoved
	case *tcpip.ErrDuplicateAddress:
		return false, admin.AddressRemovalReasonAlreadyAssigned
	default:
		panic(fmt.Sprintf("stack.AddProtocolAddress(%d, %s, %#v) unexpected error: %s", ifs.nicid, protocolAddr.AddressWithPrefix, properties, err))
	}
}

// onInterfaceAddLocked must be called with `ifs.mu` locked.
func (ns *Netstack) onInterfaceAddLocked(ifs *ifState, name string) {
	if ns.interfaceEventChan != nil {
		ns.interfaceEventChan <- interfaceAdded(initialProperties(ifs, name))
	}
}

// onInterfaceRemoveLocked must be called with `ifState.mu` of the interface
// identified by `nicid` locked.
func (ns *Netstack) onInterfaceRemoveLocked(nicid tcpip.NICID) {
	if ns.interfaceEventChan != nil {
		ns.interfaceEventChan <- interfaceRemoved(nicid)
	}
}

// onOnlineChangeLocked must be called with `ifState.mu` of the interface
// identified by `nicid` locked.
func (ns *Netstack) onOnlineChangeLocked(nicid tcpip.NICID, online bool) {
	if ns.interfaceEventChan != nil {
		ns.interfaceEventChan <- onlineChanged{
			nicid:  nicid,
			online: online,
		}
	}
}

// onDefaultIPv4RouteChangeLocked must be called with the `ifState.mu` of the
// interface identified by `nicid` and the route table locked (in that order)
// to avoid races against other route changes.
func (ns *Netstack) onDefaultIPv4RouteChangeLocked(nicid tcpip.NICID, hasDefaultRoute bool) {
	if ns.interfaceEventChan != nil {
		ns.interfaceEventChan <- defaultRouteChanged{
			nicid:               nicid,
			hasDefaultIPv4Route: &hasDefaultRoute,
		}
	}
}

// onDefaultIPv6RouteChangeLocked must be called with the `ifState.mu` of the
// interface identified by `nicid` and the route table locked (in that order)
// to avoid races against other route changes.
func (ns *Netstack) onDefaultIPv6RouteChangeLocked(nicid tcpip.NICID, hasDefaultRoute bool) {
	if ns.interfaceEventChan != nil {
		ns.interfaceEventChan <- defaultRouteChanged{
			nicid:               nicid,
			hasDefaultIPv6Route: &hasDefaultRoute,
		}
	}
}

// onDefaultRouteChangeLocked must be called with the `ifState.mu` of the
// interface identified by `nicid` and the route table locked (in that order)
// to avoid races against other route changes.
func (ns *Netstack) onDefaultRouteChangeLocked(nicid tcpip.NICID, hasDefaultIPv4Route bool, hasDefaultIPv6Route bool) {
	if ns.interfaceEventChan != nil {
		ns.interfaceEventChan <- defaultRouteChanged{
			nicid:               nicid,
			hasDefaultIPv4Route: &hasDefaultIPv4Route,
			hasDefaultIPv6Route: &hasDefaultIPv6Route,
		}
	}
}

func (ifs *ifState) dhcpLostLocked(lost tcpip.AddressWithPrefix) {
	name := ifs.ns.name(ifs.nicid)

	switch status := ifs.removeAddress(tcpip.ProtocolAddress{
		AddressWithPrefix: lost,
		Protocol:          header.IPv4ProtocolNumber,
	}); status {
	case zx.ErrOk:
		_ = syslog.Infof("NIC %s: removed DHCP address %s", name, lost)
	case zx.ErrNotFound:
		_ = syslog.Warnf("NIC %s: DHCP address %s to be removed not found", name, lost)
	case zx.ErrBadState:
		_ = syslog.Warnf("NIC %s: NIC not found when removing DHCP address %s", name, lost)
	default:
		panic(fmt.Sprintf("NIC %s: unexpected error removing DHCP address %s: %s", name, lost, status))
	}

	// Remove the dynamic routes for this interface.
	ifs.ns.UpdateRoutesByInterfaceLocked(ifs.nicid, routes.ActionDeleteDynamic)
}

func (ifs *ifState) dhcpAcquired(ctx context.Context, lost, acquired tcpip.AddressWithPrefix, config dhcp.Config) {
	select {
	case <-ctx.Done():
		return
	case ifs.dhcpLock <- struct{}{}:
		defer func() {
			_ = <-ifs.dhcpLock
		}()
	}

	name := ifs.ns.name(ifs.nicid)

	if lost == acquired {
		_ = syslog.Infof("NIC %s: DHCP renewed address %s for %s", name, acquired, config.LeaseLength)

		ifs.ns.stack.SetAddressLifetimes(ifs.nicid, acquired.Address, stack.AddressLifetimes{
			Deprecated:     false,
			PreferredUntil: tcpip.MonotonicTime{}.Add(time.Duration(math.MaxInt64)),
			ValidUntil:     tcpip.MonotonicTime{}.Add(time.Duration(config.UpdatedAt.Add(config.LeaseLength.Duration()).MonotonicNano())),
		})
	} else {
		if lost != (tcpip.AddressWithPrefix{}) {
			ifs.dhcpLostLocked(lost)
		}

		if acquired != (tcpip.AddressWithPrefix{}) {
			if ok, reason := ifs.addAddress(tcpip.ProtocolAddress{
				Protocol:          ipv4.ProtocolNumber,
				AddressWithPrefix: acquired,
			}, stack.AddressProperties{
				Lifetimes: stack.AddressLifetimes{
					PreferredUntil: tcpip.MonotonicTime{}.Add(time.Duration(math.MaxInt64)),
					ValidUntil:     tcpip.MonotonicTime{}.Add(time.Duration(config.UpdatedAt.Add(config.LeaseLength.Duration()).MonotonicNano())),
				},
			}); !ok {
				_ = syslog.Errorf("NIC %s: failed to add DHCP acquired address %s: %s", name, acquired, reason)
			} else {
				_ = syslog.Infof("NIC %s: DHCP acquired address %s for %s", name, acquired, config.LeaseLength)

				// Add a route for the local subnet.
				rs := []tcpip.Route{
					addressWithPrefixRoute(ifs.nicid, acquired),
				}
				// Add a default route through each router.
				for _, router := range config.Router {
					// Reject non-unicast addresses to avoid an explosion of traffic in
					// case of misconfiguration.
					if ip := net.IP(router); !ip.IsLinkLocalUnicast() && !ip.IsGlobalUnicast() {
						_ = syslog.Warnf("NIC %s: DHCP specified non-unicast router %s, skipping", name, ip)
						continue
					}
					rs = append(rs, defaultV4Route(ifs.nicid, router))
				}
				_ = syslog.Infof("adding routes %s with metric=<not-set> dynamic=true", rs)

				ifs.mu.Lock()
				ifs.addRoutesWithPreferenceLocked(rs, routes.MediumPreference, metricNotSet, true /* dynamic */)
				ifs.mu.Unlock()
			}
		}
	}

	if updated := ifs.setDNSServers(config.DNS); updated {
		_ = syslog.Infof("NIC %s: set DNS servers: %s", name, config.DNS)
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
	ifs.dhcpLock <- struct{}{}
	ifs.mu.Lock()
	defer func() {
		ifs.mu.Unlock()
		<-ifs.dhcpLock
	}()
	ifs.mu.dhcp.enabled = enabled
	ifs.mu.dhcp.cancelLocked()
	if ifs.mu.dhcp.enabled && ifs.IsUpLocked() {
		ifs.runDHCPLocked(name)
	}
}

// Runs the DHCP client with a fresh context and initializes ifs.mu.dhcp.cancel.
// Call the old cancel function before calling this function.
func (ifs *ifState) runDHCPLocked(name string) {
	_ = syslog.Infof("NIC %s: run DHCP", name)
	ctx, cancel := context.WithCancel(context.Background())

	completeCh := make(chan tcpip.AddressWithPrefix)
	ifs.mu.dhcp.cancelLocked = func() {
		cancel()
		if addr := <-completeCh; addr != (tcpip.AddressWithPrefix{}) {
			// Remove this address and cleanup routes.
			ifs.dhcpLostLocked(addr)
		}
	}
	ifs.mu.dhcp.running = func() bool {
		return ctx.Err() == nil
	}
	if c := ifs.mu.dhcp.Client; c != nil {
		go func() {
			defer cancel()
			completeCh <- c.Run(ctx)
			close(completeCh)
		}()
	} else {
		cancel()
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
	ifs.mu.dhcp.cancelLocked()

	// Remove DNS servers through ifs.
	ifs.ns.dnsConfig.RemoveAllServersWithNIC(ifs.nicid)
	ifs.setDNSServers(nil)

	if closed {
		// The interface is removed, force all of its routes to be removed.
		ifs.ns.UpdateRoutesByInterfaceLocked(ifs.nicid, routes.ActionDeleteAll)
	} else {
		// The interface is down, disable static routes (dynamic ones are handled
		// by the cancelled DHCP server).
		ifs.ns.UpdateRoutesByInterfaceLocked(ifs.nicid, routes.ActionDisableStatic)
	}

	_ = ifs.ns.delRouteLocked(ipv6LinkLocalOnLinkRoute(ifs.nicid))

	if closed {
		switch err := ifs.ns.stack.RemoveNIC(ifs.nicid); err.(type) {
		case nil:
			ifs.ns.resetDestinationCache()
		case *tcpip.ErrUnknownNICID:
		default:
			_ = syslog.Warnf("error removing NIC %s in stack.Stack: %s", name, err)
		}

		for _, h := range ifs.ns.nicRemovedHandlers {
			h.RemovedNIC(ifs.nicid)
		}
		// TODO(https://fxbug.dev/86665): Re-enabling bridged interfaces on removal
		// of the bridge is a hack, and needs a proper implementation.
		for _, nicid := range ifs.bridgedInterfaces {
			nicInfo, ok := ifs.ns.stack.NICInfo()[nicid]
			if !ok {
				continue
			}

			bridgedIfs := nicInfo.Context.(*ifState)
			bridgedIfs.mu.Lock()
			if bridgedIfs.IsUpLocked() {
				switch err := ifs.ns.stack.EnableNIC(nicid); err.(type) {
				case nil, *tcpip.ErrUnknownNICID:
				default:
					_ = syslog.Errorf("failed to enable bridged interface %d after removing bridge: %s", nicid, err)
				}
			}
			bridgedIfs.mu.Unlock()
		}

		ifs.ns.onInterfaceRemoveLocked(ifs.nicid)
	} else {
		if err := ifs.ns.stack.DisableNIC(ifs.nicid); err != nil {
			_ = syslog.Errorf("error disabling NIC %s in stack.Stack: %s", name, err)
		}
	}
}

func (ifs *ifState) stateChangeLocked(name string, adminUp, linkOnline bool) bool {
	before := ifs.IsUpLocked()
	after := adminUp && linkOnline

	if after != before {
		if after {
			if ifs.bridgeable.IsBridged() {
				_ = syslog.Warnf("not enabling NIC %s in stack.Stack because it is attached to a bridge", name)
			} else if err := ifs.ns.stack.EnableNIC(ifs.nicid); err != nil {
				_ = syslog.Errorf("error enabling NIC %s in stack.Stack: %s", name, err)
			}

			// DHCPv4 sends packets to the IPv4 broadcast address so make sure there is
			// a valid route to it. This route is only needed for the initial DHCPv4
			// transaction. Marking the route as dynamic will result in it being removed
			// when configurations are acquired via DHCPv4, which is okay as following
			// DHCPv4 requests will be sent directly to the DHCPv4 server instead of
			// broadcasting it to the whole link.
			ifs.ns.routeTable.AddRoute(
				tcpip.Route{Destination: util.PointSubnet(header.IPv4Broadcast), NIC: ifs.nicid},
				routes.MediumPreference,
				lowPriorityRoute,
				false, /* metricTracksInterface */
				true,  /* dynamic */
				true,  /* enabled */
			)

			// Re-enable static routes out this interface.
			ifs.ns.UpdateRoutesByInterfaceLocked(ifs.nicid, routes.ActionEnableStatic)
			if ifs.mu.dhcp.enabled {
				ifs.mu.dhcp.cancelLocked()
				ifs.runDHCPLocked(name)
			}

			// Add an on-link route for the IPv6 link-local subnet. The route is added
			// as a 'static' route because Netstack will remove dynamic routes on DHCPv4
			// changes. See staticRouteAvoidingLifeCycleHooks for more details.
			ifs.ns.routeTable.AddRoute(
				ipv6LinkLocalOnLinkRoute(ifs.nicid),
				routes.MediumPreference,
				metricNotSet,
				true, /* metricTracksInterface */
				staticRouteAvoidingLifeCycleHooks,
				true, /* enabled */
			)
			ifs.ns.routeTable.UpdateStack(ifs.ns.stack, ifs.ns.resetDestinationCache)
		} else {
			ifs.onDownLocked(name, false /* closed */)
		}
	}

	ifs.mu.adminUp = adminUp
	ifs.mu.linkOnline = linkOnline

	return after != before
}

func (ifs *ifState) onLinkOnlineChanged(linkOnline bool) {
	name := ifs.ns.name(ifs.nicid)

	ifs.dhcpLock <- struct{}{}
	ifs.mu.Lock()
	defer func() {
		ifs.mu.Unlock()
		<-ifs.dhcpLock
	}()

	changed := ifs.stateChangeLocked(name, ifs.mu.adminUp, linkOnline)
	_ = syslog.Infof("NIC %s: observed linkOnline=%t when adminUp=%t, interfacesChanged=%t", name, linkOnline, ifs.mu.adminUp, changed)
	if changed {
		ifs.ns.onOnlineChangeLocked(ifs.nicid, ifs.IsUpLocked())
	}
}

func (ifs *ifState) setState(enabled bool) (bool, error) {
	name := ifs.ns.name(ifs.nicid)

	wasEnabled, err := func() (bool, error) {
		ifs.dhcpLock <- struct{}{}
		ifs.mu.Lock()
		defer func() {
			ifs.mu.Unlock()
			<-ifs.dhcpLock
		}()

		wasEnabled := ifs.mu.adminUp
		if wasEnabled == enabled {
			return wasEnabled, nil
		}

		if controller := ifs.controller; controller != nil {
			fn := controller.Down
			if enabled {
				fn = controller.Up
			}
			if err := fn(); err != nil {
				return wasEnabled, err
			}
		}

		changed := ifs.stateChangeLocked(name, enabled, ifs.LinkOnlineLocked())
		_ = syslog.Infof("NIC %s: set adminUp=%t when linkOnline=%t, interfacesChanged=%t", name, ifs.mu.adminUp, ifs.LinkOnlineLocked(), changed)

		if changed {
			ifs.ns.onOnlineChangeLocked(ifs.nicid, ifs.IsUpLocked())
		}

		return wasEnabled, nil
	}()
	if err != nil {
		_ = syslog.Infof("NIC %s: setting adminUp=%t failed: %s", name, enabled, err)
		return wasEnabled, err
	}

	return wasEnabled, nil
}

func (ifs *ifState) Up() error {
	_, err := ifs.setState(true)
	return err
}

func (ifs *ifState) Down() error {
	_, err := ifs.setState(false)
	return err
}

func (ifs *ifState) RemoveByUser() {
	ifs.remove(admin.InterfaceRemovedReasonUser)
}

func (ifs *ifState) RemoveByLinkClose() {
	ifs.remove(admin.InterfaceRemovedReasonPortClosed)
}

func (ifs *ifState) remove(reason admin.InterfaceRemovedReason) {
	// Cannot hold `ifs.mu` across detaching and waiting for endpoint cleanup as
	// link status changes blocks endpoint cleanup and will attempt to acquire
	// `ifs.mu`.
	if func() bool {
		ifs.mu.Lock()
		defer ifs.mu.Unlock()
		removed := ifs.mu.removed
		ifs.mu.removed = true
		return removed
	}() {
		return
	}

	name := ifs.ns.name(ifs.nicid)

	_ = syslog.Infof("NIC %s: removing, reason=%s", name, reason)

	// Close all open control channels with the interface before removing it from
	// the stack. That prevents any further administrative action from happening.
	ifs.adminControls.onInterfaceRemove(reason)
	// Detach the endpoint and wait for clean termination before we remove the
	// NIC from the stack, that ensures that we can't be racing with other calls
	// to onDown that are signalling link status changes.
	ifs.endpoint.Attach(nil)
	_ = syslog.Infof("NIC %s: waiting for endpoint cleanup...", name)
	ifs.endpoint.Wait()
	_ = syslog.Infof("NIC %s: endpoint cleanup done", name)

	ifs.dhcpLock <- struct{}{}
	ifs.mu.Lock()
	defer func() {
		ifs.mu.Unlock()
		<-ifs.dhcpLock
	}()

	ifs.onDownLocked(name, true /* closed */)

	_ = syslog.Infof("NIC %s: removed", name)
}

var nameProviderErrorLogged uint32 = 0

// TODO(stijlist): figure out a way to make it impossible to accidentally
// enable DHCP on loopback interfaces.
func (ns *Netstack) addLoopback() error {
	ifs, err := ns.addEndpoint(
		func(tcpip.NICID) string {
			return "lo"
		},
		// To match linux behaviour, as per
		// https://github.com/torvalds/linux/blob/5bfc75d92efd494db37f5c4c173d3639d4772966/drivers/net/loopback.c#L162.
		ethernet.New(loopback.New()),
		nil, /* controller */
		nil, /* observer */
		defaultInterfaceMetric,
		qdiscConfig{},
	)
	if err != nil {
		return err
	}

	ifs.mu.Lock()
	nicid := ifs.nicid
	ifs.mu.Unlock()

	// Loopback interfaces do not need NDP or DAD.
	if err := func() tcpip.Error {
		ep, err := ns.stack.GetNetworkEndpoint(nicid, ipv6.ProtocolNumber)
		if err != nil {
			return err
		}

		// Must never fail, but the compiler can't tell.
		ndpEP := ep.(ipv6.NDPEndpoint)
		ndpEP.SetNDPConfigurations(ipv6.NDPConfigurations{})

		dadEP := ep.(stack.DuplicateAddressDetector)
		dadEP.SetDADConfigurations(stack.DADConfigurations{})

		return nil
	}(); err != nil {
		return fmt.Errorf("error setting NDP configurations to NIC ID %d: %s", nicid, err)
	}

	ipv4LoopbackPrefix := tcpip.AddressMask(net.IP(ipv4Loopback).DefaultMask()).Prefix()
	ipv4LoopbackProtocolAddress := tcpip.ProtocolAddress{
		Protocol: ipv4.ProtocolNumber,
		AddressWithPrefix: tcpip.AddressWithPrefix{
			Address:   ipv4Loopback,
			PrefixLen: ipv4LoopbackPrefix,
		},
	}
	ipv4LoopbackRoute := addressWithPrefixRoute(nicid, ipv4LoopbackProtocolAddress.AddressWithPrefix)
	if ok, reason := ifs.addAddress(ipv4LoopbackProtocolAddress, stack.AddressProperties{}); !ok {
		return fmt.Errorf("ifs.addAddress(%d, %s): %s", nicid, ipv4LoopbackProtocolAddress.AddressWithPrefix, reason)
	}

	ipv6LoopbackProtocolAddress := tcpip.ProtocolAddress{
		Protocol:          ipv6.ProtocolNumber,
		AddressWithPrefix: ipv6Loopback.WithPrefix(),
	}
	if ok, reason := ifs.addAddress(ipv6LoopbackProtocolAddress, stack.AddressProperties{}); !ok {
		return fmt.Errorf("ifs.addAddress(%d, %s): %s", nicid, ipv6LoopbackProtocolAddress.AddressWithPrefix, reason)
	}

	ifs.mu.Lock()
	ifs.addRoutesWithPreferenceLocked(
		[]tcpip.Route{
			ipv4LoopbackRoute,
			{
				Destination: util.PointSubnet(ipv6Loopback),
				NIC:         nicid,
			},
		},
		routes.MediumPreference,
		metricNotSet, /* use interface metric */
		false,        /* dynamic */
	)
	ifs.mu.Unlock()

	if err := ifs.Up(); err != nil {
		return err
	}

	return nil
}

func (ns *Netstack) Bridge(nics []tcpip.NICID) (*ifState, error) {
	links := make([]*bridge.BridgeableEndpoint, 0, len(nics))
	ifStates := make([]*ifState, 0, len(nics))
	metric := defaultInterfaceMetric
	for _, nicid := range nics {
		nicInfo, ok := ns.stack.NICInfo()[nicid]
		if !ok {
			return nil, fmt.Errorf("failed to find NIC %d", nicid)
		}
		ifs := nicInfo.Context.(*ifState)
		ifStates = append(ifStates, ifs)

		if controller := ifs.controller; controller != nil {
			if err := controller.SetPromiscuousMode(true); err != nil {
				return nil, fmt.Errorf("error enabling promiscuous mode for NIC %d in stack.Stack while bridging endpoint: %w", ifs.nicid, err)
			}
		}
		links = append(links, ifs.bridgeable)

		// TODO(https://fxbug.dev/86661): Replace this with explicit
		// configuration. For now, take the minimum default route
		// metric across all the links because there is currently
		// no way to specify it when creating the bridge.
		if ifs.metric < metric {
			metric = ifs.metric
		}
	}

	b, err := bridge.New(links)
	if err != nil {
		return nil, err
	}

	ifs, err := ns.addEndpoint(
		func(nicid tcpip.NICID) string {
			return fmt.Sprintf("br%d", nicid)
		},
		b,
		b,
		nil, /* observer */
		metric,
		qdiscConfig{},
	)
	if err != nil {
		return nil, err
	}

	ifs.bridgedInterfaces = nics

	for _, ifs := range ifStates {
		func() {
			// Disabling the NIC and attaching interfaces to the bridge must be called
			// under lock to avoid racing against admin/link status changes which may
			// enable the NIC.
			ifs.mu.Lock()
			defer ifs.mu.Unlock()

			// TODO(https://fxbug.dev/86665): Disabling bridged interfaces inside gVisor
			// is a hack, and in need of a proper implementation.
			switch err := ifs.ns.stack.DisableNIC(ifs.nicid); err.(type) {
			case nil:
			case *tcpip.ErrUnknownNICID:
				// TODO(https://fxbug.dev/86959): Handle bridged interface removal.
				_ = syslog.Warnf("NIC %d removed while attaching to bridge", ifs.nicid)
			default:
				panic(fmt.Sprintf("unexpected error disabling NIC %d while attaching to bridge: %s", ifs.nicid, err))
			}

			ifs.bridgeable.SetBridge(b)
		}()
	}
	return ifs, err
}

func makeEndpointName(prefix, configName string) func(nicid tcpip.NICID) string {
	return func(nicid tcpip.NICID) string {
		if len(configName) == 0 {
			return fmt.Sprintf("%s%d", prefix, nicid)
		}
		return configName
	}
}

func (ns *Netstack) addEth(topopath string, config netstack.InterfaceConfig, device fidlethernet.DeviceWithCtx) (*ifState, error) {
	client, err := eth.NewClient("netstack", topopath, config.Filepath, device)
	if err != nil {
		return nil, err
	}

	return ns.addEndpoint(
		makeEndpointName("eth", config.Name),
		ethernet.New(client),
		client,
		client,
		routes.Metric(config.Metric),
		qdiscConfig{numQueues: numQDiscFIFOQueues, queueLen: int(client.TxDepth()) * qdiscTxDepthMultiplier},
	)
}

type qdiscConfig struct {
	numQueues int
	queueLen  int
}

// addEndpoint creates a new NIC with stack.Stack.
func (ns *Netstack) addEndpoint(
	nameFn func(nicid tcpip.NICID) string,
	ep stack.LinkEndpoint,
	controller link.Controller,
	observer link.Observer,
	metric routes.Metric,
	qdisc qdiscConfig,
) (*ifState, error) {
	ifs := &ifState{
		ns:         ns,
		controller: controller,
		observer:   observer,
		metric:     metric,
	}
	ifs.dhcpLock = make(chan struct{}, 1)
	ifs.adminControls.mu.controls = make(map[*adminControlImpl]struct{})
	if observer != nil {
		observer.SetOnLinkClosed(ifs.RemoveByLinkClose)
		observer.SetOnLinkOnlineChanged(ifs.onLinkOnlineChanged)
	}

	ifs.mu.dhcp.running = func() bool { return false }
	ifs.mu.dhcp.cancelLocked = func() {}

	ns.mu.Lock()
	ifs.nicid = ns.mu.countNIC + 1
	ns.mu.countNIC++
	ns.mu.Unlock()
	name := nameFn(ifs.nicid)

	// LinkEndpoint chains:
	// Put sniffer as close as the NIC.
	// A wrapper LinkEndpoint should encapsulate the underlying
	// one, and manifest itself to 3rd party netstack.
	ifs.bridgeable = bridge.NewEndpoint(sniffer.NewWithPrefix(packetsocket.New(ep), fmt.Sprintf("[%s(id=%d)] ", name, ifs.nicid)))
	ep = ifs.bridgeable
	ifs.endpoint = ep

	ifs.mu.Lock()
	defer ifs.mu.Unlock()

	nicOpts := stack.NICOptions{Name: name, Context: ifs, Disabled: true}
	if qdisc.numQueues > 0 {
		if qdisc.queueLen == 0 {
			panic("attempted to configure qdisc with zero-length queue")
		}
		nicOpts.QDisc = fifo.New(ep, qdisc.numQueues, qdisc.queueLen)
	}
	if err := ns.stack.CreateNICWithOptions(ifs.nicid, ep, nicOpts); err != nil {
		return nil, fmt.Errorf("NIC %s: could not create NIC: %w", name, WrapTcpIpError(err))
	}

	_ = syslog.Infof("NIC %s added", name)

	if linkAddr := ep.LinkAddress(); len(linkAddr) > 0 {
		dhcpClient := dhcp.NewClient(ns.stack, ifs.nicid, linkAddr, dhcpAcquisition, dhcpBackoff, dhcpRetransmission, ifs.dhcpAcquired)
		ifs.mu.dhcp.Client = dhcpClient
	}

	ns.onInterfaceAddLocked(ifs, name)

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
			info.dhcpStateRecentHistory = ifs.mu.dhcp.StateRecentHistory()
		}

		for _, network := range []tcpip.NetworkProtocolNumber{header.IPv4ProtocolNumber, header.IPv6ProtocolNumber} {
			{
				neighbors, err := ns.stack.Neighbors(id, network)
				switch err.(type) {
				case nil:
					if info.neighbors == nil {
						info.neighbors = make(map[string]stack.NeighborEntry)
					}
					for _, n := range neighbors {
						info.neighbors[n.Addr.String()] = n
					}
				case *tcpip.ErrNotSupported:
					// NIC does not have a neighbor table, skip.
				case *tcpip.ErrUnknownNICID:
					_ = syslog.Warnf("getIfStateInfo: NIC removed before ns.stack.Neighbors(%d) could be called", id)
				default:
					_ = syslog.Errorf("getIfStateInfo: unexpected error from ns.stack.Neighbors(%d) = %s", id, err)
				}
			}
		}

		info.networkEndpointStats = make(map[string]stack.NetworkEndpointStats, len(ni.NetworkStats))
		for proto, netEPStats := range ni.NetworkStats {
			info.networkEndpointStats[networkProtocolToString(proto)] = netEPStats
		}

		ifs.mu.Unlock()
		info.controller = ifs.controller
		ifStates[id] = info
	}
	return ifStates
}

func networkProtocolToString(proto tcpip.NetworkProtocolNumber) string {
	switch proto {
	case header.IPv4ProtocolNumber:
		return "IPv4"
	case header.IPv6ProtocolNumber:
		return "IPv6"
	case header.ARPProtocolNumber:
		return "ARP"
	default:
		return fmt.Sprintf("0x%x", proto)
	}
}
