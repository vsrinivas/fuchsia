// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package sshutil

import (
	"net"
	"testing"
)

func TestNetwork(t *testing.T) {
	tests := []struct {
		id      int
		addr    net.Addr
		family  string
		wantErr bool
	}{
		// Valid tcp addresses.
		{1, &net.TCPAddr{IP: net.IPv4(1, 2, 3, 4)}, "tcp", false},
		{2, &net.UDPAddr{IP: net.IPv4(5, 6, 7, 8)}, "tcp", false},
		{3, &net.IPAddr{IP: net.IPv4(9, 10, 11, 12)}, "tcp", false},

		// Valid tcp6 addresses.
		{4, &net.TCPAddr{IP: net.IPv6loopback}, "tcp6", false},
		{5, &net.UDPAddr{IP: net.ParseIP("2001:db8::1")}, "tcp6", false},
		{6, &net.IPAddr{IP: net.IPv6linklocalallrouters}, "tcp6", false},

		// Invalid IP addresses
		{7, &net.TCPAddr{IP: net.IP("")}, "", true},
		{8, &net.UDPAddr{IP: net.IP("123456")}, "", true},
		{9, &net.IPAddr{IP: nil}, "", true},

		// Invalid net.AddrType
		{10, &net.UnixAddr{}, "", true},
	}

	for _, test := range tests {
		n, err := network(test.addr)
		if test.wantErr && err == nil {
			t.Errorf("Test %d: got no error; want error", test.id)
		} else if !test.wantErr && err != nil {
			t.Errorf("Test %d: got error %q; want no error", test.id, err)
		} else if n != test.family {
			t.Errorf("Test %d: got %q; want %q", test.id, n, test.family)
		}
	}
}
