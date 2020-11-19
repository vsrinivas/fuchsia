// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package filter

// endpointState represents the connection state of an endpoint.
type endpointState int

const (
	// Note that we currently allow numeric comparison between two
	// EndpointStates so that the state related logic can be described
	// compactly. We assume an endpointState's numeric value will only
	// increase monotonically during the lifetime of the endpoint
	// (e.g. TCPFirstPacket => TCPOpening => TCPEstablished => TCPClosing =>
	// TCPFinWait => TCPClosed).

	// ICMP states.
	// (TODO: consider more definitions.)
	ICMPFirstPacket endpointState = iota

	// UDP states.
	UDPFirstPacket
	UDPSingle
	UDPMultiple

	// TCP states.
	TCPFirstPacket
	TCPOpening
	TCPEstablished
	TCPClosing
	TCPFinWait
	TCPClosed
)
