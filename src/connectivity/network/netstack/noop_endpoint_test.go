// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//go:build !build_with_native_toolchain

package netstack

import (
	"testing"

	"fidl/fuchsia/hardware/network"
	"fidl/fuchsia/net/interfaces"

	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/link"
	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/sync"

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

func (*noopEndpoint) WritePackets(pkts stack.PacketBufferList) (int, tcpip.Error) {
	return pkts.Len(), nil
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

func (*noopEndpoint) AddHeader(stack.PacketBufferPtr) {}

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

func (*noopController) ConnectPort(port network.PortWithCtxInterfaceRequest) {
	_ = port.Close()
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

func addLinkEndpoint(t *testing.T, ns *Netstack, name string, ep stack.LinkEndpoint) *ifState {
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
		ep,
		&noopController{},
		nil,                    /* observer */
		defaultInterfaceMetric, /* metric */
		qdiscConfig{},
	)
	if err != nil {
		t.Fatal(err)
	}
	return ifs
}

func addNoopEndpoint(t *testing.T, ns *Netstack, name string) *ifState {
	t.Helper()
	return addLinkEndpoint(t, ns, name, &noopEndpoint{})
}

var _ stack.LinkEndpoint = (*sentinelEndpoint)(nil)

type waitPair struct {
	waitFor uint
	ch      chan struct{}
}

type sentinelEndpoint struct {
	noopEndpoint
	mu struct {
		sync.Mutex
		pkts          stack.PacketBufferList
		blocking      bool
		totalEnqueued uint
		waiters       []waitPair
	}
}

func (ep *sentinelEndpoint) waitForLocked(amount uint) chan struct{} {
	amount += uint(ep.mu.totalEnqueued)
	ch := make(chan struct{})
	ep.mu.waiters = append(ep.mu.waiters, waitPair{waitFor: amount, ch: ch})
	ep.signalWaitersLocked()
	return ch
}

func (ep *sentinelEndpoint) WaitFor(amount uint) chan struct{} {
	ep.mu.Lock()
	defer ep.mu.Unlock()
	return ep.waitForLocked(amount)
}

func (ep *sentinelEndpoint) signalWaitersLocked() {
	newWaiters := ep.mu.waiters[:0]
	for _, waiter := range ep.mu.waiters {
		if ep.mu.totalEnqueued >= waiter.waitFor {
			close(waiter.ch)
		} else {
			newWaiters = append(newWaiters, waiter)
		}
	}
	ep.mu.waiters = newWaiters
}

func (ep *sentinelEndpoint) WritePackets(pkts stack.PacketBufferList) (int, tcpip.Error) {
	ep.mu.Lock()
	defer ep.mu.Unlock()
	if ep.mu.blocking {
		for _, pb := range pkts.AsSlice() {
			pb.IncRef()
			ep.mu.pkts.PushBack(pb)
		}
	}
	ep.mu.totalEnqueued += uint(pkts.Len())
	ep.signalWaitersLocked()
	return ep.noopEndpoint.WritePackets(pkts)
}

func (ep *sentinelEndpoint) Drain() (uint, chan struct{}) {
	ep.mu.Lock()
	defer ep.mu.Unlock()
	drained := ep.drainLocked()
	return drained, ep.waitForLocked(drained)
}

func (ep *sentinelEndpoint) drainLocked() uint {
	drained := ep.mu.pkts.Len()
	ep.mu.pkts.Reset()
	return uint(drained)
}

func (ep *sentinelEndpoint) Enqueued() uint {
	ep.mu.Lock()
	defer ep.mu.Unlock()
	return ep.mu.totalEnqueued
}

func (ep *sentinelEndpoint) SetBlocking(blocking bool) {
	ep.mu.Lock()
	defer ep.mu.Unlock()
	ep.mu.blocking = blocking
	if !ep.mu.blocking {
		ep.drainLocked()
	}
}
