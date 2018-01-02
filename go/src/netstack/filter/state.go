// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package filter

import (
	"github.com/google/netstack/tcpip"
)

// Key is a key for the connection state maps.
type Key struct {
	transProto tcpip.TransportProtocolNumber
	srcAddr    tcpip.Address
	srcPort    uint16
	dstAddr    tcpip.Address
	dstPort    uint16
}

type State struct {
	// TODO
}

// Connection state maps.
var statesIn, statesOut map[Key]*State

func findState(dir Direction, transProto tcpip.TransportProtocolNumber, srcAddr tcpip.Address, srcPort uint16, dstAddr tcpip.Address, dstPort uint16) *State {
	if dir == Incoming {
		return statesIn[Key{transProto, srcAddr, srcPort, dstAddr, dstPort}]
	} else {
		return statesOut[Key{transProto, srcAddr, srcPort, dstAddr, dstPort}]
	}
}
