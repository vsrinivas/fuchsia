// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package filter

import (
	"testing"

	"github.com/google/netstack/tcpip/header"
)

func TestPortRangeIsvalid(t *testing.T) {
	var tests = []struct {
		portRange PortRange
		valid     bool
	}{
		{PortRange{0, 0}, true},
		{PortRange{0, 3}, false},
		{PortRange{1, 1}, true},
		{PortRange{1, 3}, true},
		{PortRange{2, 1}, false},
	}

	for _, test := range tests {
		got := test.portRange.IsValid()
		want := test.valid
		if got != want {
			t.Errorf("PortRangeIsValid: test=%+v got=%v, want=%v", test, got, want)
		}
	}
}

func TestPortRangeContains(t *testing.T) {
	var tests = []struct {
		portRange PortRange
		port      uint16
		contained bool
	}{
		{PortRange{0, 0}, 1, true},
		{PortRange{0, 0}, 5, true},
		{PortRange{2, 2}, 1, false},
		{PortRange{2, 2}, 2, true},
		{PortRange{2, 2}, 3, false},
		{PortRange{2, 4}, 1, false},
		{PortRange{2, 4}, 2, true},
		{PortRange{2, 4}, 3, true},
		{PortRange{2, 4}, 4, true},
		{PortRange{2, 4}, 5, false},
	}

	for _, test := range tests {
		got := test.portRange.Contains(test.port)
		want := test.contained
		if got != want {
			t.Errorf("PortRangeContains: test=%+v, got=%v, want=%v", test, got, want)
		}
	}
}

func TestPortRangeLength(t *testing.T) {
	var tests = []struct {
		portRange PortRange
		length    uint16
		err       error
	}{
		{PortRange{0, 0}, 0, nil},
		{PortRange{0, 3}, 0, ErrBadPortRange},
		{PortRange{2, 1}, 0, ErrBadPortRange},
		{PortRange{2, 2}, 1, nil},
		{PortRange{2, 4}, 3, nil},
	}

	for _, test := range tests {
		got, err := test.portRange.Length()
		if err != test.err {
			t.Errorf("PortRangeLength: test=%+v, err=%v, want=%v", test, err, test.err)
		}
		want := test.length
		if got != want {
			t.Errorf("PortRangeLength: test=%+v, got=%v, want=%v", test, got, want)
		}
	}
}

func TestRDRIsValid(t *testing.T) {
	var tests = []struct {
		rdr   RDR
		valid bool
	}{
		{
			// Neither PortRange can be any.
			RDR{
				transProto:      header.UDPProtocolNumber,
				dstAddr:         "\x0a\x00\x00\x00",
				dstPortRange:    PortRange{0, 0},
				newDstAddr:      "\x0b\x00\x00\x00",
				newDstPortRange: PortRange{0, 0},
			},
			false,
		},
		{
			// Both PortRange must be valid.
			RDR{
				transProto:      header.UDPProtocolNumber,
				dstAddr:         "\x0a\x00\x00\x00",
				dstPortRange:    PortRange{3, 1},
				newDstAddr:      "\x0b\x00\x00\x00",
				newDstPortRange: PortRange{1, 3},
			},
			false,
		},
		{
			// Both PortRange must be valid.
			RDR{
				transProto:      header.UDPProtocolNumber,
				dstAddr:         "\x0a\x00\x00\x00",
				dstPortRange:    PortRange{1, 3},
				newDstAddr:      "\x0b\x00\x00\x00",
				newDstPortRange: PortRange{3, 1},
			},
			false,
		},
		{
			RDR{
				transProto:      header.UDPProtocolNumber,
				dstAddr:         "\x0a\x00\x00\x00",
				dstPortRange:    PortRange{1, 3},
				newDstAddr:      "\x0b\x00\x00\x00",
				newDstPortRange: PortRange{1, 3},
			},
			true,
		},
		{
			RDR{
				transProto:      header.UDPProtocolNumber,
				dstAddr:         "\x0a\x00\x00\x00",
				dstPortRange:    PortRange{1, 2},
				newDstAddr:      "\x0b\x00\x00\x00",
				newDstPortRange: PortRange{1, 3},
			},
			false,
		},
	}

	for _, test := range tests {
		got := test.rdr.IsValid()
		want := test.valid
		if got != want {
			t.Errorf("RDRIsValid: test=%+v, got=%v, want=%v", test, got, want)
		}
	}
}
