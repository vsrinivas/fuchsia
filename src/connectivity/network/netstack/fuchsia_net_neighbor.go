// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//go:build !build_with_native_toolchain

package netstack

import (
	"context"
	"errors"
	"fmt"
	"strings"
	"syscall/zx"
	"syscall/zx/fidl"
	"time"

	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/fidlconv"
	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/sync"
	"go.fuchsia.dev/fuchsia/src/lib/component"
	syslog "go.fuchsia.dev/fuchsia/src/lib/syslog/go"

	"fidl/fuchsia/net"
	"fidl/fuchsia/net/neighbor"

	"gvisor.dev/gvisor/pkg/tcpip"
	"gvisor.dev/gvisor/pkg/tcpip/header"
	"gvisor.dev/gvisor/pkg/tcpip/stack"
)

const (
	nudTag = "NUD"
)

type nudDispatcher struct {
	ns     *Netstack
	events chan neighborEvent
}

var _ stack.NUDDispatcher = (*nudDispatcher)(nil)

func (d *nudDispatcher) log(verb string, nicID tcpip.NICID, entry stack.NeighborEntry) {
	family := func() string {
		switch l := len(entry.Addr); l {
		case header.IPv4AddressSize:
			return "v4"
		case header.IPv6AddressSize:
			return "v6"
		default:
			return fmt.Sprintf("l=%d", l)
		}
	}()
	flags := func() string {
		var defaultGateway, onLink bool
		for _, route := range d.ns.stack.GetRouteTable() {
			if route.NIC != nicID {
				continue
			}
			if len(route.Destination.ID()) != len(entry.Addr) {
				continue
			}
			if route.Destination.Prefix() == 0 {
				if route.Gateway == entry.Addr {
					defaultGateway = true
				}
			} else if route.Destination.Contains(entry.Addr) {
				if len(route.Gateway) == 0 {
					onLink = true
				}
			}
		}
		var b strings.Builder
		if defaultGateway {
			b.WriteByte('G')
		}
		if onLink {
			b.WriteByte('L')
		}
		if b.Len() != 0 {
			return b.String()
		}
		return "U"
	}()

	// TODO(https://fxbug.dev/62788): Change log level to Debug once the neighbor table
	// is able to be inspected.
	_ = syslog.InfoTf(nudTag, "%s %s (%s|%s) NIC=%d LinkAddress=%s %s", verb, entry.Addr, family, flags, nicID, entry.LinkAddr, entry.State)
}

// OnNeighborAdded implements stack.NUDDispatcher.
func (d *nudDispatcher) OnNeighborAdded(nicID tcpip.NICID, entry stack.NeighborEntry) {
	d.events <- neighborEvent{
		kind:  neighborAdded,
		entry: entry,
		nicID: nicID,
	}
	d.log("ADD", nicID, entry)
}

// OnNeighborChanged implements stack.NUDDispatcher.
func (d *nudDispatcher) OnNeighborChanged(nicID tcpip.NICID, entry stack.NeighborEntry) {
	d.events <- neighborEvent{
		kind:  neighborChanged,
		entry: entry,
		nicID: nicID,
	}
	d.log("MOD", nicID, entry)
}

// OnNeighborRemoved implements stack.NUDDispatcher.
func (d *nudDispatcher) OnNeighborRemoved(nicID tcpip.NICID, entry stack.NeighborEntry) {
	d.events <- neighborEvent{
		kind:  neighborRemoved,
		entry: entry,
		nicID: nicID,
	}
	d.log("DEL", nicID, entry)
}

type neighborEventKind int64

const (
	_ neighborEventKind = iota
	neighborAdded
	neighborChanged
	neighborRemoved
)

const maxEntryIteratorItemsQueueLen = 2 * neighbor.MaxItemBatchSize

type neighborEvent struct {
	kind  neighborEventKind
	entry stack.NeighborEntry
	nicID tcpip.NICID
}

type neighborEntryKey struct {
	nicID   tcpip.NICID
	address tcpip.Address
}

func (k *neighborEntryKey) String() string {
	return fmt.Sprintf("%s@%d", k.address, k.nicID)
}

type neighborImpl struct {
	stack *stack.Stack

	mu struct {
		sync.Mutex
		state     map[neighborEntryKey]*neighbor.Entry
		iterators map[*neighborEntryIterator]struct{}
		running   bool
	}
}

var _ neighbor.ViewWithCtx = (*neighborImpl)(nil)

