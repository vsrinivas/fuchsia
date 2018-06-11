// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package bindings

import (
	"sync"
	"syscall/zx"
	"syscall/zx/dispatch"
)

// d is a process-local dispatcher.
var d *dispatch.Dispatcher

func init() {
	disp, err := dispatch.NewDispatcher()
	if err != nil {
		panic("failed to create dispatcher: " + err.Error())
	}
	d = disp
}

// Serve is a blocking call to the process-local dispatcher's serve method.
func Serve() {
	d.Serve()
}

type bindingState int32

const (
	idle bindingState = iota
	handling
	cleanup
)

// Binding binds the implementation of a Stub to a Channel.
//
// A Binding listens for incoming messages on the Channel, decodes them, and
// asks the Stub to dispatch to the appropriate implementation of the interface.
// If the message expects a reply, the Binding will also encode the reply and
// send it back over the Channel.
type Binding struct {
	// Stub is a wrapper around an implementation of a FIDL interface which
	// knows how to dispatch to a method by ordinal.
	Stub Stub

	// Channel is the Channel primitive to which the Stub is bound.
	Channel zx.Channel

	// id is an extra bit of state about the underlying wait in the
	// process-local dispatcher so we can unbind later.
	id *dispatch.WaitID

	// errHandler is an error handler which will be called if a
	// connection error is encountered.
	errHandler func(error)

	// handling is an atomically-updated signal which represents the state of the
	// binding.
	stateMu sync.Mutex
	state bindingState
}

// Init initializes a Binding.
func (b *Binding) Init(errHandler func(error)) error {
	// Declare the wait handler as a closure.
	h := func(d *dispatch.Dispatcher, s zx.Status, sigs *zx.PacketSignal) (result dispatch.WaitResult) {
		b.stateMu.Lock()
		if b.state == cleanup {
			b.close()
			b.stateMu.Unlock()
			return dispatch.WaitFinished
		}
		b.state = handling
		b.stateMu.Unlock()
		defer func() {
			b.stateMu.Lock()
			defer b.stateMu.Unlock()
			if b.state == cleanup {
				b.close()
				result = dispatch.WaitFinished
			}
			b.state = idle
		}()
		if s != zx.ErrOk {
			b.errHandler(zx.Error{Status: s})
			return dispatch.WaitFinished
		}
		if sigs.Observed&zx.SignalChannelReadable != 0 {
			for i := uint64(0); i < sigs.Count; i++ {
				shouldWait, err := b.dispatch()
				if err != nil {
					b.errHandler(err)
					return dispatch.WaitFinished
				}
				if shouldWait {
					return dispatch.WaitAgain
				}
			}
			return dispatch.WaitAgain
		}
		b.errHandler(zx.Error{Status: zx.ErrPeerClosed})
		return dispatch.WaitFinished
	}

	b.stateMu.Lock()
	b.state = idle
	b.stateMu.Unlock()

	b.errHandler = errHandler

	// Start the wait on the Channel.
	id, err := d.BeginWait(
		zx.Handle(b.Channel),
		zx.SignalChannelReadable|zx.SignalChannelPeerClosed,
		0,
		h,
	)
	if err != nil {
		return err
	}
	b.id = &id
	return nil
}

// dispatch reads from the underlying Channel and dispatches into the Stub.
//
// Returns whether we should continue to wait before reading more from the Channel,
// and potentially an error.
func (b *Binding) dispatch() (bool, error) {
	respb := messageBytesPool.Get().([]byte)
	resph := messageHandlesPool.Get().([]zx.Handle)

	defer messageBytesPool.Put(respb)
	defer messageHandlesPool.Put(resph)

	nb, nh, err := b.Channel.Read(respb[:], resph[:], 0)
	if err != nil {
		zxErr, ok := err.(zx.Error)
		if ok && zxErr.Status == zx.ErrShouldWait {
			return true, nil
		}
		return false, err
	}
	var header MessageHeader
	if err := UnmarshalHeader(respb[:], &header); err != nil {
		return false, err
	}
	start := MessageHeaderSize
	p, err := b.Stub.Dispatch(header.Ordinal, respb[start:int(nb)], resph[:nh])
	if err != nil {
		return false, err
	}
	// Message has no response.
	if p == nil {
		return false, nil
	}
	cnb, cnh, err := MarshalMessage(&header, p, respb[:], resph[:])
	if err != nil {
		return false, err
	}
	if err := b.Channel.Write(respb[:cnb], resph[:cnh], 0); err != nil {
		return false, err
	}
	return false, nil
}

