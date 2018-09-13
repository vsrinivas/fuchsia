// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package filter

import (
	"testing"

	"gvisor.dev/gvisor/pkg/tcpip"
	"gvisor.dev/gvisor/pkg/tcpip/header"
)

func TestPortRangeIsvalid(t *testing.T) {
	var tests = []struct {
		description string
		portRange   PortRange
		valid       bool
	}{
		{
			"any port",
			PortRange{0, 0},
			true,
		},
		{
			"start can't be zero",
			PortRange{0, 3},
			false,
		},
		{
			"port 1",
			PortRange{1, 1},
			true,
		},
		{
			"port range 1 to 3",
			PortRange{1, 3},
			true,
		},
		{
			"start can't be larger than end",
			PortRange{2, 1},
			false,
		},
	}

	for _, test := range tests {
		t.Run(test.description, func(t *testing.T) {
			got := test.portRange.IsValid()
			want := test.valid
			if got != want {
				t.Errorf("got=%t, want=%t", got, want)
			}
		})
	}
}

func TestPortRangeContains(t *testing.T) {
	var tests = []struct {
		description string
		portRange   PortRange
		port        uint16
		contained   bool
	}{
		{
			"any port contains 1",
			PortRange{0, 0},
			1,
			true,
		},
		{
			"any port contains 5",
			PortRange{0, 0},
			5,
			true,
		},
		{
			"port 2 doesn't contain 1",
			PortRange{2, 2},
			1,
			false,
		},
		{
			"port 2 contains 2",
			PortRange{2, 2},
			2,
			true,
		},
		{
			"port 2 doesn't cantain 3",
			PortRange{2, 2},
			3,
			false,
		},
		{
			"range 2 to 4 doesn't contain 1",
			PortRange{2, 4},
			1,
			false,
		},
		{
			"range 2 to 4 contains 2",
			PortRange{2, 4},
			2,
			true,
		},
		{
			"range 2 to 4 contains 3",
			PortRange{2, 4},
			3,
			true,
		},
		{
			"range 2 to 4 contains 4",
			PortRange{2, 4},
			4,
			true,
		},
		{
			"range 2 to 4 doesn't contain 5",
			PortRange{2, 4},
			5,
			false,
		},
	}

	for _, test := range tests {
		t.Run(test.description, func(t *testing.T) {
			got := test.portRange.Contains(test.port)
			want := test.contained
			if got != want {
				t.Errorf("got=%t, want=%t", got, want)
			}
		})
	}
}

func TestPortRangeLength(t *testing.T) {
	var tests = []struct {
		description string
		portRange   PortRange
		length      uint16
		err         error
	}{
		{
			"the length of any port is 0",
			PortRange{0, 0},
			0,
			nil,
		},
		{
			"range start can't be zero if range end is not zero",
			PortRange{0, 3},
			0,
			ErrBadPortRange,
		},
		{
			"range start can't be larger than range end",
			PortRange{2, 1},
			0,
			ErrBadPortRange,
		},
		{
			"the length of a single port is 1 ",
			PortRange{2, 2},
			1,
			nil,
		},
		{
			"the length of range 2 to 4 is 3",
			PortRange{2, 4},
			3,
			nil,
		},
	}

	for _, test := range tests {
		t.Run(test.description, func(t *testing.T) {
			got, err := test.portRange.Length()
			if err != test.err {
				t.Errorf("err=%v, want=%v", err, test.err)
			}
			want := test.length
			if got != want {
				t.Errorf("got=%d, want=%d", got, want)

			}
		})
	}
}

