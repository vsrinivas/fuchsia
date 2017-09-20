// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package bindings

import (
	"fmt"
	"runtime"
	"sync"

	"syscall/zx"
	"syscall/zx/mxerror"
)

var defaultWaiter *asyncWaiterImpl
var once sync.Once

// GetAsyncWaiter returns a default implementation of |AsyncWaiter| interface.
func GetAsyncWaiter() AsyncWaiter {
	once.Do(func() {
		defaultWaiter = newAsyncWaiter()
	})
	return defaultWaiter
}

// AsyncWaitId is an id returned by |AsyncWait()| used to cancel it.
type AsyncWaitId uint64

// WaitResponse is a struct sent to a channel waiting for |AsyncWait()| to
// finish. It contains the same information as if |Wait()| was called on a
// handle.
type WaitResponse struct {
	Error   error
	Pending zx.Signals
}

// AsyncWaiter defines an interface for asynchronously waiting (and cancelling
// asynchronous waits) on a handle.
type AsyncWaiter interface {
	// AsyncWait asynchronously waits on a given handle until a signal
	// indicated by |signals| is satisfied or it becomes known that no
	// signal indicated by |signals| will ever be satisified. The wait
	// response will be sent to |responseChan|.
	//
	// |handle| must not be closed or transferred until the wait response
	// is received from |responseChan|.
	AsyncWait(handle zx.Handle, signals zx.Signals, responseChan chan<- WaitResponse) AsyncWaitId

	// CancelWait cancels an outstanding async wait (specified by |id|)
	// initiated by |AsyncWait()|. A response with
	// |ErrCanceled| is sent to the corresponding |responseChan|.
	CancelWait(id AsyncWaitId)
}

// waitRequest is a struct sent to asyncWaiterWorker to add another handle to
// the list of waiting handles.
type waitRequest struct {
	handle  zx.Handle
	signals zx.Signals

	// Used for |CancelWait()| calls. The worker should issue IDs so that
	// you can't cancel the wait until the worker received the wait request.
	idChan chan<- AsyncWaitId

	// A channel end to send wait results.
	responseChan chan<- WaitResponse
}

// asyncWaiterWorker does the actual work, in its own goroutine. It calls
// |WaitMany()| on all provided handles. New handles a added via |waitChan|
// and removed via |cancelChan| messages. To wake the worker asyncWaiterImpl
// sends fidl messages to a dedicated channel, the other end of which has
// index 0 in all slices of the worker.
type asyncWaiterWorker struct {
	// |items| is used to make |WaitMany()| calls directly.
	// All these arrays should be operated simultaneously; i-th element
	// of each refers to i-th handle.
	items        []zx.WaitItem
	asyncWaitIds []AsyncWaitId
	responses    []chan<- WaitResponse

	waitChan   <-chan waitRequest // should have a non-empty buffer
	cancelChan <-chan AsyncWaitId // should have a non-empty buffer
	ids        uint64             // is incremented each |AsyncWait()| call
}

// removeHandle removes handle at provided index without sending response by
// swapping all information associated with index-th handle with the last one
// and removing the last one.
func (w *asyncWaiterWorker) removeHandle(index int) {
	l := len(w.items) - 1
	// Swap with the last and remove last.
	w.items[index] = w.items[l]
	w.items = w.items[0:l]

	w.asyncWaitIds[index] = w.asyncWaitIds[l]
	w.asyncWaitIds = w.asyncWaitIds[0:l]
	w.responses[index] = w.responses[l]
	w.responses = w.responses[0:l]
}

// sendWaitResponseAndRemove send response to corresponding channel and removes
// index-th waiting handle.
func (w *asyncWaiterWorker) sendWaitResponseAndRemove(index int, err error, pending zx.Signals) {
	w.responses[index] <- WaitResponse{
		err,
		pending,
	}
	w.removeHandle(index)
}

// respondToSatisfiedWaits responds to all wait requests that have at least
// one satisfied signal and removes them.
func (w *asyncWaiterWorker) respondToSatisfiedWaits() {
	// Don't touch handle at index 0 as it is the waking channel.
	for i := 1; i < len(w.items); {
		if (w.items[i].Pending & w.items[i].WaitFor) != 0 {
			// Respond and swap i-th with last and remove last.
			w.sendWaitResponseAndRemove(i, nil, w.items[i].Pending)
		} else {
			i++
		}
	}
}

