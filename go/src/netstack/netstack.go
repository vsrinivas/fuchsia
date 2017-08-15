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
	"apps/netstack/netiface"

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
	ifStates map[tcpip.NICID]*ifState
}

// Each ifState tracks the state of a network interface.
type ifState struct {
	ns     *netstack
	ctx    context.Context
	cancel context.CancelFunc
	eth    *eth.Client
	dhcp   *dhcp.Client

	// guarded by ns.mu
	nic *netiface.NIC
}

func (ifs *ifState) dhcpAcquired(oldAddr, newAddr tcpip.Address, config dhcp.Config) {
	if oldAddr != "" && oldAddr != newAddr {
		log.Printf("NIC %d: DHCP IP %s expired", ifs.nic.ID, oldAddr)
	}
	if config.Error != nil {
		log.Printf("%v", config.Error)
		return
	}
	if newAddr == "" {
		log.Printf("NIC %d: DHCP could not acquire address", ifs.nic.ID)
		return
	}
	log.Printf("NIC %d: DHCP acquired IP %s for %s", ifs.nic.ID, newAddr, config.LeaseLength)
	log.Printf("NIC %d: DNS servers: %v", ifs.nic.ID, config.DNS)

	// Update default route with new gateway.
	ifs.ns.mu.Lock()
	ifs.nic.Routes = defaultRouteTable(ifs.nic.ID, config.Gateway)
	ifs.nic.Netmask = config.SubnetMask
	ifs.nic.Addr = newAddr
	ifs.nic.DNSServers = config.DNS
	ifs.ns.mu.Unlock()

	ifs.ns.stack.SetRouteTable(ifs.ns.flattenRouteTables())
	ifs.ns.dispatcher.dnsClient.SetRuntimeServers(ifs.ns.flattenDNSServers())
}

func (ifs *ifState) stateChange(s eth.State) {
	if s != eth.StateStopped {
		return
	}
	log.Printf("NIC %d: stopped", ifs.nic.ID)
	if ifs.cancel != nil {
		ifs.cancel()
	}

	// TODO(crawshaw): more cleanup to be done here:
	//	- remove addresses
	// 	- remove link endpoint
	//	- reclaim NICID?

	ifs.ns.mu.Lock()
	ifs.nic.Routes = nil
	ifs.nic.DNSServers = nil
	ifs.ns.mu.Unlock()

	ifs.ns.stack.SetRouteTable(ifs.ns.flattenRouteTables())
	ifs.ns.dispatcher.dnsClient.SetRuntimeServers(ifs.ns.flattenDNSServers())
}

func (ns *netstack) flattenRouteTables() []tcpip.Route {
	ns.mu.Lock()
	defer ns.mu.Unlock()

	ifss := make([]*ifState, 0, len(ns.ifStates))
	for _, ifs := range ns.ifStates {
		ifss = append(ifss, ifs)
	}
	sort.Slice(ifss, func(i, j int) bool {
		return netiface.Less(ifss[i].nic, ifss[j].nic)
	})
	if debug2 {
		for i, ifs := range ifss {
			log.Printf("[%v] nicid: %v, addr: %v, routes: %v",
				i, ifs.nic.ID, ifs.nic.Addr, ifs.nic.Routes)
		}
	}

	routeTable := []tcpip.Route{}
	for _, ifs := range ifss {
		routeTable = append(routeTable, ifs.nic.Routes...)
	}
	return routeTable
}

func (ns *netstack) flattenDNSServers() []tcpip.Address {
	ns.mu.Lock()
	defer ns.mu.Unlock()

	uniqServers := make(map[tcpip.Address]struct{})
	for _, ifs := range ns.ifStates {
		for _, server := range ifs.nic.DNSServers {
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
	nic := &netiface.NIC{
		ID:      nicid,
		Addr:    header.IPv4Loopback,
		Netmask: tcpip.AddressMask(strings.Repeat("\xff", 4)),
		Routes: []tcpip.Route{
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
	loopbackIf := &ifState{
		ns:     ns,
		ctx:    ctx,
		cancel: cancel,
		nic:    nic,
	}

	ns.mu.Lock()
	if len(ns.ifStates) > 0 {
		ns.mu.Unlock()
		return fmt.Errorf("loopback: other interfaces already registered")
	}
	ns.ifStates[nicid] = loopbackIf
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

	ifs := &ifState{
		ns:     ns,
		ctx:    ctx,
		cancel: cancel,
		nic:    &netiface.NIC{},
	}

	client, err := eth.NewClient("netstack", path, ns.arena, ifs.stateChange)
	if err != nil {
		return err
	}
	ifs.eth = client
	ep := newLinkEndpoint(client)
	if err := ep.init(); err != nil {
		log.Fatalf("%s: endpoint init failed: %v", path, err)
	}
	linkID := stack.RegisterLinkEndpoint(ep)
	lladdr := ipv6.LinkLocalAddr(tcpip.LinkAddress(ep.linkAddr))

	ns.mu.Lock()
	var nicid tcpip.NICID
	for _, ifs := range ns.ifStates {
		if ifs.nic.ID > nicid {
			nicid = ifs.nic.ID
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
	ifs.nic.ID = nicid
	ifs.nic.Routes = defaultRouteTable(nicid, "")
	ns.ifStates[nicid] = ifs
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

	ifs.dhcp = dhcp.NewClient(ns.stack, nicid, ep.linkAddr, ifs.dhcpAcquired)

	// Add default route. This will get clobbered later when we get a DHCP response.
	ns.stack.SetRouteTable(ns.flattenRouteTables())

	go ifs.dhcp.Run(ifs.ctx)

	return nil
}
