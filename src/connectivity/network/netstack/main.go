// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package netstack

import (
	"context"
	"crypto/rand"
	"encoding/base64"
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
	"fidl/fuchsia/stash"

	"gvisor.dev/gvisor/pkg/tcpip"
	"gvisor.dev/gvisor/pkg/tcpip/header"
	"gvisor.dev/gvisor/pkg/tcpip/link/sniffer"
	"gvisor.dev/gvisor/pkg/tcpip/network/arp"
	"gvisor.dev/gvisor/pkg/tcpip/network/ipv4"
	"gvisor.dev/gvisor/pkg/tcpip/network/ipv6"
	tcpipstack "gvisor.dev/gvisor/pkg/tcpip/stack"
	"gvisor.dev/gvisor/pkg/tcpip/transport/icmp"
	"gvisor.dev/gvisor/pkg/tcpip/transport/tcp"
	"gvisor.dev/gvisor/pkg/tcpip/transport/udp"
)

const (
	// stashStoreIdentificationName is the name used to identify this (netstack)
	// component to the fuchsia.stash.SecureStore service.
	stashStoreIdentificationName = "netstack-stash"

	// opaqueIIDSecretKeyName is the name of the key used to access the secret key
	// for opaque IIDs from the secure stash store.
	opaqueIIDSecretKeyName = "opaque-iid-secret-key"
)

type bindingSetCounterStat struct {
	bindingSets []*fidl.BindingSet
}