func TestRuleIsValid(t *testing.T) {
	var tests = []struct {
		description string
		rule        Rule
		want        bool
	}{
		{
			"srcSubnet is nil",
			Rule{
				srcSubnet:    nil,
				srcPortRange: PortRange{1, 2},
				dstSubnet: func() *tcpip.Subnet {
					subnet, err := tcpip.NewSubnet("\x0b\x00\x00\x00", "\xff\x00\x00\x00")
					if err != nil {
						t.Fatal(err)
					}
					return &subnet
				}(),
				dstPortRange: PortRange{10, 15},
			},
			true,
		},
		{
			"dstSubnet is nil",
			Rule{
				srcSubnet: func() *tcpip.Subnet {
					subnet, err := tcpip.NewSubnet("\x0b\x00\x00\x00", "\xff\x00\x00\x00")
					if err != nil {
						t.Fatal(err)
					}
					return &subnet
				}(),
				srcPortRange: PortRange{1, 2},
				dstSubnet:    nil,
				dstPortRange: PortRange{10, 15},
			},
			true,
		},
		{
			"both srcSubnet and dstSubnet are IPv4",
			Rule{
				srcSubnet: func() *tcpip.Subnet {
					subnet, err := tcpip.NewSubnet("\x0a\x00\x00\x00", "\xff\x00\x00\x00")
					if err != nil {
						t.Fatal(err)
					}
					return &subnet
				}(),
				srcPortRange: PortRange{1, 2},
				dstSubnet: func() *tcpip.Subnet {
					subnet, err := tcpip.NewSubnet("\x0b\x00\x00\x00", "\xff\x00\x00\x00")
					if err != nil {
						t.Fatal(err)
					}
					return &subnet
				}(),
				dstPortRange: PortRange{10, 15},
			},
			true,
		},
		{
			"srcSubnet is IPv4, and dstSubnet are IPv6",
			Rule{
				srcSubnet: func() *tcpip.Subnet {
					subnet, err := tcpip.NewSubnet("\x0a\x00\x00\x00", "\xff\x00\x00\x00")
					if err != nil {
						t.Fatal(err)
					}
					return &subnet
				}(),
				srcPortRange: PortRange{1, 2},
				dstSubnet: func() *tcpip.Subnet {
					subnet, err := tcpip.NewSubnet(
						"\x0b\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00",
						"\xff\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00")
					if err != nil {
						t.Fatal(err)
					}
					return &subnet
				}(),
				dstPortRange: PortRange{10, 15},
			},
			false,
		},
		{
			"srcPortRange is not valid",
			Rule{
				srcSubnet: func() *tcpip.Subnet {
					subnet, err := tcpip.NewSubnet("\x0a\x00\x00\x00", "\xff\x00\x00\x00")
					if err != nil {
						t.Fatal(err)
					}
					return &subnet
				}(),
				srcPortRange: PortRange{2, 1},
				dstSubnet: func() *tcpip.Subnet {
					subnet, err := tcpip.NewSubnet("\x0b\x00\x00\x00", "\xff\x00\x00\x00")
					if err != nil {
						t.Fatal(err)
					}
					return &subnet
				}(),
				dstPortRange: PortRange{10, 15},
			},
			false,
		},
		{
			"dstPortRange is not valid",
			Rule{
				srcSubnet: func() *tcpip.Subnet {
					subnet, err := tcpip.NewSubnet("\x0a\x00\x00\x00", "\xff\x00\x00\x00")
					if err != nil {
						t.Fatal(err)
					}
					return &subnet
				}(),
				srcPortRange: PortRange{1, 2},
				dstSubnet: func() *tcpip.Subnet {
					subnet, err := tcpip.NewSubnet("\x0b\x00\x00\x00", "\xff\x00\x00\x00")
					if err != nil {
						t.Fatal(err)
					}
					return &subnet
				}(),
				dstPortRange: PortRange{15, 10},
			},
			false,
		},
	}

	for _, test := range tests {
		t.Run(test.description, func(t *testing.T) {
			got := test.rule.IsValid()
			if got != test.want {
				t.Errorf("got=%t, want=%t", got, test.want)
			}
		})
	}
}

