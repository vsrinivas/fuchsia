// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"fmt"
	"log"
	"strings"
	"sync"

	"apps/netstack/eth"
	"apps/netstack/watcher"

	"github.com/google/netstack/dhcp"
	"github.com/google/netstack/tcpip"
	"github.com/google/netstack/tcpip/header"
	"github.com/google/netstack/tcpip/link/loopback"
	"github.com/google/netstack/tcpip/link/sniffer"
	"github.com/google/netstack/tcpip/network/arp"
	"github.com/google/netstack/tcpip/network/ipv4"
	"github.com/google/netstack/tcpip/network/ipv6"
	"github.com/google/netstack/tcpip/stack"
	"github.com/google/netstack/tcpip/transport/tcp"
	"github.com/google/netstack/tcpip/transport/udp"
)

var dhcpClient *dhcp.Client
var routeTables = map[tcpip.NICID][]tcpip.Route{}
var routeTablesMu sync.Mutex

func main() {
	log.SetFlags(0)
	log.SetPrefix("netstack2: ")
	log.Print("started")

	stk := stack.New([]string{
		ipv4.ProtocolName,
		ipv4.PingProtocolName,
		ipv6.ProtocolName,
		arp.ProtocolName,
	}, []string{
		tcp.ProtocolName,
		udp.ProtocolName,
	}).(*stack.Stack)
	s, err := socketDispatcher(stk)
	if err != nil {
		log.Fatal(err)
	}
	log.Print("socket dispatcher started")

	arena, err := eth.NewArena()
	if err != nil {
		log.Fatalf("ethernet: %v", err)
	}
	const ethdir = "/dev/class/ethernet"
	w, err := watcher.NewWatcher(ethdir)
	if err != nil {
		log.Fatalf("ethernet: %v", err)
	}

	log.Printf("watching for ethernet devices")
	nicid := tcpip.NICID(1)
	loopbackID := loopback.New()
	if debug2 {
		loopbackID = sniffer.New(loopbackID)
	}
	if err := stk.CreateNIC(nicid, loopbackID); err != nil {
		log.Printf("could not create loopback interface: %v", err)
	}
	if err := stk.AddAddress(nicid, ipv4.ProtocolNumber, header.IPv4Loopback); err != nil {
		log.Printf("AddAddress for localhost failed: %v", err)
	}
	if err := stk.AddAddress(nicid, ipv6.ProtocolNumber, header.IPv6Loopback); err != nil {
		log.Printf("AddAddress for localhost ipv6 failed: %v", err)
	}
	routeTablesMu.Lock()
	routeTables[nicid] = []tcpip.Route{
		{
			Destination: header.IPv4Loopback,
			Mask:        tcpip.Address(strings.Repeat("\xff", 4)),
			NIC:         1,
		},
		{
			Destination: header.IPv6Loopback,
			Mask:        tcpip.Address(strings.Repeat("\xff", 16)),
			NIC:         1,
		},
	}
	stk.SetRouteTable(flattenRouteTables())
	routeTablesMu.Unlock()

	for name := range w.C {
		nicid++
		path := ethdir + "/" + name
		if err := addEth(stk, s, nicid, path, arena); err != nil {
			log.Printf("failed to add ethernet device %s: %v", path, err)
		}
	}
}

func addEth(stk *stack.Stack, s *socketServer, nicid tcpip.NICID, path string, arena *eth.Arena) error {
	log.Printf("using ethernet device %q as NIC %d", path, nicid)

	client, err := eth.NewClient(path, arena)
	if err != nil {
		return err
	}
	ep := newLinkEndpoint(client)
	if err := ep.init(); err != nil {
		log.Fatalf("init failed: %v", err)
	}
	linkID := stack.RegisterLinkEndpoint(ep)
	lladdr := ipv6.LinkLocalAddr(tcpip.LinkAddress(ep.linkAddr))
	log.Printf("ipv6addr: %v", lladdr)

	if debug2 {
		linkID = sniffer.New(linkID)
	}
	if err := stk.CreateNIC(nicid, linkID); err != nil {
		return fmt.Errorf("CreateNIC: %v", err)
	}
	if err := stk.AddAddress(nicid, arp.ProtocolNumber, arp.ProtocolAddress); err != nil {
		return fmt.Errorf("AddAddress for arp failed: %v", err)
	}
	if err := stk.AddAddress(nicid, ipv6.ProtocolNumber, lladdr); err != nil {
		return fmt.Errorf("AddAddress for link-local IPv6: %v", err)
	}
	snaddr := ipv6.SolicitedNodeAddr(lladdr)
	if err := stk.AddAddress(nicid, ipv6.ProtocolNumber, snaddr); err != nil {
		return fmt.Errorf("AddAddress for solicited-node IPv6: %v", err)
	}

	// Add default route. This will get clobbered later when we get a DHCP response.
	routeTablesMu.Lock()
	routeTables[nicid] = defaultRouteTable(nicid, "")
	stk.SetRouteTable(flattenRouteTables())
	routeTablesMu.Unlock()

	dhcpAcquired := func(oldAddr, newAddr tcpip.Address, config dhcp.Config) {
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
		log.Printf("DHCP acquired IP %s on NIC %d for %s", newAddr, nicid, config.LeaseLength)

		// Update default route with new gateway.
		routeTablesMu.Lock()
		routeTables[nicid] = defaultRouteTable(nicid, config.Gateway)
		stk.SetRouteTable(flattenRouteTables())
		routeTablesMu.Unlock()

		if newAddr != "" {
			s.setAddr(nicid, newAddr)
		}
	}
	dhcpClient = dhcp.NewClient(stk, nicid, ep.linkAddr, dhcpAcquired)
	go dhcpClient.Start()

	return nil
}

func defaultRouteTable(nicid tcpip.NICID, gateway tcpip.Address) []tcpip.Route {
	return []tcpip.Route{
		{
			Destination: tcpip.Address(strings.Repeat("\x00", 4)),
			Mask:        tcpip.Address(strings.Repeat("\x00", 4)),
			Gateway:     gateway,
			NIC:         nicid,
		},
		{
			Destination: tcpip.Address(strings.Repeat("\x00", 16)),
			Mask:        tcpip.Address(strings.Repeat("\x00", 16)),
			NIC:         nicid,
		},
	}
}

func flattenRouteTables() []tcpip.Route {
	routeTable := []tcpip.Route{}
	for _, table := range routeTables {
		routeTable = append(routeTable, table...)
	}
	return routeTable
}
