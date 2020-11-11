// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// +build !build_with_native_toolchain

package netstack

import (
	"context"
	"net"
	"syscall/zx"
	"syscall/zx/zxwait"
	"testing"

	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/fidlconv"

	netfidl "fidl/fuchsia/net"
	"fidl/fuchsia/net/dhcp"
	"fidl/fuchsia/netstack"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
	"gvisor.dev/gvisor/pkg/tcpip"
	"gvisor.dev/gvisor/pkg/tcpip/stack"
)

func AssertNoError(t *testing.T, err error) {
	t.Helper()
	if err != nil {
		t.Errorf("Received unexpected error:\n%+v", err)
	}
}

// TODO(tamird): this exact function exists in ifconfig.
func toIpAddress(addr net.IP) netfidl.IpAddress {
	return fidlconv.ToNetIpAddress(tcpip.Address(addr))
}

// ifconfig route add 1.2.3.4/14 gateway 9.8.7.6 iface lo

func TestRouteTableTransactions(t *testing.T) {
	t.Run("no contentions", func(t *testing.T) {
		// Create a basic netstack instance with a single interface. We need at
		// least one interface in order to add routes.
		netstackServiceImpl := netstackImpl{ns: newNetstack(t)}
		ifs, err := addNoopEndpoint(netstackServiceImpl.ns, t.Name())
		if err != nil {
			t.Fatal(err)
		}

		var originalTable []netstack.RouteTableEntry2
		originalTable, err = netstackServiceImpl.GetRouteTable2(context.Background())
		AssertNoError(t, err)

		req, transactionInterface, err := netstack.NewRouteTableTransactionWithCtxInterfaceRequest()
		AssertNoError(t, err)
		defer func() {
			_ = req.Close()
			_ = transactionInterface.Close()
		}()

		success, err := netstackServiceImpl.StartRouteTableTransaction(context.Background(), req)
		// We've given req away, it's important that we don't mess with it anymore!
		AssertNoError(t, err)
		if zx.Status(success) != zx.ErrOk {
			t.Errorf("can't start a transaction")
		}

		_, destinationSubnet, err := net.ParseCIDR("1.2.3.4/24")
		AssertNoError(t, err)
		gatewayAddress := net.ParseIP("5.6.7.8")
		if gatewayAddress == nil {
			t.Fatal("Cannot create gateway IP")
		}
		gateway := toIpAddress(gatewayAddress)
		newRouteTableEntry2 := netstack.RouteTableEntry2{
			Destination: toIpAddress(destinationSubnet.IP),
			Netmask:     toIpAddress(net.IP(destinationSubnet.Mask)),
			Gateway:     &gateway,
			Nicid:       uint32(ifs.nicid),
			Metric:      100,
		}

		success, err = transactionInterface.AddRoute(context.Background(), newRouteTableEntry2)
		AssertNoError(t, err)
		if zx.Status(success) != zx.ErrOk {
			t.Fatal("can't add new route entry")
		}

		// New table should contain the one route we just added.
		actualTable2, err := netstackServiceImpl.GetRouteTable2(context.Background())
		AssertNoError(t, err)
		if len(actualTable2) == 0 {
			t.Errorf("got empty table, expected first entry equal to %+v", newRouteTableEntry2)
		} else if diff := cmp.Diff(actualTable2[0], newRouteTableEntry2, cmpopts.IgnoreTypes(struct{}{})); diff != "" {
			t.Errorf("(-want +got)\n%s", diff)
		}

		// Verify deprecated GetRouteTable() function returns equal entries.
		expectedRouteTableEntry := netstack.RouteTableEntry{
			Destination: newRouteTableEntry2.Destination,
			Netmask:     newRouteTableEntry2.Netmask,
			Gateway:     *newRouteTableEntry2.Gateway,
			Nicid:       newRouteTableEntry2.Nicid,
			// no metric
		}
		actualTable, err := netstackServiceImpl.GetRouteTable(context.Background())
		AssertNoError(t, err)
		if len(actualTable) == 0 {
			t.Errorf("got empty table, expected first entry equal to %+v", expectedRouteTableEntry)
		} else if diff := cmp.Diff(actualTable[0], expectedRouteTableEntry, cmpopts.IgnoreTypes(struct{}{})); diff != "" {
			t.Errorf("(-want +got)\n%s", diff)
		}

		success, err = transactionInterface.DelRoute(context.Background(), newRouteTableEntry2)
		AssertNoError(t, err)
		if zx.Status(success) != zx.ErrOk {
			t.Error("can't delete route entry")
		}

		// New table should be empty.
		actualTable2, err = netstackServiceImpl.GetRouteTable2(context.Background())
		AssertNoError(t, err)
		if len(actualTable2) != len(originalTable) {
			t.Errorf("got %v, want <nothing>", actualTable2)
		}

		// Same for deprecated route table.
		actualTable, err = netstackServiceImpl.GetRouteTable(context.Background())
		AssertNoError(t, err)
		if len(actualTable) != len(originalTable) {
			t.Errorf("got %v, want <nothing>", actualTable)
		}
	})

	t.Run("contentions", func(t *testing.T) {
		netstackServiceImpl := netstackImpl{
			ns: &Netstack{
				stack: stack.New(stack.Options{}),
			},
		}
		{
			req, transactionInterface, err := netstack.NewRouteTableTransactionWithCtxInterfaceRequest()
			AssertNoError(t, err)
			defer func() {
				_ = req.Close()
				_ = transactionInterface.Close()
			}()

			success, err := netstackServiceImpl.StartRouteTableTransaction(context.Background(), req)
			AssertNoError(t, err)
			if zx.Status(success) != zx.ErrOk {
				t.Errorf("expected success before starting concurrent transactions")
			}

			req2, transactionInterface2, err := netstack.NewRouteTableTransactionWithCtxInterfaceRequest()
			AssertNoError(t, err)
			defer func() {
				_ = req2.Close()
				_ = transactionInterface2.Close()
			}()

			success, err = netstackServiceImpl.StartRouteTableTransaction(context.Background(), req2)
			AssertNoError(t, err)
			if zx.Status(success) != zx.ErrShouldWait {
				t.Errorf("expected failure when trying to start concurrent transactions")
			}
			// Simulate client crashing (the kernel will close all open handles).
			_ = transactionInterface.Close()
			_ = transactionInterface2.Close()
		}
		req, transactionInterface, err := netstack.NewRouteTableTransactionWithCtxInterfaceRequest()
		AssertNoError(t, err)
		defer func() {
			_ = req.Close()
			_ = transactionInterface.Close()
		}()
		success, err := netstackServiceImpl.StartRouteTableTransaction(context.Background(), req)
		AssertNoError(t, err)
		if zx.Status(success) != zx.ErrOk {
			t.Errorf("expected success after ending the previous transaction")
		}
	})
}

func TestGetDhcpClient(t *testing.T) {
	t.Run("bad NIC", func(t *testing.T) {
		netstackServiceImpl := netstackImpl{ns: newNetstack(t)}
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
		if _, err := zxwait.Wait(*proxy.Channel.Handle(), zx.SignalChannelPeerClosed, 0); err != nil {
			t.Fatalf("zxwait.Wait(_, zx.SignalChannelPeerClosed, 0) = %s", err)
		}
	})
}