func TestRuleMatch(t *testing.T) {
	subnet, err := tcpip.NewSubnet("\x0a\x00\x00\x00", "\xff\x00\x00\x00")
	if err != nil {
		t.Fatal(err)
	}

	var tests = []struct {
		description string
		rule        Rule
		dir         Direction
		transProto  tcpip.TransportProtocolNumber
		srcAddr     tcpip.Address
		srcPort     uint16
		dstAddr     tcpip.Address
		dstPort     uint16
		want        bool
	}{
		{
			"transProto is any",
			Rule{
				direction:    Incoming,
				srcSubnet:    &subnet,
				srcPortRange: PortRange{100, 100},
			},
			Incoming,
			header.TCPProtocolNumber,
			"\x0a\x00\x00\x00",
			100,
			"\x0a\x00\x00\x02",
			200,
			true,
		},
		{
			"transProto is TCP",
			Rule{
				action:       Drop,
				direction:    Incoming,
				transProto:   header.TCPProtocolNumber,
				srcSubnet:    &subnet,
				srcPortRange: PortRange{100, 100},
			},
			Incoming,
			header.TCPProtocolNumber,
			"\x0a\x00\x00\x00",
			100,
			"\x0a\x00\x00\x02",
			200,
			true,
		},
		{
			"transProto doesn't match",
			Rule{
				direction:    Incoming,
				transProto:   header.TCPProtocolNumber,
				srcSubnet:    &subnet,
				srcPortRange: PortRange{100, 100},
			},
			Incoming,
			header.UDPProtocolNumber,
			"\x0a\x00\x00\x00",
			100,
			"\x0a\x00\x00\x02",
			200,
			false,
		},
		{
			"directions doesn't match",
			Rule{
				direction:    Incoming,
				srcSubnet:    &subnet,
				srcPortRange: PortRange{100, 100},
			},
			Outgoing,
			header.UDPProtocolNumber,
			"\x0a\x00\x00\x00",
			100,
			"\x0a\x00\x00\x02",
			200,
			false,
		},
		{
			"srcSubnet doesn't contain the address",
			Rule{
				direction:    Incoming,
				srcSubnet:    &subnet,
				srcPortRange: PortRange{100, 100},
			},
			Incoming,
			header.UDPProtocolNumber,
			"\x0b\x00\x00\x00",
			100,
			"\x0a\x00\x00\x02",
			200,
			false,
		},
		{
			"srcSubnet doesn't contain the address (invert match is on)",
			Rule{
				direction:            Incoming,
				srcSubnet:            &subnet,
				srcSubnetInvertMatch: true,
				srcPortRange:         PortRange{100, 100},
			},
			Incoming,
			header.UDPProtocolNumber,
			"\x0b\x00\x00\x00",
			100,
			"\x0a\x00\x00\x02",
			200,
			true,
		},
		{
			"dstSubnet contains the address",
			Rule{
				direction:    Incoming,
				dstSubnet:    &subnet,
				dstPortRange: PortRange{200, 200},
			},
			Incoming,
			header.UDPProtocolNumber,
			"\x0b\x00\x00\x00",
			100,
			"\x0a\x00\x00\x02",
			200,
			true,
		},
		{
			"dstSubnet doesn't contains the address (invert_match is on)",
			Rule{
				direction:            Incoming,
				dstSubnet:            &subnet,
				dstSubnetInvertMatch: true,
				dstPortRange:         PortRange{200, 200},
			},
			Incoming,
			header.UDPProtocolNumber,
			"\x0b\x00\x00\x00",
			100,
			"\x0c\x00\x00\x02",
			200,
			true,
		},
	}

	for _, test := range tests {
		t.Run(test.description, func(t *testing.T) {
			got := test.rule.Match(test.dir, test.transProto, test.srcAddr, test.srcPort, test.dstAddr, test.dstPort)
			if got != test.want {
				t.Errorf("got=%t, want=%t", got, test.want)
			}
		})
	}
}

