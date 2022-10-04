// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//go:build !build_with_native_toolchain

package netstack

import (
	"context"
	"errors"
	"fmt"
	"syscall/zx"
	"syscall/zx/fidl"

	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/fidlconv"
	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/link/netdevice"
	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/routes"
	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/sync"
	"go.fuchsia.dev/fuchsia/src/lib/component"
	syslog "go.fuchsia.dev/fuchsia/src/lib/syslog/go"

	"fidl/fuchsia/hardware/network"
	"fidl/fuchsia/net"
	"fidl/fuchsia/net/interfaces/admin"

	"gvisor.dev/gvisor/pkg/tcpip"
	"gvisor.dev/gvisor/pkg/tcpip/header"
	"gvisor.dev/gvisor/pkg/tcpip/link/ethernet"
	"gvisor.dev/gvisor/pkg/tcpip/network/ipv4"
	"gvisor.dev/gvisor/pkg/tcpip/network/ipv6"
	"gvisor.dev/gvisor/pkg/tcpip/stack"
)

const (
	addressStateProviderName = "fuchsia.net.interfaces.admin/AddressStateProvider"
	controlName              = "fuchsia.net.interfaces.admin/Control"
	deviceControlName        = "fuchsia.net.interfaces.admin/DeviceControl"
)

type addressStateProviderCollection struct {
	nicid     tcpip.NICID
	providers map[tcpip.Address]*adminAddressStateProviderImpl
}

// Called when DAD completes.
//
// Note that `online` must not change when calling this function (`ifState.mu`
// must be held).
func (pc *addressStateProviderCollection) onDuplicateAddressDetectionCompleteLocked(nicid tcpip.NICID, addr tcpip.Address, online, success bool) {
	pi, ok := pc.providers[addr]
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
		delete(pc.providers, addr)
		pi.onRemoveLocked(admin.AddressRemovalReasonDadFailed)
	}
}

func (pc *addressStateProviderCollection) onAddressRemoveLocked(addr tcpip.Address) {
	if pi, ok := pc.providers[addr]; ok {
		delete(pc.providers, addr)
		pi.onRemove(admin.AddressRemovalReasonUserRemoved)
	}
}

func (pc *addressStateProviderCollection) onInterfaceRemoveLocked() {
	for addr, pi := range pc.providers {
		delete(pc.providers, addr)
		pi.onRemove(admin.AddressRemovalReasonInterfaceRemoved)
	}
}

// TODO(https://fxbug.dev/82045): Avoid racing interface up/down against DAD.
// Note that this function should be called while holding a lock which prevents
// `online` from mutating.
func (pc *addressStateProviderCollection) onInterfaceOnlineChangeLocked(online bool) {
	for _, pi := range pc.providers {
		pi.setInitialState(online)
	}
}

var _ admin.AddressStateProviderWithCtx = (*adminAddressStateProviderImpl)(nil)

