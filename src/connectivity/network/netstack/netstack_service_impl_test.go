// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//go:build !build_with_native_toolchain
// +build !build_with_native_toolchain

package netstack

import (
	"context"
	"syscall/zx"
	"testing"

	"fidl/fuchsia/net/dhcp"
	"fidl/fuchsia/netstack"
)

func TestGetDhcpClient(t *testing.T) {
	t.Run("bad NIC", func(t *testing.T) {
		ns, _ := newNetstack(t, netstackTestOptions{})
		netstackServiceImpl := netstackImpl{ns: ns}
		req, proxy, err := dhcp.NewClientWithCtxInterfaceRequest()
		if err != nil {
			t.Fatalf("dhcp.NewClientWithCtxInterfaceRequest() = %s", err)
		}
		defer func() {
			if err := proxy.Close(); err != nil {
				t.Fatalf("proxy.Close() = %s", err)
			}
		}()
		result, err := netstackServiceImpl.GetDhcpClient(context.Background(), 1234, req)
		if err != nil {
			t.Fatalf("netstachServiceImpl.GetDhcpClient(...) = %s", err)
		}
		if got, want := result.Which(), netstack.I_netstackGetDhcpClientResultTag(netstack.NetstackGetDhcpClientResultErr); got != want {
			t.Fatalf("got result.Which() = %d, want = %d", got, want)
		}
		if got, want := zx.Status(result.Err), zx.ErrNotFound; got != want {
			t.Fatalf("got result.Err = %s, want = %s", got, want)
		}
		if status := zx.Sys_object_wait_one(*proxy.Channel.Handle(), zx.SignalChannelPeerClosed, 0, nil); status != zx.ErrOk {
			t.Fatalf("zx.Sys_object_wait_one(_, zx.SignalChannelPeerClosed, 0, _) = %s", status)
		}
	})
}
