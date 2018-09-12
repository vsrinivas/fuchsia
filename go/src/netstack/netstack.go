// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

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
	"fidl/fuchsia/netstack"
	"fidl/zircon/ethernet"

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
	arena        *eth.Arena
	socketServer *socketServer

	deviceSettings *devicesettings.DeviceSettingsManagerInterface
	dnsClient      *dns.Client

	mu struct {
		sync.Mutex
		stack              *stack.Stack
		transactionRequest *netstack.RouteTableTransactionInterfaceRequest
	}
	nodename string
	ifStates map[tcpip.NICID]*ifState

	countNIC tcpip.NICID

	filter *filter.Filter
}

type dhcpState struct {
	client  *dhcp.Client
	ctx     context.Context
	cancel  context.CancelFunc
	enabled bool
}

// Each ifState tracks the state of a network interface.
type ifState struct {
	ns     *Netstack
	ctx    context.Context
	cancel context.CancelFunc
	eth    *eth.Client
	state  eth.State
	dhcpState

	// guarded by ns.mu
	// NIC is defined in //garnet/go/src/netstack/netiface/netiface.go
	// TODO(porce): Consider replacement with //third_party/netstack/tcpip/stack/stack.go
	nic *netiface.NIC

	// LinkEndpoint responsible to track traffic statistics
	statsEP stats.StatsEndpoint
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

	ifs, ok := ns.ifStates[nic]
	if !ok {
		panic(fmt.Sprintf("Interface state table out of sync: NIC [%d] known to third_party/netstack not found in garnet/netstack", nic))
	}

	{
		addr, subnet, err := ns.mu.stack.GetMainNICAddress(nic, protocol)
		if err != nil {
			return fmt.Errorf("error querying NIC ID %d, error: %s", nic, err)
		}
		netmask := subnet.Mask()
		if netmask == "" {
			addressSize := len(addr) * 8
			netmask = util.CIDRMask(addressSize, addressSize)
		}
		ifs.staticAddressChanged(addr, netmask)
	}

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

	if err := ns.mu.stack.AddAddress(nic, protocol, addr); err != nil {
		return fmt.Errorf("error adding address %s to NIC ID %d: %s", addr, nic, err)
	}

	if err := ns.mu.stack.AddSubnet(nic, protocol, subnet); err != nil {
		return fmt.Errorf("error adding subnet %+v to NIC ID %d: %s", subnet, nic, err)
	}

	ifs, ok := ns.ifStates[nic]
	if !ok {
		panic(fmt.Sprintf("Interface state table out of sync: NIC [%d] known to third_party/netstack not found in garnet/netstack", nic))
	}

	ifs.staticAddressChanged(addr, subnet.Mask())
	return nil
}

func (ifs *ifState) staticAddressChanged(newAddr tcpip.Address, netmask tcpip.AddressMask) {
	ifs.ns.mu.Lock()
	ifs.nic.Addr = newAddr
	ifs.nic.Netmask = netmask
	ifs.ns.mu.Unlock()

	OnInterfacesChanged()
}

func (ifs *ifState) dhcpAcquired(oldAddr, newAddr tcpip.Address, config dhcp.Config) {
	if oldAddr != "" && oldAddr != newAddr {
		log.Printf("NIC %s: DHCP IP %s expired", ifs.nic.Name, oldAddr)
	}
	if config.Error != nil {
		log.Printf("%v", config.Error)
		return
	}
	if newAddr == "" {
		log.Printf("NIC %s: DHCP could not acquire address", ifs.nic.Name)
		return
	}
	log.Printf("NIC %s: DHCP acquired IP %s for %s", ifs.nic.Name, newAddr, config.LeaseLength)
	log.Printf("NIC %s: Adding DNS servers: %v", ifs.nic.Name, config.DNS)

	// Update default route with new gateway.
	ifs.ns.mu.Lock()
	ifs.nic.Routes = defaultRouteTable(ifs.nic.ID, config.Gateway)
	ifs.nic.Routes = append(ifs.nic.Routes, subnetRoute(newAddr, config.SubnetMask, ifs.nic.ID))
	ifs.nic.Netmask = config.SubnetMask
	ifs.nic.Addr = newAddr
	ifs.nic.DNSServers = config.DNS
	ifs.ns.mu.Unlock()

	ifs.ns.mu.stack.SetRouteTable(ifs.ns.flattenRouteTables())
	ifs.ns.dnsClient.SetRuntimeServers(ifs.ns.getRuntimeDNSServerRefs())

	OnInterfacesChanged()
}