// observeEvents starts observing neighbor events from neighborEvent. Can only
// be called once.
func (n *neighborImpl) observeEvents(events <-chan neighborEvent) {
	n.mu.Lock()
	running := n.mu.running
	n.mu.running = true
	n.mu.Unlock()
	if running {
		panic("called ObserveEvents twice on the same implementation")
	}

	for e := range events {
		n.processEvent(e)
	}

	n.mu.Lock()
	n.mu.running = false
	n.mu.Unlock()
}

func (n *neighborImpl) processEvent(event neighborEvent) {
	n.mu.Lock()
	defer n.mu.Unlock()
	key := neighborEntryKey{
		nicID:   event.nicID,
		address: event.entry.Addr,
	}
	propagateEvent, valid := func() (neighbor.EntryIteratorItem, bool) {
		switch event.kind {
		case neighborAdded:
			if old, ok := n.mu.state[key]; ok {
				panic(fmt.Sprintf("observed duplicate add for neighbor key %s. old=%v, event.entry=%v", &key, old, event.entry))
			}
			var newEntry neighbor.Entry
			newEntry.SetInterface(uint64(event.nicID))
			newEntry.SetNeighbor(fidlconv.ToNetIpAddress(event.entry.Addr))
			updateEntry(&newEntry, event.entry)
			n.mu.state[key] = &newEntry
			if newEntry.HasState() {
				return neighbor.EntryIteratorItemWithAdded(newEntry), true
			}
		case neighborRemoved:
			entry, ok := n.mu.state[key]
			if !ok {
				panic(fmt.Sprintf("attempted to remove non existing neighbor %s", &key))
			}
			delete(n.mu.state, key)
			if entry.HasState() {
				return neighbor.EntryIteratorItemWithRemoved(*entry), true
			}
		case neighborChanged:
			entry, ok := n.mu.state[key]
			if !ok {
				panic(fmt.Sprintf("attempted to update non existing neighbor %s. event.entry=%v", &key, event.entry))
			}
			hadState := entry.HasState()
			updateEntry(entry, event.entry)
			if entry.HasState() {
				if hadState {
					return neighbor.EntryIteratorItemWithChanged(*entry), true
				}
				return neighbor.EntryIteratorItemWithAdded(*entry), true
			}
		default:
			panic(fmt.Sprintf("unrecognized neighbor event %d", event.kind))
		}
		return neighbor.EntryIteratorItem{}, false
	}()
	if !valid {
		return
	}
	for it := range n.mu.iterators {
		it.mu.Lock()
		if len(it.mu.items) < int(maxEntryIteratorItemsQueueLen) {
			it.mu.items = append(it.mu.items, propagateEvent)
		} else {
			// Stop serving if client is not fetching events fast enough.
			it.cancelServe()
		}
		hanging := it.mu.isHanging
		it.mu.Unlock()
		if hanging {
			select {
			case it.ready <- struct{}{}:
			default:
			}
		}
	}
}

func (n *neighborImpl) OpenEntryIterator(_ fidl.Context, req neighbor.EntryIteratorWithCtxInterfaceRequest, _ neighbor.EntryIteratorOptions) error {
	n.mu.Lock()
	defer n.mu.Unlock()

	items := make([]neighbor.EntryIteratorItem, 0, len(n.mu.state)+1)

	for _, e := range n.mu.state {
		if e.HasState() {
			items = append(items, neighbor.EntryIteratorItemWithExisting(*e))
		}
	}

	items = append(items, neighbor.EntryIteratorItemWithIdle(neighbor.IdleEvent{}))

	ctx, cancel := context.WithCancel(context.Background())
	it := &neighborEntryIterator{
		cancelServe: cancel,
		ready:       make(chan struct{}, 1),
	}
	it.mu.Lock()
	it.mu.items = items
	it.mu.Unlock()
	go func() {
		defer cancel()
		component.Serve(ctx, &neighbor.EntryIteratorWithCtxStub{Impl: it}, req.Channel, component.ServeOptions{
			Concurrent: true,
			OnError: func(err error) {
				_ = syslog.WarnTf(neighbor.ViewName, "EntryIterator: %s", err)
			},
		})

		n.mu.Lock()
		delete(n.mu.iterators, it)
		n.mu.Unlock()
	}()

	n.mu.iterators[it] = struct{}{}

	return nil
}

