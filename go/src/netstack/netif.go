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

	"apps/netstack/deviceid"
	"apps/netstack/eth"

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

// A netstack tracks all of the running state of the network stack.
type netstack struct {
	arena      *eth.Arena
	stack      *stack.Stack
	dispatcher *socketServer

	mu       sync.Mutex
	nodename string
	netifs   map[tcpip.NICID]*netif
}

// A netif is a network interface.
type netif struct {
	ns     *netstack
	ctx    context.Context
	cancel context.CancelFunc
	nicid  tcpip.NICID
	eth    *eth.Client
	dhcp   *dhcp.Client

	// guarded by ns.mu
	addr       tcpip.Address
	netmask    tcpip.AddressMask
	routes     []tcpip.Route
	dnsServers []tcpip.Address
}

func (nif *netif) dhcpAcquired(oldAddr, newAddr tcpip.Address, config dhcp.Config) {
	if oldAddr != "" && oldAddr != newAddr {
		log.Printf("NIC %d: DHCP IP %s expired", nif.nicid, oldAddr)
	}
	if config.Error != nil {
		log.Printf("%v", config.Error)
		return
	}
	if newAddr == "" {
		log.Printf("NIC %d: DHCP could not acquire address", nif.nicid)
		return
	}
	log.Printf("NIC %d: DHCP acquired IP %s for %s", nif.nicid, newAddr, config.LeaseLength)
	log.Printf("NIC %d: DNS servers: %v", nif.nicid, config.DNS)

	// Update default route with new gateway.
	nif.ns.mu.Lock()
	nif.routes = defaultRouteTable(nif.nicid, config.Gateway)
	nif.netmask = config.SubnetMask
	nif.addr = newAddr
	nif.dnsServers = config.DNS
	nif.ns.mu.Unlock()

	nif.ns.stack.SetRouteTable(nif.ns.flattenRouteTables())
	nif.ns.dispatcher.dnsClient.SetRuntimeServers(nif.ns.flattenDNSServers())
}

func (nif *netif) stateChange(s eth.State) {
	if s != eth.StateStopped {
		return
	}
	log.Printf("NIC %d: stopped", nif.nicid)
	if nif.cancel != nil {
		nif.cancel()
	}

	// TODO(crawshaw): more cleanup to be done here:
	//	- remove addresses
	// 	- remove link endpoint
	//	- reclaim NICID?

	nif.ns.mu.Lock()
	nif.routes = nil
	nif.dnsServers = nil
	nif.ns.mu.Unlock()

	nif.ns.stack.SetRouteTable(nif.ns.flattenRouteTables())
	nif.ns.dispatcher.dnsClient.SetRuntimeServers(nif.ns.flattenDNSServers())
}

type byRoutability []*netif

func (l byRoutability) Len() int      { return len(l) }
func (l byRoutability) Swap(i, j int) { l[i], l[j] = l[j], l[i] }
func (l byRoutability) Less(i, j int) bool {
	if hasGateway(l[i]) && !hasGateway(l[j]) {
		return true
	}
	if l[i].addr != "" && l[j].addr == "" {
		return true
	}
	return l[i].nicid < l[j].nicid
}

func hasGateway(netif *netif) bool {
	for _, r := range netif.routes {
		if r.Gateway != "" {
			return true
		}
	}
	return false
}

func (ns *netstack) flattenRouteTables() []tcpip.Route {
	ns.mu.Lock()
	defer ns.mu.Unlock()

	netifs := make([]*netif, 0, len(ns.netifs))
	for _, netif := range ns.netifs {
		netifs = append(netifs, netif)
	}
	sort.Sort(byRoutability(netifs))

	routeTable := []tcpip.Route{}
	for _, netif := range netifs {
		routeTable = append(routeTable, netif.routes...)
	}
	return routeTable
}

func (ns *netstack) flattenDNSServers() []tcpip.Address {
	ns.mu.Lock()
	defer ns.mu.Unlock()

	uniqServers := make(map[tcpip.Address]struct{})
	for _, netif := range ns.netifs {
		for _, server := range netif.dnsServers {
			uniqServers[server] = struct{}{}
		}
	}
	servers := []tcpip.Address{}
	for server := range uniqServers {
		servers = append(servers, server)
	}
	return servers
}

