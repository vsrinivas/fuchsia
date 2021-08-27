// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//go:build !build_with_native_toolchain
// +build !build_with_native_toolchain

package netstack

import (
	"context"
	"strings"
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

func (r *routesImpl) Resolve(ctx fidl.Context, destination net.IpAddress) (routes.StateResolveResult, error) {
	const unspecifiedNIC = tcpip.NICID(0)
	const unspecifiedLocalAddress = tcpip.Address("")

	remote, proto := fidlconv.ToTCPIPAddressAndProtocolNumber(destination)
	netProtoName := strings.TrimPrefix(networkProtocolToString(proto), "IP")
	var flags string
	syslogFn := syslog.DebugTf
	if remote.Unspecified() {
		flags = "U"
		syslogFn = syslog.InfoTf
	}
	logFn := func(suffix string, err tcpip.Error) {
		_ = syslogFn(
			"fuchsia.net.routes", "stack.FindRoute(%s (%s|%s))%s = (_, %s)",
			remote,
			netProtoName,
			flags,
			suffix,
			err,
		)
	}

	route, err := r.stack.FindRoute(unspecifiedNIC, unspecifiedLocalAddress, remote, proto, false /* multicastLoop */)
	if err != nil {
		logFn("", err)
		return routes.StateResolveResultWithErr(int32(WrapTcpIpError(err).ToZxStatus())), nil
	}
	defer route.Release()

	return func() routes.StateResolveResult {
		ch := make(chan stack.ResolvedFieldsResult, 1)
		err := route.ResolvedFields(func(result stack.ResolvedFieldsResult) {
			ch <- result
		})
		switch err.(type) {
		case nil, *tcpip.ErrWouldBlock:
			select {
			case result := <-ch:
				if result.Err == nil {
					// Build our response with the resolved route.
					nicID := route.NICID()
					route := result.RouteInfo

					var node routes.Destination
					node.SetSourceAddress(fidlconv.ToNetIpAddress(route.LocalAddress))
					// If the remote link address is unspecified, then the outgoing link
					// does not support MAC addressing.
					if linkAddr := route.RemoteLinkAddress; len(linkAddr) != 0 {
						node.SetMac(fidlconv.ToNetMacAddress(linkAddr))
					}
					node.SetInterfaceId(uint64(nicID))

					var response routes.StateResolveResponse
					if len(route.NextHop) != 0 {
						node.SetAddress(fidlconv.ToNetIpAddress(route.NextHop))
						response.Result.SetGateway(node)
					} else {
						node.SetAddress(fidlconv.ToNetIpAddress(route.RemoteAddress))
						response.Result.SetDirect(node)
					}
					return routes.StateResolveResultWithResponse(response)
				}
				err = result.Err
			case <-ctx.Done():
				switch ctx.Err() {
				case context.Canceled:
					return routes.StateResolveResultWithErr(int32(zx.ErrCanceled))
				case context.DeadlineExceeded:
					return routes.StateResolveResultWithErr(int32(zx.ErrTimedOut))
				}
			}
		}
		logFn(".ResolvedFields(...)", err)
		return routes.StateResolveResultWithErr(int32(zx.ErrAddressUnreachable))
	}(), nil
}
