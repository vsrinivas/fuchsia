// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package netstack

import (
	"errors"
	"syscall/zx/fidl"

	"fidl/fuchsia/net"
	neigh "fidl/fuchsia/net/neighbor"
)

var ErrNotImplemented = errors.New("not implemented")

type neighborImpl struct {
	ns *Netstack
}

var _ neigh.ViewWithCtx = (*neighborImpl)(nil)

func (n *neighborImpl) OpenEntryIterator(ctx fidl.Context, it neigh.EntryIteratorWithCtxInterfaceRequest, options neigh.EntryIteratorOptions) error {
	// TODO(fxb/51775): Implement fuchsia.net.neighbor/View.OpenEntryIterator
	return ErrNotImplemented
}

func (n *neighborImpl) GetUnreachabilityConfig(ctx fidl.Context, interfaceID uint64) (neigh.ViewGetUnreachabilityConfigResult, error) {
	// TODO(fxb/51776): Implement fuchsia.net.neighbor/View.GetUnreachabilityConfigs
	return neigh.ViewGetUnreachabilityConfigResult{}, ErrNotImplemented
}

var _ neigh.ControllerWithCtx = (*neighborImpl)(nil)

func (n *neighborImpl) AddEntry(ctx fidl.Context, interfaceID uint64, neighbor net.IpAddress, mac net.MacAddress) (neigh.ControllerAddEntryResult, error) {
	// TODO(fxb/51777): Implement fuchsia.net.neighbor/Controller.AddEntry
	resp := neigh.ControllerAddEntryResponse{}
	result := neigh.ControllerAddEntryResultWithResponse(resp)
	return result, ErrNotImplemented
}

func (n *neighborImpl) RemoveEntry(ctx fidl.Context, interfaceID uint64, neighbor net.IpAddress) (neigh.ControllerRemoveEntryResult, error) {
	// TODO(fxb/51778): Implement fuchsia.net.neighbor/Controller.RemoveEntry
	resp := neigh.ControllerRemoveEntryResponse{}
	result := neigh.ControllerRemoveEntryResultWithResponse(resp)
	return result, ErrNotImplemented
}

func (n *neighborImpl) ClearEntries(ctx fidl.Context, interfaceID uint64) (neigh.ControllerClearEntriesResult, error) {
	// TODO(fxb/51779): Implement fuchsia.net.neighbor/Controller.ClearEntries
	resp := neigh.ControllerClearEntriesResponse{}
	result := neigh.ControllerClearEntriesResultWithResponse(resp)
	return result, ErrNotImplemented
}

func (n *neighborImpl) UpdateUnreachabilityConfig(ctx fidl.Context, interfaceID uint64, config neigh.UnreachabilityConfig) (neigh.ControllerUpdateUnreachabilityConfigResult, error) {
	// TODO(fxb/51780): Implement fuchsia.net.neighbor/Controller.UpdateUnreachabilityConfig
	resp := neigh.ControllerUpdateUnreachabilityConfigResponse{}
	result := neigh.ControllerUpdateUnreachabilityConfigResultWithResponse(resp)
	return result, ErrNotImplemented
}