func (ns *netstack) addLoopback() error {
	const nicid = 1
	ctx, cancel := context.WithCancel(context.Background())
	loopbackIf := &netif{
		ns:      ns,
		ctx:     ctx,
		cancel:  cancel,
		nicid:   nicid,
		addr:    header.IPv4Loopback,
		netmask: tcpip.AddressMask(strings.Repeat("\xff", 4)),
		routes: []tcpip.Route{
			{
				Destination: header.IPv4Loopback,
				Mask:        tcpip.Address(strings.Repeat("\xff", 4)),
				NIC:         nicid,
			},
			{
				Destination: header.IPv6Loopback,
				Mask:        tcpip.Address(strings.Repeat("\xff", 16)),
				NIC:         nicid,
			},
		},
	}

	ns.mu.Lock()
	if len(ns.netifs) > 0 {
		ns.mu.Unlock()
		return fmt.Errorf("loopback: other interfaces already registered")
	}
	ns.netifs[nicid] = loopbackIf
	ns.mu.Unlock()

	loopbackID := loopback.New()
	if debug2 {
		loopbackID = sniffer.New(loopbackID)
	}
	if err := ns.stack.CreateNIC(nicid, loopbackID); err != nil {
		return fmt.Errorf("loopback: could not create interface: %v", err)
	}
	if err := ns.stack.AddAddress(nicid, ipv4.ProtocolNumber, header.IPv4Loopback); err != nil {
		return fmt.Errorf("loopback: adding ipv4 address failed: %v", err)
	}
	if err := ns.stack.AddAddress(nicid, ipv6.ProtocolNumber, header.IPv6Loopback); err != nil {
		return fmt.Errorf("loopback: adding ipv6 address failed: %v", err)
	}

	ns.stack.SetRouteTable(ns.flattenRouteTables())

	return nil
}

func (ns *netstack) addEth(path string) error {
	ctx, cancel := context.WithCancel(context.Background())

	nif := &netif{
		ns:     ns,
		ctx:    ctx,
		cancel: cancel,
	}

	client, err := eth.NewClient("netstack", path, ns.arena, nif.stateChange)
	if err != nil {
		return err
	}
	nif.eth = client
	ep := newLinkEndpoint(client)
	if err := ep.init(); err != nil {
		log.Fatalf("%s: endpoint init failed: %v", path, err)
	}
	linkID := stack.RegisterLinkEndpoint(ep)
	lladdr := ipv6.LinkLocalAddr(tcpip.LinkAddress(ep.linkAddr))

	ns.mu.Lock()
	var nicid tcpip.NICID
	for _, netif := range ns.netifs {
		if netif.nicid > nicid {
			nicid = netif.nicid
		}
	}
	nicid++
	if err := ns.stack.CreateNIC(nicid, linkID); err != nil {
		ns.mu.Unlock()
		return fmt.Errorf("NIC %d: could not create NIC for %q: %v", nicid, path, err)
	}
	if nicid == 2 && ns.nodename == "" {
		// This is the first real ethernet device on this host.
		// No nodename has been configured for the network stack,
		// so derive it from the MAC address.
		var mac [6]byte
		copy(mac[:], ep.linkAddr)
		ns.nodename = deviceid.DeviceID(mac)
	}
	nif.nicid = nicid
	nif.routes = defaultRouteTable(nicid, "")
	ns.netifs[nicid] = nif
	ns.mu.Unlock()

	log.Printf("NIC %d added using ethernet device %q", nicid, path)

	if debug2 {
		linkID = sniffer.New(linkID)
	}

	log.Printf("NIC %d: ipv6addr: %v", nicid, lladdr)

	if err := ns.stack.AddAddress(nicid, arp.ProtocolNumber, arp.ProtocolAddress); err != nil {
		return fmt.Errorf("NIC %d: adding arp address failed: %v", nicid, err)
	}
	if err := ns.stack.AddAddress(nicid, ipv6.ProtocolNumber, lladdr); err != nil {
		return fmt.Errorf("NIC %d: adding link-local IPv6 failed: %v", nicid, err)
	}
	snaddr := ipv6.SolicitedNodeAddr(lladdr)
	if err := ns.stack.AddAddress(nicid, ipv6.ProtocolNumber, snaddr); err != nil {
		return fmt.Errorf("NIC %d: adding solicited-node IPv6 failed: %v", nicid, err)
	}

	nif.dhcp = dhcp.NewClient(ns.stack, nicid, ep.linkAddr, nif.dhcpAcquired)

	// Add default route. This will get clobbered later when we get a DHCP response.
	ns.stack.SetRouteTable(ns.flattenRouteTables())

	go nif.dhcp.Run(nif.ctx)

	return nil
}