func (n *neighborImpl) GetUnreachabilityConfig(_ fidl.Context, interfaceID uint64, ipVersion net.IpVersion) (neighbor.ViewGetUnreachabilityConfigResult, error) {
	netProto, ok := fidlconv.ToTCPIPNetProto(ipVersion)
	if !ok {
		return neighbor.ViewGetUnreachabilityConfigResultWithErr(int32(zx.ErrInvalidArgs)), nil
	}

	config, err := n.stack.NUDConfigurations(tcpip.NICID(interfaceID), netProto)
	if err != nil {
		return neighbor.ViewGetUnreachabilityConfigResultWithErr(int32(WrapTcpIpError(err).ToZxStatus())), nil
	}

	var resp neighbor.UnreachabilityConfig
	resp.SetBaseReachableTime(config.BaseReachableTime.Nanoseconds())
	resp.SetLearnBaseReachableTime(config.LearnBaseReachableTime)
	resp.SetMinRandomFactor(config.MinRandomFactor)
	resp.SetMaxRandomFactor(config.MaxRandomFactor)
	resp.SetRetransmitTimer(config.RetransmitTimer.Nanoseconds())
	resp.SetLearnRetransmitTimer(config.LearnRetransmitTimer)
	resp.SetDelayFirstProbeTime(config.DelayFirstProbeTime.Nanoseconds())
	resp.SetMaxMulticastProbes(config.MaxMulticastProbes)
	resp.SetMaxUnicastProbes(config.MaxUnicastProbes)
	resp.SetMaxAnycastDelayTime(config.MaxAnycastDelayTime.Nanoseconds())
	resp.SetMaxReachabilityConfirmations(config.MaxReachabilityConfirmations)

	return neighbor.ViewGetUnreachabilityConfigResultWithResponse(neighbor.ViewGetUnreachabilityConfigResponse{
		Config: resp,
	}), nil
}

var _ neighbor.ControllerWithCtx = (*neighborImpl)(nil)

func (n *neighborImpl) AddEntry(_ fidl.Context, interfaceID uint64, neighborIP net.IpAddress, mac net.MacAddress) (neighbor.ControllerAddEntryResult, error) {
	address, network := fidlconv.ToTCPIPAddressAndProtocolNumber(neighborIP)
	if err := n.stack.AddStaticNeighbor(tcpip.NICID(interfaceID), network, address, fidlconv.ToTCPIPLinkAddress(mac)); err != nil {
		return neighbor.ControllerAddEntryResultWithErr(int32(WrapTcpIpError(err).ToZxStatus())), nil
	}
	return neighbor.ControllerAddEntryResultWithResponse(neighbor.ControllerAddEntryResponse{}), nil
}

func (n *neighborImpl) RemoveEntry(_ fidl.Context, interfaceID uint64, neighborIP net.IpAddress) (neighbor.ControllerRemoveEntryResult, error) {
	address, network := fidlconv.ToTCPIPAddressAndProtocolNumber(neighborIP)
	if err := n.stack.RemoveNeighbor(tcpip.NICID(interfaceID), network, address); err != nil {
		var zxErr zx.Status
		switch err.(type) {
		case *tcpip.ErrBadAddress:
			zxErr = zx.ErrNotFound
		default:
			zxErr = WrapTcpIpError(err).ToZxStatus()
		}
		return neighbor.ControllerRemoveEntryResultWithErr(int32(zxErr)), nil
	}
	return neighbor.ControllerRemoveEntryResultWithResponse(neighbor.ControllerRemoveEntryResponse{}), nil
}

func (n *neighborImpl) ClearEntries(_ fidl.Context, interfaceID uint64, ipVersion net.IpVersion) (neighbor.ControllerClearEntriesResult, error) {
	netProto, ok := fidlconv.ToTCPIPNetProto(ipVersion)
	if !ok {
		return neighbor.ControllerClearEntriesResultWithErr(int32(zx.ErrInvalidArgs)), nil
	}

	if err := n.stack.ClearNeighbors(tcpip.NICID(interfaceID), netProto); err != nil {
		return neighbor.ControllerClearEntriesResultWithErr(int32(WrapTcpIpError(err).ToZxStatus())), nil
	}
	return neighbor.ControllerClearEntriesResultWithResponse(neighbor.ControllerClearEntriesResponse{}), nil
}

