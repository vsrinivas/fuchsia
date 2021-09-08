// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//go:build !build_with_native_toolchain
// +build !build_with_native_toolchain

package netstack

import (
	"context"
	"testing"

	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/fidlconv"

	"fidl/fuchsia/net"
	"fidl/fuchsia/net/name"
	"fidl/fuchsia/net/stack"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
	"gvisor.dev/gvisor/pkg/tcpip"
	"gvisor.dev/gvisor/pkg/tcpip/network/ipv4"
	"gvisor.dev/gvisor/pkg/tcpip/network/ipv6"
)

func TestValidateIPAddressMask(t *testing.T) {
	for _, tc := range []struct {
		addr      tcpip.Address
		prefixLen uint8
		want      bool
	}{
		{addr: "\x0a\x0b\xe0\x00", prefixLen: 32, want: true},
		{addr: "\x0a\x0b\xe0\x00", prefixLen: 20, want: true},
		{addr: "\x0a\x0b\xe0\x00", prefixLen: 19, want: true},
		{addr: "\x0a\x0b\xe0\x00", prefixLen: 18, want: false},

		{addr: "\x0a\x0b\xef\x00", prefixLen: 25, want: true},
		{addr: "\x0a\x0b\xef\x00", prefixLen: 24, want: true},
		{addr: "\x0a\x0b\xef\x00", prefixLen: 23, want: false},

		{addr: "\x00\x00\x00\x00", prefixLen: 0, want: true},
		{addr: "\x00\x00\x00\x00", prefixLen: 32, want: true},
		{addr: "\x00\x00\x00\x00", prefixLen: 33, want: false},
	} {
		addr := fidlconv.ToNetIpAddress(tc.addr)
		if got := validateSubnet(net.Subnet{Addr: addr, PrefixLen: tc.prefixLen}); got != tc.want {
			t.Errorf("got validateSubnet(%v) = %t, want = %t", addr, got, tc.want)
		}
	}
}