func TestNATIsValid(t *testing.T) {
	var tests = []struct {
		description string
		nat         NAT
		want        bool
	}{
		{
			"both srcSubnet and newSrcAddr are IPv4",
			NAT{
				srcSubnet: func() *tcpip.Subnet {
					subnet, err := tcpip.NewSubnet("\x0a\x00\x00\x00", "\xff\x00\x00\x00")
					if err != nil {
						t.Fatal(err)
					}
					return &subnet
				}(),
				newSrcAddr: "\x0b\x00\x00\x00",
			},
			true,
		},
		{
			"srcSubnet is IPv4 and newSrcAddr is IPv6",
			NAT{
				srcSubnet: func() *tcpip.Subnet {
					subnet, err := tcpip.NewSubnet("\x0a\x00\x00\x00", "\xff\x00\x00\x00")
					if err != nil {
						t.Fatal(err)
					}
					return &subnet
				}(),
				newSrcAddr: "\x0b\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00",
			},
			false,
		},
	}

	for _, test := range tests {
		t.Run(test.description, func(t *testing.T) {
			got := test.nat.IsValid()
			if got != test.want {
				t.Errorf("got=%t, want=%t", got, test.want)
			}
		})
	}
}

func TestNATMatch(t *testing.T) {
	subnet, err := tcpip.NewSubnet("\x0a\x00\x00\x00", "\xff\x00\x00\x00")
	if err != nil {
		t.Fatal(err)
	}

	var tests = []struct {
		description string
		nat         NAT
		transProto  tcpip.TransportProtocolNumber
		srcAddr     tcpip.Address
		want        bool
	}{
		{
			"transProto is any",
			NAT{
				srcSubnet:  &subnet,
				newSrcAddr: "\x0b\x00\x00\x00",
			},
			header.TCPProtocolNumber,
			"\x0a\x00\x00\x00",
			true,
		},
		{
			"transProto is TCP",
			NAT{
				transProto: header.TCPProtocolNumber,
				srcSubnet:  &subnet,
				newSrcAddr: "\x0b\x00\x00\x00",
			},
			header.TCPProtocolNumber,
			"\x0a\x00\x00\x00",
			true,
		},
		{
			"transProto doesn't match",
			NAT{
				transProto: header.TCPProtocolNumber,
				srcSubnet:  &subnet,
				newSrcAddr: "\x0b\x00\x00\x00",
			},
			header.UDPProtocolNumber,
			"\x0a\x00\x00\x00",
			false,
		},
		{
			"srcSubnet doesn't contain the address",
			NAT{
				srcSubnet:  &subnet,
				newSrcAddr: "\x0b\x00\x00\x00",
			},
			header.UDPProtocolNumber,
			"\x0b\x00\x00\x00",
			false,
		},
	}

	for _, test := range tests {
		t.Run(test.description, func(t *testing.T) {
			got := test.nat.Match(test.transProto, test.srcAddr)
			if got != test.want {
				t.Errorf("got=%t, want=%t", got, test.want)
			}
		})
	}
}

func TestRDRIsValid(t *testing.T) {
	var tests = []struct {
		description string
		rdr         RDR
		valid       bool
	}{
		{
			"dstPortRange can't be any",
			RDR{
				transProto:      header.UDPProtocolNumber,
				dstAddr:         "\x0a\x00\x00\x00",
				dstPortRange:    PortRange{0, 0},
				newDstAddr:      "\x0b\x00\x00\x00",
				newDstPortRange: PortRange{1, 3},
			},
			false,
		},
		{
			"newDstPortRange can't be any",
			RDR{
				transProto:      header.UDPProtocolNumber,
				dstAddr:         "\x0a\x00\x00\x00",
				dstPortRange:    PortRange{1, 3},
				newDstAddr:      "\x0b\x00\x00\x00",
				newDstPortRange: PortRange{0, 0},
			},
			false,
		},
		{
			"dstPortRange is not valid",
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
			"newDstPortRange is not valid",
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
			"transProto is any",
			RDR{
				dstAddr:         "\x0a\x00\x00\x00",
				dstPortRange:    PortRange{1, 3},
				newDstAddr:      "\x0b\x00\x00\x00",
				newDstPortRange: PortRange{1, 3},
			},
			true,
		},
		{
			"PortRanges must have the same length",
			RDR{
				transProto:      header.UDPProtocolNumber,
				dstAddr:         "\x0a\x00\x00\x00",
				dstPortRange:    PortRange{1, 2},
				newDstAddr:      "\x0b\x00\x00\x00",
				newDstPortRange: PortRange{1, 3},
			},
			false,
		},
		{
			"dstAddr is IPv4 and newDstAddr is IPv6",
			RDR{
				transProto:      header.UDPProtocolNumber,
				dstAddr:         "\x0a\x00\x00\x00",
				dstPortRange:    PortRange{1, 2},
				newDstAddr:      "\x0b\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00",
				newDstPortRange: PortRange{1, 2},
			},
			false,
		},
	}

	for _, test := range tests {
		t.Run(test.description, func(t *testing.T) {
			got := test.rdr.IsValid()
			want := test.valid
			if got != want {
				t.Errorf("got=%t, want=%t", got, want)
			}
		})
	}
}

