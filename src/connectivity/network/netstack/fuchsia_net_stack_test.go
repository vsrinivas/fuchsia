// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package netstack

import (
	"syscall/zx/fidl"
	"testing"

	"netstack/fidlconv"

	"fidl/fuchsia/net"
	"fidl/fuchsia/net/stack"

	"github.com/google/netstack/tcpip"
	tcpipstack "github.com/google/netstack/tcpip/stack"
)

func MakeFuchsiaNetstackService() stackImpl {
	ret := stackImpl{
		ns: &Netstack{},
	}
	ret.ns.mu.stack = tcpipstack.New(nil, nil, tcpipstack.Options{})
	return ret
}

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
	t.Run("Add and Delete Forwarding Entry", func(t *testing.T) {
		ni := MakeFuchsiaNetstackService()

		table, err := ni.GetForwardingTable()
		AssertNoError(t, err)

		var dest stack.ForwardingDestination
		dest.SetDeviceId(789)
		subnet := net.Subnet{
			Addr:      fidlconv.ToNetIpAddress("\x0a\x0b\xe0\x00"),
			PrefixLen: 19,
		}
		fe := stack.ForwardingEntry{
			Subnet:      subnet,
			Destination: dest,
		}

		status, err := ni.AddForwardingEntry(fe)
		AssertNoError(t, err)
		if status != nil {
			t.Errorf("got ni.AddForwardingEntry(%#v) = %#v, want = nil", fe, status)
		}

		table, err = ni.GetForwardingTable()
		AssertNoError(t, err)
		if len(table) != 1 {
			t.Errorf("got len(table) = %d, want = 1", len(table))
		}
		if table[0] != fe {
			t.Errorf("got table[0] = %#v, want = %#v\n", table[0], fe)
		}

		status, err = ni.AddForwardingEntry(fe)
		AssertNoError(t, err)
		if status == nil || status.Type != stack.ErrorTypeAlreadyExists {
			t.Errorf("got ni.AddForwardingEntry(%#v) = %#v, want = ErrorTypeAlreadyExists", fe, status)
		}

		nonexistentSubnet := net.Subnet{
			Addr:      fidlconv.ToNetIpAddress("\xaa\x0b\xe0\x00"),
			PrefixLen: 19,
		}
		status, err = ni.DelForwardingEntry(nonexistentSubnet)
		if status == nil || status.Type != stack.ErrorTypeNotFound {
			t.Errorf("got ni.DelForwardingEntry(%#v) = %#v, want = ErrorTypeNotFound", nonexistentSubnet, status)
		}

		badMaskSubnet := net.Subnet{
			Addr:      fidlconv.ToNetIpAddress("\x0a\x0b\xf0\x00"),
			PrefixLen: 19,
		}
		status, err = ni.DelForwardingEntry(badMaskSubnet)
		if status == nil || status.Type != stack.ErrorTypeInvalidArgs {
			t.Errorf("got ni.DelForwardingEntry(%#v) = %#v, want = ErrorTypeInvalidArgs", badMaskSubnet, status)
		}

		badForwardingEntry := stack.ForwardingEntry{
			Subnet:      badMaskSubnet,
			Destination: dest,
		}
		status, err = ni.AddForwardingEntry(badForwardingEntry)
		if status == nil || status.Type != stack.ErrorTypeInvalidArgs {
			t.Errorf("got ni.AddForwardingEntry(%#v) = %#v, want = ErrorTypeInvalidArgs", badForwardingEntry, status)
		}

		status, err = ni.DelForwardingEntry(subnet)
		if status != nil {
			t.Errorf("got ni.DelForwardingEntry(%#v) = %#v, want = nil", subnet, status)
		}

		table, err = ni.GetForwardingTable()
		AssertNoError(t, err)
		if len(table) != 0 {
			t.Errorf("got len(table) = %d, want len(table) = 0", len(table))
		}
	})
}
