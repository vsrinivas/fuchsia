// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"log"
	"strings"

	"application/lib/app/context"

	"apps/netstack/eth"
	"apps/netstack/watcher"

	"github.com/google/netstack/tcpip"
	"github.com/google/netstack/tcpip/network/arp"
	"github.com/google/netstack/tcpip/network/ipv4"
	"github.com/google/netstack/tcpip/network/ipv6"
	"github.com/google/netstack/tcpip/stack"
	"github.com/google/netstack/tcpip/transport/tcp"
	"github.com/google/netstack/tcpip/transport/udp"
)

var ns *netstack

func main() {
	log.SetFlags(0)
	log.SetPrefix("netstack: ")
	log.Print("started")

	ctx := context.CreateFromStartupInfo()

	stk := stack.New([]string{
		ipv4.ProtocolName,
		ipv4.PingProtocolName,
		ipv6.ProtocolName,
		arp.ProtocolName,
	}, []string{
		tcp.ProtocolName,
		udp.ProtocolName,
	}).(*stack.Stack)
	s, err := socketDispatcher(stk, ctx)
	if err != nil {
		log.Fatal(err)
	}
	log.Print("socket dispatcher started")

	AddNetstackService(ctx)
	ctx.Serve()

	arena, err := eth.NewArena()
	if err != nil {
		log.Fatalf("ethernet: %v", err)
	}

	// TODO: plumb the magenta.nodename environment variable through
	// initialization, just as devmgr does to netsvc. Set it here
	// in the ns.nodename field.
	ns = &netstack{
		arena:      arena,
		stack:      stk,
		dispatcher: s,
		ifStates:   make(map[tcpip.NICID]*ifState),
	}
	if err := ns.addLoopback(); err != nil {
		log.Fatalf("loopback: %v", err)
	}

	s.setNetstack(ns)

	const ethdir = "/dev/class/ethernet"
	w, err := watcher.NewWatcher(ethdir)
	if err != nil {
		log.Fatalf("ethernet: %v", err)
	}
	log.Printf("watching for ethernet devices")
	for name := range w.C {
		path := ethdir + "/" + name
		if err := ns.addEth(path); err != nil {
			log.Printf("failed to add ethernet device %s: %v", path, err)
		}
	}
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
