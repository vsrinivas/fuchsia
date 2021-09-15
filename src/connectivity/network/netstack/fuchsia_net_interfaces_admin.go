// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//go:build !build_with_native_toolchain
// +build !build_with_native_toolchain

package netstack

import (
	"context"
	"errors"
	"fmt"
	"strings"
	"sync"
	"syscall/zx"
	"syscall/zx/fidl"

	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/time"
	"go.fuchsia.dev/fuchsia/src/lib/component"
	syslog "go.fuchsia.dev/fuchsia/src/lib/syslog/go"

	"fidl/fuchsia/net"
	"fidl/fuchsia/net/interfaces/admin"

	"gvisor.dev/gvisor/pkg/tcpip"
	"gvisor.dev/gvisor/pkg/tcpip/header"
	"gvisor.dev/gvisor/pkg/tcpip/network/ipv4"
	"gvisor.dev/gvisor/pkg/tcpip/network/ipv6"
)

const (
	addressStateProviderName = "fuchsia.net.interfaces.admin/AddressStateProvider"
	controlName              = "fuchsia.net.interfaces.admin/Control"
)

type addressStateProviderCollection struct {
	nicid tcpip.NICID
	mu    struct {
		sync.Mutex
		providers map[tcpip.Address]*adminAddressStateProviderImpl
	}
}

// Called when DAD completes.
//
// Note that `online` must not change when calling this function (`ifState.mu`
// must be held).
func (pc *addressStateProviderCollection) onDuplicateAddressDetectionCompleteLocked(nicid tcpip.NICID, addr tcpip.Address, online, success bool) {
	pi, ok := pc.mu.providers[addr]
	if !ok {
		return
	}
	pi.mu.Lock()
	defer pi.mu.Unlock()

	if success {
		// If DAD completed successfully but the interface is currently offline, do
		// not set the assignment state to ASSIGNED.
		if !online {
			_ = syslog.Warnf("interface %d is offline when DAD completed for %s", pc.nicid, addr)
		} else {
			pi.setStateLocked(admin.AddressAssignmentStateAssigned)
		}
	} else {
		delete(pc.mu.providers, addr)
		pi.onRemoveLocked(admin.AddressRemovalReasonDadFailed)
	}
}

func (pc *addressStateProviderCollection) onAddressRemove(addr tcpip.Address) {
	pc.mu.Lock()
	defer pc.mu.Unlock()

	if pi, ok := pc.mu.providers[addr]; ok {
		delete(pc.mu.providers, addr)
		pi.onRemove(admin.AddressRemovalReasonUserRemoved)
	}
}

func (pc *addressStateProviderCollection) onInterfaceRemove() {
	pc.mu.Lock()
	defer pc.mu.Unlock()

	for addr, pi := range pc.mu.providers {
		delete(pc.mu.providers, addr)
		pi.onRemove(admin.AddressRemovalReasonInterfaceRemoved)
	}
}

// TODO(https://fxbug.dev/82045): Avoid racing interface up/down against DAD.
// Note that this function should be called while holding a lock which prevents
// `online` from mutating.
func (pc *addressStateProviderCollection) onInterfaceOnlineChangeLocked(online bool) {
	for _, pi := range pc.mu.providers {
		pi.setInitialState(online)
	}
}

var _ admin.AddressStateProviderWithCtx = (*adminAddressStateProviderImpl)(nil)

type adminAddressStateProviderImpl struct {
	cancelServe  context.CancelFunc
	ready        chan struct{}
	protocolAddr tcpip.ProtocolAddress
	mu           struct {
		sync.Mutex
		eventProxy admin.AddressStateProviderEventProxy
		isHanging  bool
		// NB: lastObserved is the zero value iff the client has yet to observe the
		// state for the first time.
		state, lastObserved admin.AddressAssignmentState
		detached            bool
	}
}

func (pi *adminAddressStateProviderImpl) setInitialState(online bool) {
	pi.mu.Lock()
	defer pi.mu.Unlock()

	// If DAD won the race and set the assignment state to ASSIGNED already,
	// leave the state as ASSIGNED rather than setting it to TENTATIVE.
	if online && pi.mu.state == admin.AddressAssignmentStateAssigned {
		_ = syslog.WarnTf(addressStateProviderName, "%s already in ASSIGNED state when interface became online", pi.protocolAddr.AddressWithPrefix.Address)
	} else {
		// TODO(https://fxbug.dev/82045): Don't assume that DAD is always enabled.
		pi.setStateLocked(initialAddressAssignmentState(pi.protocolAddr, online))
	}
}

