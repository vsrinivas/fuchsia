// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// +build !build_with_native_toolchain

package netstack

import (
	"context"
	"syscall/zx"
	"syscall/zx/fidl"

	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/fidlconv"
	syslog "go.fuchsia.dev/fuchsia/src/lib/syslog/go"

	"fidl/fuchsia/net"
	"fidl/fuchsia/net/routes"

	"gvisor.dev/gvisor/pkg/tcpip"
	"gvisor.dev/gvisor/pkg/tcpip/header"
	"gvisor.dev/gvisor/pkg/tcpip/stack"
)

var _ routes.StateWithCtx = (*routesImpl)(nil)

// routesImpl provides implementation for protocols in fuchsia.net.routes.
type routesImpl struct {
	stack *stack.Stack
}

func (r *routesImpl) Resolve(ctx fidl.Context, destination net.IpAddress) (routes.StateResolveResult, error) {
	const unspecifiedNIC = tcpip.NICID(0)
	const unspecifiedLocalAddress = tcpip.Address("")

	remote, proto := fidlconv.ToTCPIPAddressAndProtocolNumber(destination)
	route, err := r.stack.FindRoute(unspecifiedNIC, unspecifiedLocalAddress, remote, proto, false /* multicastLoop */)
	if err != nil {
		_ = syslog.InfoTf(
			"fuchsia.net.routes", "stack.FindRoute returned: %s; unspecified=%t, v4=%t, v6=%t, proto=%d",
			err,
			remote.Unspecified(),
			len(remote) == header.IPv4AddressSize,
			len(remote) == header.IPv6AddressSize,
			proto,
		)
		return routes.StateResolveResultWithErr(int32(WrapTcpIpError(err).ToZxStatus())), nil
	}
	defer route.Release()
	// Check if we need to resolve the link address for this route.
	if route.IsResolutionRequired() {
		attemptedResolution := false
		for {
			ch, err := route.Resolve(nil)
			switch err {
			case nil:
			case tcpip.ErrWouldBlock:
				if !attemptedResolution {
					attemptedResolution = true
					select {
					case <-ch:
						continue
					case <-ctx.Done():
						switch ctx.Err() {
						case context.Canceled:
							return routes.StateResolveResultWithErr(int32(zx.ErrCanceled)), nil
						case context.DeadlineExceeded:
							return routes.StateResolveResultWithErr(int32(zx.ErrTimedOut)), nil
						}
					}
				}
				err = tcpip.ErrNoLinkAddress
				fallthrough
			default:
				_ = syslog.InfoTf(
					"fuchsia.net.routes", "route.Resolve(nil) returned: %s; unspecified=%t, v4=%t, v6=%t, proto=%d",
					err,
					remote.Unspecified(),
					len(remote) == header.IPv4AddressSize,
					len(remote) == header.IPv6AddressSize,
					proto,
				)
				return routes.StateResolveResultWithErr(int32(zx.ErrAddressUnreachable)), nil
			}
			break
		}
	}

	// Build our response with the resolved route.
	var response routes.StateResolveResponse
	var node routes.Destination

	node.SetSourceAddress(fidlconv.ToNetIpAddress(route.LocalAddress))
	// If the remote link address is unspecified, then the outgoing link does not
	// support MAC addressing.
	if linkAddr := route.RemoteLinkAddress(); len(linkAddr) != 0 {
		node.SetMac(fidlconv.ToNetMacAddress(linkAddr))
	}
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
