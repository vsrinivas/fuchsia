// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package netstack

import (
	"context"
	"flag"
	"fmt"
	"log"
	"os"
	"reflect"
	"runtime"
	"sync"
	"sync/atomic"
	"syscall/zx"
	"syscall/zx/fidl"

	appcontext "app/context"
	"syslog"

	"netstack/connectivity"
	"netstack/dns"
	"netstack/filter"
	"netstack/link/eth"
	networking_metrics "networking_metrics_golib"

	"fidl/fuchsia/cobalt"
	"fidl/fuchsia/device"
	inspect "fidl/fuchsia/inspect/deprecated"
	"fidl/fuchsia/net"
	"fidl/fuchsia/net/stack"
	"fidl/fuchsia/netstack"
	"fidl/fuchsia/posix/socket"

	"gvisor.dev/gvisor/pkg/tcpip"
	"gvisor.dev/gvisor/pkg/tcpip/link/sniffer"
	"gvisor.dev/gvisor/pkg/tcpip/network/arp"
	"gvisor.dev/gvisor/pkg/tcpip/network/ipv4"
	"gvisor.dev/gvisor/pkg/tcpip/network/ipv6"
	tcpipstack "gvisor.dev/gvisor/pkg/tcpip/stack"
	"gvisor.dev/gvisor/pkg/tcpip/transport/icmp"
	"gvisor.dev/gvisor/pkg/tcpip/transport/tcp"
	"gvisor.dev/gvisor/pkg/tcpip/transport/udp"
)

type bindingSetCounterStat struct {
	bindingSet *fidl.BindingSet
}

func (s *bindingSetCounterStat) Value() uint64 {
	return uint64(s.bindingSet.Size())
}

