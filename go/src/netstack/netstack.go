// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package netstack

import (
	"context"
	"fmt"
	"log"
	"sort"
	"strings"
	"sync"

	"netstack/dns"
	"netstack/fidlconv"
	"netstack/filter"
	"netstack/link/bridge"
	"netstack/link/eth"
	"netstack/link/stats"
	"netstack/netiface"
	"netstack/util"

	"fidl/fuchsia/devicesettings"
	"fidl/fuchsia/hardware/ethernet"
	"fidl/fuchsia/net"
	"fidl/fuchsia/netstack"

	"github.com/google/netstack/dhcp"
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
	deviceSettingsManagerNodenameKey = "DeviceName"
	defaultNodename                  = "fuchsia-unset-device-name"

	ipv4Loopback tcpip.Address = "\x7f\x00\x00\x01"
	ipv6Loopback tcpip.Address = "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01"
)

// A Netstack tracks all of the running state of the network stack.
type Netstack struct {
	arena *eth.Arena

	deviceSettings *devicesettings.DeviceSettingsManagerInterface
	dnsClient      *dns.Client

	mu struct {
		sync.Mutex
		stack              *stack.Stack
		transactionRequest *netstack.RouteTableTransactionInterfaceRequest
		countNIC           tcpip.NICID
		ifStates           map[tcpip.NICID]*ifState
	}
	nodename string

	filter *filter.Filter

	OnInterfacesChanged func([]netstack.NetInterface)
}

type dhcpState struct {
	client  *dhcp.Client
	cancel  context.CancelFunc
	enabled bool
}

// Each ifState tracks the state of a network interface.
type ifState struct {
	ns  *Netstack
	eth eth.Controller
	mu  struct {
		sync.Mutex
		state eth.State
		// TODO(NET-1223): remove and replace with refs to stack via ns
		nic *netiface.NIC
		dhcpState
	}
	ctx    context.Context
	cancel context.CancelFunc

	// The "outermost" LinkEndpoint implementation (the composition of link
	// endpoint functionality happens by wrapping other link endpoints).
	endpoint stack.LinkEndpoint

	// statsEP and bridgeable are wrapper LinkEndpoint implementations that need
	// to be accessed by their concrete type.
	statsEP    *stats.StatsEndpoint
	bridgeable *bridge.BridgeableEndpoint
}

func defaultRouteTable(nicid tcpip.NICID, gateway tcpip.Address) []tcpip.Route {
	return []tcpip.Route{
		{
			Destination: tcpip.Address(strings.Repeat("\x00", 4)),
			Mask:        tcpip.AddressMask(strings.Repeat("\x00", 4)),
			Gateway:     gateway,
			NIC:         nicid,
		},
		{
			Destination: tcpip.Address(strings.Repeat("\x00", 16)),
			Mask:        tcpip.AddressMask(strings.Repeat("\x00", 16)),
			NIC:         nicid,
		},
	}
}

func subnetRoute(addr tcpip.Address, mask tcpip.AddressMask, nicid tcpip.NICID) tcpip.Route {
	return tcpip.Route{
		Destination: util.ApplyMask(addr, mask),
		Mask:        tcpip.AddressMask(mask),
		Gateway:     tcpip.Address(""),
		NIC:         nicid,
	}
}