func TestRDRMatch(t *testing.T) {
	var tests = []struct {
		description string
		rdr         RDR
		transProto  tcpip.TransportProtocolNumber
		dstAddr     tcpip.Address
		dstPort     uint16
		want        bool
	}{
		{
			"transProto is any; it matches TCP",
			RDR{
				dstAddr:         "\x0a\x00\x00\x00",
				dstPortRange:    PortRange{100, 101},
				newDstAddr:      "\x0b\x00\x00\x00",
				newDstPortRange: PortRange{100, 101},
			},
			header.TCPProtocolNumber,
			"\x0a\x00\x00\x00",
			100,
			true,
		},
		{
			"transProto is any; it matches UDP",
			RDR{
				dstAddr:         "\x0a\x00\x00\x00",
				dstPortRange:    PortRange{100, 101},
				newDstAddr:      "\x0b\x00\x00\x00",
				newDstPortRange: PortRange{100, 101},
			},
			header.UDPProtocolNumber,
			"\x0a\x00\x00\x00",
			100,
			true,
		},
		{
			"transProto is TCP",
			RDR{
				transProto:      header.TCPProtocolNumber,
				dstAddr:         "\x0a\x00\x00\x00",
				dstPortRange:    PortRange{100, 101},
				newDstAddr:      "\x0b\x00\x00\x00",
				newDstPortRange: PortRange{100, 101},
			},
			header.TCPProtocolNumber,
			"\x0a\x00\x00\x00",
			100,
			true,
		},
		{
			"transProto doesn't match",
			RDR{
				transProto:      header.TCPProtocolNumber,
				dstAddr:         "\x0a\x00\x00\x00",
				dstPortRange:    PortRange{100, 101},
				newDstAddr:      "\x0b\x00\x00\x00",
				newDstPortRange: PortRange{100, 101},
			},
			header.UDPProtocolNumber,
			"\x0a\x00\x00\x00",
			100,
			false,
		},
		{
			"dstAddr doesn't match",
			RDR{
				dstAddr:         "\x0a\x00\x00\x00",
				dstPortRange:    PortRange{100, 101},
				newDstAddr:      "\x0b\x00\x00\x00",
				newDstPortRange: PortRange{100, 101},
			},
			header.UDPProtocolNumber,
			"\x0a\x00\x00\x01",
			100,
			false,
		},
		{
			"dstPortRange doesn't contain the port",
			RDR{
				dstAddr:         "\x0a\x00\x00\x00",
				dstPortRange:    PortRange{100, 101},
				newDstAddr:      "\x0b\x00\x00\x00",
				newDstPortRange: PortRange{100, 101},
			},
			header.UDPProtocolNumber,
			"\x0a\x00\x00\x00",
			200,
			false,
		},
	}

	for _, test := range tests {
		t.Run(test.description, func(t *testing.T) {
			got := test.rdr.Match(test.transProto, test.dstAddr, test.dstPort)
			if got != test.want {
				t.Errorf("got=%t, want=%t", got, test.want)
			}
		})
	}
}