func (ifs *ifState) setDHCPStatus(enabled bool) {
	ifs.ns.mu.Lock()
	defer ifs.ns.mu.Unlock()
	d := &ifs.dhcpState
	if enabled == d.enabled {
		return
	}
	if enabled {
		d.ctx, d.cancel = context.WithCancel(ifs.ctx)
		d.client.Run(d.ctx)
	} else if d.cancel != nil {
		d.cancel()
	}
	d.enabled = enabled
}

func (ifs *ifState) stateChange(s eth.State) {
	switch s {
	case eth.StateClosed:
		ifs.ns.mu.Lock()
		delete(ifs.ns.ifStates, ifs.nic.ID)
		ifs.ns.mu.Unlock()
		fallthrough
	case eth.StateDown:
		log.Printf("NIC %s: stopped", ifs.nic.Name)
		if ifs.cancel != nil {
			ifs.cancel()
		}
		if ifs.dhcpState.cancel != nil {
			// TODO: consider remembering DHCP status
			ifs.setDHCPStatus(false)
		}

		// TODO(crawshaw): more cleanup to be done here:
		// 	- remove link endpoint
		//	- reclaim NICID?

		ifs.ns.mu.Lock()
		ifs.nic.Routes = nil
		ifs.nic.Netmask = ""
		ifs.nic.Addr = ""
		ifs.nic.DNSServers = nil
		ifs.ns.mu.Unlock()

		ifs.ns.mu.stack.SetRouteTable(ifs.ns.flattenRouteTables())
		ifs.ns.dnsClient.SetRuntimeServers(ifs.ns.getRuntimeDNSServerRefs())
	case eth.StateStarted:
		// Only call `restarted` if we are not in the initial state (which means we're still starting).
		if ifs.state != eth.StateUnknown {
			log.Printf("NIC %s: restarting", ifs.nic.Name)
			ifs.ns.mu.Lock()
			ifs.ctx, ifs.cancel = context.WithCancel(context.Background())
			ifs.nic.Routes = defaultRouteTable(ifs.nic.ID, "")
			ifs.ns.mu.Unlock()

			ifs.ns.mu.stack.SetRouteTable(ifs.ns.flattenRouteTables())
			ifs.setDHCPStatus(true)
		}
	}
	ifs.state = s
	// Note: This will fire again once DHCP succeeds.
	OnInterfacesChanged()
}

func (ns *Netstack) flattenRouteTables() []tcpip.Route {
	ns.mu.Lock()
	defer ns.mu.Unlock()

	routes := make([]tcpip.Route, 0)
	nics := make(map[tcpip.NICID]*netiface.NIC)
	for _, ifs := range ns.ifStates {
		routes = append(routes, ifs.nic.Routes...)
		nics[ifs.nic.ID] = ifs.nic
	}
	sort.Slice(routes, func(i, j int) bool {
		return netiface.Less(&routes[i], &routes[j], nics)
	})
	if debug {
		for i, ifs := range ns.ifStates {
			log.Printf("[%v] nicid: %v, addr: %v, routes: %v",
				i, ifs.nic.ID, ifs.nic.Addr, ifs.nic.Routes)
		}
	}

	return routes
}

// Return a slice of references to each NIC's DNS servers.
// The caller takes ownership of the returned slice.
func (ns *Netstack) getRuntimeDNSServerRefs() []*[]tcpip.Address {
	ns.mu.Lock()
	defer ns.mu.Unlock()

	refs := make([]*[]tcpip.Address, 0, len(ns.ifStates))
	for _, ifs := range ns.ifStates {
		refs = append(refs, &ifs.nic.DNSServers)
	}
	return refs
}