func (pi *adminAddressStateProviderImpl) setStateLocked(state admin.AddressAssignmentState) {
	pi.mu.state = state
	if pi.mu.lastObserved != pi.mu.state {
		syslog.DebugTf(addressStateProviderName, "address %+v state changed from %s to %s", pi.protocolAddr, pi.mu.lastObserved, pi.mu.state)
		select {
		case pi.ready <- struct{}{}:
		default:
		}
	}
}

func (pi *adminAddressStateProviderImpl) onRemove(reason admin.AddressRemovalReason) {
	pi.mu.Lock()
	defer pi.mu.Unlock()

	pi.onRemoveLocked(reason)
}

func (pi *adminAddressStateProviderImpl) onRemoveLocked(reason admin.AddressRemovalReason) {
	if err := pi.mu.eventProxy.OnAddressRemoved(reason); err != nil {
		var zxError *zx.Error
		if !errors.As(err, &zxError) || (zxError.Status != zx.ErrPeerClosed && zxError.Status != zx.ErrBadHandle) {
			_ = syslog.WarnTf(addressStateProviderName, "failed to send OnAddressRemoved(%s) for %s: %s", reason, pi.protocolAddr.AddressWithPrefix.Address, err)
		}
	}
	pi.cancelServe()
}

// TODO(https://fxbug.dev/80621): Add support for updating an address's
// properties (valid and expected lifetimes).
func (pi *adminAddressStateProviderImpl) UpdateAddressProperties(_ fidl.Context, addressProperties admin.AddressProperties) error {
	_ = syslog.WarnTf(addressStateProviderName, "UpdateAddressProperties for %s: not supported", pi.protocolAddr.AddressWithPrefix.Address)

	pi.onRemove(admin.AddressRemovalReasonUserRemoved)
	return nil
}

func (pi *adminAddressStateProviderImpl) Detach(fidl.Context) error {
	pi.mu.Lock()
	defer pi.mu.Unlock()

	pi.mu.detached = true
	return nil
}

func (pi *adminAddressStateProviderImpl) WatchAddressAssignmentState(ctx fidl.Context) (admin.AddressAssignmentState, error) {
	pi.mu.Lock()
	defer pi.mu.Unlock()

	if pi.mu.isHanging {
		pi.cancelServe()
		return 0, errors.New("not allowed to call WatchAddressAssignmentState when a call is already in progress")
	}

	for {
		if pi.mu.lastObserved != pi.mu.state {
			state := pi.mu.state
			pi.mu.lastObserved = state
			syslog.DebugTf(addressStateProviderName, "address %+v observed state: %s", pi.protocolAddr, state)
			return state, nil
		}

		pi.mu.isHanging = true
		pi.mu.Unlock()

		var err error
		select {
		case <-pi.ready:
		case <-ctx.Done():
			err = fmt.Errorf("cancelled: %w", ctx.Err())
		}

		pi.mu.Lock()
		pi.mu.isHanging = false
		if err != nil {
			return 0, err
		}
	}
}

func interfaceAddressToProtocolAddress(addr net.InterfaceAddress) tcpip.ProtocolAddress {
	var protocolAddr tcpip.ProtocolAddress
	switch tag := addr.Which(); tag {
	case net.InterfaceAddressIpv4:
		protocolAddr.Protocol = ipv4.ProtocolNumber
		protocolAddr.AddressWithPrefix.Address = tcpip.Address(addr.Ipv4.Addr.Addr[:])
		protocolAddr.AddressWithPrefix.PrefixLen = int(addr.Ipv4.PrefixLen)
	case net.InterfaceAddressIpv6:
		protocolAddr.Protocol = ipv6.ProtocolNumber
		protocolAddr.AddressWithPrefix.Address = tcpip.Address(addr.Ipv6.Addr[:])
		// TODO(https://fxbug.dev/81929): Don't lie about the prefix length to gVisor.
		protocolAddr.AddressWithPrefix.PrefixLen = 8 * header.IPv6AddressSize
	default:
		panic(fmt.Sprintf("unknown address: %#v", addr))
	}
	return protocolAddr
}