func Main() {
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	logLevel := syslog.InfoLevel

	flags := flag.NewFlagSet(os.Args[0], flag.ContinueOnError)
	sniff := flags.Bool("sniff", false, "Enable the sniffer")
	flags.Var(&logLevel, "verbosity", "Set the logging verbosity")
	if err := flags.Parse(os.Args[1:]); err != nil {
		panic(err)
	}

	if *sniff {
		atomic.StoreUint32(&sniffer.LogPackets, 1)
	} else {
		atomic.StoreUint32(&sniffer.LogPackets, 0)
	}

	appCtx := appcontext.CreateFromStartupInfo()

	l, err := syslog.NewLogger(syslog.LogInitOptions{
		LogLevel:                      logLevel,
		MinSeverityForFileAndLineInfo: syslog.InfoLevel,
		Tags:                          []string{"netstack"},
	})
	if err != nil {
		panic(err)
	}
	syslog.SetDefaultLogger(l)
	log.SetOutput(&syslog.Writer{Logger: l})
	log.SetFlags(log.Lshortfile)

	ndpDisp := newNDPDispatcher()

	stk := tcpipstack.New(tcpipstack.Options{
		NetworkProtocols: []tcpipstack.NetworkProtocol{
			arp.NewProtocol(),
			ipv4.NewProtocol(),
			ipv6.NewProtocol(),
		},
		TransportProtocols: []tcpipstack.TransportProtocol{
			icmp.NewProtocol4(),
			icmp.NewProtocol6(),
			tcp.NewProtocol(),
			udp.NewProtocol(),
		},
		HandleLocal: true,
		NDPConfigs: tcpipstack.NDPConfigurations{
			HandleRAs:              true,
			DiscoverDefaultRouters: true,
			DiscoverOnLinkPrefixes: true,
			AutoGenGlobalAddresses: true,
		},
		NDPDisp:              ndpDisp,
		AutoGenIPv6LinkLocal: true,
		// Raw sockets are typically used for implementing custom protocols. We intend
		// to support custom protocols through structured FIDL APIs in the future, so
		// disable raw sockets to prevent them from accidentally becoming load-bearing.
		RawFactory: nil,
	})
	if err := stk.SetTransportProtocolOption(tcp.ProtocolNumber, tcp.SACKEnabled(true)); err != nil {
		syslog.Fatalf("method SetTransportProtocolOption(%v, tcp.SACKEnabled(true)) failed: %v", tcp.ProtocolNumber, err)
	}

	arena, err := eth.NewArena()
	if err != nil {
		syslog.Fatalf("ethernet: %s", err)
	}

	req, np, err := device.NewNameProviderInterfaceRequest()
	if err != nil {
		syslog.Fatalf("could not connect to device name provider service: %s", err)
	}
	appCtx.ConnectToEnvService(req)

	ns := &Netstack{
		arena:        arena,
		dnsClient:    dns.NewClient(stk),
		nameProvider: np,
	}
	ns.mu.ifStates = make(map[tcpip.NICID]*ifState)
	ns.mu.stack = stk
	ndpDisp.ns = ns
	ndpDisp.start(ctx)

	if err := ns.addLoopback(); err != nil {
		syslog.Fatalf("loopback: %s", err)
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
					syslog.Warnf("OnInterfacesChanged failed: %v", err)
				}
			}
		}
	}

	var posixSocketProviderService socket.ProviderService

	socketNotifications := make(chan struct{}, 1)
	socketProviderImpl := providerImpl{ns: ns, metadata: socketMetadata{endpoints: &ns.endpoints, newSocketNotifications: socketNotifications}}
	ns.stats = stats{
		Stats:            stk.Stats(),
		SocketCount:      bindingSetCounterStat{bindingSet: &socketProviderImpl.controlService.BindingSet},
		SocketsCreated:   &socketProviderImpl.metadata.socketsCreated,
		SocketsDestroyed: &socketProviderImpl.metadata.socketsDestroyed,
	}

	var inspectService inspect.InspectService
	appCtx.OutgoingService.AddDiagnostics("counters", &appcontext.DirectoryWrapper{
		Directory: &inspectDirectory{
			asService: (&inspectImpl{
				inner: &statCounterInspectImpl{
					name:  "Networking Stat Counters",
					value: reflect.ValueOf(ns.stats),
				},
				service: &inspectService,
			}).asService,
		},
	})
	appCtx.OutgoingService.AddDiagnostics("interfaces", &appcontext.DirectoryWrapper{
		Directory: &inspectDirectory{
			// asService is late-bound so that each call retrieves fresh NIC info.
			asService: func() *appcontext.Service {
				return (&inspectImpl{
					inner:   &nicInfoMapInspectImpl{value: ns.getIfStateInfo(stk.NICInfo())},
					service: &inspectService,
				}).asService()
			},
		},
	})

	appCtx.OutgoingService.AddDiagnostics("sockets", &appcontext.DirectoryWrapper{
		Directory: &inspectDirectory{
			asService: (&inspectImpl{
				inner: &socketInfoMapInspectImpl{
					value: &ns.endpoints,
				},
				service: &inspectService,
			}).asService,
		},
	})

	appCtx.OutgoingService.AddService(
		netstack.NetstackName,
		&netstack.NetstackStub{Impl: &netstackImpl{
			ns: ns,
		}},
		func(s fidl.Stub, c zx.Channel) error {
			k, err := netstackService.BindingSet.Add(s, c, nil)
			if err != nil {
				syslog.Fatalf("%v", err)
			}
			// Send a synthetic InterfacesChanged event to each client when they join
			// Prevents clients from having to race GetInterfaces / InterfacesChanged.
			if p, ok := netstackService.EventProxyFor(k); ok {
				ns.mu.Lock()
				interfaces2 := ns.getNetInterfaces2Locked()
				interfaces := interfaces2ListToInterfacesList(interfaces2)
				ns.mu.Unlock()

				if err := p.OnInterfacesChanged(interfaces); err != nil {
					syslog.Warnf("OnInterfacesChanged failed: %v", err)
				}
			}
			return nil
		},
	)

	var dnsService netstack.ResolverAdminService
	appCtx.OutgoingService.AddService(
		netstack.ResolverAdminName,
		&netstack.ResolverAdminStub{Impl: &dnsImpl{ns: ns}},
		func(s fidl.Stub, c zx.Channel) error {
			_, err := dnsService.BindingSet.Add(s, c, nil)
			return err
		},
	)

	var stackService stack.StackService
	appCtx.OutgoingService.AddService(
		stack.StackName,
		&stack.StackStub{Impl: &stackImpl{
			ns: ns,
		}},
		func(s fidl.Stub, c zx.Channel) error {
			_, err := stackService.BindingSet.Add(s, c, nil)
			return err
		},
	)

	var logService stack.LogService
	appCtx.OutgoingService.AddService(
		stack.LogName,
		&stack.LogStub{Impl: &logImpl{logger: l}},
		func(s fidl.Stub, c zx.Channel) error {
			_, err := logService.BindingSet.Add(s, c, nil)
			return err
		})

	var nameLookupService net.NameLookupService
	appCtx.OutgoingService.AddService(
		net.NameLookupName,
		&net.NameLookupStub{Impl: &nameLookupImpl{dnsClient: ns.dnsClient}},
		func(s fidl.Stub, c zx.Channel) error {
			_, err := nameLookupService.BindingSet.Add(s, c, nil)
			return err
		},
	)

	appCtx.OutgoingService.AddService(
		socket.ProviderName,
		&socket.ProviderStub{Impl: &socketProviderImpl},
		func(s fidl.Stub, c zx.Channel) error {
			_, err := posixSocketProviderService.BindingSet.Add(s, c, nil)
			return err
		},
	)

	if cobaltLogger, err := connectCobaltLogger(appCtx); err != nil {
		syslog.Warnf("could not initialize cobalt client: %s", err)
	} else {
		go func() {
			if err := runCobaltClient(ctx, cobaltLogger, &ns.stats, ns.mu.stack, socketNotifications); err != nil {
				syslog.Errorf("cobalt client exited unexpectedly: %s", err)
			}
		}()
	}

	if err := connectivity.AddOutgoingService(appCtx); err != nil {
		syslog.Fatalf("%v", err)
	}

	f := filter.New(stk.PortManager)
	if err := filter.AddOutgoingService(appCtx, f); err != nil {
		syslog.Fatalf("%v", err)
	}
	ns.filter = f

	go pprofListen()

	var wg sync.WaitGroup
	for i := 0; i < runtime.NumCPU(); i++ {
		wg.Add(1)
		go func() {
			fidl.Serve()
			wg.Done()
		}()
	}
	wg.Wait()
}

func connectCobaltLogger(ctx *appcontext.Context) (*cobalt.LoggerInterface, error) {
	freq, cobaltLoggerFactory, err := cobalt.NewLoggerFactoryInterfaceRequest()
	if err != nil {
		return nil, fmt.Errorf("could not connect to cobalt logger factory service: %s", err)
	}
	ctx.ConnectToEnvService(freq)
	lreq, cobaltLogger, err := cobalt.NewLoggerInterfaceRequest()
	if err != nil {
		return nil, fmt.Errorf("could not connect to cobalt logger service: %s", err)
	}
	result, err := cobaltLoggerFactory.CreateLoggerFromProjectId(networking_metrics.ProjectId, lreq)
	if err != nil {
		return nil, fmt.Errorf("CreateLoggerFromProjectId(%d, ...) = _, %s", networking_metrics.ProjectId, err)
	}
	if result != cobalt.StatusOk {
		return nil, fmt.Errorf("could not create logger for project %s: result: %s", networking_metrics.ProjectName, result)
	}

	return cobaltLogger, nil
}
