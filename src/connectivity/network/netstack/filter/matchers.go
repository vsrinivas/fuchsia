// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//go:build !build_with_native_toolchain

package filter

import (
	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/sync"

	"gvisor.dev/gvisor/pkg/tcpip"
	"gvisor.dev/gvisor/pkg/tcpip/header"
	"gvisor.dev/gvisor/pkg/tcpip/stack"
)

var _ stack.Matcher = (*filterDisabledNICMatcher)(nil)

// filterDisabledNICMatcher is a stack.Matcher that keeps track of which
// NICs have filtering disabled.
//
// All NICs are considered to have filtering disabled until explicitly
// enabled.
type filterDisabledNICMatcher struct {
	mu struct {
		sync.RWMutex
		// nicNames are the NICs where the filter is enabled on.
		nicNames map[string]tcpip.NICID
	}
}

// Name implements stack.Matcher.
func (*filterDisabledNICMatcher) Name() string {
	return "filterDisabledNICMatcher"
}

// Match implements stack.Matcher.
func (d *filterDisabledNICMatcher) Match(hook stack.Hook, _ stack.PacketBufferPtr, inNicName, outNicName string) (matches bool, hotdrop bool) {
	if inNicName != "" && d.nicDisabled(inNicName) {
		return true, false
	}
	if outNicName != "" && d.nicDisabled(outNicName) {
		return true, false
	}
	return false, false
}

func (d *filterDisabledNICMatcher) init() {
	d.mu.Lock()
	defer d.mu.Unlock()
	d.mu.nicNames = make(map[string]tcpip.NICID)
}

func (d *filterDisabledNICMatcher) nicDisabled(name string) bool {
	d.mu.RLock()
	defer d.mu.RUnlock()
	_, ok := d.mu.nicNames[name]
	return !ok
}

// portMatcher matches port to some range.
type portMatcher struct {
	start uint16
	end   uint16
}

func (m *portMatcher) match(port uint16) bool {
	return port >= m.start && port <= m.end
}

func NewTCPSourcePortMatcher(start, end uint16) *TCPSourcePortMatcher {
	return &TCPSourcePortMatcher{portMatcher: portMatcher{start, end}}
}

var _ stack.Matcher = (*TCPSourcePortMatcher)(nil)

// TCPSourcePortMatcher matches TCP packets and ports.
type TCPSourcePortMatcher struct {
	portMatcher
}

// Name implements stack.Matcher.
func (*TCPSourcePortMatcher) Name() string {
	return "TCPSourcePortMatcher"
}

// Match implements stack.Matcher.
func (m *TCPSourcePortMatcher) Match(_ stack.Hook, pkt stack.PacketBufferPtr, _, _ string) (matches bool, hotdrop bool) {
	tcp := header.TCP(pkt.TransportHeader().Slice())
	if len(tcp) < header.TCPMinimumSize {
		// Drop immediately as the packet is invalid.
		return false, true
	}

	return m.portMatcher.match(tcp.SourcePort()), false
}

func NewTCPDestinationPortMatcher(start, end uint16) *TCPDestinationPortMatcher {
	return &TCPDestinationPortMatcher{portMatcher: portMatcher{start, end}}
}

var _ stack.Matcher = (*TCPDestinationPortMatcher)(nil)

// TCPDestinationPortMatcher matches TCP packets and ports.
type TCPDestinationPortMatcher struct {
	portMatcher
}

// Name implements stack.Matcher.
func (*TCPDestinationPortMatcher) Name() string {
	return "TCPDestinationPortMatcher"
}

// Match implements stack.Matcher.
func (m *TCPDestinationPortMatcher) Match(_ stack.Hook, pkt stack.PacketBufferPtr, _, _ string) (matches bool, hotdrop bool) {
	tcp := header.TCP(pkt.TransportHeader().Slice())
	if len(tcp) < header.TCPMinimumSize {
		// Drop immediately as the packet is invalid.
		return false, true
	}

	return m.portMatcher.match(tcp.DestinationPort()), false
}

func NewUDPSourcePortMatcher(start, end uint16) *UDPSourcePortMatcher {
	return &UDPSourcePortMatcher{portMatcher: portMatcher{start, end}}
}

var _ stack.Matcher = (*UDPSourcePortMatcher)(nil)

// UDPSourcePortMatcher matches UDP packets and ports.
type UDPSourcePortMatcher struct {
	portMatcher
}

// Name implements stack.Matcher.
func (*UDPSourcePortMatcher) Name() string {
	return "UDPSourcePortMatcher"
}

// Match implements stack.Matcher.
func (m *UDPSourcePortMatcher) Match(_ stack.Hook, pkt stack.PacketBufferPtr, _, _ string) (matches bool, hotdrop bool) {
	udp := header.UDP(pkt.TransportHeader().Slice())
	if len(udp) < header.UDPMinimumSize {
		// Drop immediately as the packet is invalid.
		return false, true
	}

	return m.portMatcher.match(udp.SourcePort()), false
}

func NewUDPDestinationPortMatcher(start, end uint16) *UDPDestinationPortMatcher {
	return &UDPDestinationPortMatcher{portMatcher: portMatcher{start, end}}
}

var _ stack.Matcher = (*UDPDestinationPortMatcher)(nil)

// UDPDestinationPortMatcher matches UDP packets and ports.
type UDPDestinationPortMatcher struct {
	portMatcher
}

// Name implements stack.Matcher.
func (*UDPDestinationPortMatcher) Name() string {
	return "UDPDestinationPortMatcher"
}

// Match implements stack.Matcher.
func (m *UDPDestinationPortMatcher) Match(_ stack.Hook, pkt stack.PacketBufferPtr, _, _ string) (matches bool, hotdrop bool) {
	udp := header.UDP(pkt.TransportHeader().Slice())
	if len(udp) < header.UDPMinimumSize {
		// Drop immediately as the packet is invalid.
		return false, true
	}

	return m.portMatcher.match(udp.DestinationPort()), false
}
