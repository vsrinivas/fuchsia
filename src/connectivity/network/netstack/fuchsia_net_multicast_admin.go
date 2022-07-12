// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package netstack

import (
	"context"
	"errors"
	"fmt"
	"syscall/zx/fidl"

	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/sync"

	"fidl/fuchsia/net/multicast/admin"

	"gvisor.dev/gvisor/pkg/tcpip"
	"gvisor.dev/gvisor/pkg/tcpip/stack"
)

const bufferCapacity = int(admin.MaxRoutingEvents)

type multicastEvent struct {
	event   admin.RoutingEvent
	context stack.MulticastPacketContext
}

var _ stack.MulticastForwardingEventDispatcher = (*multicastEventDispatcher)(nil)

// TODO(https://fxbug.dev/81922): Extract the common logic for implementing
// hanging gets.
type multicastEventDispatcher struct {
	cancelServe context.CancelFunc
	ready       chan struct{}
	mu          struct {
		sync.Mutex
		isHanging   bool
		eventBuffer multicastEventBuffer
	}
}

type multicastEventBuffer struct {
	data             []multicastEvent
	index            int
	size             int
	numDroppedEvents uint64
}

func (m *multicastEventBuffer) isFull() bool {
	return m.size == bufferCapacity
}

func (m *multicastEventBuffer) isEmpty() bool {
	return m.size == 0
}

func (m *multicastEventBuffer) enqueue(event multicastEvent) {
	if m.isFull() {
		m.data[m.index] = event
		m.index = m.physicalIndex(1)
		m.numDroppedEvents++
	} else {
		m.size++
		m.data[m.physicalIndex(m.size-1)] = event
	}
}

func (m *multicastEventBuffer) dequeue() (multicastEvent, uint64, bool) {
	if m.isEmpty() {
		return multicastEvent{}, 0, false
	}

	val := m.data[m.index]

	m.index = m.physicalIndex(1)
	m.size--
	droppedEvents := m.numDroppedEvents
	m.numDroppedEvents = 0
	return val, droppedEvents, true
}

func (m *multicastEventBuffer) physicalIndex(pos int) int {
	return (m.index + pos) % bufferCapacity
}

func newMulticastEventDispatcher(cancelServe context.CancelFunc) *multicastEventDispatcher {
	dispatcher := &multicastEventDispatcher{
		cancelServe: cancelServe,
		ready:       make(chan struct{}, 1),
	}
	dispatcher.mu.eventBuffer = multicastEventBuffer{data: make([]multicastEvent, bufferCapacity)}
	return dispatcher
}

func (m *multicastEventDispatcher) onEvent(event multicastEvent) {
	m.mu.Lock()
	m.mu.eventBuffer.enqueue(event)
	isHanging := m.mu.isHanging
	m.mu.Unlock()

	if isHanging {
		select {
		case m.ready <- struct{}{}:
		default:
		}
	}
}

// nextMulticastEvent returns the next queued multicast event along with the
// number of events that were dropped immediately before it.
//
// If no events are ready, then blocks until one is ready. If this method is
// called while another invocation of the method is blocking, then an error is
// returned and the onClose callback is invoked. Additionally, returns an error
// if the context is cancelled.
func (m *multicastEventDispatcher) nextMulticastEvent(ctx fidl.Context, onClose func()) (multicastEvent, uint64, error) {
	m.mu.Lock()
	defer m.mu.Unlock()

	if m.mu.isHanging {
		onClose()
		m.cancelServe()
		return multicastEvent{}, 0, errors.New("request to hanging nextMulticastEvent already in-flight")
	}

	for {
		if event, numDroppedEvents, ok := m.mu.eventBuffer.dequeue(); ok {
			return event, numDroppedEvents, nil
		}

		m.mu.isHanging = true
		m.mu.Unlock()

		var err error
		select {
		case <-m.ready:
		case <-ctx.Done():
			err = fmt.Errorf("context cancelled during hanging get: %w", ctx.Err())
		}

		m.mu.Lock()
		m.mu.isHanging = false

		if err != nil {
			return multicastEvent{}, 0, err
		}
	}
}

func (m *multicastEventDispatcher) OnMissingRoute(context stack.MulticastPacketContext) {
	event := multicastEvent{
		event:   admin.RoutingEventWithMissingRoute(admin.Empty{}),
		context: context,
	}
	m.onEvent(event)
}

func (m *multicastEventDispatcher) OnUnexpectedInputInterface(context stack.MulticastPacketContext, expectedInputInterface tcpip.NICID) {
	var wrongInputInterface admin.WrongInputInterface
	wrongInputInterface.SetExpectedInputInterface(uint64(expectedInputInterface))
	event := multicastEvent{
		event:   admin.RoutingEventWithWrongInputInterface(wrongInputInterface),
		context: context,
	}
	m.onEvent(event)
}