func (ns *Netstack) removeInterfaceAddress(nic tcpip.NICID, protocol tcpip.NetworkProtocolNumber, addr tcpip.Address, prefixLen uint8) error {
	subnet, err := toSubnet(addr, prefixLen)
	if err != nil {
		return fmt.Errorf("error parsing subnet format for NIC ID %d: %s", nic, err)
	}

	ns.mu.Lock()
	if err := func() error {
		if hasSubnet, err := ns.mu.stack.ContainsSubnet(nic, subnet); err != nil {
			return fmt.Errorf("error finding subnet %+v for NIC ID %d: %s", subnet, nic, err)
		} else if hasSubnet {
			if err := ns.mu.stack.RemoveSubnet(nic, subnet); err != nil {
				return fmt.Errorf("error removing subnet %+v from NIC ID %d: %s", subnet, nic, err)
			}
		} else {
			return fmt.Errorf("no such subnet %+v for NIC ID %d", subnet, nic)
		}

		if err := ns.mu.stack.RemoveAddress(nic, addr); err != nil {
			return fmt.Errorf("error removing address %s from NIC ID %d: %s", addr, nic, err)
		}

		if addr, subnet, err := ns.mu.stack.GetMainNICAddress(nic, protocol); err != nil {
			return fmt.Errorf("error querying NIC ID %d, error: %s", nic, err)
		} else {
			netmask := subnet.Mask()
			if netmask == "" {
				addressSize := len(addr) * 8
				netmask = util.CIDRMask(addressSize, addressSize)
			}

			if ifs, ok := ns.mu.ifStates[nic]; !ok {
				panic(fmt.Sprintf("Interface state table out of sync: NIC [%d] known to third_party/netstack not found in garnet/netstack", nic))
			} else {
				ifs.staticAddressChanged(addr, netmask)
				ifs.ns.mu.stack.SetRouteTable(ifs.ns.flattenRouteTablesLocked())
			}
		}
		return nil
	}(); err != nil {
		ns.mu.Unlock()
		return err
	}

	interfaces := ns.getNetInterfacesLocked()
	ns.mu.Unlock()
	ns.OnInterfacesChanged(interfaces)
	return nil
}

func toSubnet(address tcpip.Address, prefixLen uint8) (tcpip.Subnet, error) {
	m := util.CIDRMask(int(prefixLen), int(len(address)*8))
	return tcpip.NewSubnet(util.ApplyMask(address, m), m)
}

func (ns *Netstack) setInterfaceAddress(nic tcpip.NICID, protocol tcpip.NetworkProtocolNumber, addr tcpip.Address, prefixLen uint8) error {
	subnet, err := toSubnet(addr, prefixLen)
	if err != nil {
		return fmt.Errorf("error parsing subnet format for NIC ID %d: %s", nic, err)
	}

	ns.mu.Lock()
	if err := func() error {
		if err := ns.mu.stack.AddAddress(nic, protocol, addr); err != nil {
			return fmt.Errorf("error adding address %s to NIC ID %d: %s", addr, nic, err)
		}

		if err := ns.mu.stack.AddSubnet(nic, protocol, subnet); err != nil {
			return fmt.Errorf("error adding subnet %+v to NIC ID %d: %s", subnet, nic, err)
		}

		if ifs, ok := ns.mu.ifStates[nic]; !ok {
			panic(fmt.Sprintf("Interface state table out of sync: NIC [%d] known to third_party/netstack not found in garnet/netstack", nic))
		} else {
			ifs.staticAddressChanged(addr, subnet.Mask())
			ifs.ns.mu.stack.SetRouteTable(ifs.ns.flattenRouteTablesLocked())
		}
		return nil
	}(); err != nil {
		ns.mu.Unlock()
		return err
	}

	interfaces := ns.getNetInterfacesLocked()
	ns.mu.Unlock()
	ns.OnInterfacesChanged(interfaces)
	return nil
}

func (ifs *ifState) staticAddressChanged(newAddr tcpip.Address, netmask tcpip.AddressMask) {
	ifs.mu.Lock()
	ifs.mu.nic.Addr = newAddr
	ifs.mu.nic.Netmask = netmask
	ifs.mu.nic.Routes = append(ifs.mu.nic.Routes, subnetRoute(newAddr, netmask, ifs.mu.nic.ID))
	ifs.mu.Unlock()
}

