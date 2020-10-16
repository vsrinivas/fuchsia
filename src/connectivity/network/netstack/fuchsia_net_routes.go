// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package netstack

import (
	"syscall/zx"
	"syscall/zx/fidl"

	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/fidlconv"
	syslog "go.fuchsia.dev/fuchsia/src/lib/syslog/go"

	"fidl/fuchsia/net"
	"fidl/fuchsia/net/routes"

	"gvisor.dev/gvisor/pkg/tcpip"
	"gvisor.dev/gvisor/pkg/tcpip/stack"
)

var _ routes.StateWithCtx = (*routesImpl)(nil)

// routesImpl provides implementation for protocols in fuchsia.net.routes.
type routesImpl struct {
	stack *stack.Stack
}

func (r *routesImpl) Resolve(_ fidl.Context, destination net.IpAddress) (routes.StateResolveResult, error) {
	remote, proto := fidlconv.ToTCPIPAddressAndProtocolNumber(destination)
	route, err := r.stack.FindRoute(0, "", remote, proto, false /* multicastLoop */)
	if err != nil {
		_ = syslog.WarnTf("fuchsia.net.routes", "stack.FindRoute failed: %s", err)
		return routes.StateResolveResultWithErr(int32(WrapTcpIpError(err).ToZxStatus())), nil
	}
	// Check if we need to resolve the link address for this route.
	if route.IsResolutionRequired() {
		for {
			ch, err := route.Resolve(nil)
			switch err {
			case nil:
			case tcpip.ErrWouldBlock:
				// Wait for resolution.
				<-ch
				continue
			default:
				_ = syslog.WarnTf("fuchsia.net.routes", "route.Resolve failed: %s", err)
				return routes.StateResolveResultWithErr(int32(zx.ErrAddressUnreachable)), nil
			}
			break
		}

	}
	// Build our response with the resolved route.
	var response routes.StateResolveResponse
	var node routes.Destination
	node.SetMac(fidlconv.ToNetMacAddress(route.RemoteLinkAddress))
	node.SetInterfaceId(uint64(route.NICID()))
	if len(route.NextHop) != 0 {
		node.SetAddress(fidlconv.ToNetIpAddress(route.NextHop))
		response.Result.SetGateway(node)
	} else {
		node.SetAddress(fidlconv.ToNetIpAddress(route.RemoteAddress))
		response.Result.SetDirect(node)
	}
	return routes.StateResolveResultWithResponse(response), nil
}
