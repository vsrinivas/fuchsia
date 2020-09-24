// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package netstack

import (
	"errors"
	"fmt"
	"syscall/zx/fidl"

	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/fidlconv"
	"go.fuchsia.dev/fuchsia/src/lib/component"
	syslog "go.fuchsia.dev/fuchsia/src/lib/syslog/go"

	"fidl/fuchsia/net"
	"fidl/fuchsia/net/neighbor"

	"gvisor.dev/gvisor/pkg/tcpip"
	"gvisor.dev/gvisor/pkg/tcpip/stack"
)

var ErrNotImplemented = errors.New("not implemented")

type neighborImpl struct {
	ns *Netstack
}

var _ neighbor.ViewWithCtx = (*neighborImpl)(nil)

func (n *neighborImpl) OpenEntryIterator(ctx fidl.Context, it neighbor.EntryIteratorWithCtxInterfaceRequest, options neighbor.EntryIteratorOptions) error {
	// TODO(fxbug.dev/59425): Watch for changes.
	var items []neighbor.EntryIteratorItem

	for nicID := range n.ns.stack.NICInfo() {
		neighbors, err := n.ns.stack.Neighbors(nicID)
		switch err {
		case nil:
		case tcpip.ErrNotSupported:
			// This NIC does not use a neighbor table.
			continue
		case tcpip.ErrUnknownNICID:
			// This NIC was removed since stack.NICInfo() was called.
			continue
		default:
			_ = syslog.ErrorTf(neighbor.ViewName, "EntryIterator received unexpected error from Neighbors(%d): %s", nicID, err)
			return WrapTcpIpError(err)
		}

		for _, n := range neighbors {
			if netEntry, ok := toNeighborEntry(nicID, n); ok {
				items = append(items, neighbor.EntryIteratorItemWithExisting(netEntry))
			}
		}
	}

	// End the list with a special item to indicate the end of existing entries.
	items = append(items, neighbor.EntryIteratorItemWithIdle(neighbor.IdleEvent{}))

	stub := neighbor.EntryIteratorWithCtxStub{
		Impl: &neighborEntryIterator{
			items: items,
		},
	}
	go component.ServeExclusive(ctx, &stub, it.Channel, func(err error) {
		_ = syslog.WarnTf(neighbor.ViewName, "EntryIterator: %s", err)
	})

	return nil
}

func (n *neighborImpl) GetUnreachabilityConfig(ctx fidl.Context, interfaceID uint64) (neighbor.ViewGetUnreachabilityConfigResult, error) {
	// TODO(fxbug.dev/51776): Implement fuchsia.net.neighbor/View.GetUnreachabilityConfigs
	return neighbor.ViewGetUnreachabilityConfigResult{}, ErrNotImplemented
}

var _ neighbor.ControllerWithCtx = (*neighborImpl)(nil)

func (n *neighborImpl) AddEntry(ctx fidl.Context, interfaceID uint64, neighborIP net.IpAddress, mac net.MacAddress) (neighbor.ControllerAddEntryResult, error) {
	// TODO(fxbug.dev/51777): Implement fuchsia.net.neighbor/Controller.AddEntry
	resp := neighbor.ControllerAddEntryResponse{}
	result := neighbor.ControllerAddEntryResultWithResponse(resp)
	return result, ErrNotImplemented
}

func (n *neighborImpl) RemoveEntry(ctx fidl.Context, interfaceID uint64, neighborIP net.IpAddress) (neighbor.ControllerRemoveEntryResult, error) {
	// TODO(fxbug.dev/51778): Implement fuchsia.net.neighbor/Controller.RemoveEntry
	resp := neighbor.ControllerRemoveEntryResponse{}
	result := neighbor.ControllerRemoveEntryResultWithResponse(resp)
	return result, ErrNotImplemented
}

func (n *neighborImpl) ClearEntries(ctx fidl.Context, interfaceID uint64) (neighbor.ControllerClearEntriesResult, error) {
	// TODO(fxbug.dev/51779): Implement fuchsia.net.neighbor/Controller.ClearEntries
	resp := neighbor.ControllerClearEntriesResponse{}
	result := neighbor.ControllerClearEntriesResultWithResponse(resp)
	return result, ErrNotImplemented
}

