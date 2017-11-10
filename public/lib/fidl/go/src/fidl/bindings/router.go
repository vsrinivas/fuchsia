// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package bindings

import (
	"fmt"
	"sync"

	"syscall/zx"
	"syscall/zx/mxerror"
)

// MessageReadResult contains information returned after reading and parsing
// a message: a non-nil error of a valid message.
type MessageReadResult struct {
	Message *Message
	Error   error
}

// routeRequest is a request sent from Router to routerWorker.
type routeRequest struct {
	// The outgoing message with non-zero request id.
	message *Message
	// The channel to send respond for the message.
	responseChan chan<- MessageReadResult
}

// routerWorker sends messages that require a response and and routes responses
// to appropriate receivers. The work is done on a separate go routine.
type routerWorker struct {
	// The channel handle to send requests and receive responses.
	channel *zx.Channel
	// Map from request id to response channel.
	responders map[uint64]chan<- MessageReadResult
	// The channel of incoming requests that require responses.
	requestChan <-chan routeRequest
	// The channel that indicates that the worker should terminate.
	done <-chan struct{}
	// The channel to notify that the worker is exiting.
	workerDone chan<- struct{}
	// Implementation of async waiter.
	waiter   AsyncWaiter
	waitChan chan WaitResponse
	waitId   AsyncWaitId
}

// readOutstandingMessages reads and dispatches available messages in the
// channel until the messages is empty or there are no waiting responders.
// If the worker is currently waiting on the channel, returns immediately
// without an error.
func (w *routerWorker) readAndDispatchOutstandingMessages() error {
	if w.waitId != 0 {
		// Still waiting for a new message in the channel.
		return nil
	}
	for len(w.responders) > 0 {
		// TODO: what are the best initial sizes?
		bytes := make([]byte, 128)
		handles := make([]zx.Handle, 3)
	retry:
		numBytes, numHandles, err := w.channel.Read(bytes, handles, 0)
		switch mxerror.Status(err) {
		case zx.ErrOk:
			// NOP
		case zx.ErrBufferTooSmall:
			bytes = make([]byte, numBytes)
			handles = make([]zx.Handle, numHandles)
			goto retry
		case zx.ErrShouldWait:
			w.waitId = w.waiter.AsyncWait(w.channel.Handle,
				zx.SignalChannelReadable|zx.SignalChannelPeerClosed,
				w.waitChan)
			return nil
		default:
			return err
		}
		message, err := ParseMessage(bytes[:numBytes], handles[:numHandles])
		if err != nil {
			return err
		}
		id := message.Header.RequestId
		w.responders[id] <- MessageReadResult{message, nil}
		delete(w.responders, id)
	}
	return nil
}

func (w *routerWorker) cancelIfWaiting() {
	if w.waitId != 0 {
		w.waiter.CancelWait(w.waitId)
		w.waitId = 0
	}
}

// runLoop is the main run loop of the worker. It processes incoming requests
// from Router and waits on a channel for new messages.
// Returns an error describing the cause of stopping.
func (w *routerWorker) runLoop() error {
	for {
		select {
		case waitResponse := <-w.waitChan:
			w.waitId = 0
			if waitResponse.Error != nil {
				return waitResponse.Error
			}
		case request := <-w.requestChan:
			err := w.channel.Write(request.message.Bytes, request.message.Handles, 0)
			if request.responseChan != nil {
				w.responders[request.message.Header.RequestId] = request.responseChan
			}
			// Returns an error after registering the responseChan
			// so that the error will be reported to the chan.
			if err != nil {
				return err
			}
		case <-w.done:
			return zx.Error{Status: zx.ErrPeerClosed, Text: "bindings.routerWoker.runLoop"}
		}
		// Returns immediately without an error if still waiting for
		// a new message.
		if err := w.readAndDispatchOutstandingMessages(); err != nil {
			return err
		}
	}
}