type adminAddressStateProviderImpl struct {
	controlImpl  *adminControlImpl
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
func (pi *adminAddressStateProviderImpl) UpdateAddressProperties(_ fidl.Context, properties admin.AddressProperties) error {
	var lifetimes stack.AddressLifetimes
	// Note that absence of preferred lifetime means infinite preferred lifetime,
	// so the zero value for Deprecated is left untouched.
	if properties.HasPreferredLifetimeInfo() {
		switch preferred := properties.GetPreferredLifetimeInfo(); preferred.Which() {
		case admin.PreferredLifetimeInfoPreferredLifetimeEnd:
			// TODO(https://fxbug.dev/93825): Store the preferred lifetime.
			panic(fmt.Sprintf("UpdateAddressProperties on addr %s with preferred lifetime %d not supported", pi.protocolAddr.AddressWithPrefix.Address, preferred))
		case admin.PreferredLifetimeInfoDeprecated:
			lifetimes.Deprecated = true
		default:
			panic(fmt.Sprintf("unexpected preferred lifetime info tag: %+v", preferred))
		}
	}
	switch err := pi.controlImpl.ns.stack.SetAddressLifetimes(pi.controlImpl.nicid, pi.protocolAddr.AddressWithPrefix.Address, lifetimes); err.(type) {
	case nil:
	case *tcpip.ErrUnknownNICID, *tcpip.ErrBadLocalAddress:
		// TODO(https://fxbug.dev/94442): Upgrade to panic once we're guaranteed that we get here iff the address still exists.
		_ = syslog.WarnTf(addressStateProviderName, "SetAddressLifetimes(%d, %s, %#v) failed: %s",
			pi.controlImpl.nicid, pi.protocolAddr.AddressWithPrefix.Address, lifetimes, err)
	default:
		panic(fmt.Sprintf("SetAddressLifetimes(%d, %s, %#v) failed with unexpected error: %s",
			pi.controlImpl.nicid, pi.protocolAddr.AddressWithPrefix.Address, lifetimes, err))
	}

	if properties.HasValidLifetimeEnd() {
		// TODO(https://fxbug.dev/93825): Store the valid lifetime.
		panic(fmt.Sprintf("UpdateAddressProperties on addr %s with valid lifetime %d not supported",
			pi.protocolAddr.AddressWithPrefix.Address, properties.GetValidLifetimeEnd()))
	}
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
	doneChannel chan struct{}
	// TODO(https://fxbug.dev/85061): encode owned, strong, and weak refs once
	// cloning Control is allowed.
	isStrongRef bool
}

func (ci *adminControlImpl) getNICContext() *ifState {
	nicInfo, ok := ci.ns.stack.NICInfo()[ci.nicid]
	if !ok {
		// All serving control channels must be canceled before removing NICs from
		// the stack, this is a violation of that invariant.
		panic(fmt.Sprintf("NIC %d not found", ci.nicid))
	}
	return nicInfo.Context.(*ifState)
}

func (ci *adminControlImpl) Enable(fidl.Context) (admin.ControlEnableResult, error) {
	wasEnabled, err := ci.getNICContext().setState(true /* enabled */)
	if err != nil {
		// The only known error path that causes this failure is failure from the
		// device layers, which all mean we're possible racing with shutdown.
		_ = syslog.Errorf("ifs.Up() failed (NIC %d): %s", ci.nicid, err)
		return admin.ControlEnableResult{}, err
	}

	return admin.ControlEnableResultWithResponse(
		admin.ControlEnableResponse{
			DidEnable: !wasEnabled,
		}), nil
}

func (ci *adminControlImpl) Disable(fidl.Context) (admin.ControlDisableResult, error) {
	wasEnabled, err := ci.getNICContext().setState(false /* enabled */)
	if err != nil {
		// The only known error path that causes this failure is failure from the
		// device layers, which all mean we're possible racing with shutdown.
		_ = syslog.Errorf("ifs.Down() failed (NIC %d): %s", ci.nicid, err)
		return admin.ControlDisableResult{}, err
	}

	return admin.ControlDisableResultWithResponse(
		admin.ControlDisableResponse{
			DidDisable: wasEnabled,
		}), nil
}

func (ci *adminControlImpl) Detach(fidl.Context) error {
	// Make it a weak ref but don't decrease the reference count. If this was a
	// strong ref, the interface will leak.
	//
	// TODO(https://fxbug.dev/87963): Detach should only be allowed on OWNED refs
	// once we allow cloning Control.
	ci.isStrongRef = false
	return nil
}

func (ci *adminControlImpl) AddAddress(_ fidl.Context, interfaceAddr net.Subnet, parameters admin.AddressParameters, request admin.AddressStateProviderWithCtxInterfaceRequest) error {
	protocolAddr := fidlconv.ToTCPIPProtocolAddress(interfaceAddr)
	addr := protocolAddr.AddressWithPrefix.Address

	ifs := ci.getNICContext()

	ctx, cancel := context.WithCancel(context.Background())
	impl := &adminAddressStateProviderImpl{
		controlImpl:  ci,
		ready:        make(chan struct{}, 1),
		cancelServe:  cancel,
		protocolAddr: protocolAddr,
	}
	impl.mu.eventProxy.Channel = request.Channel

	if reason := func() admin.AddressRemovalReason {
		var properties stack.AddressProperties
		if parameters.HasTemporary() && parameters.GetTemporary() {
			properties.Temporary = true
		}
		if parameters.HasInitialProperties() {
			initProperties := parameters.GetInitialProperties()
			if initProperties.HasValidLifetimeEnd() {
				// TODO(https://fxbug.dev/93825): Store the valid lifetime.
				panic(fmt.Sprintf("adding address %s with valid lifetime %d is not supported", addr, initProperties.GetValidLifetimeEnd()))
			}
			// Note that absence of preferred lifetime means infinite preferred lifetime,
			// so the zero value for Deprecated is left untouched.
			if initProperties.HasPreferredLifetimeInfo() {
				switch preferred := initProperties.GetPreferredLifetimeInfo(); preferred.Which() {
				case admin.PreferredLifetimeInfoDeprecated:
					properties.Lifetimes.Deprecated = true
				case admin.PreferredLifetimeInfoPreferredLifetimeEnd:
					// TODO(https://fxbug.dev/93825): Store the preferred lifetime.
					panic(fmt.Sprintf("adding address %s with preferred lifetime %d is not supported", addr, preferred))
				default:
					panic(fmt.Sprintf("unknown preferred lifetime info tag: %+v", preferred))
				}
			}
		}

		if protocolAddr.AddressWithPrefix.PrefixLen > 8*len(protocolAddr.AddressWithPrefix.Address) {
			return admin.AddressRemovalReasonInvalid
		}

		ifs.mu.Lock()
		defer ifs.mu.Unlock()
		online := ifs.IsUpLocked()

		if _, ok := ifs.mu.addressStateProviders.providers[addr]; ok {
			return admin.AddressRemovalReasonAlreadyAssigned
		}

		if ok, status := ifs.addAddressLocked(protocolAddr, properties); !ok {
			return status
		}
		impl.mu.state = initialAddressAssignmentState(protocolAddr, online)
		_ = syslog.DebugTf(addressStateProviderName, "initial state for %s: %s", protocolAddr.AddressWithPrefix, impl.mu.state)
		ifs.mu.addressStateProviders.providers[addr] = impl

		return 0
	}(); reason != 0 {
		defer cancel()
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
		defer cancel()
		component.Serve(ctx, &admin.AddressStateProviderWithCtxStub{Impl: impl}, request.Channel, component.ServeOptions{
			Concurrent: true,
			OnError: func(err error) {
				_ = syslog.WarnTf(addressStateProviderName, "address state provider for %s: %s", addr, err)
			},
		})

		if pi := func() *adminAddressStateProviderImpl {
			ifs.mu.Lock()
			defer ifs.mu.Unlock()

			// The impl may have already been removed due to address removal.
			// Removing the address will also attempt to delete from
			// the address state providers map, so delete from it and unlock
			// immediately to avoid lock ordering and deadlock issues. We must proceed
			// with address removal only if this impl is the one stored in the map.
			// A new address state provider may have won the race here and could be
			// trying to assign the address again.
			if pi, ok := ifs.mu.addressStateProviders.providers[addr]; ok && pi == impl {
				delete(ifs.mu.addressStateProviders.providers, addr)
				return pi
			}
			return nil
		}(); pi != nil {
			pi.mu.Lock()
			remove := !pi.mu.detached
			pi.mu.Unlock()

			if remove {
				// NB: Removing the address will also attempt to access the address state
				// provider collection and delete the impl out of it if found. The lock
				// on the collection must not be held at this point to prevent the
				// deadlock.
				switch status := ifs.removeAddress(pi.protocolAddr); status {
				case zx.ErrOk, zx.ErrNotFound:
				case zx.ErrBadState:
					_ = syslog.WarnTf(addressStateProviderName, "interface %d removed when trying to remove address %s upon channel closure: %s", ci.nicid, addr, status)
				default:
					panic(fmt.Sprintf("unknown error trying to remove address %s upon channel closure: %s", addr, status))
				}
			}
		}
	}()
	return nil
}

func (ci *adminControlImpl) RemoveAddress(_ fidl.Context, address net.Subnet) (admin.ControlRemoveAddressResult, error) {
	protocolAddr := fidlconv.ToTCPIPProtocolAddress(address)
	nicInfo, ok := ci.ns.stack.NICInfo()[ci.nicid]
	if !ok {
		panic(fmt.Sprintf("NIC %d not found when removing %s", ci.nicid, protocolAddr.AddressWithPrefix))
	}
	switch zxErr := nicInfo.Context.(*ifState).removeAddress(protocolAddr); zxErr {
	case zx.ErrOk:
		return admin.ControlRemoveAddressResultWithResponse(admin.ControlRemoveAddressResponse{DidRemove: true}), nil
	case zx.ErrNotFound:
		return admin.ControlRemoveAddressResultWithResponse(admin.ControlRemoveAddressResponse{DidRemove: false}), nil
	default:
		panic(fmt.Sprintf("removeInterfaceAddress(%d, %v, false) = %s", ci.nicid, protocolAddr, zxErr))
	}
}

func (ci *adminControlImpl) GetId(fidl.Context) (uint64, error) {
	return uint64(ci.nicid), nil
}

// handleIPForwardingConfigurationResult handles the result of getting or
// setting IP forwarding or IP multicast forwarding configuration.
//
// Returns the result if the invoked function was successful. Otherwise, panics
// if an unexpected error occurred.
func handleIPForwardingConfigurationResult(result bool, err tcpip.Error, invokedFunction string) bool {
	switch err.(type) {
	case nil:
		return result
	case *tcpip.ErrUnknownNICID:
		// Impossible as this Control would be invalid if the interface is not
		// recognized.
		panic(fmt.Sprintf("got UnknownNICID error when Control is still valid from %s = %s", invokedFunction, err))
	default:
		panic(fmt.Sprintf("%s: %s", invokedFunction, err))
	}
}

// setIPForwardingLocked sets the IP forwarding configuration for the interface.
//
// The caller must hold the interface's write lock.
func (ci *adminControlImpl) setIPForwardingLocked(netProto tcpip.NetworkProtocolNumber, enabled bool) bool {
	prevEnabled, err := ci.ns.stack.SetNICForwarding(tcpip.NICID(ci.nicid), netProto, enabled)
	return handleIPForwardingConfigurationResult(prevEnabled, err, fmt.Sprintf("ci.ns.stack.SetNICForwarding(tcpip.NICID(%d), %d, %t)", ci.nicid, netProto, enabled))
}

// setMulticastIPForwardingLocked sets the IP multicast forwarding configuration
// for the interface.
//
// The caller must hold the interface's write lock.
func (ci *adminControlImpl) setMulticastIPForwardingLocked(netProto tcpip.NetworkProtocolNumber, enabled bool) bool {
	prevEnabled, err := ci.ns.stack.SetNICMulticastForwarding(tcpip.NICID(ci.nicid), netProto, enabled)
	return handleIPForwardingConfigurationResult(prevEnabled, err, fmt.Sprintf("ci.ns.stack.SetNICMulticastForwarding(tcpip.NICID(%d), %d, %t)", ci.nicid, netProto, enabled))
}

func (ci *adminControlImpl) SetConfiguration(_ fidl.Context, config admin.Configuration) (admin.ControlSetConfigurationResult, error) {
	ifs := ci.getNICContext()
	ifs.mu.Lock()
	defer ifs.mu.Unlock()

	var previousConfig admin.Configuration

	if config.HasIpv4() {
		var previousIpv4Config admin.Ipv4Configuration
		ipv4Config := config.Ipv4

		if ipv4Config.HasForwarding() {
			previousIpv4Config.SetForwarding(ci.setIPForwardingLocked(ipv4.ProtocolNumber, ipv4Config.Forwarding))
		}

		if ipv4Config.HasMulticastForwarding() {
			previousIpv4Config.SetMulticastForwarding(ci.setMulticastIPForwardingLocked(ipv4.ProtocolNumber, ipv4Config.MulticastForwarding))
		}

		previousConfig.SetIpv4(previousIpv4Config)
	}

	if config.HasIpv6() {
		var previousIpv6Config admin.Ipv6Configuration
		ipv6Config := config.Ipv6

		if ipv6Config.HasForwarding() {
			previousIpv6Config.SetForwarding(ci.setIPForwardingLocked(ipv6.ProtocolNumber, ipv6Config.Forwarding))
		}

		if ipv6Config.HasMulticastForwarding() {
			previousIpv6Config.SetMulticastForwarding(ci.setMulticastIPForwardingLocked(ipv6.ProtocolNumber, ipv6Config.MulticastForwarding))
		}

		previousConfig.SetIpv6(previousIpv6Config)
	}

	return admin.ControlSetConfigurationResultWithResponse(admin.ControlSetConfigurationResponse{
		PreviousConfig: previousConfig,
	}), nil
}

// ipForwardingRLocked gets the IP forwarding configuration for the interface.
//
// The caller must hold the interface's read lock.
func (ci adminControlImpl) ipForwardingRLocked(netProto tcpip.NetworkProtocolNumber) bool {
	enabled, err := ci.ns.stack.NICForwarding(tcpip.NICID(ci.nicid), netProto)
	return handleIPForwardingConfigurationResult(enabled, err, fmt.Sprintf("ci.ns.stack.NICForwarding(tcpip.NICID(%d), %d)", ci.nicid, netProto))
}

// multicastIPForwardingRLocked gets the IP multicast forwarding configuration
// for the interface.
//
// The caller must hold the interface's read lock.
func (ci adminControlImpl) multicastIPForwardingRLocked(netProto tcpip.NetworkProtocolNumber) bool {
	enabled, err := ci.ns.stack.NICMulticastForwarding(tcpip.NICID(ci.nicid), netProto)
	return handleIPForwardingConfigurationResult(enabled, err, fmt.Sprintf("ci.ns.stack.NICMulticastForwarding(tcpip.NICID(%d), %d)", ci.nicid, netProto))
}

func (ci *adminControlImpl) GetConfiguration(fidl.Context) (admin.ControlGetConfigurationResult, error) {
	ifs := ci.getNICContext()
	ifs.mu.RLock()
	defer ifs.mu.RUnlock()

	var config admin.Configuration

	{
		var ipv4Config admin.Ipv4Configuration
		ipv4Config.SetForwarding(ci.ipForwardingRLocked(ipv4.ProtocolNumber))
		ipv4Config.SetMulticastForwarding(ci.multicastIPForwardingRLocked(ipv4.ProtocolNumber))
		config.SetIpv4(ipv4Config)
	}

	{
		var ipv6Config admin.Ipv6Configuration
		ipv6Config.SetForwarding(ci.ipForwardingRLocked(ipv6.ProtocolNumber))
		ipv6Config.SetMulticastForwarding(ci.multicastIPForwardingRLocked(ipv6.ProtocolNumber))
		config.SetIpv6(ipv6Config)
	}

	return admin.ControlGetConfigurationResultWithResponse(admin.ControlGetConfigurationResponse{
		Config: config,
	}), nil
}

type adminControlCollection struct {
	mu struct {
		sync.Mutex
		removalReason  admin.InterfaceRemovedReason
		controls       map[*adminControlImpl]struct{}
		strongRefCount uint
	}
}

func (c *adminControlCollection) onInterfaceRemove(reason admin.InterfaceRemovedReason) {
	c.mu.Lock()
	controls := c.mu.controls
	c.mu.controls = nil
	c.mu.removalReason = reason
	c.mu.Unlock()

	for control := range controls {
		control.cancelServe()
	}
	for control := range controls {
		<-control.doneChannel
	}
}

func (ifs *ifState) addAdminConnection(request admin.ControlWithCtxInterfaceRequest, strong bool) {

	impl, ctx, cancel := func() (*adminControlImpl, context.Context, context.CancelFunc) {
		ifs.adminControls.mu.Lock()
		defer ifs.adminControls.mu.Unlock()

		// Do not add more connections to an interface that is tearing down.
		if ifs.adminControls.mu.removalReason != 0 {
			if err := request.Channel.Close(); err != nil {
				_ = syslog.ErrorTf(controlName, "request.channel.Close() = %s", err)
			}
			return nil, nil, nil
		}

		ctx, cancel := context.WithCancel(context.Background())
		impl := &adminControlImpl{
			ns:          ifs.ns,
			nicid:       ifs.nicid,
			cancelServe: cancel,
			doneChannel: make(chan struct{}),
			isStrongRef: strong,
		}

		ifs.adminControls.mu.controls[impl] = struct{}{}
		if impl.isStrongRef {
			ifs.adminControls.mu.strongRefCount++
		}

		return impl, ctx, cancel
	}()
	if impl == nil {
		return
	}

	go func() {
		defer cancel()
		defer close(impl.doneChannel)

		requestChannel := request.Channel
		defer func() {
			if !requestChannel.Handle().IsValid() {
				return
			}
			if err := requestChannel.Close(); err != nil {
				_ = syslog.ErrorTf(controlName, "requestChannel.Close() = %s", err)
			}
		}()

		component.Serve(ctx, &admin.ControlWithCtxStub{Impl: impl}, requestChannel, component.ServeOptions{
			Concurrent:       false,
			KeepChannelAlive: true,
			OnError: func(err error) {
				_ = syslog.WarnTf(controlName, "%s", err)
			},
		})

		// NB: anonymous function is used to restrict section where the lock is
		// held.
		ifStateToRemove := func() *ifState {
			ifs.adminControls.mu.Lock()
			defer ifs.adminControls.mu.Unlock()
			wasCanceled := errors.Is(ctx.Err(), context.Canceled)
			if wasCanceled {
				reason := ifs.adminControls.mu.removalReason
				if reason == 0 {
					panic("serve context canceled without storing a reason")
				}
				eventProxy := admin.ControlEventProxy{Channel: requestChannel}
				if err := eventProxy.OnInterfaceRemoved(reason); err != nil {
					_ = syslog.WarnTf(controlName, "failed to send interface close reason %s: %s", reason, err)
				}
				// Take the channel back from the proxy, since the proxy *may* have closed
				// the channel, in which case we want to prevent a double close on
				// the deferred cleanup.
				requestChannel = eventProxy.Channel
			}

			delete(ifs.adminControls.mu.controls, impl)
			// Don't consider destroying if not a strong ref.
			//
			// Note that the implementation can change from a strong to a weak ref if
			// Detach is called, which is how we allow interfaces to leak.
			//
			// This is also how we prevent destruction from interfaces created with
			// the legacy API, since they never have strong refs.
			if !impl.isStrongRef {
				return nil
			}
			ifs.adminControls.mu.strongRefCount--

			// If serving was canceled, that means that removal happened due to
			// outside cancelation already.
			if wasCanceled {
				return nil
			}
			// Don't destroy if there are any strong refs left.
			if ifs.adminControls.mu.strongRefCount != 0 {
				return nil
			}

			// We're good to remove this interface.
			// Prevent new connections while we're holding the collection lock,
			// avoiding races between here and removing the interface below.
			ifs.adminControls.mu.removalReason = admin.InterfaceRemovedReasonUser

			nicInfo, ok := impl.ns.stack.NICInfo()[impl.nicid]
			if !ok {
				panic(fmt.Sprintf("failed to find interface %d", impl.nicid))
			}
			// We can safely remove the interface now because we're certain that
			// this control impl is not in the collection anymore, so it can't
			// deadlock waiting for control interfaces to finish.
			return nicInfo.Context.(*ifState)
		}()

		if ifStateToRemove != nil {
			ifStateToRemove.RemoveByUser()
		}

	}()
}

var _ admin.InstallerWithCtx = (*interfacesAdminInstallerImpl)(nil)

type interfacesAdminInstallerImpl struct {
	ns *Netstack
}

func (i *interfacesAdminInstallerImpl) InstallDevice(_ fidl.Context, device network.DeviceWithCtxInterface, deviceControl admin.DeviceControlWithCtxInterfaceRequest) error {
	client, err := netdevice.NewClient(context.Background(), &device, &netdevice.SimpleSessionConfigFactory{})
	if err != nil {
		_ = syslog.WarnTf(controlName, "InstallDevice: %s", err)
		_ = deviceControl.Close()
		return nil
	}

	ctx, cancel := context.WithCancel(context.Background())
	impl := &interfacesAdminDeviceControlImpl{
		ns:           i.ns,
		deviceClient: client,
	}

	// Running the device client and serving the FIDL are tied to the same
	// context because their lifecycles are linked.
	var wg sync.WaitGroup
	wg.Add(1)
	go func() {
		defer wg.Done()
		impl.deviceClient.Run(ctx)
		cancel()
	}()

	go func() {
		component.Serve(ctx, &admin.DeviceControlWithCtxStub{Impl: impl}, deviceControl.Channel, component.ServeOptions{
			OnError: func(err error) {
				_ = syslog.WarnTf(deviceControlName, "%s", err)
			},
		})
		if !impl.detached {
			cancel()
		}
		// Wait for device goroutine to finish before closing the device.
		wg.Wait()
		if err := impl.deviceClient.Close(); err != nil {
			_ = syslog.ErrorTf(deviceControlName, "deviceClient.Close() = %s", err)
		}
	}()

	return nil
}

var _ admin.DeviceControlWithCtx = (*interfacesAdminDeviceControlImpl)(nil)

type interfacesAdminDeviceControlImpl struct {
	ns           *Netstack
	deviceClient *netdevice.Client
	detached     bool
}

func (d *interfacesAdminDeviceControlImpl) CreateInterface(_ fidl.Context, portId network.PortId, control admin.ControlWithCtxInterfaceRequest, options admin.Options) error {

	ifs, closeReason := func() (*ifState, admin.InterfaceRemovedReason) {
		port, err := d.deviceClient.NewPort(context.Background(), portId)
		if err != nil {
			_ = syslog.WarnTf(deviceControlName, "NewPort(_, %d) failed: %s", portId, err)
			{
				var unsupported *netdevice.InvalidPortOperatingModeError
				if errors.As(err, &unsupported) {
					return nil, admin.InterfaceRemovedReasonBadPort
				}
			}
			{
				var alreadyBound *netdevice.PortAlreadyBoundError
				if errors.As(err, &alreadyBound) {
					return nil, admin.InterfaceRemovedReasonPortAlreadyBound
				}
			}

			// Assume all other errors are due to problems communicating with the
			// port.
			return nil, admin.InterfaceRemovedReasonPortClosed
		}
		defer func() {
			if port != nil {
				_ = port.Close()
			}
		}()

		var namePrefix string
		var linkEndpoint stack.LinkEndpoint
		switch mode := port.Mode(); mode {
		case netdevice.PortModeEthernet:
			namePrefix = "eth"
			linkEndpoint = ethernet.New(port)
		case netdevice.PortModeIp:
			namePrefix = "ip"
			linkEndpoint = port
		default:
			panic(fmt.Sprintf("unknown port mode %d", mode))
		}

		metric := defaultInterfaceMetric
		if options.HasMetric() {
			metric = routes.Metric(options.GetMetric())
		}
		ifs, err := d.ns.addEndpoint(
			makeEndpointName(namePrefix, options.GetNameWithDefault("")),
			linkEndpoint,
			port,
			port,
			metric,
			qdiscConfig{numQueues: numQDiscFIFOQueues, queueLen: int(port.TxDepth()) * qdiscTxDepthMultiplier},
		)
		if err != nil {
			_ = syslog.WarnTf(deviceControlName, "addEndpoint failed: %s", err)
			var tcpipError *TcpIpError
			if errors.As(err, &tcpipError) {
				switch tcpipError.Err.(type) {
				case *tcpip.ErrDuplicateNICID:
					return nil, admin.InterfaceRemovedReasonDuplicateName
				}
			}
			panic(fmt.Sprintf("unexpected error ns.AddEndpoint(..) = %s", err))
		}

		// Prevent deferred cleanup from running.
		port = nil

		return ifs, 0
	}()

	if closeReason != 0 {
		proxy := admin.ControlEventProxy{
			Channel: control.Channel,
		}
		if err := proxy.OnInterfaceRemoved(closeReason); err != nil {
			_ = syslog.WarnTf(deviceControlName, "failed to write terminal event %s: %s", closeReason, err)
		}
		_ = control.Close()
		return nil
	}

	ifs.addAdminConnection(control, true /* strong */)

	return nil
}

func (d *interfacesAdminDeviceControlImpl) Detach(fidl.Context) error {
	d.detached = true
	return nil
}