func (n *neighborImpl) UpdateUnreachabilityConfig(ctx fidl.Context, interfaceID uint64, config neighbor.UnreachabilityConfig) (neighbor.ControllerUpdateUnreachabilityConfigResult, error) {
	// TODO(fxbug.dev/51780): Implement fuchsia.net.neighbor/Controller.UpdateUnreachabilityConfig
	resp := neighbor.ControllerUpdateUnreachabilityConfigResponse{}
	result := neighbor.ControllerUpdateUnreachabilityConfigResultWithResponse(resp)
	return result, ErrNotImplemented
}

// neighborEntryIterator queues events received from the neighbor table for
// consumption by a FIDL client.
type neighborEntryIterator struct {
	// items contains neighbor entry notifications waiting for client consumption.
	items []neighbor.EntryIteratorItem
}

var _ neighbor.EntryIteratorWithCtx = (*neighborEntryIterator)(nil)

// GetNext implements neighbor.EntryIteratorWithCtx.GetNext.
func (it *neighborEntryIterator) GetNext(ctx fidl.Context) ([]neighbor.EntryIteratorItem, error) {
	if len(it.items) == 0 {
		// TODO(fxbug.dev/59425): Watch for changes instead of closing the
		// connection. This was deferred to unblock listing entries.
		return nil, errors.New("watching for changes not supported")
	}

	items := it.items
	if uint64(len(it.items)) > neighbor.MaxItemBatchSize {
		// There are too many items to send; only send the max amount and leave the
		// rest for subsequent calls.
		items = items[:neighbor.MaxItemBatchSize]
	}
	it.items = it.items[len(items):]

	// Avoid memory leak on always-appended slice.
	if len(it.items) == 0 {
		it.items = nil
	}

	return items, nil
}

// toNeighborEntry converts a stack.NeighborEntry to a
// fuchsia.net.neighbor/Entry. Returns the converted entry and true if the
// conversion was successful, false otherwise.
func toNeighborEntry(nicID tcpip.NICID, n stack.NeighborEntry) (neighbor.Entry, bool) {
	e := neighbor.Entry{}
	e.SetInterface(uint64(nicID))
	e.SetUpdatedAt(n.UpdatedAt.UnixNano())

	if len(n.Addr) != 0 {
		e.SetNeighbor(fidlconv.ToNetIpAddress(n.Addr))
	}
	if len(n.LinkAddr) != 0 {
		e.SetMac(fidlconv.ToNetMacAddress(n.LinkAddr))
	}

	switch n.State {
	case stack.Unknown:
		// Unknown is an internal state used by the netstack to represent a newly
		// created or deleted entry. Clients do not need to be concerned with this
		// in-between state; all transitions into and out of the Unknown state
		// triggers an event.
		return e, false
	case stack.Incomplete:
		e.SetState(neighbor.EntryStateWithIncomplete(neighbor.IncompleteState{}))
	case stack.Reachable:
		// TODO(fxbug.dev/59372): Populate expires_at.
		e.SetState(neighbor.EntryStateWithReachable(neighbor.ReachableState{}))
	case stack.Stale:
		e.SetState(neighbor.EntryStateWithStale(neighbor.StaleState{}))
	case stack.Delay:
		e.SetState(neighbor.EntryStateWithDelay(neighbor.DelayState{}))
	case stack.Probe:
		e.SetState(neighbor.EntryStateWithProbe(neighbor.ProbeState{}))
	case stack.Static:
		e.SetState(neighbor.EntryStateWithStatic(neighbor.StaticState{}))
	case stack.Failed:
		// Failed is an internal state used by the netstack to inform transport
		// endpoints of a failure to resolve a link-layer address. Clients should
		// not be concerned with this error, thus is not representable by the
		// fuchsia.net.neighbor FIDL. When an entry with this state is received, the
		// entry should be skipped.
		return e, false
	default:
		panic(fmt.Sprintf("invalid NeighborState = %d: %#v", n.State, n))
	}

	return e, true
}