// Close cancels any outstanding waits, resets the Binding's state, and closes
// the bound Channel once any out-standing requests are finished being handled.
func (b *Binding) Close() error {
	b.stateMu.Lock()
	defer b.stateMu.Unlock()
	if zx.Handle(b.Channel) == zx.HandleInvalid || b.state == cleanup {
		panic("double binding close")
	}
	switch b.state {
	case idle:
		return b.close()
	case handling:
		b.state = cleanup
	}
	return nil
}

// close cancels any outstanding waits, resets the Binding's state, and
// closes the bound Channel. This method is not thread-safe, and should be
// called with the binding's mutex set.
func (b *Binding) close() error {
	if err := d.CancelWait(*b.id); err != nil {
		zxErr, ok := err.(zx.Error)
		// If it just says that the ID isn't found, there are cases where this is
		// a reasonable error (particularly when we're in the middle of handling
		// a signal from the dispatcher).
		if !ok || zxErr.Status != zx.ErrNotFound {
			// Attempt to close the channel if we hit a more serious error.
			b.Channel.Close()
			return err
		}
	}
	b.id = nil
	b.state = idle
	return b.Channel.Close()
}

// BindingKey is a key which maps to a specific binding.
//
// It is only valid for the BindingSet that produced it.
type BindingKey uint64

// BindingSet is a managed set of Bindings which know how to unbind and
// remove themselves in the event of a connection error.
type BindingSet struct {
	nextKey  BindingKey
	mu       sync.Mutex
	Bindings map[BindingKey]*Binding
}

// Add creates a new Binding, initializes it, and adds it to the set.
//
// onError is an optional handler than may be passed which will be called after
// the binding between the Stub and the Channel is successfully closed.
func (b *BindingSet) Add(s Stub, c zx.Channel, onError func(error)) (BindingKey, error) {
	binding := &Binding{
		Stub:    s,
		Channel: c,
	}
	b.mu.Lock()
	defer b.mu.Unlock()
	if b.Bindings == nil {
		b.Bindings = make(map[BindingKey]*Binding)
	}
	key := b.nextKey
	err := binding.Init(func(err error) {
		if b.Remove(key) && onError != nil {
			onError(err)
		}
	})
	if err != nil {
		return 0, err
	}
	b.Bindings[key] = binding
	b.nextKey += 1
	return key, nil
}

// ProxyFor returns an event proxy created from the channel of the binding referred
// to by key.
func (b *BindingSet) ProxyFor(key BindingKey) (*Proxy, bool) {
	b.mu.Lock()
	defer b.mu.Unlock()
	if binding, ok := b.Bindings[key]; ok {
		return &Proxy{Channel: binding.Channel}, true
	}
	return nil, false
}

// Remove removes a Binding from the set when it is next idle.
//
// Note that this method invalidates the key, and it will never remove a Binding
// while it is actively being handled.
//
// Returns true if a Binding was found and removed.
func (b *BindingSet) Remove(key BindingKey) bool {
	b.mu.Lock()
	if binding, ok := b.Bindings[key]; ok {
		delete(b.Bindings, key)
		b.mu.Unlock()

		// Close the binding before calling the callback.
		if err := binding.Close(); err != nil {
			// Just panic. The only reason this can fail is if the handle
			// is bad, which it shouldn't be if we're tracking things. If
			// it does fail, better to fail fast.
			panic(err)
		}
		return true
	}
	b.mu.Unlock()
	return false
}

// Close removes all the bindings from the set.
func (b *BindingSet) Close() {
	// Lock, close all the bindings, and clear the map.
	b.mu.Lock()
	for _, binding := range b.Bindings {
		if err := binding.Close(); err != nil {
			// Just panic. The only reason this can fail is if the handle
			// is bad, which it shouldn't be if we're tracking things. If
			// it does fail, better to fail fast.
			panic(err)
		}
	}
	b.Bindings = make(map[BindingKey]*Binding)
	b.mu.Unlock()
}