func (ifs *ifState) dhcpAcquired(oldAddr, newAddr tcpip.Address, config dhcp.Config) {
	if oldAddr != "" && oldAddr != newAddr {
		log.Printf("NIC %s: DHCP IP %s expired", ifs.mu.nic.Name, oldAddr)
	}
	if config.Error != nil {
		log.Printf("%v", config.Error)
		return
	}
	if newAddr == "" {
		log.Printf("NIC %s: DHCP could not acquire address", ifs.mu.nic.Name)
		return
	}
	log.Printf("NIC %s: DHCP acquired IP %s for %s", ifs.mu.nic.Name, newAddr, config.LeaseLength)
	log.Printf("NIC %s: Adding DNS servers: %v", ifs.mu.nic.Name, config.DNS)

	// Update default route with new gateway.
	ifs.mu.Lock()
	ifs.mu.nic.Routes = defaultRouteTable(ifs.mu.nic.ID, config.Gateway)
	ifs.mu.nic.Routes = append(ifs.mu.nic.Routes, subnetRoute(newAddr, config.SubnetMask, ifs.mu.nic.ID))
	ifs.mu.nic.Netmask = config.SubnetMask
	ifs.mu.nic.Addr = newAddr
	ifs.mu.nic.DNSServers = config.DNS
	ifs.mu.Unlock()

	ifs.ns.mu.Lock()
	ifs.ns.mu.stack.SetRouteTable(ifs.ns.flattenRouteTablesLocked())
	interfaces := ifs.ns.getNetInterfacesLocked()
	ifs.ns.mu.Unlock()

	ifs.ns.dnsClient.SetRuntimeServers(ifs.ns.getRuntimeDNSServerRefs())
	ifs.ns.OnInterfacesChanged(interfaces)
}

func (ifs *ifState) setDHCPStatusLocked(enabled bool) {
	d := &ifs.mu.dhcpState
	if enabled == d.enabled {
		return
	}
	if enabled {
		ctx, cancel := context.WithCancel(ifs.ctx)
		d.cancel = cancel
		if c := d.client; c != nil {
			c.Run(ctx)
		} else {
			panic(fmt.Sprintf("nil DHCP client on interface %s", ifs.mu.nic.Name))
		}
	} else if d.cancel != nil {
		d.cancel()
	}
	d.enabled = enabled
}

func (ifs *ifState) stateChange(s eth.State) {
	ifs.ns.mu.Lock()
	ifs.mu.Lock()
	switch s {
	case eth.StateClosed:
		delete(ifs.ns.mu.ifStates, ifs.mu.nic.ID)
		fallthrough
	case eth.StateDown:
		log.Printf("NIC %s: stopped", ifs.mu.nic.Name)
		if ifs.cancel != nil {
			ifs.cancel()
		}
		if ifs.mu.dhcpState.cancel != nil {
			// TODO: consider remembering DHCP status
			ifs.setDHCPStatusLocked(false)
		}

		// TODO(crawshaw): more cleanup to be done here:
		// 	- remove link endpoint
		//	- reclaim NICID?

		ifs.mu.nic.Routes = nil
		ifs.mu.nic.Netmask = "\xff\xff\xff\xff"
		ifs.mu.nic.Addr = "\x00\x00\x00\x00"
		ifs.mu.nic.DNSServers = nil

	case eth.StateStarted:
		// Only call `restarted` if we are not in the initial state (which means we're still starting).
		if ifs.mu.state != eth.StateUnknown {
			log.Printf("NIC %s: restarting", ifs.mu.nic.Name)
			ifs.ctx, ifs.cancel = context.WithCancel(context.Background())
			ifs.mu.nic.Routes = defaultRouteTable(ifs.mu.nic.ID, "")
			ifs.setDHCPStatusLocked(true)

		}
	}
	ifs.mu.state = s
	ifs.mu.Unlock()

	ifs.ns.mu.stack.SetRouteTable(ifs.ns.flattenRouteTablesLocked())

	interfaces := ifs.ns.getNetInterfacesLocked()
	ifs.ns.mu.Unlock()

	ifs.ns.dnsClient.SetRuntimeServers(ifs.ns.getRuntimeDNSServerRefs())
	ifs.ns.OnInterfacesChanged(interfaces)
}

