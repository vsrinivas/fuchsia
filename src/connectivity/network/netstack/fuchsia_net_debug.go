// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//go:build !build_with_native_toolchain
// +build !build_with_native_toolchain

package netstack

import (
	"syscall/zx/fidl"

	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/fidlconv"
	syslog "go.fuchsia.dev/fuchsia/src/lib/syslog/go"

	"fidl/fuchsia/net/debug"
	"fidl/fuchsia/net/interfaces/admin"

	"gvisor.dev/gvisor/pkg/tcpip"
)

var _ debug.InterfacesWithCtx = (*debugInterfacesImpl)(nil)

type debugInterfacesImpl struct {
	ns *Netstack
}

func (ci *debugInterfacesImpl) GetAdmin(_ fidl.Context, nicid uint64, request admin.ControlWithCtxInterfaceRequest) error {
	{
		nicid := tcpip.NICID(nicid)
		nicInfo, ok := ci.ns.stack.NICInfo()[nicid]
		if !ok {
			// Just close the channel without a terminal event.
			if err := request.Close(); err != nil {
				_ = syslog.WarnTf(debug.InterfacesName, "GetAdmin(%d) close error: %s", nicid, err)
			}
			return nil
		}

		ifs := nicInfo.Context.(*ifState)
		ifs.addAdminConnection(request, false /* strong */)
		return nil
	}
}

func (ci *debugInterfacesImpl) GetMac(_ fidl.Context, nicid uint64) (debug.InterfacesGetMacResult, error) {
	if nicInfo, ok := ci.ns.stack.NICInfo()[tcpip.NICID(nicid)]; ok {
		var response debug.InterfacesGetMacResponse
		if linkAddress := nicInfo.LinkAddress; len(linkAddress) != 0 {
			mac := fidlconv.ToNetMacAddress(linkAddress)
			response.Mac = &mac
		}
		return debug.InterfacesGetMacResultWithResponse(response), nil
	}
	return debug.InterfacesGetMacResultWithErr(debug.InterfacesGetMacErrorNotFound), nil
}