func (ns *Netstack) getDNSServers() []tcpip.Address {
	ns.mu.Lock()
	defer ns.mu.Unlock()

	defaultServers := ns.dnsClient.GetDefaultServers()
	uniqServers := make(map[tcpip.Address]struct{})
	for _, ifs := range ns.ifStates {
		for _, server := range ifs.nic.DNSServers {
			uniqServers[server] = struct{}{}
		}
	}

	out := make([]tcpip.Address, 0, len(defaultServers)+len(uniqServers))
	out = append(out, defaultServers...)
	for server := range uniqServers {
		out = append(out, server)
	}
	return out
}

// TODO(tamird): refactor to use addEndpoint.
func (ns *Netstack) addLoopback() error {
	const nicid = 1
	ctx, cancel := context.WithCancel(context.Background())
	nic := &netiface.NIC{
		ID:       nicid,
		Addr:     ipv4Loopback,
		Netmask:  tcpip.AddressMask(strings.Repeat("\xff", 4)),
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
		nic:    nic,
		state:  eth.StateStarted,
	}
	ifs.statsEP.Nic = ifs.nic

	ns.mu.Lock()
	if len(ns.ifStates) > 0 {
		ns.mu.Unlock()
		return fmt.Errorf("loopback: other interfaces already registered")
	}
	ns.ifStates[nicid] = ifs
	ns.countNIC++
	ns.mu.Unlock()

	linkID := loopback.New()
	if debug {
		linkID = sniffer.New(linkID)
	}
	linkID = ifs.statsEP.Wrap(linkID)

	if err := ns.mu.stack.CreateNIC(nicid, linkID); err != nil {
		return fmt.Errorf("loopback: could not create interface: %v", err)
	}
	if err := ns.mu.stack.AddAddress(nicid, ipv4.ProtocolNumber, ipv4Loopback); err != nil {
		return fmt.Errorf("loopback: adding ipv4 address failed: %v", err)
	}
	if err := ns.mu.stack.AddAddress(nicid, ipv6.ProtocolNumber, ipv6Loopback); err != nil {
		return fmt.Errorf("loopback: adding ipv6 address failed: %v", err)
	}

	ns.mu.stack.SetRouteTable(ns.flattenRouteTables())

	return nil
}

func (ns *Netstack) Bridge(nics []tcpip.NICID) error {
	// TODO(stijlist): save bridge in netstack state as NetInterface
	links := make([]stack.LinkEndpoint, 0, len(nics))
	for _, nicid := range nics {
		nic, ok := ns.ifStates[nicid]
		if !ok {
			panic("NIC known by netstack not in interface table")
		}
		if err := nic.eth.SetPromiscuousMode(true); err != nil {
			return err
		}
		links = append(links, &nic.statsEP)
	}

	return ns.addEndpoint(func(*ifState) (stack.LinkEndpoint, error) {
		return bridge.New(links), nil
	}, func(*ifState) error {
		return nil
	})
}

func (ns *Netstack) addEth(topological_path string, config netstack.InterfaceConfig, device ethernet.DeviceInterface) error {
	var client *eth.Client
	return ns.addEndpoint(func(ifs *ifState) (stack.LinkEndpoint, error) {
		var err error
		client, err = eth.NewClient("netstack", topological_path, &device, ns.arena, ifs.stateChange)
		if err != nil {
			return nil, err
		}
		ifs.eth = client
		ifs.nic.Features = client.Info.Features
		ifs.nic.Name = config.Name
		return eth.NewLinkEndpoint(client), nil
	}, func(ifs *ifState) error {
		// TODO(NET-298): Delete this condition after enabling multiple concurrent DHCP clients
		// in third_party/netstack.
		if client.Info.Features&ethernet.InfoFeatureWlan != 0 {
			// WLAN: Upon 802.1X port open, the state change will ensue, which
			// will invoke the DHCP Client.
			return nil
		}

		status, err := client.GetStatus()
		if err != nil {
			return fmt.Errorf("NIC %s: failed to get device status for MAC=%x: %v", ifs.nic.Name, client.Info.Mac, err)
		}

		switch config.IpAddressConfig.Which() {
		case netstack.IpAddressConfigDhcp:
			if status == eth.LinkUp {
				ifs.setDHCPStatus(true)
			}
		case netstack.IpAddressConfigStaticIp:
			subnet := config.IpAddressConfig.StaticIp
			protocol, tcpipAddr, retval := ns.validateInterfaceAddress(subnet.Addr, subnet.PrefixLen)
			if retval.Status != netstack.StatusOk {
				return fmt.Errorf("NIC %s: received static IpAddressConfig with an invalid IP specified: [%+v]", ifs.nic.Name, subnet)
			}
			ns.setInterfaceAddress(ifs.nic.ID, protocol, tcpipAddr, subnet.PrefixLen)
		}
		return nil
	})
}

