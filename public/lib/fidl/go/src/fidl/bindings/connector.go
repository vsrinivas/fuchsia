// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package bindings

import (
	"sync"

	"syscall/zx"
	"syscall/zx/mxerror"
)

var errConnectionClosed = zx.Error{Status: zx.ErrPeerClosed}

// Connector owns a channel handle. It can read and write messages
// from the channel waiting on it if necessary. The operation are
// thread-safe.
type Connector struct {
	mu      sync.RWMutex // protects channel handle
	channel *zx.Channel

	done      chan struct{}
	waitMutex sync.Mutex
	waiter    AsyncWaiter
	waitChan  chan WaitResponse
}

// NewStubConnector returns a new |Connector| instance that sends and
// receives messages from a provided channel handle.
func NewConnector(handle zx.Handle, waiter AsyncWaiter) *Connector {
	return &Connector{
		channel:  &zx.Channel{handle},
		waiter:   waiter,
		done:     make(chan struct{}),
		waitChan: make(chan WaitResponse, 1),
	}
}

// ReadMessage reads a message from channel waiting on it if necessary.
func (c *Connector) ReadMessage() (*Message, error) {
	// Make sure that only one goroutine at a time waits a the handle.
	// We use separate lock so that we can send messages to the channel
	// while waiting on the channel.
	//
	// It is better to acquire this lock first so that a potential queue of
	// readers will wait while closing the channel in case of Close()
	// call.
	c.waitMutex.Lock()
	defer c.waitMutex.Unlock()
	// Get read lock to use channel handle
	c.mu.RLock()
	defer c.mu.RUnlock()

	if !c.channel.Handle.IsValid() {
		return nil, errConnectionClosed
	}

	// TODO: what are the best initial sizes?
	bytes := make([]byte, 128)
	handles := make([]zx.Handle, 3)
retry:
	numBytes, numHandles, err := c.channel.Read(bytes, handles, 0)
	switch mxerror.Status(err) {
	case zx.ErrOk:
		// NOP
	case zx.ErrBufferTooSmall:
		bytes = make([]byte, numBytes)
		handles = make([]zx.Handle, numHandles)
		goto retry
	case zx.ErrShouldWait:
		waitId := c.waiter.AsyncWait(c.channel.Handle,
			zx.SignalChannelReadable|zx.SignalChannelPeerClosed,
			c.waitChan)
		select {
		case <-c.waitChan:
			// We've got a message. Retry reading.
			goto retry
		case <-c.done:
			c.waiter.CancelWait(waitId)
			return nil, errConnectionClosed
		}
	default:
		return nil, err
	}
	return ParseMessage(bytes, handles)
}

// WriteMessage writes a message to the channel.
func (c *Connector) WriteMessage(message *Message) error {
	// Get read lock to use channel handle
	c.mu.RLock()
	defer c.mu.RUnlock()
	if !c.channel.Handle.IsValid() {
		return errConnectionClosed
	}
	return c.channel.Write(message.Bytes, message.Handles, 0)
}

// Close closes the channel aborting wait on the channel.
// Panics if you try to close the |Connector| more than once.
func (c *Connector) Close() {
	// Stop waiting to acquire the lock.
	close(c.done)
	// Get write lock to close channel handle
	c.mu.Lock()
	c.channel.Close()
	c.mu.Unlock()
}