func (ns *Netstack) flattenRouteTablesLocked() []tcpip.Route {
	routes := make([]tcpip.Route, 0)
	nics := make(map[tcpip.NICID]*netiface.NIC)
	for _, ifs := range ns.mu.ifStates {
		ifs.mu.Lock()
		routes = append(routes, ifs.mu.nic.Routes...)
		nics[ifs.mu.nic.ID] = ifs.mu.nic
		ifs.mu.Unlock()
	}
	sort.Slice(routes, func(i, j int) bool {
		return netiface.Less(&routes[i], &routes[j], nics)
	})
	if debug {
		for i, ifs := range ns.mu.ifStates {
			log.Printf("[%v] nicid: %v, addr: %v, routes: %v",
				i, ifs.mu.nic.ID, ifs.mu.nic.Addr, ifs.mu.nic.Routes)
		}
	}

	return routes
}

// Return a slice of references to each NIC's DNS servers.
// The caller takes ownership of the returned slice.
func (ns *Netstack) getRuntimeDNSServerRefs() []*[]tcpip.Address {
	ns.mu.Lock()
	defer ns.mu.Unlock()

	refs := make([]*[]tcpip.Address, 0, len(ns.mu.ifStates))
	for _, ifs := range ns.mu.ifStates {
		ifs.mu.Lock()
		refs = append(refs, &ifs.mu.nic.DNSServers)
		ifs.mu.Unlock()
	}
	return refs
}

