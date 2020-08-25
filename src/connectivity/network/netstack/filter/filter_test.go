// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package filter

import (
	"math"
	"math/rand"
	"testing"
	"time"

	"gvisor.dev/gvisor/pkg/tcpip"
	"gvisor.dev/gvisor/pkg/tcpip/buffer"
	"gvisor.dev/gvisor/pkg/tcpip/header"
)

func TestRun(t *testing.T) {
	subnet, err := tcpip.NewSubnet("\x0a\x00\x00\x00", "\xff\x00\x00\x00")
	if err != nil {
		t.Fatal(err)
	}

	var tests = []struct {
		description string
		ruleset     []Rule
		dir         Direction
		netProto    tcpip.NetworkProtocolNumber
		packet      func() (buffer.View, buffer.VectorisedView)
		want        Action
	}{
		{
			"TcpDrop",
			[]Rule{
				{
					action:       Drop,
					direction:    Incoming,
					transProto:   header.TCPProtocolNumber,
					srcSubnet:    &subnet,
					srcPortRange: PortRange{100, 100},
					log:          testing.Verbose(),
				},
			},
			Incoming,
			header.IPv4ProtocolNumber,
			func() (buffer.View, buffer.VectorisedView) {
				return tcpV4Packet([]byte("payload"), &tcpParams{
					srcAddr: "\x0a\x00\x00\x00",
					srcPort: 100,
					dstAddr: "\x0a\x00\x00\x02",
					dstPort: 200,
				})
			},
			Drop,
		},
		{
			"TcpDropInRange",
			[]Rule{
				{
					action:       Drop,
					direction:    Incoming,
					transProto:   header.TCPProtocolNumber,
					srcSubnet:    &subnet,
					srcPortRange: PortRange{100, 101},
					log:          testing.Verbose(),
				},
			},
			Incoming,
			header.IPv4ProtocolNumber,
			func() (buffer.View, buffer.VectorisedView) {
				return tcpV4Packet([]byte("payload"), &tcpParams{
					srcAddr: "\x0a\x00\x00\x00",
					srcPort: 100,
					dstAddr: "\x0a\x00\x00\x02",
					dstPort: 200,
				})
			},
			Drop,
		},
		{
			"TcpDropInRange",
			[]Rule{
				{
					action:       Drop,
					direction:    Incoming,
					transProto:   header.TCPProtocolNumber,
					srcSubnet:    &subnet,
					srcPortRange: PortRange{100, 101},
					log:          testing.Verbose(),
				},
			},
			Incoming,
			header.IPv4ProtocolNumber,
			func() (buffer.View, buffer.VectorisedView) {
				return tcpV4Packet([]byte("payload"), &tcpParams{
					srcAddr: "\x0a\x00\x00\x00",
					srcPort: 101,
					dstAddr: "\x0a\x00\x00\x02",
					dstPort: 200,
				})
			},
			Drop,
		},
		{
			"TcpFragmentPass",
			[]Rule{
				{
					action:       Drop,
					direction:    Incoming,
					transProto:   header.TCPProtocolNumber,
					srcSubnet:    &subnet,
					srcPortRange: PortRange{100, 100},
					log:          testing.Verbose(),
				},
			},
			Incoming,
			header.IPv4ProtocolNumber,
			func() (buffer.View, buffer.VectorisedView) {
				headers, payload := tcpV4Packet([]byte("payload"), &tcpParams{
					srcAddr: "\x0a\x00\x00\x00",
					srcPort: 100,
					dstAddr: "\x0a\x00\x00\x02",
					dstPort: 200,
				})
				ip := header.IPv4(headers)
				ip.SetFlagsFragmentOffset(ip.Flags(), 8)
				return headers, payload
			},
			Pass,
		},
		{
			"UdpPass",
			[]Rule{
				{
					action:     Drop,
					direction:  Incoming,
					transProto: header.UDPProtocolNumber,
					log:        testing.Verbose(),
				},
				{
					action:       Pass,
					direction:    Incoming,
					transProto:   header.UDPProtocolNumber,
					srcSubnet:    &subnet,
					srcPortRange: PortRange{100, 100},
					log:          testing.Verbose(),
				},
			},
			Incoming,
			header.IPv4ProtocolNumber,
			func() (buffer.View, buffer.VectorisedView) {
				return udpV4Packet([]byte("payload"), &udpParams{
					srcAddr: "\x0a\x00\x00\x00",
					srcPort: 100,
					dstAddr: "\x0a\x00\x00\x02",
					dstPort: 200,
				})
			},
			Pass,
		},
		{
			"AnyDrop",
			[]Rule{
				{
					action:       Drop,
					direction:    Incoming,
					srcSubnet:    &subnet,
					srcPortRange: PortRange{100, 101},
					log:          testing.Verbose(),
				},
			},
			Incoming,
			header.IPv4ProtocolNumber,
			func() (buffer.View, buffer.VectorisedView) {
				return udpV4Packet([]byte("payload"), &udpParams{
					srcAddr: "\x0a\x00\x00\x00",
					srcPort: 100,
					dstAddr: "\x0a\x00\x00\x02",
					dstPort: 200,
				})
			},
			Drop,
		},
	}

	f := New(nil)

	for _, test := range tests {
		t.Run(test.description, func(t *testing.T) {
			f.rulesetMain.Lock()
			f.rulesetMain.v = test.ruleset
			f.rulesetMain.Unlock()
			hdr, payload := test.packet()
			if got := f.Run(test.dir, test.netProto, hdr, payload); got != test.want {
				t.Fatalf("wrong action, want %v, got %v", test.want, got)
			}
		})
	}
}

