// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"context"
	"fmt"
	"log"
	"strings"
	"sync"

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

	mu     sync.Mutex
	netifs map[tcpip.NICID]*netif
}

// A netif is a network interface.
type netif struct {
	ns     *netstack
	ctx    context.Context
	cancel context.CancelFunc
	nicid  tcpip.NICID
	eth    *eth.Client
	dhcp   *dhcp.Client

	routes []tcpip.Route // guarded by ns.mu
}

func (nif *netif) dhcpAcquired(oldAddr, newAddr tcpip.Address, config dhcp.Config) {
	if oldAddr != "" && oldAddr != newAddr {
		log.Printf("DHCP IP %s expired", oldAddr)
	}
	if config.Error != nil {
		log.Printf("%v", config.Error)
		return
	}
	if newAddr == "" {
		log.Printf("DHCP could not acquire address")
		return
	}
	log.Printf("DHCP acquired IP %s on NIC %d for %s", newAddr, nif.nicid, config.LeaseLength)

	// Update default route with new gateway.
	nif.ns.mu.Lock()
	nif.routes = defaultRouteTable(nif.nicid, config.Gateway)
	nif.ns.mu.Unlock()

	nif.ns.stack.SetRouteTable(nif.ns.flattenRouteTables())

	if newAddr != "" {
		nif.ns.dispatcher.setAddr(nif.nicid, newAddr)
	}
}

func (ns *netstack) flattenRouteTables() []tcpip.Route {
	ns.mu.Lock()
	defer ns.mu.Unlock()

	routeTable := []tcpip.Route{}
	for _, netif := range ns.netifs {
		routeTable = append(routeTable, netif.routes...)
	}
	return routeTable
}

func (ns *netstack) addLoopback() error {
	const nicid = 1
	ctx, cancel := context.WithCancel(context.Background())
	loopbackIf := &netif{
		ns:     ns,
		ctx:    ctx,
		cancel: cancel,
		nicid:  nicid,
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
		return fmt.Errorf("cannot register loopback interface after other interfaces")
	}
	ns.netifs[nicid] = loopbackIf
	ns.mu.Unlock()

	loopbackID := loopback.New()
	if debug2 {
		loopbackID = sniffer.New(loopbackID)
	}
	if err := ns.stack.CreateNIC(nicid, loopbackID); err != nil {
		return fmt.Errorf("could not create loopback interface: %v", err)
	}
	if err := ns.stack.AddAddress(nicid, ipv4.ProtocolNumber, header.IPv4Loopback); err != nil {
		return fmt.Errorf("AddAddress for localhost failed: %v", err)
	}
	if err := ns.stack.AddAddress(nicid, ipv6.ProtocolNumber, header.IPv6Loopback); err != nil {
		return fmt.Errorf("AddAddress for localhost ipv6 failed: %v", err)
	}

	ns.stack.SetRouteTable(ns.flattenRouteTables())

	return nil
}

func (ns *netstack) addEth(path string) error {
	client, err := eth.NewClient(path, ns.arena)
	if err != nil {
		return err
	}
	ep := newLinkEndpoint(client)
	if err := ep.init(); err != nil {
		log.Fatalf("%s: endpoint init failed: %v", path, err)
	}
	linkID := stack.RegisterLinkEndpoint(ep)
	lladdr := ipv6.LinkLocalAddr(tcpip.LinkAddress(ep.linkAddr))

	if debug2 {
		linkID = sniffer.New(linkID)
	}

	ctx, cancel := context.WithCancel(context.Background())

	ns.mu.Lock()
	var nicid tcpip.NICID
	for _, netif := range ns.netifs {
		if netif.nicid > nicid {
			nicid = netif.nicid
		}
	}
	nicid++ // NICID 0 is reserved to mean "any NIC"
	if err := ns.stack.CreateNIC(nicid, linkID); err != nil {
		ns.mu.Unlock()
		return fmt.Errorf("netstack: could not create new NIC: %v", err)
	}
	nif := &netif{
		ns:     ns,
		ctx:    ctx,
		cancel: cancel,
		nicid:  nicid,
		eth:    client,
		routes: defaultRouteTable(nicid, ""),
	}
	ns.netifs[nicid] = nif
	ns.mu.Unlock()

	log.Printf("using ethernet device %q as NIC %d", path, nicid)
	log.Printf("ipv6addr: %v", lladdr)

	if err := ns.stack.AddAddress(nicid, arp.ProtocolNumber, arp.ProtocolAddress); err != nil {
		return fmt.Errorf("AddAddress for arp failed: %v", err)
	}
	if err := ns.stack.AddAddress(nicid, ipv6.ProtocolNumber, lladdr); err != nil {
		return fmt.Errorf("AddAddress for link-local IPv6: %v", err)
	}
	snaddr := ipv6.SolicitedNodeAddr(lladdr)
	if err := ns.stack.AddAddress(nicid, ipv6.ProtocolNumber, snaddr); err != nil {
		return fmt.Errorf("AddAddress for solicited-node IPv6: %v", err)
	}

	nif.dhcp = dhcp.NewClient(ns.stack, nicid, ep.linkAddr, nif.dhcpAcquired)

	// Add default route. This will get clobbered later when we get a DHCP response.
	ns.stack.SetRouteTable(ns.flattenRouteTables())

	go nif.dhcp.Run(nif.ctx)

	return nil
}
