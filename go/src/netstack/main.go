// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"log"

	"app/context"

	"netstack/link/eth"
	"netstack/watcher"

	"fidl/bindings"
	"fidl/fuchsia/devicesettings"

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
		ipv6.ProtocolName,
		arp.ProtocolName,
	}, []string{
		ipv4.PingProtocolName,
		tcp.ProtocolName,
		udp.ProtocolName,
	})
	s, err := socketDispatcher(stk, ctx)
	if err != nil {
		log.Fatal(err)
	}
	log.Print("socket dispatcher started")

	if err := AddNetstackService(ctx); err != nil {
		log.Fatal(err)
	}
	if err := AddStackService(ctx); err != nil {
		log.Fatal(err)
	}

	arena, err := eth.NewArena()
	if err != nil {
		log.Fatalf("ethernet: %v", err)
	}

	req, ds, err := devicesettings.NewDeviceSettingsManagerInterfaceRequest()
	if err != nil {
		log.Printf("could not connect to device settings service: %s", err)
	}

	ctx.ConnectToEnvService(req)

	ns = &netstack{
		arena:          arena,
		stack:          stk,
		dispatcher:     s,
		deviceSettings: ds,
		ifStates:       make(map[tcpip.NICID]*ifState),
	}
	if err := ns.addLoopback(); err != nil {
		log.Fatalf("loopback: %v", err)
	}

	s.setNetstack(ns)

	// Serve FIDL bindings on two threads. Since the Go FIDL bindings are blocking,
	// this allows two outstanding requests at a time.
	// TODO(tkilbourn): revisit this and tune the number of serving threads.
	ctx.Serve()
	go bindings.Serve()
	go bindings.Serve()

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