func initialAddressAssignmentState(protocolAddr tcpip.ProtocolAddress, online bool) admin.AddressAssignmentState {
	if !online {
		return admin.AddressAssignmentStateUnavailable
	}
	switch protocolAddr.Protocol {
	case header.IPv4ProtocolNumber:
		return admin.AddressAssignmentStateAssigned
	case header.IPv6ProtocolNumber:
		return admin.AddressAssignmentStateTentative
	default:
		panic(fmt.Sprintf("unknown protocol in address %s: %d", protocolAddr.AddressWithPrefix, protocolAddr.Protocol))
	}
}

var _ admin.ControlWithCtx = (*adminControlImpl)(nil)

type adminControlImpl struct {
	ns          *Netstack
	nicid       tcpip.NICID
	cancelServe context.CancelFunc
}

func (ci *adminControlImpl) AddAddress(_ fidl.Context, interfaceAddr net.InterfaceAddress, parameters admin.AddressParameters, request admin.AddressStateProviderWithCtxInterfaceRequest) error {
	protocolAddr := interfaceAddressToProtocolAddress(interfaceAddr)
	addr := protocolAddr.AddressWithPrefix.Address

	nicInfo, ok := ci.ns.stack.NICInfo()[ci.nicid]
	if !ok {
		return fmt.Errorf("interface %d cannot be found", ci.nicid)
	}
	ifs := nicInfo.Context.(*ifState)

	ctx, cancel := context.WithCancel(context.Background())
	impl := adminAddressStateProviderImpl{
		ready:        make(chan struct{}, 1),
		cancelServe:  cancel,
		protocolAddr: protocolAddr,
	}
	impl.mu.eventProxy.Channel = request.Channel

	if reason := func() admin.AddressRemovalReason {
		// TODO(https://fxbug.dev/80621): Add support for address lifetimes.
		var b strings.Builder
		if parameters.HasTemporary() {
			b.WriteString(fmt.Sprintf(" temporary=%t", parameters.GetTemporary()))
		}
		if parameters.HasInitialProperties() {
			properties := parameters.GetInitialProperties()
			if properties.HasPreferredLifetimeInfo() {
				b.WriteString(fmt.Sprintf(" preferredLifetimeInfo=%#v", properties.GetPreferredLifetimeInfo()))
			}
			if properties.HasValidLifetimeEnd() {
				b.WriteString(fmt.Sprintf(" validLifetimeEnd=%s", time.Monotonic(properties.GetValidLifetimeEnd())))
			}
		}
		if unsupportedProperties := b.String(); unsupportedProperties != "" {
			_ = syslog.WarnTf(controlName, "AddAddress called with unsupported parameters:%s", unsupportedProperties)
			return admin.AddressRemovalReasonInvalid
		}

		if protocolAddr.AddressWithPrefix.PrefixLen > 8*len(protocolAddr.AddressWithPrefix.Address) {
			return admin.AddressRemovalReasonInvalid
		}

		// NB: Must lock `ifState.mu` and then `addressStateProviderCollection.mu`
		// to prevent interface online changes (which acquire the same locks in
		// the same order) from interposing a modification to address assignment
		// state before the `impl` is inserted into the collection.
		//
		// `ifState.mu` is released as soon as possible to avoid deadlock issues.
		online := func() bool {
			ifs.mu.Lock()
			defer ifs.mu.Unlock()

			ifs.addressStateProviders.mu.Lock()
			return ifs.IsUpLocked()
		}()
		defer ifs.addressStateProviders.mu.Unlock()

		if _, ok := ifs.addressStateProviders.mu.providers[addr]; ok {
			return admin.AddressRemovalReasonAlreadyAssigned
		}

		switch status := ci.ns.addInterfaceAddress(ci.nicid, protocolAddr, false /* addRoute */); status {
		case zx.ErrOk:
			impl.mu.state = initialAddressAssignmentState(protocolAddr, online)
			syslog.DebugTf(addressStateProviderName, "initial state for %+v: %s", protocolAddr, impl.mu.state)
			ifs.addressStateProviders.mu.providers[addr] = &impl
			return 0
		case zx.ErrInvalidArgs:
			return admin.AddressRemovalReasonInvalid
		case zx.ErrNotFound:
			return admin.AddressRemovalReasonInterfaceRemoved
		case zx.ErrAlreadyExists:
			return admin.AddressRemovalReasonAlreadyAssigned
		default:
			panic(fmt.Errorf("unexpected internal AddAddress error %s", status))
			return admin.AddressRemovalReasonUserRemoved
		}
	}(); reason != 0 {
		if err := impl.mu.eventProxy.OnAddressRemoved(reason); err != nil {
			var zxError *zx.Error
			if !errors.As(err, &zxError) || (zxError.Status != zx.ErrPeerClosed && zxError.Status != zx.ErrBadHandle) {
				_ = syslog.WarnTf(controlName, "failed to send OnAddressRemoved(%s) for %s: %s", reason, protocolAddr.AddressWithPrefix.Address, err)
			}
		}
		if err := impl.mu.eventProxy.Close(); err != nil {
			_ = syslog.WarnTf(controlName, "failed to close %s channel", addressStateProviderName)
		}
		return nil
	}

	go func() {
		component.ServeExclusiveConcurrent(ctx, &admin.AddressStateProviderWithCtxStub{Impl: &impl}, request.Channel, func(err error) {
			_ = syslog.WarnTf(addressStateProviderName, "address state provider for %s: %s", addr, err)
		})

		if pi, ok := func() (*adminAddressStateProviderImpl, bool) {
			ifs.addressStateProviders.mu.Lock()
			defer ifs.addressStateProviders.mu.Unlock()

			// The impl may have already been removed due to address removal.
			pi, ok := ifs.addressStateProviders.mu.providers[addr]
			if ok {
				// Removing the address will also attempt to delete from
				// the address state providers map, so delete from it and unlock
				// immediately to avoid lock ordering and deadlock issues.
				delete(ifs.addressStateProviders.mu.providers, addr)
			}
			return pi, ok
		}(); ok {
			pi.mu.Lock()
			remove := !pi.mu.detached
			pi.mu.Unlock()

			if remove {
				// NB: Removing the address will also attempt to access the address state
				// provider collection and delete the impl out of it if found. The lock
				// on the collection must not be held at this point to prevent the
				// deadlock.
				if status := ci.ns.removeInterfaceAddress(ci.nicid, pi.protocolAddr, false /* removeRoute */); status != zx.ErrOk && status != zx.ErrNotFound {
					// If address has already been removed, don't consider it an error.
					_ = syslog.ErrorTf(addressStateProviderName, "failed to remove address %s on channel closure: %s", addr, status)
				}
			}
		}
	}()
	return nil
}

