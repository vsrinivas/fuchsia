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

const (
	srcAddr = "\x0a\x00\x00\x00"
	dstAddr = "\x0a\x00\x00\x02"
	subnet  = "\x0a\x00\x00\x00"
	netmask = "\xff\x00\x00\x00"
)

func ruleset1() ([]*Rule, error) {
	srcNet, err := tcpip.NewSubnet(subnet, netmask)
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
	srcNet, err := tcpip.NewSubnet(subnet, netmask)
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

func tcpPacket1() (tcpip.NetworkProtocolNumber, []byte, buffer.View) {
	return tcpV4Packet([]byte("payload"), &tcpParams{
		srcPort: 100,
		dstPort: 200,
	})
}

func udpPacket1() (tcpip.NetworkProtocolNumber, []byte, buffer.View) {
	return udpV4Packet([]byte("payload"), &udpParams{
		srcPort: 100,
		dstPort: 200,
	})
}

func TestRun(t *testing.T) {
	rulesetMain.RLock()
	saved := rulesetMain.v
	rulesetMain.RUnlock()
	defer func() {
		rulesetMain.Lock()
		rulesetMain.v = saved
		rulesetMain.Unlock()
	}()

	var tests = []struct {
		ruleset func() ([]*Rule, error)
		dir     Direction
		packet  func() (tcpip.NetworkProtocolNumber, []byte, buffer.View)
		want    Action
	}{
		{ruleset1, Incoming, tcpPacket1, Drop},
		{ruleset2, Incoming, udpPacket1, Pass},
	}

	for _, test := range tests {
		netProto, hdr, pl := test.packet()
		var err error
		rulesetMain.Lock()
		rulesetMain.v, err = test.ruleset()
		rulesetMain.Unlock()
		if err != nil {
			t.Fatalf("failed to generate a ruleset: %v", err)
		}
		a := Run(test.dir, netProto, hdr, pl)
		if a != test.want {
			t.Fatalf("wrong action, want %v, got %v", test.want, a)
		}
	}
}
