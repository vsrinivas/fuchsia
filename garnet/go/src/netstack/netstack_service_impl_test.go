// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package netstack

import (
	"net"
	"netstack/fidlconv"
	"syscall/zx"
	"syscall/zx/fidl"
	"testing"

	netfidl "fidl/fuchsia/net"
	"fidl/fuchsia/netstack"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
	"github.com/google/netstack/tcpip"
	"github.com/google/netstack/tcpip/stack"
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

func MakeNetstackService() netstackImpl {
	var ns Netstack
	ns.mu.stack = stack.New(nil, nil, stack.Options{})
	return netstackImpl{
		ns: &ns,
	}
}

func TestRouteTableTransactions(t *testing.T) {
	go fidl.Serve()
	t.Run("no contentions", func(t *testing.T) {
		netstackServiceImpl := MakeNetstackService()
		req, transactionInterface, err := netstack.NewRouteTableTransactionInterfaceRequest()
		AssertNoError(t, err)

		defer transactionInterface.Close()
		success, err := netstackServiceImpl.StartRouteTableTransaction(req)
		// We've given req away, it's important that we don't mess with it anymore!
		AssertNoError(t, err)
		if zx.Status(success) != zx.ErrOk {
			t.Errorf("can't start a transaction")
		}

		rs, err := transactionInterface.GetRouteTable()
		AssertNoError(t, err)

		destinationAddress, destinationSubnet, err := net.ParseCIDR("1.2.3.4/24")
		AssertNoError(t, err)
		gatewayAddress, _, err := net.ParseCIDR("5.6.7.8/32")
		newRouteTableEntry := netstack.RouteTableEntry{
			Destination: toIpAddress(destinationAddress),
			Netmask:     toIpAddress(net.IP(destinationSubnet.Mask)),
			Gateway:     toIpAddress(gatewayAddress),
		}

		newRouteTable := append(rs, newRouteTableEntry)
		AssertNoError(t, transactionInterface.SetRouteTable(newRouteTable))

		_, err = transactionInterface.Commit()
		AssertNoError(t, err)

		actual, err := netstackServiceImpl.GetRouteTable()
		AssertNoError(t, err)
		if diff := cmp.Diff(actual, newRouteTable, cmpopts.IgnoreTypes(struct{}{})); diff != "" {
			t.Errorf("(-want +got)\n%s", diff)
		}
	})

	t.Run("contentions", func(t *testing.T) {
		netstackServiceImpl := MakeNetstackService()
		{
			req, transactionInterface, err := netstack.NewRouteTableTransactionInterfaceRequest()
			AssertNoError(t, err)
			defer transactionInterface.Close()
			success, err := netstackServiceImpl.StartRouteTableTransaction(req)
			AssertNoError(t, err)
			if zx.Status(success) != zx.ErrOk {
				t.Errorf("expected success before starting concurrent transactions")
			}

			req2, transactionInterface2, err := netstack.NewRouteTableTransactionInterfaceRequest()
			AssertNoError(t, err)
			defer transactionInterface2.Close()
			success, err = netstackServiceImpl.StartRouteTableTransaction(req2)
			AssertNoError(t, err)
			if zx.Status(success) != zx.ErrShouldWait {
				t.Errorf("expected failure when trying to start concurrent transactions")
			}
			// Simulate client crashing (the kernel will close all open handles).
			transactionInterface.Close()
			transactionInterface2.Close()
		}
		req, transactionInterface, err := netstack.NewRouteTableTransactionInterfaceRequest()
		AssertNoError(t, err)
		defer transactionInterface.Close()
		success, err := netstackServiceImpl.StartRouteTableTransaction(req)
		AssertNoError(t, err)
		if zx.Status(success) != zx.ErrOk {
			t.Errorf("expected success after ending the previous transaction")
		}
	})
}
