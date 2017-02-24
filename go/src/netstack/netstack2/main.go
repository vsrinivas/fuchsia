// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"fmt"
	"log"
	"strings"
	"sync"
	"syscall"

	"apps/netstack/eth"

	"github.com/google/netstack/dhcp"
	"github.com/google/netstack/tcpip"
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
	w, err := syscall.NewWatcher(ethdir)
	if err != nil {
		log.Fatalf("ethernet: %v", err)
	}
	log.Printf("watching for ethernet devices")
	var nicid tcpip.NICID
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
	lladdr := ipv6LinkLocalAddr(tcpip.LinkAddress(ep.linkAddr))

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

	if err := stk.AddAddress(nicid, ipv4.ProtocolNumber, "\xff\xff\xff\xff"); err != nil {
		return err
	}
	if err := stk.AddAddress(nicid, ipv4.ProtocolNumber, "\x00\x00\x00\x00"); err != nil {
		return err
	}

	// Add default route. This will get clobbered later when we get a DHCP response.
	routeTablesMu.Lock()
	routeTables[nicid] = defaultRouteTable(nicid, "")
	stk.SetRouteTable(flattenRouteTables())
	routeTablesMu.Unlock()

	dhcpClient = dhcp.NewClient(stk, nicid, ep.linkAddr)
	go dhcpClient.Start(func(config dhcp.Config) {
		// Update default route with new gateway.
		routeTablesMu.Lock()
		routeTables[nicid] = defaultRouteTable(nicid, config.Gateway)
		stk.SetRouteTable(flattenRouteTables())
		routeTablesMu.Unlock()

		stk.RemoveAddress(nicid, "\xff\xff\xff\xff")
		stk.RemoveAddress(nicid, "\x00\x00\x00\x00")
		s.setAddr(nicid, dhcpClient.Address())
	})
	return nil
}

func ipv6LinkLocalAddr(linkAddr tcpip.LinkAddress) tcpip.Address {
	// Convert a 48-bit MAC to an EUI-64 and then prepend the
	// link-local header, FE80::.
	//
	// The conversion is very nearly:
	//	aa:bb:cc:dd:ee:ff => FE80::Aabb:ccFF:FEdd:eeff
	// Note the capital A. The conversion aa->Aa involves a bit flip.
	lladdrb := [16]byte{
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
	}
	return tcpip.Address(lladdrb[:])
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
			Gateway:     gateway,
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
