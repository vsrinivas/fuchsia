// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package netstack

import (
	"syscall/zx/fidl"
	"testing"

	"netstack/fidlconv"

	"fidl/fuchsia/hardware/ethernet"
	"fidl/fuchsia/net"
	"fidl/fuchsia/net/stack"
	"fidl/fuchsia/netstack"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
	"github.com/google/netstack/tcpip"
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
	go fidl.Serve()
	t.Run("Add and Delete Forwarding Entries", func(t *testing.T) {
		ns := newNetstack(t)
		eth := deviceForAddEth(ethernet.Info{}, t)
		if _, err := ns.addEth(testTopoPath, netstack.InterfaceConfig{Name: testDeviceName}, &eth); err != nil {
			t.Fatal(err)
		}
		ni := stackImpl{ns: ns}

		table, err := ni.GetForwardingTable()
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
		status, err := ni.AddForwardingEntry(invalidRoute)
		AssertNoError(t, err)
		if status == nil || status.Type != stack.ErrorTypeInvalidArgs {
			t.Errorf("got ni.AddForwardingEntry(%#v) = %#v, want = ErrorTypeInvalidArgs", invalidRoute, status)
		}

		// Add a local subnet route.
		status, err = ni.AddForwardingEntry(localRoute)
		AssertNoError(t, err)
		if status != nil {
			t.Fatalf("got ni.AddForwardingEntry(%#v) = %#v, want = nil", localRoute, status)
		}
		table, err = ni.GetForwardingTable()
		AssertNoError(t, err)
		expectedTable := []stack.ForwardingEntry{localRoute}
		if diff := cmp.Diff(table, expectedTable, cmpopts.IgnoreTypes(struct{}{})); diff != "" {
			t.Fatalf("forwarding table mismatch (-want +got):\n%s", diff)
		}

		// Add the same entry again.
		status, err = ni.AddForwardingEntry(localRoute)
		AssertNoError(t, err)
		if status != nil {
			t.Errorf("got ni.AddForwardingEntry(%#v) = %#v, want = nil", localRoute, status)
		}
		table, err = ni.GetForwardingTable()
		AssertNoError(t, err)
		if diff := cmp.Diff(table, expectedTable, cmpopts.IgnoreTypes(struct{}{})); diff != "" {
			t.Fatalf("forwarding table mismatch (-want +got):\n%s", diff)
		}

		// Add default route.
		status, err = ni.AddForwardingEntry(defaultRoute)
		AssertNoError(t, err)
		if status != nil {
			t.Errorf("got ni.AddForwardingEntry(%#v) = %#v, want = nil", defaultRoute, status)
		}
		table, err = ni.GetForwardingTable()
		AssertNoError(t, err)
		expectedTable = append(expectedTable, defaultRoute)
		if diff := cmp.Diff(table, expectedTable, cmpopts.IgnoreTypes(struct{}{})); diff != "" {
			t.Fatalf("forwarding table mismatch (-want +got):\n%s", diff)
		}

		// Remove nonexistent subnet.
		status, err = ni.DelForwardingEntry(nonexistentSubnet)
		if status == nil || status.Type != stack.ErrorTypeNotFound {
			t.Errorf("got ni.DelForwardingEntry(%#v) = %#v, want = ErrorTypeNotFound", nonexistentSubnet, status)
		}

		// Remove subnet with bad subnet mask.
		status, err = ni.DelForwardingEntry(badMaskSubnet)
		if status == nil || status.Type != stack.ErrorTypeInvalidArgs {
			t.Errorf("got ni.DelForwardingEntry(%#v) = %#v, want = ErrorTypeInvalidArgs", badMaskSubnet, status)
		}

		// Remove local route.
		status, err = ni.DelForwardingEntry(localSubnet)
		if status != nil {
			t.Fatalf("got ni.DelForwardingEntry(%#v) = %#v, want = nil", localRoute, status)
		}
		table, err = ni.GetForwardingTable()
		AssertNoError(t, err)
		expectedTable = []stack.ForwardingEntry{defaultRoute}
		if diff := cmp.Diff(table, expectedTable, cmpopts.IgnoreTypes(struct{}{})); diff != "" {
			t.Fatalf("forwarding table mismatch (-want +got):\n%s", diff)
		}

		// Remove default route.
		status, err = ni.DelForwardingEntry(defaultSubnet)
		if status != nil {
			t.Fatalf("got ni.DelForwardingEntry(%#v) = %#v, want = nil", localRoute, status)
		}
		table, err = ni.GetForwardingTable()
		AssertNoError(t, err)
		expectedTable = []stack.ForwardingEntry{}
		if diff := cmp.Diff(table, expectedTable, cmpopts.IgnoreTypes(struct{}{})); diff != "" {
			t.Fatalf("forwarding table mismatch (-want +got):\n%s", diff)
		}
	})
}