// Router sends messages to a channel and routes responses back to senders
// of messages with non-zero request ids. The caller should issue unique request
// ids for each message given to the router.
type Router struct {
	// Mutex protecting requestChan from new requests in case the router is
	// closed and the handle.
	mu sync.Mutex
	// The channel to send requests and receive responses.
	channel *zx.Channel
	// Channel to communicate with worker.
	requestChan chan<- routeRequest

	// Makes sure that the done channel is closed once.
	closeOnce sync.Once
	// Channel to stop the worker.
	done chan<- struct{}
	// Channel that indicates that the worker exits.
	workerDone <-chan struct{}
}

// NewRouter returns a new Router instance that sends and receives messages
// from a provided channel handle.
func NewRouter(handle zx.Handle, waiter AsyncWaiter) *Router {
	requestChan := make(chan routeRequest, 10)
	doneChan := make(chan struct{})
	workerDoneChan := make(chan struct{})
	channel := &zx.Channel{handle}
	router := &Router{
		channel:     channel,
		requestChan: requestChan,
		done:        doneChan,
		workerDone:  workerDoneChan,
	}
	router.runWorker(&routerWorker{
		channel,
		make(map[uint64]chan<- MessageReadResult),
		requestChan,
		doneChan,
		workerDoneChan,
		waiter,
		make(chan WaitResponse, 1),
		0,
	})
	return router
}

// Close closes the router and the underlying channel. All new incoming
// requests are returned with an error.
func (r *Router) Close() {
	r.closeOnce.Do(func() {
		close(r.done)
		<- r.workerDone
	})
}

// Accept sends a message to the channel. The message should have a
// zero request id in header.
func (r *Router) Accept(message *Message) error {
	if message.Header.RequestId != 0 {
		return fmt.Errorf("message header should have a zero request ID")
	}
	r.mu.Lock()
	defer r.mu.Unlock()
	if !r.channel.Handle.IsValid() {
		return zx.Error{Status: zx.ErrPeerClosed, Text: "bindings.Router.Accept"}
	}
	r.requestChan <- routeRequest{message, nil}
	return nil
}

func (r *Router) runWorker(worker *routerWorker) {
	// Run worker on a separate go routine.
	go func() {
		// Get the reason why the worker stopped. The error means that
		// either the router is closed or there was an error reading
		// or writing to a channel. In both cases it will be
		// the reason why we can't process any more requests.
		err := worker.runLoop()
		worker.cancelIfWaiting()
		// Respond to all pending requests.
		for _, responseChan := range worker.responders {
			responseChan <- MessageReadResult{nil, err}
		}
		// Respond to incoming requests until we make sure that all
		// new requests return with an error before sending request
		// to responseChan.
		go func() {
			for responder := range worker.requestChan {
				responder.responseChan <- MessageReadResult{nil, err}
			}
		}()
		r.mu.Lock()
		r.channel.Close()
		// If we acquire the lock then no other go routine is waiting
		// to write to responseChan. All go routines that acquire the
		// lock after us will return before sending to responseChan as
		// the underlying handle is invalid (already closed).
		// We can safely close the requestChan.
		close(r.requestChan)
		r.mu.Unlock()
		// Router.Close waits for this signal.
		worker.workerDone <- struct{}{}
	}()
}

// AcceptWithResponse sends a message to the channel and returns a channel
// that will stream the result of reading corresponding response. The message
// should have a non-zero request id in header. It is responsibility of the
// caller to issue unique request ids for all given messages.
func (r *Router) AcceptWithResponse(message *Message) <-chan MessageReadResult {
	responseChan := make(chan MessageReadResult, 1)
	if message.Header.RequestId == 0 {
		responseChan <- MessageReadResult{nil, fmt.Errorf("message header should have a request ID")}
		return responseChan
	}
	r.mu.Lock()
	defer r.mu.Unlock()
	// Return an error before sending a request to requestChan if the router
	// is closed so that we can safely close responseChan once we close the
	// router.
	if !r.channel.Handle.IsValid() {
		responseChan <- MessageReadResult{nil, zx.Error{Status: zx.ErrPeerClosed, Text: "bindings.Router.AcceptWithResponse"}}
		return responseChan
	}
	r.requestChan <- routeRequest{message, responseChan}
	return responseChan
}