func (ns *Netstack) addEndpoint(makeEndpoint func(*ifState) (stack.LinkEndpoint, error), finalize func(*ifState) error) error {
	ctx, cancel := context.WithCancel(context.Background())

	ifs := &ifState{
		ns:     ns,
		ctx:    ctx,
		cancel: cancel,
		nic:    &netiface.NIC{},
		state:  eth.StateUnknown,
	}
	ifs.statsEP.Nic = ifs.nic

	ep, err := makeEndpoint(ifs)
	if err != nil {
		return err
	}
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

	linkID = ifs.statsEP.Wrap(linkID)

	ns.mu.Lock()
	ifs.nic.Ipv6addrs = []tcpip.Address{lladdr}

	nicid := ns.countNIC + 1
	ifs.nic.ID = nicid
	ifs.nic.Routes = defaultRouteTable(nicid, "")
	ns.ifStates[nicid] = ifs
	ns.countNIC++
	ns.mu.Unlock()

	log.Printf("NIC %s added", ifs.nic.Name)

	if err := ns.mu.stack.CreateNIC(nicid, linkID); err != nil {
		return fmt.Errorf("NIC %s: could not create NIC: %v", ifs.nic.Name, err)
	}
	if err := ns.mu.stack.AddAddress(nicid, arp.ProtocolNumber, arp.ProtocolAddress); err != nil {
		return fmt.Errorf("NIC %s: adding arp address failed: %v", ifs.nic.Name, err)
	}
	if err := ns.mu.stack.AddAddress(nicid, ipv6.ProtocolNumber, lladdr); err != nil {
		return fmt.Errorf("NIC %s: adding link-local IPv6 %v failed: %v", ifs.nic.Name, lladdr, err)
	}
	snaddr := header.SolicitedNodeAddr(lladdr)
	if err := ns.mu.stack.AddAddress(nicid, ipv6.ProtocolNumber, snaddr); err != nil {
		return fmt.Errorf("NIC %s: adding solicited-node IPv6 %v (link-local IPv6 %v) failed: %v", ifs.nic.Name, snaddr, lladdr, err)
	}
	log.Printf("NIC %s: link-local IPv6: %v", ifs.nic.Name, lladdr)

	ifs.dhcpState.client = dhcp.NewClient(ns.mu.stack, nicid, linkAddr, ifs.dhcpAcquired)

	// Add default route. This will get clobbered later when we get a DHCP response.
	ns.mu.stack.SetRouteTable(ns.flattenRouteTables())

	return finalize(ifs)
}

func (ns *Netstack) validateInterfaceAddress(address netstack.NetAddress, prefixLen uint8) (tcpip.NetworkProtocolNumber, tcpip.Address, netstack.NetErr) {
	var protocol tcpip.NetworkProtocolNumber
	switch address.Family {
	case netstack.NetAddressFamilyIpv4:
		protocol = ipv4.ProtocolNumber
	case netstack.NetAddressFamilyIpv6:
		return 0, "", netstack.NetErr{Status: netstack.StatusIpv4Only, Message: "IPv6 not yet supported"}
	}

	addr := fidlconv.NetAddressToTCPIPAddress(address)

	if (8 * len(addr)) < int(prefixLen) {
		return 0, "", netstack.NetErr{Status: netstack.StatusParseError, Message: "prefix length exceeds address length"}
	}

	return protocol, addr, netstack.NetErr{Status: netstack.StatusOk}
}