// processIncomingRequests processes all queued async wait or cancel requests
// sent by asyncWaiterImpl.
func (w *asyncWaiterWorker) processIncomingRequests() {
	for {
		select {
		case request := <-w.waitChan:
			w.items = append(w.items, zx.WaitItem{Handle: request.handle, WaitFor: request.signals})
			w.responses = append(w.responses, request.responseChan)

			w.ids++
			id := AsyncWaitId(w.ids)
			w.asyncWaitIds = append(w.asyncWaitIds, id)
			request.idChan <- id
		case asyncWaitId := <-w.cancelChan:
			// Zero index is reserved for the waking channel handle.
			index := 0
			for i := 1; i < len(w.asyncWaitIds); i++ {
				if w.asyncWaitIds[i] == asyncWaitId {
					index = i
					break
				}
			}
			// Do nothing if the id was not found as wait response may be
			// already sent if the async wait was successful.
			if index > 0 {
				w.sendWaitResponseAndRemove(index, zx.Error{Status: zx.ErrCanceled, Text: "bindings.CancelWait"}, zx.Signals(0))
			}
		default:
			return
		}
	}
}

// runLoop run loop of the asyncWaiterWorker. Blocks on |WaitMany()|. If the
// wait is interrupted by waking channel (index 0) then it means that the worker
// was woken by waiterImpl, so the worker processes incoming requests from
// waiterImpl; otherwise responses to corresponding wait request.
func (w *asyncWaiterWorker) runLoop() {
Loop:
	for {
		err := zx.WaitMany(w.items, zx.TimensecInfinite)
		switch mxerror.Status(err) {
		case zx.ErrOk, zx.ErrTimedOut:
			// NOP
		default:
			panic(fmt.Sprintf("error waiting on handles: %v", err))
			break Loop
		}
		// Zero index means that the worker was signaled by asyncWaiterImpl.
		if (w.items[0].Pending & w.items[0].WaitFor) != 0 {
			// Clear the signal from asyncWaiterImpl.
			w.items[0].Handle.Signal(zx.SignalUser0, 0)
			w.processIncomingRequests()
		} else {
			w.respondToSatisfiedWaits()
		}
	}
}

// asyncWaiterImpl is an implementation of |AsyncWaiter| interface.
// Runs a worker in a separate goroutine and comunicates with it by sending a
// signal to |wakingEvent| to wake worker from |WaitMany()| call and
// sending request via |waitChan| and |cancelChan|.
type asyncWaiterImpl struct {
	wakingEvent zx.Event

	waitChan   chan<- waitRequest // should have a non-empty buffer
	cancelChan chan<- AsyncWaitId // should have a non-empty buffer
}

func finalizeWorker(worker *asyncWaiterWorker) {
	// Close waking channel on worker side.
	worker.items[0].Handle.Close()
}

func finalizeAsyncWaiter(waiter *asyncWaiterImpl) {
	waiter.wakingEvent.Close()
}

// newAsyncWaiter creates an asyncWaiterImpl and starts its worker goroutine.
func newAsyncWaiter() *asyncWaiterImpl {
	e0, err := zx.NewEvent(0)
	if err != nil {
		panic(fmt.Sprintf("can't create event %v", err))
	}
	waitChan := make(chan waitRequest, 10)
	cancelChan := make(chan AsyncWaitId, 10)
	e1, err := e0.Duplicate(zx.RightSameRights)
	if err != nil {
		panic(fmt.Sprintf("can't duplicate event %v", err))
	}
	item := zx.WaitItem{Handle: zx.Handle(e1), WaitFor: zx.SignalUser0}
	worker := &asyncWaiterWorker{
		[]zx.WaitItem{item},
		[]AsyncWaitId{0},
		[]chan<- WaitResponse{make(chan WaitResponse)},
		waitChan,
		cancelChan,
		0,
	}
	runtime.SetFinalizer(worker, finalizeWorker)
	go worker.runLoop()
	waiter := &asyncWaiterImpl{
		wakingEvent: e0,
		waitChan:    waitChan,
		cancelChan:  cancelChan,
	}
	runtime.SetFinalizer(waiter, finalizeAsyncWaiter)
	return waiter
}

// wakeWorker wakes the worker from |WaitMany()| call. This should be called
// after sending a message to |waitChan| or |cancelChan| to avoid deadlock.
func (w *asyncWaiterImpl) wakeWorker() {
	// Send a signal.
	// TODO: Add Signal method to zx.Event type
	err := zx.Handle(w.wakingEvent).Signal(0, zx.SignalUser0)
	if err != nil {
		panic("can't signal an event")
	}
}

func (w *asyncWaiterImpl) AsyncWait(handle zx.Handle, signals zx.Signals, responseChan chan<- WaitResponse) AsyncWaitId {
	idChan := make(chan AsyncWaitId, 1)
	w.waitChan <- waitRequest{
		handle,
		signals,
		idChan,
		responseChan,
	}
	w.wakeWorker()
	return <-idChan
}

func (w *asyncWaiterImpl) CancelWait(id AsyncWaitId) {
	w.cancelChan <- id
	w.wakeWorker()
}
