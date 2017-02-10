// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"log"
	"strings"
	"syscall/mx"
	"syscall/mx/mxruntime"

	"github.com/google/netstack/dhcp"
	"github.com/google/netstack/tcpip"
	"github.com/google/netstack/tcpip/network/arp"
	"github.com/google/netstack/tcpip/network/ipv4"
	"github.com/google/netstack/tcpip/network/ipv6"
	"github.com/google/netstack/tcpip/stack"
	"github.com/google/netstack/tcpip/transport/tcp"
	"github.com/google/netstack/tcpip/transport/udp"
)

var dhcpClient *dhcp.Client

func main() {
	log.SetFlags(0)
	log.SetPrefix("netstack2: ")
	log.Print("started")

	h := mxruntime.GetStartupHandle(mxruntime.HandleInfo{Type: mxruntime.HandleUser0, Arg: 0})
	if h == 0 {
		log.Fatal("invalid startup handle")
	}
	log.Print("sending ipc signal")
	if err := h.SignalPeer(0, mx.SignalUser0); err != nil {
		log.Fatalf("could not send ipc signal: %v", err)
	}

	ep := newLinkEndpoint(&mx.Channel{h})
	if err := ep.init(); err != nil {
		log.Fatalf("init failed: %v", err)
	}
	linkID := stack.RegisterLinkEndpoint(ep)
	lladdr := ipv6LinkLocalAddr(tcpip.LinkAddress(ep.linkAddr))

	stk := stack.New([]string{ipv4.ProtocolName, ipv4.PingProtocolName, ipv6.ProtocolName, arp.ProtocolName}, []string{tcp.ProtocolName, udp.ProtocolName}).(*stack.Stack)
	if err := stk.CreateNIC(1, linkID); err != nil {
		log.Fatalf("CreateNIC: %v", err)
	}
	if err := stk.AddAddress(1, arp.ProtocolNumber, arp.ProtocolAddress); err != nil {
		log.Fatalf("AddAddress for arp failed: %v", err)
	}
	if err := stk.AddAddress(1, ipv6.ProtocolNumber, lladdr); err != nil {
		log.Fatalf("AddAddress for link-local IPv6: %v", err)
	}

	// Add default route. This will get clobbered later when we get a DHCP response.
	stk.SetRouteTable(defaultRouteTable(""))

	if err := stk.AddAddress(1, ipv4.ProtocolNumber, "\xff\xff\xff\xff"); err != nil {
		log.Fatal(err)
	}
	if err := stk.AddAddress(1, ipv4.ProtocolNumber, "\x00\x00\x00\x00"); err != nil {
		log.Fatal(err)
	}

	dhcpClient = dhcp.NewClient(stk, 1, ep.linkAddr)
	go dhcpClient.Start(func(config dhcp.Config) {
		// Update default route with new gateway.
		stk.SetRouteTable(defaultRouteTable(config.Gateway))
		stk.RemoveAddress(1, "\xff\xff\xff\xff")
		stk.RemoveAddress(1, "\x00\x00\x00\x00")
	})

	_, err := socketDispatcher(stk)
	if err != nil {
		log.Fatal(err)
	}
	log.Print("socket dispatcher started")
	select {}
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

func defaultRouteTable(gateway tcpip.Address) []tcpip.Route {
	return []tcpip.Route{
		{
			Destination: tcpip.Address(strings.Repeat("\x00", 4)),
			Mask:        tcpip.Address(strings.Repeat("\x00", 4)),
			Gateway:     gateway,
			NIC:         1,
		},
		{
			Destination: tcpip.Address(strings.Repeat("\x00", 16)),
			Mask:        tcpip.Address(strings.Repeat("\x00", 16)),
			Gateway:     gateway,
			NIC:         1,
		},
	}
}