func TestFuchsiaNetStack(t *testing.T) {
	t.Run("Add and Delete Forwarding Entries", func(t *testing.T) {
		ns, _ := newNetstack(t)
		t.Cleanup(addNoopEndpoint(t, ns, "").Remove)
		ni := stackImpl{ns: ns}

		table, err := ni.GetForwardingTable(context.Background())
		AssertNoError(t, err)

		var destInvalid, destNic, destNextHop stack.ForwardingDestination
		destInvalid.SetDeviceId(789)
		destNic.SetDeviceId(1)
		destNextHop.SetNextHop(fidlconv.ToNetIpAddress("\xc0\xa8\x20\x01"))

		nonexistentSubnet := net.Subnet{
			Addr:      fidlconv.ToNetIpAddress("\xaa\x0b\xe0\x00"),
			PrefixLen: 19,
		}
		badMaskSubnet := net.Subnet{
			Addr:      fidlconv.ToNetIpAddress("\xc0\xa8\x11\x01"),
			PrefixLen: 19,
		}
		localSubnet := net.Subnet{
			Addr:      fidlconv.ToNetIpAddress("\xc0\xa8\x20\x00"),
			PrefixLen: 19,
		}
		defaultSubnet := net.Subnet{
			Addr:      fidlconv.ToNetIpAddress("\x00\x00\x00\x00"),
			PrefixLen: 0,
		}

		invalidRoute := stack.ForwardingEntry{
			Subnet:      localSubnet,
			Destination: destInvalid,
		}
		localRoute := stack.ForwardingEntry{
			Subnet:      localSubnet,
			Destination: destNic,
		}
		defaultRoute := stack.ForwardingEntry{
			Subnet:      defaultSubnet,
			Destination: destNextHop,
		}

		// Add an invalid entry.
		addResult, err := ni.AddForwardingEntry(context.Background(), invalidRoute)
		AssertNoError(t, err)
		if addResult != stack.StackAddForwardingEntryResultWithErr(stack.ErrorInvalidArgs) {
			t.Errorf("got ni.AddForwardingEntry(%#v) = %#v, want = Err(ErrorInvalidArgs)", invalidRoute, addResult)
		}

		// Add a local subnet route.
		addResult, err = ni.AddForwardingEntry(context.Background(), localRoute)
		AssertNoError(t, err)
		if addResult != stack.StackAddForwardingEntryResultWithResponse(stack.StackAddForwardingEntryResponse{}) {
			t.Fatalf("got ni.AddForwardingEntry(%#v) = %#v, want = Response()", localRoute, addResult)
		}
		table, err = ni.GetForwardingTable(context.Background())
		AssertNoError(t, err)
		expectedTable := []stack.ForwardingEntry{localRoute}
		if diff := cmp.Diff(table, expectedTable, cmpopts.IgnoreTypes(struct{}{})); diff != "" {
			t.Fatalf("forwarding table mismatch (-want +got):\n%s", diff)
		}

		// Add the same entry again.
		addResult, err = ni.AddForwardingEntry(context.Background(), localRoute)
		AssertNoError(t, err)
		if addResult != stack.StackAddForwardingEntryResultWithResponse(stack.StackAddForwardingEntryResponse{}) {
			t.Errorf("got ni.AddForwardingEntry(%#v) = %#v, want = Response()", localRoute, addResult)
		}
		table, err = ni.GetForwardingTable(context.Background())
		AssertNoError(t, err)
		if diff := cmp.Diff(table, expectedTable, cmpopts.IgnoreTypes(struct{}{})); diff != "" {
			t.Fatalf("forwarding table mismatch (-want +got):\n%s", diff)
		}

		// Add default route.
		addResult, err = ni.AddForwardingEntry(context.Background(), defaultRoute)
		AssertNoError(t, err)
		if addResult != stack.StackAddForwardingEntryResultWithResponse(stack.StackAddForwardingEntryResponse{}) {
			t.Errorf("got ni.AddForwardingEntry(%#v) = %#v, want = Response()", defaultRoute, addResult)
		}
		table, err = ni.GetForwardingTable(context.Background())
		AssertNoError(t, err)
		expectedTable = append(expectedTable, defaultRoute)
		if diff := cmp.Diff(table, expectedTable, cmpopts.IgnoreTypes(struct{}{})); diff != "" {
			t.Fatalf("forwarding table mismatch (-want +got):\n%s", diff)
		}

		// Remove nonexistent subnet.
		delResult, err := ni.DelForwardingEntry(context.Background(), nonexistentSubnet)
		AssertNoError(t, err)
		if delResult != stack.StackDelForwardingEntryResultWithErr(stack.ErrorNotFound) {
			t.Errorf("got ni.DelForwardingEntry(%#v) = %#v, want = Err(ErrorNotFound)", nonexistentSubnet, delResult)
		}

		// Remove subnet with bad subnet mask.
		delResult, err = ni.DelForwardingEntry(context.Background(), badMaskSubnet)
		AssertNoError(t, err)
		if delResult != stack.StackDelForwardingEntryResultWithErr(stack.ErrorInvalidArgs) {
			t.Errorf("got ni.DelForwardingEntry(%#v) = %#v, want = Err(ErrorInvalidArgs)", badMaskSubnet, delResult)
		}

		// Remove local route.
		delResult, err = ni.DelForwardingEntry(context.Background(), localSubnet)
		AssertNoError(t, err)
		if delResult != stack.StackDelForwardingEntryResultWithResponse(stack.StackDelForwardingEntryResponse{}) {
			t.Fatalf("got ni.DelForwardingEntry(%#v) = Err(%s), want = Response()", localRoute, delResult.Err)
		}
		table, err = ni.GetForwardingTable(context.Background())
		AssertNoError(t, err)
		expectedTable = []stack.ForwardingEntry{defaultRoute}
		if diff := cmp.Diff(table, expectedTable, cmpopts.IgnoreTypes(struct{}{})); diff != "" {
			t.Fatalf("forwarding table mismatch (-want +got):\n%s", diff)
		}

		// Remove default route.
		delResult, err = ni.DelForwardingEntry(context.Background(), defaultSubnet)
		AssertNoError(t, err)
		if delResult != stack.StackDelForwardingEntryResultWithResponse(stack.StackDelForwardingEntryResponse{}) {
			t.Fatalf("got ni.DelForwardingEntry(%#v) = Err(%s), want = Response()", localRoute, delResult.Err)
		}
		table, err = ni.GetForwardingTable(context.Background())
		AssertNoError(t, err)
		expectedTable = []stack.ForwardingEntry{}
		if diff := cmp.Diff(table, expectedTable, cmpopts.IgnoreTypes(struct{}{})); diff != "" {
			t.Fatalf("forwarding table mismatch (-want +got):\n%s", diff)
		}
	})

	t.Run("Enable and Disable IP Forwarding", func(t *testing.T) {
		ns, _ := newNetstack(t)
		ifs1 := addNoopEndpoint(t, ns, "")
		t.Cleanup(ifs1.Remove)

		ifs2 := addNoopEndpoint(t, ns, "")
		t.Cleanup(ifs2.Remove)
		ni := stackImpl{ns: ns}

		protocols := [...]struct {
			tcpip tcpip.NetworkProtocolNumber
			fidl  net.IpVersion
		}{
			{
				tcpip: ipv4.ProtocolNumber,
				fidl:  net.IpVersionV4,
			},
			{
				tcpip: ipv6.ProtocolNumber,
				fidl:  net.IpVersionV6,
			},
		}

		nicIDs := [...]tcpip.NICID{ifs1.nicid, ifs2.nicid}

		checkForwarding := func(nicID tcpip.NICID, netProto tcpip.NetworkProtocolNumber, ipVersion net.IpVersion, want bool) {
			t.Helper()

			if got, err := ns.stack.NICForwarding(nicID, netProto); err != nil {
				t.Errorf("ns.stack.NICForwarding(%d, %d): %s", nicID, netProto, err)
			} else if got != want {
				t.Errorf("got ns.stack.NICForwarding(%d, %d) = %t, want = %t", nicID, netProto, got, want)
			}

			resp, err := ni.GetInterfaceIpForwarding(context.Background(), uint64(nicID), ipVersion)
			if err != nil {
				t.Fatalf("ni.GetInterfaceIpForwarding(_, uint64(%d), %d): %s", nicID, ipVersion, err)
			}
			if diff := cmp.Diff(stack.StackGetInterfaceIpForwardingResultWithResponse(stack.StackGetInterfaceIpForwardingResponse{Enabled: want}), resp); diff != "" {
				t.Errorf("ni.GetInterfaceIpForwarding(_, uint64(%d), %d) mismatch (-want +got):\n%s", nicID, ipVersion, diff)
			}
		}

		checkAllForwarding := func(want bool) {
			t.Helper()

			for _, protocol := range protocols {
				for _, nicID := range nicIDs {
					checkForwarding(nicID, protocol.tcpip, protocol.fidl, want)
				}
			}

			if t.Failed() {
				t.FailNow()
			}
		}

		checkAllForwardingExcept := func(want bool, exceptNICID tcpip.NICID, exceptProtocol net.IpVersion) {
			t.Helper()

			for _, protocol := range protocols {
				for _, nicID := range nicIDs {
					want := want
					if nicID == exceptNICID && protocol.fidl == exceptProtocol {
						want = !want
					}

					checkForwarding(nicID, protocol.tcpip, protocol.fidl, want)
				}
			}

			if t.Failed() {
				t.FailNow()
			}
		}

		setInterfaceForwarding := func(nicID tcpip.NICID, ipVersion net.IpVersion, enabled bool) {
			t.Helper()

			resp, err := ni.SetInterfaceIpForwarding(context.Background(), uint64(nicID), ipVersion, enabled)
			if err != nil {
				t.Fatalf("ni.SetInterfaceIpForwarding(_, uint64(%d), %d, %t): %s", nicID, ipVersion, enabled, err)
			}
			if diff := cmp.Diff(stack.StackSetInterfaceIpForwardingResultWithResponse(stack.StackSetInterfaceIpForwardingResponse{}), resp); diff != "" {
				t.Fatalf("ni.SetInterfaceIpForwarding(_, %d, %d, %t) mismatch (-want +got):\n%s", nicID, ipVersion, enabled, diff)
			}
		}

		// Forwarding should initially be disabled.
		checkAllForwarding(false)

		// We should be able to control forwarding of all interfaces.
		if err := ni.EnableIpForwarding(context.Background()); err != nil {
			t.Fatalf("ni.EnableIpForwarding(_): %s", err)
		}
		checkAllForwarding(true)
		if err := ni.DisableIpForwarding(context.Background()); err != nil {
			t.Fatalf("ni.DisableIpForwarding(_): %s", err)
		}
		checkAllForwarding(false)

		// We should be able to enable forwarding on a single interface.
		setInterfaceForwarding(ifs1.nicid, net.IpVersionV4, true)
		checkAllForwardingExcept(false, ifs1.nicid, net.IpVersionV4)

		if err := ni.EnableIpForwarding(context.Background()); err != nil {
			t.Fatalf("ni.EnableIpForwarding(_): %s", err)
		}
		checkAllForwarding(true)

		// We should be able to disable forwarding on a single interface.
		setInterfaceForwarding(ifs2.nicid, net.IpVersionV6, false)
		checkAllForwardingExcept(true, ifs2.nicid, net.IpVersionV6)

		if err := ni.DisableIpForwarding(context.Background()); err != nil {
			t.Fatalf("ni.DisableIpForwarding(_): %s", err)
		}
		checkAllForwarding(false)
	})
}

func TestDnsServerWatcher(t *testing.T) {
	ns, _ := newNetstack(t)
	watcherCollection := newDnsServerWatcherCollection(ns.dnsConfig.GetServersCacheAndChannel)
	ni := stackImpl{ns: ns, dnsWatchers: watcherCollection}
	request, watcher, err := name.NewDnsServerWatcherWithCtxInterfaceRequest()
	if err != nil {
		t.Fatalf("failed to create watcher request: %s", err)
	}
	defer func() {
		_ = request.Close()
		_ = watcher.Close()
	}()
	if err := ni.GetDnsServerWatcher(context.Background(), request); err != nil {
		t.Fatalf("failed to get watcher: %s", err)
	}
}
