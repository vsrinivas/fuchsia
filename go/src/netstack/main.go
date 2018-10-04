// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"flag"
	"log"
	"net/http"
	_ "net/http/pprof"
	"syscall/zx"
	"syscall/zx/fidl"

	"app/context"
	"netstack/connectivity"
	"netstack/dns"
	"netstack/link/eth"

	"fidl/fuchsia/devicesettings"
	"fidl/fuchsia/net"
	"fidl/fuchsia/net/stack"
	"fidl/fuchsia/netstack"

	"github.com/google/netstack/tcpip"
	"github.com/google/netstack/tcpip/network/arp"
	"github.com/google/netstack/tcpip/network/ipv4"
	"github.com/google/netstack/tcpip/network/ipv6"
	tcpipstack "github.com/google/netstack/tcpip/stack"
	"github.com/google/netstack/tcpip/transport/ping"
	"github.com/google/netstack/tcpip/transport/tcp"
	"github.com/google/netstack/tcpip/transport/udp"
)

var pprofServer = flag.Bool("pprof", false, "run the pprof http server")

var OnInterfacesChanged func()

func main() {
	flag.Parse()
	log.SetFlags(0)
	log.SetPrefix("netstack: ")
	log.Print("started")

	ctx := context.CreateFromStartupInfo()

	stk := tcpipstack.New([]string{
		ipv4.ProtocolName,
		ipv6.ProtocolName,
		arp.ProtocolName,
	}, []string{
		ping.ProtocolName4,
		tcp.ProtocolName,
		udp.ProtocolName,
	}, tcpipstack.Options{})
	if err := stk.SetTransportProtocolOption(tcp.ProtocolNumber, tcp.SACKEnabled(true)); err != nil {
		log.Fatalf("method SetTransportProtocolOption(%v, tcp.SACKEnabled(true)) failed: %v", tcp.ProtocolNumber, err)
	}

	s, err := newSocketServer(stk, ctx)
	if err != nil {
		log.Fatal(err)
	}
	log.Print("socket server started")

	arena, err := eth.NewArena()
	if err != nil {
		log.Fatalf("ethernet: %s", err)
	}

	req, ds, err := devicesettings.NewDeviceSettingsManagerInterfaceRequest()
	if err != nil {
		log.Fatalf("could not connect to device settings service: %s", err)
	}

	ctx.ConnectToEnvService(req)

	ns := &Netstack{
		arena:          arena,
		socketServer:   s,
		dnsClient:      dns.NewClient(stk),
		deviceSettings: ds,
		ifStates:       make(map[tcpip.NICID]*ifState),
	}
	ns.mu.stack = stk

	if err := ns.addLoopback(); err != nil {
		log.Fatalf("loopback: %s", err)
	}

	s.setNetstack(ns)

	// TODO(NET-1263): register resolver admin service once clients don't crash netstack
	// var dnsService netstack.ResolverAdminService
	var netstackService netstack.NetstackService

	OnInterfacesChanged = func() {
		interfaces := getInterfaces(ns)
		connectivity.InferAndNotify(interfaces)
		for key := range netstackService.Bindings {
			if p, ok := netstackService.EventProxyFor(key); ok {
				if err := p.OnInterfacesChanged(interfaces); err != nil {
					log.Printf("OnInterfacesChanged failed: %v", err)
				}
			}
		}
	}

	ctx.OutgoingService.AddService(netstack.NetstackName, func(c zx.Channel) error {
		k, err := netstackService.Add(&netstackImpl{
			ns: ns,
		}, c, nil)
		if err != nil {
			log.Fatal(err)
		}
		// Send a synthetic InterfacesChanged event to each client when they join
		// Prevents clients from having to race GetInterfaces / InterfacesChanged.
		if p, ok := netstackService.EventProxyFor(k); ok {
			if err := p.OnInterfacesChanged(getInterfaces(ns)); err != nil {
				log.Printf("OnInterfacesChanged failed: %v", err)
			}
		}
		return nil
	})

	// TODO(NET-1263): register resolver admin service once clients don't crash netstack
	// when registering.
	// ctx.OutgoingService.AddService(netstack.ResolverAdminName, func(c zx.Channel) error {
	//   _, err := dnsService.Add(&dnsImpl{ns: ns}, c, nil)
	//   return err
	// })

	var stackService stack.StackService
	ctx.OutgoingService.AddService(stack.StackName, func(c zx.Channel) error {
		_, err := stackService.Add(&stackImpl{ns: ns}, c, nil)
		return err
	})

	var socketProvider net.LegacySocketProviderService
	ctx.OutgoingService.AddService(net.LegacySocketProviderName, func(c zx.Channel) error {
		_, err := socketProvider.Add(&socketProviderImpl{ns: ns}, c, nil)
		return err
	})
	if err := connectivity.AddOutgoingService(ctx); err != nil {
		log.Fatal(err)
	}

	// Serve FIDL bindings on two threads. Since the Go FIDL bindings are blocking,
	// this allows two outstanding requests at a time.
	// TODO(tkilbourn): revisit this and tune the number of serving threads.
	ctx.Serve()
	go fidl.Serve()
	go fidl.Serve()

	if *pprofServer {
		go func() {
			log.Println("starting http pprof server on 0.0.0.0:6060")
			log.Println(http.ListenAndServe("0.0.0.0:6060", nil))
		}()
	}

	<-(chan struct{})(nil)
}
