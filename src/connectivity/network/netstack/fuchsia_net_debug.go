// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//go:build !build_with_native_toolchain
// +build !build_with_native_toolchain

package netstack

import (
	"context"
	"syscall/zx/fidl"

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
			// TODO(https://fxbug.dev/76695): Sending epitaphs not supported in Go.
			if err := request.Close(); err != nil {
				_ = syslog.WarnTf(debug.InterfacesName, "GetAdmin(%d) epitaph error: %s", nicid, err)
			}
			return nil
		}

		ifs := nicInfo.Context.(*ifState)
		ctx, cancel := context.WithCancel(context.Background())
		impl := adminControlImpl{
			ns:          ifs.ns,
			nicid:       ifs.nicid,
			cancelServe: cancel,
		}

		ifs.adminControls.addImpl(ctx, &impl, request)
		return nil
	}
}