func (ci *adminControlImpl) RemoveAddress(_ fidl.Context, address net.InterfaceAddress) (admin.ControlRemoveAddressResult, error) {
	protocolAddr := interfaceAddressToProtocolAddress(address)
	if zxErr := ci.ns.removeInterfaceAddress(ci.nicid, protocolAddr, false /* removeRoute */); zxErr != zx.ErrOk {
		return admin.ControlRemoveAddressResultWithErr(int32(zxErr)), nil
	}
	return admin.ControlRemoveAddressResultWithResponse(admin.ControlRemoveAddressResponse{}), nil
}

type adminControlCollection struct {
	ns *Netstack

	mu struct {
		sync.Mutex
		controls map[*adminControlImpl]struct{}
	}
}

func (c *adminControlCollection) onInterfaceRemove() {
	c.mu.Lock()
	controls := c.mu.controls
	c.mu.controls = nil
	c.mu.Unlock()

	for control := range controls {
		control.cancelServe()
	}
}

func (c *adminControlCollection) addImpl(ctx context.Context, impl *adminControlImpl, request admin.ControlWithCtxInterfaceRequest) {
	c.mu.Lock()
	c.mu.controls[impl] = struct{}{}
	c.mu.Unlock()

	go func() {
		component.ServeExclusive(ctx, &admin.ControlWithCtxStub{Impl: impl}, request.Channel, func(err error) {
			_ = syslog.WarnTf("fuchsia.net.interfaces.admin/Control", "%s", err)
		})

		c.mu.Lock()
		delete(c.mu.controls, impl)
		c.mu.Unlock()
	}()
}
