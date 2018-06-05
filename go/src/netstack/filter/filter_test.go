// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package filter

import (
	"testing"

	"github.com/google/netstack/tcpip"
	"github.com/google/netstack/tcpip/buffer"
	"github.com/google/netstack/tcpip/header"
)

func ruleset1() ([]*Rule, error) {
	srcNet, err := tcpip.NewSubnet("\x0a\x00\x00\x00", "\xff\x00\x00\x00")
	if err != nil {
		return nil, err
	}
	return []*Rule{
		&Rule{
			action:     Drop,
			direction:  Incoming,
			transProto: header.TCPProtocolNumber,
			srcNet:     &srcNet,
			srcPort:    100,
			log:        true,
		},
	}, nil
}

func ruleset2() ([]*Rule, error) {
	srcNet, err := tcpip.NewSubnet("\x0a\x00\x00\x00", "\xff\x00\x00\x00")
	if err != nil {
		return nil, err
	}
	return []*Rule{
		&Rule{
			action:     Drop,
			direction:  Incoming,
			transProto: header.UDPProtocolNumber,
			log:        true,
		},
		&Rule{
			action:     Pass,
			direction:  Incoming,
			transProto: header.UDPProtocolNumber,
			srcNet:     &srcNet,
			srcPort:    100,
			log:        true,
		},
	}, nil
}

func TestRun(t *testing.T) {
	var tests = []struct {
		ruleset  func() ([]*Rule, error)
		dir      Direction
		netProto tcpip.NetworkProtocolNumber
		packet   func() []byte
		want     Action
	}{
		{
			ruleset1,
			Incoming,
			header.IPv4ProtocolNumber,
			func() []byte {
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
			ruleset2,
			Incoming,
			header.IPv4ProtocolNumber,
			func() []byte {
				return udpV4Packet([]byte("payload"), &udpParams{
					srcAddr: "\x0a\x00\x00\x00",
					srcPort: 100,
					dstAddr: "\x0a\x00\x00\x02",
					dstPort: 200,
				})
			},
			Pass,
		},
	}

	f := New(nil)

	for _, test := range tests {
		var err error
		f.rulesetMain.Lock()
		f.rulesetMain.v, err = test.ruleset()
		f.rulesetMain.Unlock()
		if err != nil {
			t.Fatalf("failed to generate a ruleset: %v", err)
		}
		b := test.packet()
		vv := buffer.NewVectorisedView(len(b), []buffer.View{b})
		a := f.Run(test.dir, test.netProto, &vv)
		if a != test.want {
			t.Fatalf("wrong action, want %v, got %v", test.want, a)
		}
	}
}
