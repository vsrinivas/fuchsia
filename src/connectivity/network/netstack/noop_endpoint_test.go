// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package netstack

import (
	"fidl/fuchsia/hardware/network"

	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/link"

	"gvisor.dev/gvisor/pkg/tcpip"
	"gvisor.dev/gvisor/pkg/tcpip/buffer"
	"gvisor.dev/gvisor/pkg/tcpip/header"
	"gvisor.dev/gvisor/pkg/tcpip/stack"
)

var _ stack.LinkEndpoint = (*noopEndpoint)(nil)

type noopEndpoint struct {
	linkAddress tcpip.LinkAddress
	attached    chan struct{}
}

func (*noopEndpoint) MTU() uint32 {
	return 0
}

func (*noopEndpoint) Capabilities() stack.LinkEndpointCapabilities {
	return 0
}

func (*noopEndpoint) MaxHeaderLength() uint16 {
	return 0
}

func (ep *noopEndpoint) LinkAddress() tcpip.LinkAddress {
	return ep.linkAddress
}

func (*noopEndpoint) WritePacket(*stack.Route, *stack.GSO, tcpip.NetworkProtocolNumber, *stack.PacketBuffer) *tcpip.Error {
	return nil
}

func (*noopEndpoint) WritePackets(*stack.Route, *stack.GSO, stack.PacketBufferList, tcpip.NetworkProtocolNumber) (int, *tcpip.Error) {
	return 0, nil
}

func (*noopEndpoint) WriteRawPacket(buffer.VectorisedView) *tcpip.Error {
	return nil
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

func (*noopController) SetPromiscuousMode(v bool) error {
	return nil
}

func (*noopController) DeviceClass() network.DeviceClass {
	return network.DeviceClassUnknown
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

func addNoopEndpoint(ns *Netstack, name string) (*ifState, error) {
	return ns.addEndpoint(
		makeEndpointName("test", name),
		&noopEndpoint{},
		&noopController{},
		nil,  /* observer */
		true, /* doFilter */
		0,    /* metric */
	)
}
