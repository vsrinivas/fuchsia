// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package netstack

import (
	"fmt"
	"testing"

	"netstack/fidlconv"

	"fidl/fuchsia/hardware/ethernet"
	"fidl/fuchsia/net"
	"fidl/fuchsia/net/stack"
	"fidl/fuchsia/netstack"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
	"gvisor.dev/gvisor/pkg/tcpip"
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
		addResult, err := ni.AddForwardingEntry(invalidRoute)
		AssertNoError(t, err)
		if addResult != stack.StackAddForwardingEntryResultWithErr(stack.ErrorInvalidArgs) {
			t.Errorf("got ni.AddForwardingEntry(%#v) = %#v, want = Err(ErrorInvalidArgs)", invalidRoute, addResult)
		}

		// Add a local subnet route.
		addResult, err = ni.AddForwardingEntry(localRoute)
		AssertNoError(t, err)
		if addResult != stack.StackAddForwardingEntryResultWithResponse(stack.StackAddForwardingEntryResponse{}) {
			t.Fatalf("got ni.AddForwardingEntry(%#v) = %#v, want = Response()", localRoute, addResult)
		}
		table, err = ni.GetForwardingTable()
		AssertNoError(t, err)
		expectedTable := []stack.ForwardingEntry{localRoute}
		if diff := cmp.Diff(table, expectedTable, cmpopts.IgnoreTypes(struct{}{})); diff != "" {
			t.Fatalf("forwarding table mismatch (-want +got):\n%s", diff)
		}

		// Add the same entry again.
		addResult, err = ni.AddForwardingEntry(localRoute)
		AssertNoError(t, err)
		if addResult != stack.StackAddForwardingEntryResultWithResponse(stack.StackAddForwardingEntryResponse{}) {
			t.Errorf("got ni.AddForwardingEntry(%#v) = %#v, want = Response()", localRoute, addResult)
		}
		table, err = ni.GetForwardingTable()
		AssertNoError(t, err)
		if diff := cmp.Diff(table, expectedTable, cmpopts.IgnoreTypes(struct{}{})); diff != "" {
			t.Fatalf("forwarding table mismatch (-want +got):\n%s", diff)
		}

		// Add default route.
		addResult, err = ni.AddForwardingEntry(defaultRoute)
		AssertNoError(t, err)
		if addResult != stack.StackAddForwardingEntryResultWithResponse(stack.StackAddForwardingEntryResponse{}) {
			t.Errorf("got ni.AddForwardingEntry(%#v) = %#v, want = Response()", defaultRoute, addResult)
		}
		table, err = ni.GetForwardingTable()
		AssertNoError(t, err)
		expectedTable = append(expectedTable, defaultRoute)
		if diff := cmp.Diff(table, expectedTable, cmpopts.IgnoreTypes(struct{}{})); diff != "" {
			t.Fatalf("forwarding table mismatch (-want +got):\n%s", diff)
		}

		// Remove nonexistent subnet.
		delResult, err := ni.DelForwardingEntry(nonexistentSubnet)
		AssertNoError(t, err)
		if delResult != stack.StackDelForwardingEntryResultWithErr(stack.ErrorNotFound) {
			t.Errorf("got ni.DelForwardingEntry(%#v) = %#v, want = Err(ErrorNotFound)", nonexistentSubnet, delResult)
		}

		// Remove subnet with bad subnet mask.
		delResult, err = ni.DelForwardingEntry(badMaskSubnet)
		AssertNoError(t, err)
		if delResult != stack.StackDelForwardingEntryResultWithErr(stack.ErrorInvalidArgs) {
			t.Errorf("got ni.DelForwardingEntry(%#v) = %#v, want = Err(ErrorInvalidArgs)", badMaskSubnet, delResult)
		}

		// Remove local route.
		delResult, err = ni.DelForwardingEntry(localSubnet)
		AssertNoError(t, err)
		if delResult != stack.StackDelForwardingEntryResultWithResponse(stack.StackDelForwardingEntryResponse{}) {
			t.Fatalf("got ni.DelForwardingEntry(%#v) = Err(%s), want = Response()", localRoute, delResult.Err)
		}
		table, err = ni.GetForwardingTable()
		AssertNoError(t, err)
		expectedTable = []stack.ForwardingEntry{defaultRoute}
		if diff := cmp.Diff(table, expectedTable, cmpopts.IgnoreTypes(struct{}{})); diff != "" {
			t.Fatalf("forwarding table mismatch (-want +got):\n%s", diff)
		}

		// Remove default route.
		delResult, err = ni.DelForwardingEntry(defaultSubnet)
		AssertNoError(t, err)
		if delResult != stack.StackDelForwardingEntryResultWithResponse(stack.StackDelForwardingEntryResponse{}) {
			t.Fatalf("got ni.DelForwardingEntry(%#v) = Err(%s), want = Response()", localRoute, delResult.Err)
		}
		table, err = ni.GetForwardingTable()
		AssertNoError(t, err)
		expectedTable = []stack.ForwardingEntry{}
		if diff := cmp.Diff(table, expectedTable, cmpopts.IgnoreTypes(struct{}{})); diff != "" {
			t.Fatalf("forwarding table mismatch (-want +got):\n%s", diff)
		}
	})

	t.Run("Enable and Disable PacketFilter", func(t *testing.T) {
		ns := newNetstack(t)
		eth := deviceForAddEth(ethernet.Info{}, t)
		if _, err := ns.addEth(testTopoPath, netstack.InterfaceConfig{Name: testDeviceName}, &eth); err != nil {
			t.Fatal(err)
		}
		ni := stackImpl{ns: ns}

		{
			result, err := ni.EnablePacketFilter(1)
			AssertNoError(t, err)
			if result != stack.StackEnablePacketFilterResultWithResponse(stack.StackEnablePacketFilterResponse{}) {
				t.Fatalf("got ni.EnablePacketFilter(1) = %#v, want = Response()", result)
			}
			enabled, err := ni.isPacketFilterEnabled(1)
			AssertNoError(t, err)
			if !enabled {
				t.Fatalf("got ni.isPacketFilterEnabled(1) = Response(%v), want = Response(t)", enabled)
			}
		}
		{
			result, err := ni.DisablePacketFilter(1)
			AssertNoError(t, err)
			if result != stack.StackDisablePacketFilterResultWithResponse(stack.StackDisablePacketFilterResponse{}) {
				t.Fatalf("got ni.DisablePacketFilter(1) = %#v, want = Response()", result)
			}
			enabled, err := ni.isPacketFilterEnabled(1)
			AssertNoError(t, err)
			if enabled {
				t.Fatalf("got ni.isPacketFilterEnabled(1) = Response(%v), want = Response(f)", enabled)
			}
		}
		{
			result, err := ni.EnablePacketFilter(1)
			AssertNoError(t, err)
			if result != stack.StackEnablePacketFilterResultWithResponse(stack.StackEnablePacketFilterResponse{}) {
				t.Fatalf("got ni.EnablePacketFilter(1) = %#v, want = Response()", result)
			}
			enabled, err := ni.isPacketFilterEnabled(1)
			AssertNoError(t, err)
			if !enabled {
				t.Fatalf("got ni.isPacketFilterEnabled(1) = Response(%v), want = Response(t)", enabled)
			}
		}
	})

	t.Run("Enable and Disable IP Forwarding", func(t *testing.T) {
		ns := newNetstack(t)
		eth := deviceForAddEth(ethernet.Info{}, t)
		if _, err := ns.addEth(testTopoPath, netstack.InterfaceConfig{Name: testDeviceName}, &eth); err != nil {
			t.Fatal(err)
		}
		ni := stackImpl{ns: ns}

		err := ni.EnableIpForwarding()
		AssertNoError(t, err)
		enabled := ni.isIpForwardingEnabled()
		if !enabled {
			t.Fatalf("got ni.isIpForwardingEnabled() = %v, want = t", enabled)
		}

		err = ni.DisableIpForwarding()
		AssertNoError(t, err)
		enabled = ni.isIpForwardingEnabled()
		AssertNoError(t, err)
		if enabled {
			t.Fatalf("got ni.isIpForwardingEnabled() = %v, want = false", enabled)
		}

		err = ni.EnableIpForwarding()
		AssertNoError(t, err)
		enabled = ni.isIpForwardingEnabled()
		if !enabled {
			t.Fatalf("got ni.isIpForwardingEnabled() = %v, want = t", enabled)
		}
	})
}

func (ni *stackImpl) isPacketFilterEnabled(id uint64) (bool, error) {
	ni.ns.mu.Lock()
	ifs, ok := ni.ns.mu.ifStates[tcpip.NICID(id)]
	ni.ns.mu.Unlock()

	if !ok {
		return false, fmt.Errorf("ifStates %d not found", id)
	}
	return ifs.filterEndpoint.IsEnabled(), nil
}

func (ni *stackImpl) isIpForwardingEnabled() bool {
	ni.ns.mu.Lock()
	defer ni.ns.mu.Unlock()
	return ni.ns.mu.stack.Forwarding()
}
