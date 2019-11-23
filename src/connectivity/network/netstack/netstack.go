// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package netstack

import (
	"context"
	"fmt"
	"net"
	"netstack/util"
	"sync"
	"sync/atomic"
	"syscall/zx"
	"time"

	"syslog"

	"netstack/dhcp"
	"netstack/dns"
	"netstack/filter"
	"netstack/link"
	"netstack/link/bridge"
	"netstack/link/eth"
	"netstack/routes"

	"fidl/fuchsia/device"
	"fidl/fuchsia/hardware/ethernet"
	"fidl/fuchsia/netstack"

	"github.com/google/netstack/tcpip"
	"github.com/google/netstack/tcpip/header"
	"github.com/google/netstack/tcpip/link/loopback"
	"github.com/google/netstack/tcpip/link/sniffer"
	"github.com/google/netstack/tcpip/network/arp"
	"github.com/google/netstack/tcpip/network/ipv4"
	"github.com/google/netstack/tcpip/network/ipv6"
	"github.com/google/netstack/tcpip/stack"
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

// A Netstack tracks all of the running state of the network stack.
type Netstack struct {
	arena *eth.Arena

	nameProvider *device.NameProviderInterface
	dnsClient    *dns.Client

	mu struct {
		sync.Mutex
		stack              *stack.Stack
		routeTable         routes.RouteTable
		transactionRequest *netstack.RouteTableTransactionInterfaceRequest
		countNIC           tcpip.NICID
		ifStates           map[tcpip.NICID]*ifState
	}
	nodename string
	sniff    bool

	filter *filter.Filter

	OnInterfacesChanged func([]netstack.NetInterface2)

	// endpoints is a map of transport endpoints of each socket associated
	// with the netstack.
	endpoints sync.Map
}

// Each ifState tracks the state of a network interface.
type ifState struct {
	ns       *Netstack
	eth      link.Controller
	nicid    tcpip.NICID
	filepath string
	// features can include any value that's valid in fuchsia.hardware.ethernet.Info.features.
	features uint32
	mu       struct {
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

// defaultRoutes returns the IPv4 and IPv6 default routes.
func defaultRoutes(nicid tcpip.NICID, gateway tcpip.Address) []tcpip.Route {
	return []tcpip.Route{
		{
			Destination: header.IPv4EmptySubnet,
			Gateway:     gateway,
			NIC:         nicid,
		},
		{
			Destination: header.IPv6EmptySubnet,
			NIC:         nicid,
		},
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

func (ns *Netstack) nameLocked(nicid tcpip.NICID) string {
	if nicInfo, ok := ns.mu.stack.NICInfo()[nicid]; ok {
		return nicInfo.Name
	}
	return fmt.Sprintf("stack.NICInfo()[%d]: %s", nicid, tcpip.ErrUnknownNICID)
}

// AddRoute adds a single route to the route table in a sorted fashion. This
// takes the lock.
func (ns *Netstack) AddRoute(r tcpip.Route, metric routes.Metric, dynamic bool) error {
	syslog.Infof("adding route %+v metric:%d dynamic=%v", r, metric, dynamic)
	ns.mu.Lock()
	defer ns.mu.Unlock()
	return ns.AddRouteLocked(r, metric, dynamic)
}

// AddRouteLocked adds a single route to the route table in a sorted fashion. It
// assumes the lock has already been taken.
func (ns *Netstack) AddRouteLocked(r tcpip.Route, metric routes.Metric, dynamic bool) error {
	return ns.AddRoutesLocked([]tcpip.Route{r}, metric, dynamic)
}

// AddRoutesLocked adds one or more routes to the route table in a sorted
// fashion. It assumes the lock has already been taken.
func (ns *Netstack) AddRoutesLocked(rs []tcpip.Route, metric routes.Metric, dynamic bool) error {
	metricTracksInterface := false
	if metric == metricNotSet {
		metricTracksInterface = true
	}

	for _, r := range rs {
		// If we don't have an interface set, find it using the gateway address.
		if r.NIC == 0 {
			nic, err := ns.mu.routeTable.FindNIC(r.Gateway)
			if err != nil {
				return fmt.Errorf("error finding NIC for gateway %v: %s", r.Gateway, err)
			}
			r.NIC = nic
		}

		ifs, ok := ns.mu.ifStates[r.NIC]
		if !ok {
			return fmt.Errorf("error getting ifState for NIC %d, not in map", r.NIC)
		}

		enabled := ifs.mu.state == link.StateStarted
		if metricTracksInterface {
			metric = ifs.mu.metric
		}

		ns.mu.routeTable.AddRoute(r, metric, metricTracksInterface, dynamic, enabled)
	}
	ns.mu.stack.SetRouteTable(ns.mu.routeTable.GetNetstackTable())
	return nil
}

// DelRoute deletes a single route from the route table. This takes the lock.
func (ns *Netstack) DelRoute(r tcpip.Route) error {
	syslog.Infof("deleting route %+v", r)
	ns.mu.Lock()
	defer ns.mu.Unlock()
	return ns.DelRouteLocked(r)
}

// DelRoute deletes a single route from the route table. It assumes the lock has
// already been taken.
func (ns *Netstack) DelRouteLocked(r tcpip.Route) error {
	if err := ns.mu.routeTable.DelRoute(r); err != nil {
		return fmt.Errorf("error deleting route, %s", err)
	}
	ns.mu.stack.SetRouteTable(ns.mu.routeTable.GetNetstackTable())
	return nil
}

// GetExtendedRouteTable returns a copy of the current extended route table.
// This takes the lock.
func (ns *Netstack) GetExtendedRouteTable() []routes.ExtendedRoute {
	ns.mu.Lock()
	defer ns.mu.Unlock()
	return ns.mu.routeTable.GetExtendedRouteTable()
}

// UpdateRoutesByInterfaceLocked applies update actions to the routes for a
// given interface. It assumes the lock has already been taken.
func (ns *Netstack) UpdateRoutesByInterfaceLocked(nicid tcpip.NICID, action routes.Action) {
	ns.mu.routeTable.UpdateRoutesByInterface(nicid, action)
	ns.mu.stack.SetRouteTable(ns.mu.routeTable.GetNetstackTable())
}

func (ns *Netstack) removeInterfaceAddress(nic tcpip.NICID, addr tcpip.ProtocolAddress) (bool, error) {
	route := addressWithPrefixRoute(nic, addr.AddressWithPrefix)
	syslog.Infof("removing static IP %s from NIC %d, deleting subnet route %+v", addr.AddressWithPrefix, nic, route)

	ns.mu.Lock()
	info, ok := ns.mu.stack.NICInfo()[nic]
	if !ok {
		ns.mu.Unlock()
		return false, nil
	}
	if err := func() error {
		if _, found := findAddress(info.ProtocolAddresses, addr); !found {
			return fmt.Errorf("address %s doesn't exist on NIC ID %d", addr.AddressWithPrefix, nic)
		}

		if err := ns.DelRouteLocked(route); err != nil {
			// The route might have been removed by user action. Continue.
		}

		if err := ns.mu.stack.RemoveAddress(nic, addr.AddressWithPrefix.Address); err == tcpip.ErrUnknownNICID {
			panic(fmt.Sprintf("stack.RemoveAddress(_): NIC [%d] not found", nic))
		} else if err != nil {
			return fmt.Errorf("error removing address %s from NIC ID %d: %s", addr.AddressWithPrefix, nic, err)
		}

		return nil
	}(); err != nil {
		ns.mu.Unlock()
		return true, err
	}

	interfaces := ns.getNetInterfaces2Locked()
	ns.mu.Unlock()
	ns.OnInterfacesChanged(interfaces)
	return true, nil
}

func (ns *Netstack) addInterfaceAddress(nic tcpip.NICID, addr tcpip.ProtocolAddress) (bool, error) {
	route := addressWithPrefixRoute(nic, addr.AddressWithPrefix)
	syslog.Infof("adding static IP %s to NIC %d, creating subnet route %+v with metric=<not-set>, dynamic=false", addr.AddressWithPrefix, nic, route)

	ns.mu.Lock()
	info, ok := ns.mu.stack.NICInfo()[nic]
	if !ok {
		ns.mu.Unlock()
		return false, nil
	}
	if err := func() error {
		if a, found := findAddress(info.ProtocolAddresses, addr); found {
			if a.AddressWithPrefix.PrefixLen == addr.AddressWithPrefix.PrefixLen {
				return fmt.Errorf("address %s already exists on NIC ID %d", addr.AddressWithPrefix, nic)
			}
			// Same address but different prefix. Remove the address and re-add it
			// with the new prefix (below).
			if err := ns.mu.stack.RemoveAddress(nic, addr.AddressWithPrefix.Address); err != nil {
				return fmt.Errorf("NIC %d: failed to remove address %s: %s", nic, addr.AddressWithPrefix, err)
			}
		}

		if err := ns.mu.stack.AddProtocolAddress(nic, addr); err != nil {
			return fmt.Errorf("error adding address %s to NIC ID %d: %s", addr.AddressWithPrefix, nic, err)
		}

		if err := ns.AddRouteLocked(route, metricNotSet, false); err != nil {
			return fmt.Errorf("error adding subnet route %v to NIC ID %d: %s", route, nic, err)
		}
		return nil
	}(); err != nil {
		ns.mu.Unlock()
		return true, err
	}

	interfaces := ns.getNetInterfaces2Locked()
	ns.mu.Unlock()
	ns.OnInterfacesChanged(interfaces)
	return true, nil
}

func (ifs *ifState) updateMetric(metric routes.Metric) {
	ifs.mu.Lock()
	ifs.mu.metric = metric
	ifs.mu.Unlock()
}

func (ifs *ifState) dhcpAcquired(oldAddr, newAddr tcpip.AddressWithPrefix, config dhcp.Config) {
	ifs.ns.mu.Lock()

	name := ifs.ns.nameLocked(ifs.nicid)

	if oldAddr == newAddr {
		syslog.Infof("NIC %s: DHCP renewed address %s for %s", name, newAddr, config.LeaseLength)
	} else {
		if oldAddr != (tcpip.AddressWithPrefix{}) {
			if err := ifs.ns.mu.stack.RemoveAddress(ifs.nicid, oldAddr.Address); err != nil {
				syslog.Infof("NIC %s: failed to remove DHCP address %s: %s", name, oldAddr, err)
			} else {
				syslog.Infof("NIC %s: removed DHCP address %s", name, oldAddr)
			}

			// Remove the dynamic routes for this interface.
			ifs.ns.UpdateRoutesByInterfaceLocked(ifs.nicid, routes.ActionDeleteDynamic)
		}

		if newAddr != (tcpip.AddressWithPrefix{}) {
			if err := ifs.ns.mu.stack.AddProtocolAddressWithOptions(ifs.nicid, tcpip.ProtocolAddress{
				Protocol:          ipv4.ProtocolNumber,
				AddressWithPrefix: newAddr,
			}, stack.FirstPrimaryEndpoint); err != nil {
				syslog.Infof("NIC %s: failed to add DHCP acquired address %s: %s", name, newAddr, err)
			} else {
				syslog.Infof("NIC %s: DHCP acquired address %s for %s", name, newAddr, config.LeaseLength)

				// Add a default route and a route for the local subnet.
				rs := defaultRoutes(ifs.nicid, config.Gateway)
				rs = append(rs, addressWithPrefixRoute(ifs.nicid, newAddr))
				syslog.Infof("adding routes %+v with metric=<not-set> dynamic=true", rs)

				if err := ifs.ns.AddRoutesLocked(rs, metricNotSet, true /* dynamic */); err != nil {
					syslog.Infof("error adding routes for DHCP address/gateway: %s", err)
				}
			}
		}
		ifs.ns.OnInterfacesChanged(ifs.ns.getNetInterfaces2Locked())
	}
	ifs.ns.mu.Unlock()

	ifs.mu.Lock()
	sameDNS := len(ifs.mu.dnsServers) == len(config.DNS)
	if sameDNS {
		for i := range ifs.mu.dnsServers {
			sameDNS = ifs.mu.dnsServers[i] == config.DNS[i]
			if !sameDNS {
				break
			}
		}
	}
	if !sameDNS {
		syslog.Infof("NIC %s: setting DNS servers: %s", name, config.DNS)

		ifs.mu.dnsServers = config.DNS

	}
	ifs.mu.Unlock()

	if !sameDNS {
		ifs.ns.dnsClient.SetRuntimeServers(ifs.ns.getRuntimeDNSServerRefs())
	}
}

func (ifs *ifState) setDHCPStatusLocked(name string, enabled bool) {
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
	ifs.ns.mu.Lock()

	name := ifs.ns.nameLocked(ifs.nicid)

	ifs.mu.Lock()
	switch s {
	case link.StateClosed:
		syslog.Infof("NIC %s: link.StateClosed", name)
		delete(ifs.ns.mu.ifStates, ifs.nicid)
		fallthrough
	case link.StateDown:
		syslog.Infof("NIC %s: link.StateDown", name)

		// Stop DHCP, this triggers the removal of all dynamically obtained configuration (IP, routes,
		// DNS servers).
		ifs.mu.dhcp.cancel()

		// TODO(crawshaw): more cleanup to be done here:
		// 	- remove link endpoint
		//	- reclaim NICID?

		if s == link.StateClosed {
			// The interface is removed, force all of its routes to be removed.
			ifs.ns.UpdateRoutesByInterfaceLocked(ifs.nicid, routes.ActionDeleteAll)
		} else {
			// The interface is down, disable static routes (dynamic ones are handled
			// by the cancelled DHCP server).
			ifs.ns.UpdateRoutesByInterfaceLocked(ifs.nicid, routes.ActionDisableStatic)
		}

	case link.StateStarted:
		syslog.Infof("NIC %s: link.StateStarted", name)
		// Re-enable static routes out this interface.
		ifs.ns.UpdateRoutesByInterfaceLocked(ifs.nicid, routes.ActionEnableStatic)
		if ifs.mu.dhcp.enabled {
			ifs.mu.dhcp.cancel()
			ifs.runDHCPLocked(name)
		}
		// TODO(ckuiper): Remove this, as we shouldn't create default routes w/o a
		// gateway given. Before doing so make sure nothing is still relying on
		// this.
		// Update the state before adding the routes, so they are properly enabled.
		ifs.mu.state = s
		if err := ifs.ns.AddRoutesLocked(defaultRoutes(ifs.nicid, ""), lowPriorityRoute, true /* dynamic */); err != nil {
			syslog.Infof("error adding default routes: %v", err)
		}
	}
	ifs.mu.state = s
	ifs.mu.Unlock()

	interfaces := ifs.ns.getNetInterfaces2Locked()
	ifs.ns.mu.Unlock()

	ifs.ns.dnsClient.SetRuntimeServers(ifs.ns.getRuntimeDNSServerRefs())
	ifs.ns.OnInterfacesChanged(interfaces)
}

// Return a slice of references to each NIC's DNS servers.
// The caller takes ownership of the returned slice.
func (ns *Netstack) getRuntimeDNSServerRefs() []*[]tcpip.Address {
	ns.mu.Lock()
	defer ns.mu.Unlock()

	refs := make([]*[]tcpip.Address, 0, len(ns.mu.ifStates))
	for _, ifs := range ns.mu.ifStates {
		ifs.mu.Lock()
		refs = append(refs, &ifs.mu.dnsServers)
		ifs.mu.Unlock()
	}
	return refs
}

func (ns *Netstack) getdnsServers() []tcpip.Address {
	defaultServers := ns.dnsClient.GetDefaultServers()
	uniqServers := make(map[tcpip.Address]struct{})

	ns.mu.Lock()
	for _, ifs := range ns.mu.ifStates {
		ifs.mu.Lock()
		for _, server := range ifs.mu.dnsServers {
			uniqServers[server] = struct{}{}
		}
		ifs.mu.Unlock()
	}
	ns.mu.Unlock()

	out := make([]tcpip.Address, 0, len(defaultServers)+len(uniqServers))
	out = append(out, defaultServers...)
	for server := range uniqServers {
		out = append(out, server)
	}
	return out
}

var nameProviderErrorLogged uint32 = 0

func (ns *Netstack) getDeviceName() string {
	result, err := ns.nameProvider.GetDeviceName()
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
	}, loopback.New(), link.NewLoopbackController(), false, defaultInterfaceMetric, ethernet.InfoFeatureLoopback, "[none]")
	if err != nil {
		return err
	}

	ifs.mu.Lock()
	ifs.mu.state = link.StateStarted
	nicid := ifs.nicid
	ifs.mu.Unlock()

	ipv4LoopbackPrefix := tcpip.AddressMask(net.IP(ipv4Loopback).DefaultMask()).Prefix()
	ipv4LoopbackAddressWithPrefix := tcpip.AddressWithPrefix{
		Address:   ipv4Loopback,
		PrefixLen: ipv4LoopbackPrefix,
	}
	ipv4LoopbackRoute := addressWithPrefixRoute(nicid, ipv4LoopbackAddressWithPrefix)

	if err := ns.mu.stack.AddProtocolAddress(nicid, tcpip.ProtocolAddress{
		Protocol:          ipv4.ProtocolNumber,
		AddressWithPrefix: ipv4LoopbackAddressWithPrefix,
	}); err != nil {
		return fmt.Errorf("error adding address %s to NIC ID %d: %s", ipv4LoopbackAddressWithPrefix, nicid, err)
	}

	if err := ns.mu.stack.AddAddressRange(nicid, ipv4.ProtocolNumber, ipv4LoopbackRoute.Destination); err != nil {
		return fmt.Errorf("loopback: adding ipv4 subnet failed: %s", err)
	}

	if err := ns.mu.stack.AddAddress(nicid, ipv6.ProtocolNumber, ipv6Loopback); err != nil {
		return fmt.Errorf("loopback: adding ipv6 address failed: %s", err)
	}

	if err := ns.AddRoutesLocked(
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
	ns.mu.Lock()
	for _, nicid := range nics {
		ifs, ok := ns.mu.ifStates[nicid]
		if !ok {
			panic("NIC known by netstack not in interface table")
		}
		if err := ifs.eth.SetPromiscuousMode(true); err != nil {
			return nil, err
		}
		links = append(links, ifs.bridgeable)
	}
	ns.mu.Unlock()

	b := bridge.New(links)
	return ns.addEndpoint(func(nicid tcpip.NICID) string {
		return fmt.Sprintf("br%d", nicid)
	}, b, b, false, defaultInterfaceMetric, 0, "[none]")
}

func (ns *Netstack) addEth(topological_path string, config netstack.InterfaceConfig, device ethernet.Device) (*ifState, error) {
	client, err := eth.NewClient("netstack", topological_path, device, ns.arena)
	if err != nil {
		return nil, err
	}

	return ns.addEndpoint(func(nicid tcpip.NICID) string {
		if len(config.Name) == 0 {
			return fmt.Sprintf("eth%d", nicid)
		}
		return config.Name
	}, eth.NewLinkEndpoint(client), client, true, routes.Metric(config.Metric), client.Info.Features, config.Filepath)
}

func (ns *Netstack) addEndpoint(
	nameFn func(nicid tcpip.NICID) string,
	ep stack.LinkEndpoint,
	controller link.Controller,
	doFilter bool,
	metric routes.Metric,
	features uint32,
	filepath string,
) (*ifState, error) {
	ifs := &ifState{
		ns:       ns,
		eth:      controller,
		filepath: filepath,
		features: features,
	}
	createFn := ns.mu.stack.CreateNamedNIC
	if features&ethernet.InfoFeatureLoopback != 0 {
		createFn = ns.mu.stack.CreateNamedLoopbackNIC
	}

	ifs.mu.state = link.StateUnknown
	ifs.mu.metric = metric
	ifs.mu.dhcp.running = func() bool { return false }
	ifs.mu.dhcp.cancel = func() {}

	ifs.eth.SetOnStateChange(ifs.stateChange)

	// LinkEndpoint chains:
	// Put sniffer as close as the NIC.
	if ns.sniff {
		// A wrapper LinkEndpoint should encapsulate the underlying
		// one, and manifest itself to 3rd party netstack.
		ep = sniffer.New(ep)
	}

	if doFilter {
		ifs.filterEndpoint = filter.NewEndpoint(ns.filter, ep)
		ep = ifs.filterEndpoint
	}
	ifs.bridgeable = bridge.NewEndpoint(ep)
	ep = ifs.bridgeable
	ifs.endpoint = ep

	ns.mu.Lock()
	defer ns.mu.Unlock()

	ifs.nicid = ns.mu.countNIC + 1
	name := nameFn(ifs.nicid)
	ns.mu.ifStates[ifs.nicid] = ifs
	ns.mu.countNIC++

	syslog.Infof("NIC %s added [sniff = %t]", name, ns.sniff)

	if err := createFn(ifs.nicid, name, ep); err != nil {
		return nil, fmt.Errorf("NIC %s: could not create NIC: %s", name, err)
	}
	if ep.Capabilities()&stack.CapabilityResolutionRequired > 0 {
		if err := ns.mu.stack.AddAddress(ifs.nicid, arp.ProtocolNumber, arp.ProtocolAddress); err != nil {
			return nil, fmt.Errorf("NIC %s: adding arp address failed: %s", name, err)
		}
	}

	ifs.mu.Lock()
	defer ifs.mu.Unlock()

	// TODO(cl/268322915): remove this.
	if linkAddr := ep.LinkAddress(); len(linkAddr) > 0 {
		lladdr := header.LinkLocalAddr(linkAddr)
		if err := ns.mu.stack.AddAddress(ifs.nicid, ipv6.ProtocolNumber, lladdr); err != nil {
			return nil, fmt.Errorf("NIC %s: adding link-local IPv6 %s failed: %s", name, lladdr, err)
		}

		ifs.mu.dhcp.Client = dhcp.NewClient(ns.mu.stack, ifs.nicid, linkAddr, dhcpAcquisition, dhcpBackoff, dhcpRetransmission, ifs.dhcpAcquired)

		syslog.Infof("NIC %s: link-local IPv6: %s", name, lladdr)
	}

	return ifs, nil
}

func (ns *Netstack) getIfStateInfo(nicInfo map[tcpip.NICID]stack.NICInfo) map[tcpip.NICID]ifStateInfo {
	ifStates := make(map[tcpip.NICID]ifStateInfo)
	ns.mu.Lock()
	for id, ifs := range ns.mu.ifStates {
		ni, ok := nicInfo[id]
		if !ok {
			continue
		}
		ifs.mu.Lock()
		info := ifStateInfo{
			NICInfo:     ni,
			nicid:       ifs.nicid,
			features:    ifs.features,
			filepath:    ifs.filepath,
			state:       ifs.mu.state,
			dnsServers:  ifs.mu.dnsServers,
			dhcpEnabled: ifs.mu.dhcp.enabled,
		}
		if ifs.mu.dhcp.enabled {
			info.dhcpInfo = ifs.mu.dhcp.Info()
			info.dhcpStats = ifs.mu.dhcp.Stats()
		}
		ifs.mu.Unlock()
		ifStates[id] = info
	}
	ns.mu.Unlock()
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
