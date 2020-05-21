// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package connectivity

import (
	"sync"
	"syscall/zx"
	"syscall/zx/fidl"

	"go.fuchsia.dev/fuchsia/src/lib/component"
	syslog "go.fuchsia.dev/fuchsia/src/lib/syslog/go"

	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/fidlconv"
	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/util"

	"fidl/fuchsia/net"
	"fidl/fuchsia/netstack"
)

var mu struct {
	sync.Mutex
	reachable bool
	proxies   map[*net.ConnectivityEventProxy]struct{}
}

func init() {
	mu.proxies = make(map[*net.ConnectivityEventProxy]struct{})
}

func AddOutgoingService(ctx *component.Context) {
	stub := net.ConnectivityWithCtxStub{Impl: struct{}{}}
	ctx.OutgoingService.AddService(
		net.ConnectivityName,
		func(ctx fidl.Context, c zx.Channel) error {
			mu.Lock()
			reachable := mu.reachable
			mu.Unlock()

			pxy := net.ConnectivityEventProxy{Channel: c}
			if err := pxy.OnNetworkReachable(reachable); err != nil {
				return err
			}
			mu.Lock()
			mu.proxies[&pxy] = struct{}{}
			mu.Unlock()

			go func() {
				defer func() {
					mu.Lock()
					delete(mu.proxies, &pxy)
					mu.Unlock()
				}()
				component.ServeExclusive(ctx, &stub, c, func(err error) {
					_ = syslog.Warnf("%s", err)
				})
			}()
			return nil
		},
	)
}

// TODO(NET-1001): extract into a separate reachability service based on a
// better network reachability signal.
func InferAndNotify(ifs []netstack.NetInterface2) {
	syslog.VLogf(syslog.TraceVerbosity, "inferring network reachability")
	current := inferReachability(ifs)

	mu.Lock()
	if current != mu.reachable {
		syslog.VLogf(syslog.TraceVerbosity, "notifying clients of new reachability status: %t", current)
		mu.reachable = current

		for pxy := range mu.proxies {
			_ = pxy.OnNetworkReachable(current)
		}
	}
	mu.Unlock()
}

func hasDHCPAddress(nic netstack.NetInterface2) bool {
	return nic.Flags&netstack.NetInterfaceFlagDhcp != 0 && nic.Flags&netstack.NetInterfaceFlagUp != 0 && !util.IsAny(fidlconv.ToTCPIPAddress(nic.Addr))
}

func inferReachability(ifs []netstack.NetInterface2) bool {
	for _, nic := range ifs {
		if hasDHCPAddress(nic) {
			return true
		}
	}
	return false
}