func (n *neighborImpl) UpdateUnreachabilityConfig(_ fidl.Context, interfaceID uint64, ipVersion net.IpVersion, config neighbor.UnreachabilityConfig) (neighbor.ControllerUpdateUnreachabilityConfigResult, error) {
	if !n.stack.HasNIC(tcpip.NICID(interfaceID)) {
		return neighbor.ControllerUpdateUnreachabilityConfigResultWithErr(int32(zx.ErrNotFound)), nil
	}

	netProto, ok := fidlconv.ToTCPIPNetProto(ipVersion)
	if !ok {
		return neighbor.ControllerUpdateUnreachabilityConfigResultWithErr(int32(zx.ErrInvalidArgs)), nil
	}

	currentConfig, err := n.stack.NUDConfigurations(tcpip.NICID(interfaceID), netProto)
	if err != nil {
		return neighbor.ControllerUpdateUnreachabilityConfigResultWithErr(int32(WrapTcpIpError(err).ToZxStatus())), nil
	}

	invalidArgsResult := neighbor.ControllerUpdateUnreachabilityConfigResultWithErr(int32(zx.ErrInvalidArgs))

	// See fuchsia.net.neighbor/UnreachabilityConfig for the list of constraints.
	if config.HasBaseReachableTime() {
		if v := config.GetBaseReachableTime(); v <= 0 {
			_ = syslog.WarnTf(neighbor.ControllerName, "UpdateUnreachabilityConfig: invalid `base_reachable_time` %d: must be > 0", v)
			return invalidArgsResult, nil
		}
		currentConfig.BaseReachableTime = time.Duration(config.GetBaseReachableTime())
	}
	if config.HasLearnBaseReachableTime() {
		currentConfig.LearnBaseReachableTime = config.GetLearnBaseReachableTime()
	}
	if config.HasMinRandomFactor() {
		if v := config.GetMinRandomFactor(); v <= 0 {
			_ = syslog.WarnTf(neighbor.ControllerName, "UpdateUnreachabilityConfig: invalid `min_random_factor` %f: must be > 0", v)
			return invalidArgsResult, nil
		}
		currentConfig.MinRandomFactor = config.GetMinRandomFactor()
	}
	if config.HasMaxRandomFactor() {
		if v := config.GetMaxRandomFactor(); v < currentConfig.MinRandomFactor {
			_ = syslog.WarnTf(neighbor.ControllerName, "UpdateUnreachabilityConfig: invalid `max_random_factor` %f: must be >= `min_random_factor` %f", v, currentConfig.MinRandomFactor)
			return invalidArgsResult, nil
		}
		currentConfig.MaxRandomFactor = config.GetMaxRandomFactor()
	}
	if config.HasRetransmitTimer() {
		if v := config.GetRetransmitTimer(); v <= 0 {
			_ = syslog.WarnTf(neighbor.ControllerName, "UpdateUnreachabilityConfig: invalid `retransmit_timer` %d: must be > 0", v)
			return invalidArgsResult, nil
		}
		currentConfig.RetransmitTimer = time.Duration(config.GetRetransmitTimer())
	}
	if config.HasLearnRetransmitTimer() {
		currentConfig.LearnRetransmitTimer = config.GetLearnRetransmitTimer()
	}
	if config.HasDelayFirstProbeTime() {
		if v := config.GetDelayFirstProbeTime(); v <= 0 {
			_ = syslog.WarnTf(neighbor.ControllerName, "UpdateUnreachabilityConfig: invalid `delay_first_probe_time` %d: must be > 0", v)
			return invalidArgsResult, nil
		}
		currentConfig.DelayFirstProbeTime = time.Duration(config.GetDelayFirstProbeTime())
	}
	if config.HasMaxMulticastProbes() {
		if v := config.GetMaxMulticastProbes(); v <= 0 {
			_ = syslog.WarnTf(neighbor.ControllerName, "UpdateUnreachabilityConfig: invalid `max_multicast_probes` %d: must be > 0", v)
			return invalidArgsResult, nil
		}
		currentConfig.MaxMulticastProbes = config.GetMaxMulticastProbes()
	}
	if config.HasMaxUnicastProbes() {
		if v := config.GetMaxUnicastProbes(); v <= 0 {
			_ = syslog.WarnTf(neighbor.ControllerName, "UpdateUnreachabilityConfig: invalid `max_unicast_probes` %d: must be > 0", v)
			return invalidArgsResult, nil
		}
		currentConfig.MaxUnicastProbes = config.GetMaxUnicastProbes()
	}
	if config.HasMaxAnycastDelayTime() {
		if v := config.GetMaxAnycastDelayTime(); v < 0 {
			_ = syslog.WarnTf(neighbor.ControllerName, "UpdateUnreachabilityConfig: invalid `max_anycast_delay_time` %d: must be >= 0", v)
			return invalidArgsResult, nil
		}
		currentConfig.MaxAnycastDelayTime = time.Duration(config.GetMaxAnycastDelayTime())
	}
	if config.HasMaxReachabilityConfirmations() {
		currentConfig.MaxReachabilityConfirmations = config.GetMaxReachabilityConfirmations()
	}

	if err := n.stack.SetNUDConfigurations(tcpip.NICID(interfaceID), netProto, currentConfig); err != nil {
		return neighbor.ControllerUpdateUnreachabilityConfigResultWithErr(int32(WrapTcpIpError(err).ToZxStatus())), nil
	}
	return neighbor.ControllerUpdateUnreachabilityConfigResultWithResponse(neighbor.ControllerUpdateUnreachabilityConfigResponse{}), nil
}

