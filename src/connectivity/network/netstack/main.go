// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package netstack

import (
	"bytes"
	"flag"
	"fmt"
	"log"
	"reflect"
	"runtime"
	"syscall/zx"
	"syscall/zx/fidl"

	"app/context"
	"syslog/logger"

	"netstack/connectivity"
	"netstack/dns"
	"netstack/filter"
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
	"github.com/google/netstack/tcpip/transport/icmp"
	"github.com/google/netstack/tcpip/transport/tcp"
	"github.com/google/netstack/tcpip/transport/udp"
)

type logWriter struct{}

func (*logWriter) Write(data []byte) (n int, err error) {
	origLen := len(data)

	// Strip out the trailing newline the `log` library adds because the
	// logging service also adds a trailing newline.
	data = bytes.TrimSuffix(data, []byte("\n"))

	if err := logger.VLogf(logger.TraceVerbosity, "%s", data); err != nil {
		return 0, err
	}

	return origLen, nil
}

func Main() {
	flag.Parse()

	ctx := context.CreateFromStartupInfo()

	options := logger.LogInitOptions{
		Loglevel:  logger.InfoLevel,
		Connector: ctx.Connector(),
		Tags:      []string{"netstack"},
		MinSeverityForFileAndLineInfo: logger.InfoLevel,
	}
	if err := logger.InitDefaultLoggerWithConfig(options); err != nil {
		panic(fmt.Sprintf("netstack: failed to initialize syslog interface: %s", err))
	}

	log.SetOutput((*logWriter)(nil))
	log.SetFlags(log.Lshortfile)
	logger.Infof("started")

	stk := tcpipstack.New([]string{
		ipv4.ProtocolName,
		ipv6.ProtocolName,
		arp.ProtocolName,
	}, []string{
		icmp.ProtocolName4,
		tcp.ProtocolName,
		udp.ProtocolName,
	}, tcpipstack.Options{
		HandleLocal: true,
	})
	if err := stk.SetTransportProtocolOption(tcp.ProtocolNumber, tcp.SACKEnabled(true)); err != nil {
		logger.Fatalf("method SetTransportProtocolOption(%v, tcp.SACKEnabled(true)) failed: %v", tcp.ProtocolNumber, err)
	}

	arena, err := eth.NewArena()
	if err != nil {
		logger.Fatalf("ethernet: %s", err)
	}

	req, ds, err := devicesettings.NewDeviceSettingsManagerInterfaceRequest()
	if err != nil {
		logger.Fatalf("could not connect to device settings service: %s", err)
	}

	ctx.ConnectToEnvService(req)

	ns := &Netstack{
		arena:          arena,
		dnsClient:      dns.NewClient(stk),
		deviceSettings: ds,
	}
	ns.mu.ifStates = make(map[tcpip.NICID]*ifState)
	ns.mu.stack = stk

	if err := ns.addLoopback(); err != nil {
		logger.Fatalf("loopback: %s", err)
	}

	var netstackService netstack.NetstackService

	ns.OnInterfacesChanged = func(interfaces2 []netstack.NetInterface2) {
		connectivity.InferAndNotify(interfaces2)
		// TODO(NET-2078): Switch to the new NetInterface struct once Chromium stops
		// using netstack.fidl.
		interfaces := interfaces2ListToInterfacesList(interfaces2)
		for _, key := range netstackService.BindingKeys() {
			if p, ok := netstackService.EventProxyFor(key); ok {
				if err := p.OnInterfacesChanged(interfaces); err != nil {
					logger.Warnf("OnInterfacesChanged failed: %v", err)
				}
			}
		}
	}

	stats := reflect.ValueOf(stk.Stats())
	ctx.OutgoingService.AddObjects("counters", &context.DirectoryWrapper{
		Directory: &context.DirectoryWrapper{
			Directory: &statCounterInspectImpl{
				name:  "Networking Stat Counters",
				Value: stats,
			},
		},
	})

	netstackImpl := &netstackImpl{
		ns: ns,
		getIO: (&context.DirectoryWrapper{
			Directory: &reflectNode{
				Value: stats,
			},
		}).GetDirectory,
	}
	ctx.OutgoingService.AddService(netstack.NetstackName, func(c zx.Channel) error {
		k, err := netstackService.Add(netstackImpl, c, nil)
		if err != nil {
			logger.Fatalf("%v", err)
		}
		// Send a synthetic InterfacesChanged event to each client when they join
		// Prevents clients from having to race GetInterfaces / InterfacesChanged.
		if p, ok := netstackService.EventProxyFor(k); ok {
			ns.mu.Lock()
			interfaces2 := ns.getNetInterfaces2Locked()
			interfaces := interfaces2ListToInterfacesList(interfaces2)
			ns.mu.Unlock()

			if err := p.OnInterfacesChanged(interfaces); err != nil {
				logger.Warnf("OnInterfacesChanged failed: %v", err)
			}
		}
		return nil
	})

	var dnsService netstack.ResolverAdminService
	ctx.OutgoingService.AddService(netstack.ResolverAdminName, func(c zx.Channel) error {
		_, err := dnsService.Add(&dnsImpl{ns: ns}, c, nil)
		return err
	})

	var stackService stack.StackService
	ctx.OutgoingService.AddService(stack.StackName, func(c zx.Channel) error {
		_, err := stackService.Add(&stackImpl{ns: ns}, c, nil)
		return err
	})

	var socketProvider net.SocketProviderService
	ctx.OutgoingService.AddService(net.SocketProviderName, func(c zx.Channel) error {
		_, err := socketProvider.Add(&socketProviderImpl{ns: ns}, c, nil)
		return err
	})
	if err := connectivity.AddOutgoingService(ctx); err != nil {
		logger.Fatalf("%v", err)
	}

	f := filter.New(stk.PortManager)
	if err := filter.AddOutgoingService(ctx, f); err != nil {
		logger.Fatalf("%v", err)
	}
	ns.filter = f

	go pprofListen()

	for i := 1; i < runtime.NumCPU(); i++ {
		go fidl.Serve()
	}
	fidl.Serve()
}
