// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//go:build tracing
// +build tracing

// This file implements a Go equivalent of C++ trace library found at:
// https://fuchsia.googlesource.com/fuchsia/+/HEAD/zircon/system/ulib/trace/include/lib/trace/internal/event_common.h
// https://fuchsia.googlesource.com/fuchsia/+/HEAD/zircon/system/ulib/trace/event.cc

// Package trace provides the functions for instrumenting Go programs to capture
// trace data. It also implements low-level trace engine functions.
package trace

import (
	"syscall/zx"
)

// Instant writes an instant event representing a single moment in time (a probe).
func Instant(category, name string, scope EventScope) {
	internalEventRecord(category, name, func(c *context, ticks ticksT, threadRef *threadRef, categoryRef, nameRef *stringRef) {
		c.writeInstantEventRecord(ticks, threadRef, categoryRef, nameRef, scope, nil)
	})
}

// Counter writes a counter event with the specified id.
func Counter(category, name string, counterID CounterID) {
	internalEventRecord(category, name, func(c *context, ticks ticksT, threadRef *threadRef, categoryRef, nameRef *stringRef) {
		c.writeCounterEventRecord(ticks, threadRef, categoryRef, nameRef, counterID, nil)
	})
}

// DurationBegin writes a duration begin event.
func DurationBegin(category, name string) {
	internalEventRecord(category, name, func(c *context, ticks ticksT, threadRef *threadRef, categoryRef, nameRef *stringRef) {
		c.writeDurationBeginEventRecord(ticks, threadRef, categoryRef, nameRef, nil)
	})
}

// DurationEnd writes a duration end event.
func DurationEnd(category, name string) {
	internalEventRecord(category, name, func(c *context, ticks ticksT, threadRef *threadRef, categoryRef, nameRef *stringRef) {
		c.writeDurationEndEventRecord(ticks, threadRef, categoryRef, nameRef, nil)
	})
}

// AsyncBegin writes an asynchronous begin event with the specified id.
func AsyncBegin(category, name string, asyncID AsyncID) {
	internalEventRecord(category, name, func(c *context, ticks ticksT, threadRef *threadRef, categoryRef, nameRef *stringRef) {
		c.writeAsyncBeginEventRecord(ticks, threadRef, categoryRef, nameRef, asyncID, nil)
	})
}

// AsyncInstant writes an asynchronous instant event with the specified id.
func AsyncInstant(category, name string, asyncID AsyncID) {
	internalEventRecord(category, name, func(c *context, ticks ticksT, threadRef *threadRef, categoryRef, nameRef *stringRef) {
		c.writeAsyncInstantEventRecord(ticks, threadRef, categoryRef, nameRef, asyncID, nil)
	})
}

// AsyncEnd writes an asynchronous end event with the specified id.
func AsyncEnd(category, name string, asyncID AsyncID) {
	internalEventRecord(category, name, func(c *context, ticks ticksT, threadRef *threadRef, categoryRef, nameRef *stringRef) {
		c.writeAsyncEndEventRecord(ticks, threadRef, categoryRef, nameRef, asyncID, nil)
	})
}

// FlowBegin writes a flow begin event with the specified id.
func FlowBegin(category, name string, flowID FlowID) {
	internalEventRecord(category, name, func(c *context, ticks ticksT, threadRef *threadRef, categoryRef, nameRef *stringRef) {
		c.writeFlowBeginEventRecord(ticks, threadRef, categoryRef, nameRef, flowID, nil)
	})
}

// FlowStep writes a flow step event with the specified id.
func FlowStep(category, name string, flowID FlowID) {
	internalEventRecord(category, name, func(c *context, ticks ticksT, threadRef *threadRef, categoryRef, nameRef *stringRef) {
		c.writeFlowStepEventRecord(ticks, threadRef, categoryRef, nameRef, flowID, nil)
	})
}

// FlowEnd writes a flow end event with the specified id.
func FlowEnd(category, name string, flowID FlowID) {
	internalEventRecord(category, name, func(c *context, ticks ticksT, threadRef *threadRef, categoryRef, nameRef *stringRef) {
		c.writeFlowEndEventRecord(ticks, threadRef, categoryRef, nameRef, flowID, nil)
	})
}

// Blob writes a blob of binary data to the trace buffer.
func Blob(typ BlobType, name string, blob []byte) {
	internalSimpleRecord(name, func(c *context, nameRef *stringRef) {
		c.writeBlobRecord(typ, nameRef, blob)
	})
}

func internalEventRecord(category, name string, f func(c *context, ticks ticksT, threadRef *threadRef, categoryRef, nameRef *stringRef)) {
	c, ok := acquireContext()
	if !ok {
		return
	}
	defer releaseContext()
	c.mu.Lock()
	defer c.mu.Unlock()
	categoryRef, enabled := c.registerStringLocked(category, true)
	if !enabled {
		return
	}
	nameRef, _ := c.registerStringLocked(name, false)
	threadRef := c.registerCurrentThreadLocked()
	ticks := ticksT(zx.Sys_ticks_get())
	f(c, ticks, &threadRef, &categoryRef, &nameRef)
}

func internalSimpleRecord(name string, f func(c *context, nameRef *stringRef)) {
	c, ok := acquireContext()
	if !ok {
		return
	}
	defer releaseContext()
	c.mu.Lock()
	defer c.mu.Unlock()
	nameRef, _ := c.registerStringLocked(name, false)
	f(c, &nameRef)
}
