// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//go:build !build_with_native_toolchain
// +build !build_with_native_toolchain

package netstack

import (
	"context"
	"errors"
	"fmt"
	"net"
	"sync"
	"syscall/zx"

	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/dhcp"
	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/dns"
	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/link"
	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/link/bridge"
	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/link/eth"
	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/routes"
	zxtime "go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/time"
	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/util"
	syslog "go.fuchsia.dev/fuchsia/src/lib/syslog/go"

	fidlethernet "fidl/fuchsia/hardware/ethernet"
	"fidl/fuchsia/netstack"

	"gvisor.dev/gvisor/pkg/tcpip"
	"gvisor.dev/gvisor/pkg/tcpip/header"
	"gvisor.dev/gvisor/pkg/tcpip/link/ethernet"
	"gvisor.dev/gvisor/pkg/tcpip/link/loopback"
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
)

var (
	errNoSuchAddress = errors.New("no such address")
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

	interfaceWatchers interfaceWatcherCollection

	stack      *stack.Stack
	routeTable routes.RouteTable

	mu struct {
		sync.Mutex
		transactionRequest *netstack.RouteTableTransactionWithCtxInterfaceRequest
		countNIC           tcpip.NICID
	}

	stats stats

	endpoints endpointsMap

	nicRemovedHandlers []NICRemovedHandler
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

	adminControls         adminControlCollection
	addressStateProviders addressStateProviderCollection

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

func (ns *Netstack) name(nicid tcpip.NICID) string {
	name := ns.stack.FindNICNameFromID(nicid)
	if len(name) == 0 {
		name = fmt.Sprintf("unknown(NICID=%d)", nicid)
	}
	return name
}

// AddRoute adds a single route to the route table in a sorted fashion.
func (ns *Netstack) AddRoute(r tcpip.Route, metric routes.Metric, dynamic bool) error {
	return ns.AddRoutes([]tcpip.Route{r}, metric, dynamic)
}

func (ns *Netstack) addRouteWithPreference(r tcpip.Route, prf routes.Preference, metric routes.Metric, dynamic bool) error {
	return ns.addRoutesWithPreference([]tcpip.Route{r}, prf, metric, dynamic)
}

// AddRoutes adds one or more routes to the route table in a sorted
// fashion.
//
// The routes will be added with the default (medium) medium preference value.
func (ns *Netstack) AddRoutes(rs []tcpip.Route, metric routes.Metric, dynamic bool) error {
	return ns.addRoutesWithPreference(rs, routes.MediumPreference, metric, dynamic)
}

func (ns *Netstack) addRoutesWithPreference(rs []tcpip.Route, prf routes.Preference, metric routes.Metric, dynamic bool) error {
	metricTracksInterface := false
	if metric == metricNotSet {
		metricTracksInterface = true
	}

	_ = syslog.Infof("adding routes [%s] prf=%d metric=%d dynamic=%t", rs, prf, metric, dynamic)

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

		ns.routeTable.AddRoute(r, prf, metric, metricTracksInterface, dynamic, enabled)
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
	_ = syslog.Infof("deleting route %s", r)
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
	// TODO(https://fxbug.dev/82590): Avoid spawning the goroutine by not
	// computing all interface properties and just sending the default route
	// changes.
	//
	// ifState may be locked here, so run the default route change handler in a
	// goroutine to prevent deadlock.
	go ns.onDefaultRouteChange()
}

func (ns *Netstack) removeInterfaceAddress(nic tcpip.NICID, addr tcpip.ProtocolAddress, removeRoute bool) zx.Status {
	_ = syslog.Infof("removing static IP %s from NIC %d, removeRoute=%t", addr.AddressWithPrefix, nic, removeRoute)

	nicInfo, ok := ns.stack.NICInfo()[nic]
	if !ok {
		return zx.ErrNotFound
	}

	if _, foundAddr := findAddress(nicInfo.ProtocolAddresses, addr); !foundAddr {
		return zx.ErrNotFound
	}

	if removeRoute {
		route := addressWithPrefixRoute(nic, addr.AddressWithPrefix)
		_ = syslog.Infof("removing subnet route %s", route)
		if err := ns.DelRoute(route); err == routes.ErrNoSuchRoute {
			// The route might have been removed by user action. Continue.
		} else if err != nil {
			panic(fmt.Sprintf("unexpected error deleting route: %s", err))
		}
	}

	switch err := ns.stack.RemoveAddress(nic, addr.AddressWithPrefix.Address); err.(type) {
	case nil:
	case *tcpip.ErrUnknownNICID:
		panic(fmt.Sprintf("stack.RemoveAddress(_): NIC [%d] not found", nic))
	case *tcpip.ErrBadLocalAddress:
		return zx.ErrNotFound
	default:
		return zx.ErrInternal
	}

	ns.onPropertiesChange(nic, nil)
	nicInfo.Context.(*ifState).addressStateProviders.onAddressRemove(addr.AddressWithPrefix.Address)
	return zx.ErrOk
}

// addInterfaceAddress adds `addr` to `nic`, returning `zx.ErrOk` if successful.
//
// TODO(https://fxbug.dev/21222): Change this function to return
// `admin.AddressRemovalReason` when we no longer need it for
// `fuchsia.net.stack/Stack` or `fuchsia.netstack/Netstack`.
func (ns *Netstack) addInterfaceAddress(nic tcpip.NICID, addr tcpip.ProtocolAddress, addRoute bool) zx.Status {
	_ = syslog.Infof("adding static IP %s to NIC %d, addRoute=%t", addr.AddressWithPrefix, nic, addRoute)

	if info, ok := ns.stack.NICInfo()[nic]; ok {
		if a, addrFound := findAddress(info.ProtocolAddresses, addr); addrFound {
			if a.AddressWithPrefix.PrefixLen == addr.AddressWithPrefix.PrefixLen {
				return zx.ErrAlreadyExists
			}
			// Same address but different prefix. Remove the address and re-add it
			// with the new prefix (below).
			switch err := ns.stack.RemoveAddress(nic, addr.AddressWithPrefix.Address); err.(type) {
			case nil:
			case *tcpip.ErrUnknownNICID:
				return zx.ErrNotFound
			case *tcpip.ErrBadLocalAddress:
				// We lost a race, the address was already removed.
			default:
				panic(fmt.Sprintf("NIC %d: failed to remove address %s: %s", nic, addr.AddressWithPrefix, err))
			}
		}
	}

	switch err := ns.stack.AddProtocolAddress(nic, addr); err.(type) {
	case nil:
	case *tcpip.ErrUnknownNICID:
		return zx.ErrNotFound
	case *tcpip.ErrDuplicateAddress:
		return zx.ErrAlreadyExists
	default:
		panic(fmt.Sprintf("NIC %d: failed to add address %s: %s", nic, addr.AddressWithPrefix, err))
	}

	if addRoute {
		route := addressWithPrefixRoute(nic, addr.AddressWithPrefix)
		_ = syslog.Infof("creating subnet route %s with metric=<not-set>, dynamic=false", route)
		if err := ns.AddRoute(route, metricNotSet, false); err != nil {
			if !errors.Is(err, routes.ErrNoSuchNIC) {
				panic(fmt.Sprintf("NIC %d: failed to add subnet route %s: %s", nic, route, err))
			}
			return zx.ErrNotFound
		}
	}

	ns.onPropertiesChange(nic, nil)
	return zx.ErrOk
}

// Called when DAD completes with either success or failure.
//
// Note that this function should not be called if DAD was aborted due to
// interface going offline, as the link online change handler will update the
// address assignment state accordingly.
func (ns *Netstack) onDuplicateAddressDetectionComplete(nicid tcpip.NICID, addr tcpip.Address, success bool) {
	nicInfo, ok := ns.stack.NICInfo()[nicid]
	if !ok {
		_ = syslog.Warnf("DAD completed for address %s on interface %d, interface not found", addr, nicid)
		return
	}

	ifs := nicInfo.Context.(*ifState)
	ifs.onDuplicateAddressDetectionComplete(addr, success)
}

func (ifs *ifState) onDuplicateAddressDetectionComplete(addr tcpip.Address, success bool) {
	// TODO(https://fxbug.dev/82045): DAD completion and interface
	// online/offline race against each other since they both set address
	// assignment state, which means that we must lock `ifState.mu`
	// and then `addressStateProviderCollection.mu` here to prevent
	// interface online change concurrently attempting to mutate the
	// address assignment state.
	online := func() bool {
		ifs.mu.Lock()
		defer ifs.mu.Unlock()

		ifs.addressStateProviders.mu.Lock()
		return ifs.IsUpLocked()
	}()
	defer ifs.addressStateProviders.mu.Unlock()
	ifs.addressStateProviders.onDuplicateAddressDetectionCompleteLocked(ifs.nicid, addr, online, success)
}

func (ifs *ifState) updateMetric(metric routes.Metric) {
	ifs.mu.Lock()
	ifs.mu.metric = metric
	ifs.mu.Unlock()
}

func (ifs *ifState) dhcpAcquired(lost, acquired tcpip.AddressWithPrefix, config dhcp.Config) {
	name := ifs.ns.name(ifs.nicid)

	if lost == acquired {
		_ = syslog.Infof("NIC %s: DHCP renewed address %s for %s", name, acquired, config.LeaseLength)
	} else {
		if lost != (tcpip.AddressWithPrefix{}) {
			if err := ifs.ns.stack.RemoveAddress(ifs.nicid, lost.Address); err != nil {
				_ = syslog.Errorf("NIC %s: failed to remove DHCP address %s: %s", name, lost, err)
			} else {
				_ = syslog.Infof("NIC %s: removed DHCP address %s", name, lost)
			}

			// Remove the dynamic routes for this interface.
			ifs.ns.UpdateRoutesByInterface(ifs.nicid, routes.ActionDeleteDynamic)
		}

		if acquired != (tcpip.AddressWithPrefix{}) {
			if err := ifs.ns.stack.AddProtocolAddressWithOptions(ifs.nicid, tcpip.ProtocolAddress{
				Protocol:          ipv4.ProtocolNumber,
				AddressWithPrefix: acquired,
			}, stack.FirstPrimaryEndpoint); err != nil {
				_ = syslog.Errorf("NIC %s: failed to add DHCP acquired address %s: %s", name, acquired, err)
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

				if err := ifs.ns.AddRoutes(rs, metricNotSet, true /* dynamic */); err != nil {
					_ = syslog.Infof("error adding routes for DHCP: %s", err)
				}
			}
		}
	}

	// Patch the address data exposed by fuchsia.net.interfaces with a validUntil
	// value derived from the DHCP configuration.
	patches := []addressPatch{
		{
			addr:       acquired,
			validUntil: config.UpdatedAt.Add(config.LeaseLength.Duration()),
		},
	}
	// TODO(https://fxbug.dev/82590): Avoid spawning this goroutine by sending
	// the delta of the address property change only instead of computing the
	// delta of all properties, which requires acquiring `ifState.mu`.
	//
	// Dispatch interface change handlers on another goroutine to prevent a
	// deadlock while holding ifState.mu since dhcpAcquired is called on
	// cancellation.
	go ifs.ns.onPropertiesChange(ifs.nicid, patches)

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
		_ = syslog.Errorf("error deleting link-local on-link route for nicID (%d): %s", ifs.nicid, err)
	}

	if closed {
		switch err := ifs.ns.stack.RemoveNIC(ifs.nicid); err.(type) {
		case nil, *tcpip.ErrUnknownNICID:
		default:
			_ = syslog.Errorf("error removing NIC %s in stack.Stack: %s", name, err)
		}

		for _, h := range ifs.ns.nicRemovedHandlers {
			h.RemovedNIC(ifs.nicid)
		}
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
			if err := ifs.ns.stack.EnableNIC(ifs.nicid); err != nil {
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
				routes.MediumPreference,
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

	if func() bool {
		after, changed := func() (bool, bool) {
			ifs.mu.Lock()
			defer func() {
				ifs.addressStateProviders.mu.Lock()
				ifs.mu.Unlock()
			}()

			changed := ifs.stateChangeLocked(name, ifs.mu.adminUp, linkOnline)
			_ = syslog.Infof("NIC %s: observed linkOnline=%t when adminUp=%t, interfacesChanged=%t", name, linkOnline, ifs.mu.adminUp, changed)
			return ifs.IsUpLocked(), changed
		}()
		defer ifs.addressStateProviders.mu.Unlock()

		ifs.addressStateProviders.onInterfaceOnlineChangeLocked(after)
		return changed
	}() {
		ifs.ns.onPropertiesChange(ifs.nicid, nil)
	}
}

func (ifs *ifState) setState(enabled bool) error {
	name := ifs.ns.name(ifs.nicid)

	changed, err := func() (bool, error) {
		after, changed, err := func() (bool, bool, error) {
			ifs.mu.Lock()
			defer func() {
				ifs.addressStateProviders.mu.Lock()
				ifs.mu.Unlock()
			}()

			if ifs.mu.adminUp == enabled {
				return ifs.IsUpLocked(), false, nil
			}

			if controller := ifs.controller; controller != nil {
				fn := controller.Down
				if enabled {
					fn = controller.Up
				}
				if err := fn(); err != nil {
					return false, false, err
				}
			}

			changed := ifs.stateChangeLocked(name, enabled, ifs.LinkOnlineLocked())
			_ = syslog.Infof("NIC %s: set adminUp=%t when linkOnline=%t, interfacesChanged=%t", name, ifs.mu.adminUp, ifs.LinkOnlineLocked(), changed)

			return ifs.IsUpLocked(), changed, nil
		}()
		defer ifs.addressStateProviders.mu.Unlock()

		if err != nil {
			return false, err
		}

		ifs.addressStateProviders.onInterfaceOnlineChangeLocked(after)
		return changed, nil
	}()
	if err != nil {
		_ = syslog.Infof("NIC %s: setting adminUp=%t failed: %s", name, enabled, err)
		return err
	}

	if changed {
		ifs.ns.onPropertiesChange(ifs.nicid, nil)
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

	ifs.ns.interfaceWatchers.onInterfaceRemove(ifs.nicid)
	ifs.adminControls.onInterfaceRemove()
	ifs.addressStateProviders.onInterfaceRemove()
}

var nameProviderErrorLogged uint32 = 0

// TODO(stijlist): figure out a way to make it impossible to accidentally
// enable DHCP on loopback interfaces.
func (ns *Netstack) addLoopback() error {
	ifs, err := ns.addEndpoint(
		func(tcpip.NICID) string {
			return "lo"
		},
		loopback.New(),
		nil, /* controller */
		nil, /* observer */
		defaultInterfaceMetric,
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
		nil, /* observer */
		defaultInterfaceMetric,
	)
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
	)
}

// addEndpoint creates a new NIC with stack.Stack.
func (ns *Netstack) addEndpoint(
	nameFn func(nicid tcpip.NICID) string,
	ep stack.LinkEndpoint,
	controller link.Controller,
	observer link.Observer,
	metric routes.Metric,
) (*ifState, error) {
	ifs := &ifState{
		ns:         ns,
		controller: controller,
		observer:   observer,
	}
	ifs.adminControls.mu.controls = make(map[*adminControlImpl]struct{})
	ifs.addressStateProviders.mu.providers = make(map[tcpip.Address]*adminAddressStateProviderImpl)
	if observer != nil {
		observer.SetOnLinkClosed(ifs.Remove)
		observer.SetOnLinkOnlineChanged(ifs.onLinkOnlineChanged)
	}

	ifs.mu.metric = metric
	ifs.mu.dhcp.running = func() bool { return false }
	ifs.mu.dhcp.cancel = func() {}

	ns.mu.Lock()
	ifs.nicid = ns.mu.countNIC + 1
	ns.mu.countNIC++
	ns.mu.Unlock()
	name := nameFn(ifs.nicid)

	// LinkEndpoint chains:
	// Put sniffer as close as the NIC.
	// A wrapper LinkEndpoint should encapsulate the underlying
	// one, and manifest itself to 3rd party netstack.
	ep = sniffer.NewWithPrefix(ep, fmt.Sprintf("[%s(id=%d)] ", name, ifs.nicid))

	ifs.bridgeable = bridge.NewEndpoint(ep)
	ep = ifs.bridgeable
	ifs.endpoint = ep

	if err := ns.stack.CreateNICWithOptions(ifs.nicid, ep, stack.NICOptions{Name: name, Context: ifs, Disabled: true}); err != nil {
		return nil, fmt.Errorf("NIC %s: could not create NIC: %w", name, WrapTcpIpError(err))
	}

	_ = syslog.Infof("NIC %s added", name)

	if linkAddr := ep.LinkAddress(); len(linkAddr) > 0 {
		dhcpClient := dhcp.NewClient(ns.stack, ifs.nicid, linkAddr, dhcpAcquisition, dhcpBackoff, dhcpRetransmission, ifs.dhcpAcquired)
		ifs.mu.Lock()
		ifs.mu.dhcp.Client = dhcpClient
		ifs.mu.Unlock()
	}

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

func findAddress(addrs []tcpip.ProtocolAddress, addr tcpip.ProtocolAddress) (tcpip.ProtocolAddress, bool) {
	// Ignore prefix length.
	addr.AddressWithPrefix.PrefixLen = 0
	for _, candidate := range addrs {
		// Copy to avoid mutating the return value.
		matcher := candidate
		matcher.AddressWithPrefix.PrefixLen = 0
		if matcher == addr {
			return candidate, true
		}
	}
	return tcpip.ProtocolAddress{}, false
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