// neighborEntryIterator queues events received from the neighbor table for
// consumption by a FIDL client.
type neighborEntryIterator struct {
	cancelServe context.CancelFunc
	ready       chan struct{}
	mu          struct {
		sync.Mutex
		items     []neighbor.EntryIteratorItem
		isHanging bool
	}
}

var _ neighbor.EntryIteratorWithCtx = (*neighborEntryIterator)(nil)

// GetNext implements neighbor.EntryIteratorWithCtx.GetNext.
func (it *neighborEntryIterator) GetNext(ctx fidl.Context) ([]neighbor.EntryIteratorItem, error) {
	it.mu.Lock()
	defer it.mu.Unlock()

	if it.mu.isHanging {
		it.cancelServe()
		return nil, errors.New("not allowed to call EntryIterator.GetNext when a call is already pending")
	}

	for {
		items := it.mu.items
		avail := len(items)
		if avail != 0 {
			if avail > int(neighbor.MaxItemBatchSize) {
				items = items[:neighbor.MaxItemBatchSize]
				it.mu.items = it.mu.items[neighbor.MaxItemBatchSize:]
			} else {
				it.mu.items = nil
			}
			return items, nil
		}

		it.mu.isHanging = true
		it.mu.Unlock()

		var err error
		select {
		case <-it.ready:
		case <-ctx.Done():
			err = fmt.Errorf("cancelled: %w", ctx.Err())
		}

		it.mu.Lock()
		it.mu.isHanging = false
		if err != nil {
			return nil, err
		}
	}
}

func updateEntry(e *neighbor.Entry, n stack.NeighborEntry) {
	e.SetUpdatedAt(int64(fidlconv.ToZxTime(n.UpdatedAt)))
	if len(n.LinkAddr) != 0 {
		e.SetMac(fidlconv.ToNetMacAddress(n.LinkAddr))
	}

	if entryState, valid := func() (neighbor.EntryState, bool) {
		switch n.State {
		case stack.Unknown:
			// Unknown is an internal state used by the netstack to represent a newly
			// created or deleted entry. Clients do not need to be concerned with this
			// in-between state; all transitions into and out of the Unknown state
			// trigger an event.
			return 0, false
		case stack.Incomplete:
			return neighbor.EntryStateIncomplete, true
		case stack.Reachable:
			return neighbor.EntryStateReachable, true
		case stack.Stale:
			return neighbor.EntryStateStale, true
		case stack.Delay:
			return neighbor.EntryStateDelay, true
		case stack.Probe:
			return neighbor.EntryStateProbe, true
		case stack.Static:
			return neighbor.EntryStateStatic, true
		case stack.Unreachable:
			return neighbor.EntryStateUnreachable, true
		default:
			panic(fmt.Sprintf("invalid NeighborState = %d: %#v", n.State, n))
		}
	}(); valid {
		e.SetState(entryState)
	}
}

func newNudDispatcher() *nudDispatcher {
	return &nudDispatcher{
		// Create channel with enough of a backlog to not block nudDispatcher.
		events: make(chan neighborEvent, 128),
	}
}

func newNeighborImpl(stack *stack.Stack) *neighborImpl {
	impl := &neighborImpl{stack: stack}
	impl.mu.Lock()
	impl.mu.state = make(map[neighborEntryKey]*neighbor.Entry)
	impl.mu.iterators = make(map[*neighborEntryIterator]struct{})
	impl.mu.Unlock()
	return impl
}
