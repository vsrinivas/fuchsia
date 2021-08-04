// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//go:build !build_with_native_toolchain
// +build !build_with_native_toolchain

package netstack

import (
	"testing"

	"fidl/fuchsia/hardware/network"
	"fidl/fuchsia/net/interfaces"

	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/link"

	"gvisor.dev/gvisor/pkg/tcpip"
	"gvisor.dev/gvisor/pkg/tcpip/header"
	"gvisor.dev/gvisor/pkg/tcpip/stack"
)

var _ stack.LinkEndpoint = (*noopEndpoint)(nil)

type noopEndpoint struct {
	capabilities stack.LinkEndpointCapabilities
	linkAddress  tcpip.LinkAddress
	attached     chan struct{}
}

func (*noopEndpoint) MTU() uint32 {
	return header.IPv4MinimumMTU
}

func (ep *noopEndpoint) Capabilities() stack.LinkEndpointCapabilities {
	return ep.capabilities
}

func (*noopEndpoint) MaxHeaderLength() uint16 {
	return 0
}

func (ep *noopEndpoint) LinkAddress() tcpip.LinkAddress {
	return ep.linkAddress
}

func (*noopEndpoint) WritePacket(stack.RouteInfo, tcpip.NetworkProtocolNumber, *stack.PacketBuffer) tcpip.Error {
	return nil
}

func (*noopEndpoint) WritePackets(stack.RouteInfo, stack.PacketBufferList, tcpip.NetworkProtocolNumber) (int, tcpip.Error) {
	return 0, nil
}

func (ep *noopEndpoint) Attach(dispatcher stack.NetworkDispatcher) {
	if dispatcher != nil {
		ep.attached = make(chan struct{})
	} else {
		if ch := ep.attached; ch != nil {
			close(ch)
		}
		ep.attached = nil
	}
}

func (ep *noopEndpoint) IsAttached() bool {
	return ep.attached != nil
}

func (ep *noopEndpoint) Wait() {
	if ch := ep.attached; ch != nil {
		<-ch
	}
}

func (ep *noopEndpoint) ARPHardwareType() header.ARPHardwareType {
	return header.ARPHardwareNone
}

func (*noopEndpoint) AddHeader(_, _ tcpip.LinkAddress, _ tcpip.NetworkProtocolNumber, _ *stack.PacketBuffer) {
}

var _ link.Controller = (*noopController)(nil)

type noopController struct {
	onUp func()
}

func (n *noopController) Up() error {
	if fn := n.onUp; fn != nil {
		fn()
	}
	return nil
}

func (*noopController) Down() error {
	return nil
}

func (*noopController) SetPromiscuousMode(_ bool) error {
	return nil
}

func (*noopController) DeviceClass() network.DeviceClass {
	return network.DeviceClassVirtual
}

var _ link.Observer = (*noopObserver)(nil)

type noopObserver struct {
	onLinkClosed        func()
	onLinkOnlineChanged func(bool)
}

func (n *noopObserver) SetOnLinkClosed(fn func()) {
	n.onLinkClosed = fn
}

func (n *noopObserver) SetOnLinkOnlineChanged(fn func(bool)) {
	n.onLinkOnlineChanged = fn
}

func addNoopEndpoint(t *testing.T, ns *Netstack, name string) *ifState {
	t.Helper()
	ifs, err := ns.addEndpoint(
		func(nicid tcpip.NICID) string {
			prefix := t.Name()
			for {
				candidate := makeEndpointName(prefix, name)(nicid)
				if overflow := len(candidate) - int(interfaces.InterfaceNameLength); overflow > 0 {
					prefix = prefix[:len(prefix)-overflow]
					continue
				}
				return candidate
			}
		},
		&noopEndpoint{},
		&noopController{},
		nil, /* observer */
		0,   /* metric */
	)
	if err != nil {
		t.Fatal(err)
	}
	return ifs
}