type Packet struct {
	hdr     buffer.View
	payload buffer.VectorisedView
}

func generateRandomUdp4Packet() Packet {
	buf := make([]byte, 4)
	rand.Read(buf)
	srcAddr := tcpip.Address(buf)
	rand.Read(buf)
	dstAddr := tcpip.Address(buf)
	p := udpParams{
		srcAddr: srcAddr,
		srcPort: uint16(rand.Int31n(math.MaxUint16)),
		dstAddr: dstAddr,
		dstPort: uint16(rand.Int31n(math.MaxUint16)),
	}
	hdr, payload := udpV4Packet([]byte("payload"), &p)
	return Packet{hdr, payload}
}

func BenchmarkFilterConcurrency(b *testing.B) {
	subnet, err := tcpip.NewSubnet("\x0a\x00\x00\x00", "\xff\x00\x00\x00")
	if err != nil {
		b.Fatal(err)
	}

	b.StopTimer()

	f := New(nil)

	f.rulesetMain.Lock()
	f.rulesetMain.v = []Rule{
		{
			action:       Drop,
			direction:    Incoming,
			transProto:   header.TCPProtocolNumber,
			srcSubnet:    &subnet,
			srcPortRange: PortRange{100, 100},
			log:          testing.Verbose(),
		},
	}
	f.rulesetMain.Unlock()

	// Unique number of src+dst combinations
	uniques := int(math.Ceil(float64(b.N) / 500.0))
	packets := make([]Packet, uniques)
	for n := range packets {
		packets[n] = generateRandomUdp4Packet()
	}

	b.SetParallelism(5)
	b.StartTimer()
	b.RunParallel(func(pb *testing.PB) {
		rng := rand.New(rand.NewSource(rand.Int63() + time.Now().UnixNano()))
		for pb.Next() {
			// x2 for incoming vs outgoing
			r := rng.Intn(uniques * 2)
			var dir Direction
			if r&1 == 0 {
				dir = Incoming
			} else {
				dir = Outgoing
			}
			r = r >> 1
			p := packets[r]
			hdr := p.hdr
			payload := p.payload
			f.Run(dir, header.IPv4ProtocolNumber, hdr, payload)
		}
	})
}
