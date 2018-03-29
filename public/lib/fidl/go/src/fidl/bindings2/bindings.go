// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package bindings2

import (
	"log"
	"sync"
	"syscall/zx"
	"syscall/zx/dispatch"
)

// d is a process-local dispatcher.
var d *dispatch.Dispatcher

func init() {
	disp, err := dispatch.NewDispatcher()
	if err != nil {
		log.Panicf("failed to create dispatcher: %v", err)
	}
	d = disp
}

// Serve is a blocking call to the process-local dispatcher's serve method.
func Serve() {
	d.Serve()
}

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
}

// Init initializes a Binding.
func (b *Binding) Init(e func(error)) error {
	b.errHandler = e

	// Declare the wait handler as a closure.
	h := func(d *dispatch.Dispatcher, s zx.Status, sigs *zx.PacketSignal) dispatch.WaitResult {
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
	// Allocate maximum size of a message on the stack.
	var respb [zx.ChannelMaxMessageBytes]byte
	var resph [zx.ChannelMaxMessageHandles]zx.Handle
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
	p, err := b.Stub.Dispatch(header.Ordinal, respb[start:start+int(nb)], resph[:nh])
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

// Close cancels an outstanding waits, resets the Binding's state, and
// closes the bound Channel.
func (b *Binding) Close() error {
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
	return b.Channel.Close()
}

// BindingSet is a managed set of Bindings which know how to unbind and
// remove themselves in the event of a connection error.
type BindingSet struct {
	mu sync.Mutex
	Bindings []*Binding
}

// Add creates a new Binding, initializes it, and adds it to the set.
func (b *BindingSet) Add(s Stub, c zx.Channel) error {
	binding := &Binding{
		Stub: s,
		Channel: c,
	}
	err := binding.Init(func(err error) {
		if s, ok := err.(zx.Error); !ok || s.Status != zx.ErrPeerClosed {
			log.Printf("encountered in handling: %v", err)
		}
		if err := b.Remove(binding); err != nil {
			log.Printf("failed to remove binding: %v", err)
		}
	})
	if err != nil {
		return err
	}
	b.mu.Lock()
	b.Bindings = append(b.Bindings, binding)
	b.mu.Unlock()
	return nil
}

// Remove removes a Binding from the set.
func (b *BindingSet) Remove(binding *Binding) error {
	b.mu.Lock()
	defer b.mu.Unlock()
	for i := 0; i < len(b.Bindings); i++ {
		if b.Bindings[i] != binding {
			continue
		}
		// Swap remove the binding.
		b.Bindings[i] = b.Bindings[len(b.Bindings)-1]
		b.Bindings = b.Bindings[:len(b.Bindings)-1]
		break
	}
	return binding.Close()
}

// Close removes all the bindings from the set.
func (b *BindingSet) Close() {
	b.mu.Lock()
	defer b.mu.Unlock()
	for _, binding := range b.Bindings {
		binding.Close()
	}
	b.Bindings = []*Binding{}
}
