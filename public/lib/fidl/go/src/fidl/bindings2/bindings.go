// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package bindings2

import (
	"errors"
	"log"
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

// Binding binds the implementation of a Stub to a channel.
//
// A Binding listens for incoming messages on the channel, decodes them, and
// asks the Stub to dispatch to the appropriate implementation of the interface.
// If the message expects a reply, the Binding will also encode the reply and
// send it back over the channel.
type Binding struct {
	// stub is a wrapper around an implementation of a FIDL interface which
	// knows how to dispatch to a method by ordinal.
	stub Stub

	// channel is the channel primitive to which the stub is bound.
	channel zx.Channel

	// id is an extra bit of state about the underlying wait in the
	// process-local dispatcher so we can unbind later.
	id *dispatch.WaitID

	// errHandler is an error handler which will be called if a
	// connection error is encountered.
	errHandler func(error)
}

// NewBinding returns a new Binding with only the Stub set.
//
// One must explicitly call Bind on the Binding to bind a channel to the Stub.
func NewBinding(s Stub) *Binding {
	return &Binding{stub: s}
}

// dispatch reads from the underlying channel and dispatches into the stub.
//
// Returns whether we should continue to wait before reading more from the channel,
// and potentially an error.
func (b *Binding) dispatch() (bool, error) {
	// Allocate maximum size of a message on the stack.
	var respb [zx.ChannelMaxMessageBytes]byte
	var resph [zx.ChannelMaxMessageHandles]zx.Handle
	nb, nh, err := b.channel.Read(respb[:], resph[:], 0)
	if err != nil {
		zxErr, ok := err.(zx.Error)
		if ok && zxErr.Status == zx.ErrShouldWait {
			return true, nil
		}
		return false, nil
	}
	var header MessageHeader
	if err := UnmarshalHeader(respb[:], &header); err != nil {
		return false, err
	}
	start := MessageHeaderSize
	p, err := b.stub.Dispatch(header.Ordinal, respb[start:start+int(nb)], resph[:nh])
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
	if err := b.channel.Write(respb[:cnb], resph[:cnh], 0); err != nil {
		return false, err
	}
	return false, nil
}

// Bind binds a channel to the stub. A Binding may only be bound to one
// channel at at a time.
//
// Bind also accepts an error handler which will be executed when a
// connection error occurs.
func (b *Binding) Bind(c zx.Channel, e func(error)) error {
	if b.Bound() {
		return errors.New("binding already bound")
	}
	b.channel = c
	b.errHandler = e

	// Declare the wait handler as a closure.
	h := func(d *dispatch.Dispatcher, s zx.Status, sigs *zx.PacketSignal) dispatch.WaitResult {
		if s != zx.ErrOk {
			b.errHandler(&zx.Error{Status: s})
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
		b.errHandler(&zx.Error{Status: zx.ErrPeerClosed})
		return dispatch.WaitFinished
	}

	// Start the wait on the channel.
	id, err := d.BeginWait(zx.Handle(c), zx.SignalChannelReadable|zx.SignalChannelPeerClosed, 0, h)
	if err != nil {
		return err
	}
	b.id = &id
	return nil
}

// Bound returns true if the Binding already has a channel bound to it.
func (b *Binding) Bound() bool {
	return b.id != nil
}

// Unbind cancels an outstanding waits, resets the Binding's state, and
// closes the bound channel.
func (b *Binding) Unbind() error {
	if !b.Bound() {
		return nil
	}
	if err := d.CancelWait(*b.id); err != nil {
		return err
	}
	b.id = nil
	return b.channel.Close()
}

// BindingSet is a managed set of Bindings which know how to unbind and
// remove themselves in the event of a connection error.
type BindingSet struct {
	bindings []*Binding
}

// Add creates a new Binding and adds it to the set.
func (b *BindingSet) Add(s Stub, c zx.Channel) error {
	binding := NewBinding(s)
	err := binding.Bind(c, func(err error) {
		log.Println(err)
		b.Remove(binding)
	})
	if err != nil {
		return err
	}
	b.bindings = append(b.bindings, binding)
	return nil
}

// Remove removes a Binding from the set.
func (b *BindingSet) Remove(binding *Binding) error {
	for i := 0; i < len(b.bindings); i++ {
		if b.bindings[i] != binding {
			continue
		}
		b.bindings = append(b.bindings[:i], b.bindings[i+1:]...)
		break
	}
	return binding.Unbind()
}