func (ns *Netstack) getDNSServers() []tcpip.Address {
	defaultServers := ns.dnsClient.GetDefaultServers()
	uniqServers := make(map[tcpip.Address]struct{})

	ns.mu.Lock()
	for _, ifs := range ns.mu.ifStates {
		ifs.mu.Lock()
		for _, server := range ifs.mu.nic.DNSServers {
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

func (ns *Netstack) getNodeName() string {
	nodename, status, err := ns.deviceSettings.GetString(deviceSettingsManagerNodenameKey)
	if err != nil {
		log.Printf("getNodeName: error accessing device settings: %s", err)
		return defaultNodename
	}
	if status != devicesettings.StatusOk {
		var reportStatus string
		switch status {
		case devicesettings.StatusErrNotSet:
			reportStatus = "key not set"
		case devicesettings.StatusErrInvalidSetting:
			reportStatus = "invalid setting"
		case devicesettings.StatusErrRead:
			reportStatus = "error reading key"
		case devicesettings.StatusErrIncorrectType:
			reportStatus = "value type was incorrect"
		case devicesettings.StatusErrUnknown:
			reportStatus = "unknown"
		default:
			reportStatus = fmt.Sprintf("unknown status code: %d", status)
		}
		if debug {
			log.Printf("getNodeName: device settings error: %s", reportStatus)
		}
		return defaultNodename
	}
	return nodename
}

// TODO(tamird): refactor to use addEndpoint.
func (ns *Netstack) addLoopback() error {
	const nicid = 1
	ctx, cancel := context.WithCancel(context.Background())
	nic := &netiface.NIC{
		ID:       nicid,
		Addr:     ipv4Loopback,
		Netmask:  tcpip.AddressMask(strings.Repeat("\xff", len(ipv4Loopback))),
		Features: ethernet.InfoFeatureLoopback,
		Routes: []tcpip.Route{
			{
				Destination: ipv4Loopback,
				Mask:        tcpip.AddressMask(strings.Repeat("\xff", 4)),
				NIC:         nicid,
			},
			{
				Destination: ipv6Loopback,
				Mask:        tcpip.AddressMask(strings.Repeat("\xff", 16)),
				NIC:         nicid,
			},
		},
	}

	nic.Name = "lo"

	ifs := &ifState{
		ns:     ns,
		ctx:    ctx,
		cancel: cancel,
	}

	ifs.mu.state = eth.StateStarted
	ifs.mu.nic = nic

	ns.mu.Lock()
	defer ns.mu.Unlock()
	if len(ns.mu.ifStates) > 0 {
		return fmt.Errorf("loopback: other interfaces already registered")
	}
	ns.mu.ifStates[nicid] = ifs
	ns.mu.countNIC++

	linkID := loopback.New()
	if debug {
		linkID = sniffer.New(linkID)
	}
	linkID, ifs.statsEP = stats.NewEndpoint(linkID)
	ifs.statsEP.Nic = ifs.mu.nic

	if err := ns.mu.stack.CreateNIC(nicid, linkID); err != nil {
		return fmt.Errorf("loopback: could not create interface: %v", err)
	}
	if err := ns.mu.stack.AddAddress(nicid, ipv4.ProtocolNumber, ipv4Loopback); err != nil {
		return fmt.Errorf("loopback: adding ipv4 address failed: %v", err)
	}
	if err := ns.mu.stack.AddAddress(nicid, ipv6.ProtocolNumber, ipv6Loopback); err != nil {
		return fmt.Errorf("loopback: adding ipv6 address failed: %v", err)
	}

	ns.mu.stack.SetRouteTable(ns.flattenRouteTablesLocked())

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

	return ns.addEndpoint(func(ifs *ifState) (stack.LinkEndpoint, error) {
		b := bridge.New(links)
		ifs.eth = b
		return b, nil
	}, func(ifs *ifState) error {
		if len(ifs.mu.nic.Name) == 0 {
			ifs.mu.nic.Name = fmt.Sprintf("br%d", ifs.mu.nic.ID)
		}
		return ifs.eth.Up()
	})
}

func (ns *Netstack) addEth(topological_path string, config netstack.InterfaceConfig, device ethernet.Device) (*ifState, error) {
	var client *eth.Client
	return ns.addEndpoint(func(ifs *ifState) (stack.LinkEndpoint, error) {
		var err error
		client, err = eth.NewClient("netstack", topological_path, device, ns.arena)
		if err != nil {
			return nil, err
		}
		ifs.eth = client
		ifs.mu.nic.Features = client.Info.Features
		ifs.mu.nic.Name = config.Name
		return eth.NewLinkEndpoint(client), nil
	}, func(ifs *ifState) error {
		if len(ifs.mu.nic.Name) == 0 {
			ifs.mu.nic.Name = fmt.Sprintf("eth%d", ifs.mu.nic.ID)
		}

		switch config.IpAddressConfig.Which() {
		case netstack.IpAddressConfigDhcp:
			ifs.mu.Lock()
			ifs.setDHCPStatusLocked(true)
			ifs.mu.Unlock()
		case netstack.IpAddressConfigStaticIp:
			subnet := config.IpAddressConfig.StaticIp
			protocol, tcpipAddr, retval := ns.validateInterfaceAddress(subnet.Addr, subnet.PrefixLen)
			if retval.Status != netstack.StatusOk {
				return fmt.Errorf("NIC %s: received static IpAddressConfig with an invalid IP specified: [%+v]", ifs.mu.nic.Name, subnet)
			}
			ns.setInterfaceAddress(ifs.mu.nic.ID, protocol, tcpipAddr, subnet.PrefixLen)
		}
		return ifs.eth.Up()
	})
}

func (ns *Netstack) addEndpoint(makeEndpoint func(*ifState) (stack.LinkEndpoint, error), finalize func(*ifState) error) (*ifState, error) {
	ctx, cancel := context.WithCancel(context.Background())

	ifs := &ifState{
		ns:     ns,
		ctx:    ctx,
		cancel: cancel,
	}
	ifs.mu.state = eth.StateUnknown
	ifs.mu.nic = &netiface.NIC{
		Addr:    "\x00\x00\x00\x00",
		Netmask: "\xff\xff\xff\xff",
	}

	ep, err := makeEndpoint(ifs)
	if err != nil {
		return nil, err
	}
	if ifs.eth == nil {
		return nil, fmt.Errorf("makeEndpoint func did not set ifs.eth")
	}
	ifs.eth.SetOnStateChange(ifs.stateChange)
	linkID := stack.RegisterLinkEndpoint(ep)
	linkAddr := ep.LinkAddress()
	lladdr := header.LinkLocalAddr(linkAddr)

	// LinkEndpoint chains:
	// Put sniffer as close as the NIC.
	if debug {
		// A wrapper LinkEndpoint should encapsulate the underlying
		// one, and manifest itself to 3rd party netstack.
		linkID = sniffer.New(linkID)
	}

	linkID = filter.NewEndpoint(ns.filter, linkID)
	linkID, ifs.statsEP = stats.NewEndpoint(linkID)
	linkID, ifs.bridgeable = bridge.NewEndpoint(linkID)
	ifs.endpoint = ifs.bridgeable

	ns.mu.Lock()

	nicid := ns.mu.countNIC + 1
	ns.mu.ifStates[nicid] = ifs
	ns.mu.countNIC++

	log.Printf("NIC %s added", ifs.mu.nic.Name)

	if err := ns.mu.stack.CreateNIC(nicid, linkID); err != nil {
		return nil, fmt.Errorf("NIC %s: could not create NIC: %v", ifs.mu.nic.Name, err)
	}
	if err := ns.mu.stack.AddAddress(nicid, arp.ProtocolNumber, arp.ProtocolAddress); err != nil {
		return nil, fmt.Errorf("NIC %s: adding arp address failed: %v", ifs.mu.nic.Name, err)
	}
	if err := ns.mu.stack.AddAddress(nicid, ipv6.ProtocolNumber, lladdr); err != nil {
		return nil, fmt.Errorf("NIC %s: adding link-local IPv6 %v failed: %v", ifs.mu.nic.Name, lladdr, err)
	}
	snaddr := header.SolicitedNodeAddr(lladdr)
	if err := ns.mu.stack.AddAddress(nicid, ipv6.ProtocolNumber, snaddr); err != nil {
		return nil, fmt.Errorf("NIC %s: adding solicited-node IPv6 %v (link-local IPv6 %v) failed: %v", ifs.mu.nic.Name, snaddr, lladdr, err)
	}
	log.Printf("NIC %s: link-local IPv6: %v", ifs.mu.nic.Name, lladdr)

	ifs.mu.Lock()
	ifs.mu.nic.ID = nicid
	ifs.mu.nic.Routes = defaultRouteTable(nicid, "")
	ifs.mu.nic.Ipv6addrs = []tcpip.Address{lladdr}
	ifs.statsEP.Nic = ifs.mu.nic
	ifs.mu.dhcpState.client = dhcp.NewClient(ns.mu.stack, nicid, linkAddr, ifs.dhcpAcquired)
	ifs.mu.Unlock()

	// Add default route. This will get clobbered later when we get a DHCP response.
	ns.mu.stack.SetRouteTable(ns.flattenRouteTablesLocked())
	ns.mu.Unlock()

	return ifs, finalize(ifs)
}

func (ns *Netstack) validateInterfaceAddress(address net.IpAddress, prefixLen uint8) (tcpip.NetworkProtocolNumber, tcpip.Address, netstack.NetErr) {
	var protocol tcpip.NetworkProtocolNumber
	switch address.Which() {
	case net.IpAddressIpv4:
		protocol = ipv4.ProtocolNumber
	case net.IpAddressIpv6:
		return 0, "", netstack.NetErr{Status: netstack.StatusIpv4Only, Message: "IPv6 not yet supported"}
	}

	addr := fidlconv.ToTCPIPAddress(address)

	if (8 * len(addr)) < int(prefixLen) {
		return 0, "", netstack.NetErr{Status: netstack.StatusParseError, Message: "prefix length exceeds address length"}
	}

	return protocol, addr, netstack.NetErr{Status: netstack.StatusOk}
}
