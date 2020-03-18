// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package connectivity

import (
	"sync"
	"syscall/zx"
	"syscall/zx/fidl"

	"app/context"
	"syslog"

	"netstack/fidlconv"
	"netstack/util"

	"fidl/fuchsia/net"
	"fidl/fuchsia/netstack"
)

var service = &net.ConnectivityService{}
var reachable = false
var mu sync.Mutex

func AddOutgoingService(ctx *context.Context) error {
	ctx.OutgoingService.AddService(
		net.ConnectivityName,
		&net.ConnectivityWithCtxStub{Impl: struct{}{}},
		func(s fidl.Stub, c zx.Channel) error {
			k, err := service.BindingSet.Add(s, c, nil)
			// Let clients know the status of the network when they get added.
			if p, ok := service.EventProxyFor(k); ok {
				p.OnNetworkReachable(reachable)
			}
			return err
		},
	)
	return nil
}

// TODO(NET-1001): extract into a separate reachability service based on a
// better network reachability signal.
func InferAndNotify(ifs []netstack.NetInterface2) {
	syslog.VLogf(syslog.TraceVerbosity, "inferring network reachability")
	mu.Lock()
	current := inferReachability(ifs)
	if current != reachable {
		syslog.VLogf(syslog.TraceVerbosity, "notifying clients of new reachability status: %t", current)
		reachable = current
		notify(reachable)
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

func notify(reachable bool) {
	for _, key := range service.BindingKeys() {
		if p, ok := service.EventProxyFor(key); ok {
			p.OnNetworkReachable(reachable)
		}
	}
}