func (s *bindingSetCounterStat) Value() uint64 {
	var sum int
	for _, s := range s.bindingSets {
		sum += s.Size()
	}
	return uint64(sum)
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

	secretKeyForOpaqueIID, err := getSecretKeyForOpaqueIID(appCtx)
	if err != nil {
		panic(fmt.Sprintf("failed to get secret key for opaque IIDs: %s", err))
	}

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
		OpaqueIIDOpts: tcpipstack.OpaqueInterfaceIdentifierOptions{
			NICNameFromID: func(nicID tcpip.NICID, nicName string) string {
				// As of writing, Netstack creates NICs with names so we return the name
				// the NIC was created with. Just in case, we have a default NIC name
				// format for NICs that were not created with a name.
				if nicName != "" {
					return nicName
				}
				return fmt.Sprintf("opaqueIIDNIC%d", nicID)
			},
			SecretKey: secretKeyForOpaqueIID,
		},
	})
	if err := stk.SetTransportProtocolOption(tcp.ProtocolNumber, tcp.SACKEnabled(true)); err != nil {
		syslog.Fatalf("method SetTransportProtocolOption(%v, tcp.SACKEnabled(true)) failed: %v", tcp.ProtocolNumber, err)
	}
	if err := stk.SetTransportProtocolOption(tcp.ProtocolNumber, tcp.DelayEnabled(true)); err != nil {
		syslog.Fatalf("method SetTransportProtocolOption(%v, tcp.DelayEnabled(true)) failed: %v", tcp.ProtocolNumber, err)
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

	socketProviderImpl := providerImpl{ns: ns}
	ns.stats = stats{
		Stats: stk.Stats(),
		SocketCount: bindingSetCounterStat{bindingSets: []*fidl.BindingSet{
			&socketProviderImpl.controlService.BindingSet,
			&socketProviderImpl.datagramSocketService.BindingSet,
			&socketProviderImpl.streamSocketService.BindingSet,
		}},
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
			if err := runCobaltClient(ctx, cobaltLogger, &ns.stats, ns.mu.stack); err != nil {
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

// newSecretKeyForOpaqueIID returns a new secret key for opaque IID generation.
func newSecretKeyForOpaqueIID() ([]byte, error) {
	var secretKeyBuf [header.OpaqueIIDSecretKeyMinBytes]byte
	secretKey := secretKeyBuf[:]
	n, err := rand.Read(secretKey)
	if err != nil {
		return nil, fmt.Errorf("failed to populate a new secret key for opaque IIDs: %s", err)
	}
	if n != header.OpaqueIIDSecretKeyMinBytes {
		return nil, fmt.Errorf("failed to populate a new secret key for opaque IIDs: got rand.Read(_) = %d, want = %d", n, header.OpaqueIIDSecretKeyMinBytes)
	}
	return secretKey, nil
}

// getSecretKeyForOpaqueIID gets a secret key for opaque IID generation from the
// secure stash store service, or attempts to create one. If the stash service
// is unavailable, a temporary secret key will be returned.
func getSecretKeyForOpaqueIID(appCtx *appcontext.Context) ([]byte, error) {
	syslog.VLogf(syslog.DebugVerbosity, "getting or creating secret key for opaque IID from secure stash store")

	// Connect to the secure stash store service.
	storeReq, store, err := stash.NewSecureStoreInterfaceRequest()
	if err != nil {
		syslog.Errorf("could not create the request to connect to the %s service: %s", stash.SecureStoreName, err)
		return newSecretKeyForOpaqueIID()
	}
	defer store.Close()
	appCtx.ConnectToEnvService(storeReq)

	// Use our secure stash.
	if err := store.Identify(stashStoreIdentificationName); err != nil {
		syslog.Errorf("failed to identify as %s to the secure stash store: %s", stashStoreIdentificationName, err)
		return newSecretKeyForOpaqueIID()
	}
	storeAccessorReq, storeAccessor, err := stash.NewStoreAccessorInterfaceRequest()
	if err != nil {
		syslog.Errorf("could not create the secure stash store accessor request: %s", err)
		return newSecretKeyForOpaqueIID()
	}
	defer storeAccessor.Close()
	if err := store.CreateAccessor(false /* readOnly */, storeAccessorReq); err != nil {
		syslog.Errorf("failed to create accessor to the secure stash store: %s", err)
		return newSecretKeyForOpaqueIID()
	}

	// Attempt to get the existing secret key.
	opaqueIIDSecretKeyValue, err := storeAccessor.GetValue(opaqueIIDSecretKeyName)
	if err != nil {
		syslog.Errorf("failed to get opaque IID secret key from secure stash store: %s", err)
		return newSecretKeyForOpaqueIID()
	}

	// If a key exists, make sure it is valid before returning it.
	//
	// The value should be stored as a base64 encoded string.
	//
	// We use a string because stash.Value.Bytesval uses a fuchsia.mem.Buffer
	// which uses a VMO (which uses memory in page size increments). This is
	// wasteful as the key only uses 16 bytes. We base64 encode the string
	// because stash.Value.Stringval expects an ascii string; the store returns an
	// error when flushing the store with a stash.Value.Stringval of raw bytes.
	if opaqueIIDSecretKeyValue != nil && opaqueIIDSecretKeyValue.Which() == stash.ValueStringval {
		syslog.VLogf(syslog.DebugVerbosity, "found a secret key for opaque IIDs in the secure stash store")

		if secretKey, err := base64.StdEncoding.DecodeString(opaqueIIDSecretKeyValue.Stringval); err != nil {
			syslog.Errorf("failed to decode the secret key string: %s", err)
		} else if l := len(secretKey); l != header.OpaqueIIDSecretKeyMinBytes {
			syslog.Errorf("invalid secret key for opaque IIDs; got length = %d, want = %d", l, header.OpaqueIIDSecretKeyMinBytes)
		} else {
			syslog.VLogf(syslog.DebugVerbosity, "using existing secret key for opaque IIDs")
			return secretKey, nil
		}
	}

	// Generate a new secret key as we either do not have one or the one we have
	// is invalid.
	syslog.VLogf(syslog.DebugVerbosity, "generating a new secret key for opaque IIDs")
	secretKey, err := newSecretKeyForOpaqueIID()
	if err != nil {
		return nil, err
	}

	// Store the newly generated key to the secure stash store as a base64
	// encoded string.
	if err := storeAccessor.SetValue(opaqueIIDSecretKeyName, stash.ValueWithStringval(base64.StdEncoding.EncodeToString(secretKey))); err != nil {
		syslog.Errorf("failed to set newly created secret key for opaque IID to secure stash store: %s", err)
		return secretKey, nil
	}
	flushResp, err := storeAccessor.Flush()
	if err != nil {
		syslog.Errorf("failed to flush secure stash store with updated secret key for opaque IID: %s", err)
		return secretKey, nil
	}
	switch w := flushResp.Which(); w {
	case stash.StoreAccessorFlushResultErr:
		syslog.Errorf("got error response when flushing secure stash store with updated secret key for opaque IID: %s", flushResp.Err)
		return secretKey, nil

	case stash.StoreAccessorFlushResultResponse:
		syslog.VLogf(syslog.DebugVerbosity, "saved newly generated secret key for opaque IIDs to secure stash store")
		return secretKey, nil

	default:
		panic(fmt.Sprintf("unexpected store accessor flush result type: %d", w))
	}
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
