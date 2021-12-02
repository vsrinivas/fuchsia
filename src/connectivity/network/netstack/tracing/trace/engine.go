// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//go:build tracing
// +build tracing

// This file implements a Go equivalent of:
// https://fuchsia.googlesource.com/fuchsia/+/HEAD/zircon/system/ulib/trace-engine/engine.cc

package trace

import (
	"errors"
	"fmt"
	"sync/atomic"
	"syscall/zx"
	"time"

	syslog "go.fuchsia.dev/fuchsia/src/lib/syslog/go"
)

const tag = "trace-engine"

const (
	Stopped uint64 = iota
	Started
	Stopping
)

// gState holds the state of the trace engine.
var gState uint64

// gContext holds the pointer to the current Context.
var gContext *context

// gContextRefs maintains the reference count for the current Context. There are
// actually two separate counters allocated in one word so that the two counter
// values are retrieved with one atomic action:
//
// |63 ... 8| = buffer acquisition count
// |7 ... 0| = prolonged acquisition count
//
// Buffer acquisition count is updated by acquireContext/releaseContext when a
// trace site writes to the trace buffer. Prolonged acquisition count is
// updated by Provider via EngineStart/EngineStop.
//
// When the engine is stopped, both counters are 0. If both counters are 0,
// acquireContext cannot update the counter. Only EngineStart can update the
// counter if both counters are 0.
var gContextRefs int64

const (
	prolongedCounterShift     = 0
	prolongedCounterIncrement = 1 << prolongedCounterShift
	maxProlongedCounter       = 127
	prolongedCounterMask      = 0xff
	bufferCounterShift        = 8
	bufferCounterIncrement    = 1 << bufferCounterShift
)

var (
	ErrNotSupported = errors.New("not supported")
)

func getProlongedContextRefs() int64 {
	return atomic.LoadInt64(&gContextRefs) & prolongedCounterMask
}

func getBufferContextRefs() int64 {
	return atomic.LoadInt64(&gContextRefs) >> bufferCounterShift
}

// EngineState returns the current state of the trace engine.
func EngineState() uint64 {
	return atomic.LoadUint64(&gState)
}

// EngineInitialize is called by Provider to initialize the trace engine.
// (We assume Engine{Initialize,Start,Stop,Terminate} are not called concurrently)
func EngineInitialize(start uintptr, size uint64, mode BufferingMode, handler Handler) error {
	switch mode {
	case Oneshot:
		// do nothing
	case Circular:
		// TODO(https://fxbug.dev/78263): Support circular mode.
		return fmt.Errorf("Circular mode (%d): %w", mode, ErrNotSupported)
	case Streaming:
		// TODO(https://fxbug.dev/78265): Support streaming mode.
		return fmt.Errorf("Streaming mode (%d): %w", mode, ErrNotSupported)
	default:
		panic(fmt.Sprintf("unknown mode (%d)", mode))
	}
	gContext = newContext(start, size, mode, handler)
	gContext.clearEntireBuffer()
	gContext.writeInitializationRecord(uint64(zx.Sys_ticks_per_second()))
	return nil
}

// EngineStart is called by Provider when the trace is started.
// (We assume Engine{Initialize,Start,Stop,Terminate} are not called concurrently)
func EngineStart(startMode StartMode) error {
	switch state := atomic.LoadUint64(&gState); state {
	case Started, Stopping:
		return fmt.Errorf("Engine is not stopped (state = %d)", atomic.LoadUint64(&gState))
	case Stopped:
		// do nothing
	default:
		panic(fmt.Sprintf("unknown state (%d)", state))
	}
	atomic.StoreUint64(&gState, Started)
	gContext.handler.TraceStarted()

	switch startMode {
	case ClearEntireBuffer:
		gContext.clearEntireBuffer()
		gContext.writeInitializationRecord(uint64(zx.Sys_ticks_per_second()))
	case ClearNonDurableBuffer:
		gContext.clearRollingBuffers()
		gContext.writeInitializationRecord(uint64(zx.Sys_ticks_per_second()))
	case RetainBuffer:
		// do nothing.
	default:
		panic(fmt.Sprintf("unknown startMode (%d)", startMode))
	}
	atomic.AddInt64(&gContextRefs, prolongedCounterIncrement)
	return nil
}

// EngineStop is called by Provider when the trace is stopped.
// (We assume Engine{Initialize,Start,Stop,Terminate} are not called concurrently)
func EngineStop() {
	switch state := atomic.LoadUint64(&gState); state {
	case Stopping, Stopped:
		return
	case Started:
		// do nothing
	default:
		panic(fmt.Sprintf("unknown state (%d)", state))
	}
	engineStopCommon()
	gContext.handler.TraceStopped()
}

var engineStopErrorCounter uint64

func engineStopCommon() {
	switch state := atomic.LoadUint64(&gState); state {
	case Stopped:
		return
	case Started:
		atomic.AddInt64(&gContextRefs, -prolongedCounterIncrement)
		atomic.StoreUint64(&gState, Stopping)
	case Stopping:
		// do nothing.
	default:
		panic(fmt.Sprintf("unknown state (%d)", state))
	}
	// Wait until all clients call releaseContext.
	interval := time.Millisecond
	maxElapsedTime := time.Second
	elapsedTime := time.Duration(0)
	for atomic.LoadInt64(&gContextRefs) > 0 {
		if elapsedTime >= maxElapsedTime {
			atomic.AddUint64(&engineStopErrorCounter, 1)
			_ = syslog.WarnTf(tag, "EngineStop: gContextRefs = %d (want: 0)", atomic.LoadInt64(&gContextRefs))
			break
		}
		time.Sleep(interval)
		elapsedTime += interval
		interval *= 2
	}
	atomic.StoreInt64(&gContextRefs, 0)

	atomic.StoreUint64(&gState, Stopped)
	gContext.UpdateBufferHeaderAfterStopped()
}

// EngineTerminate is called by Provider when the trace is terminated.
// (We assume Engine{Initialize,Start,Stop,Terminate} are not called concurrently)
func EngineTerminate() {
	switch state := atomic.LoadUint64(&gState); state {
	case Started, Stopping:
		engineStopCommon()
	case Stopped:
		// do nothing
	default:
		panic(fmt.Sprintf("unknown state (%d)", state))
	}
	gContext.handler.TraceTerminated()
	gContext = nil
}

// acquireContext is called from the trace sites when they start recording
// events.
func acquireContext() (*context, bool) {
	// Check if we could write into the trace buffer. gContextRef has two
	// counters inside. prolongedCounter is incremented when the trace is
	// started and decremented when the trace is stopped. Another is the
	// buffer reference counter. If both counters are zero, we can't write
	// into the buffer. See the comments for gContextRefs for more details.
	if atomic.LoadInt64(&gContextRefs) == 0 {
		return nil, false
	}
	// Increment the buffer reference counter.
	atomic.AddInt64(&gContextRefs, bufferCounterIncrement)
	return gContext, true
}

// releaseContext is called by the trace sites when they finished recording
// events.
func releaseContext() {
	// Decrement the buffer reference counter.
	atomic.AddInt64(&gContextRefs, -bufferCounterIncrement)
}
