// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_TRACE_EVENT_H_
#define GARNET_LIB_TRACE_EVENT_H_

#include "garnet/lib/trace/internal/event_helpers.h"

// Converts a |zx_koid_t| koid value into one which can be passed as an
// argument to the trace macros to distinguish it from other 64-bit integers.
#define TRACE_KOID(value) (::tracing::Koid(value))

// Returns true if tracing is active.
#define TRACE_ENABLED() TRACE_INTERNAL_ENABLED()

// Returns true if tracing is active and the category is enabled right now.
#define TRACE_CATEGORY_ENABLED(category) \
  TRACE_INTERNAL_CATEGORY_ENABLED(category)

// Specifies that an event is only relevant to the thread it occurred on.
#define TRACE_SCOPE_THREAD (::tracing::EventScope::kThread)

// Specifies that an event is only relevant to the process in which it occurred.
#define TRACE_SCOPE_PROCESS (::tracing::EventScope::kProcess)

// Specifies that an event is globally relevant.
#define TRACE_SCOPE_GLOBAL (::tracing::EventScope::kGlobal)

// Returns an uint64 that can be used as identifier for TRACE_ASYNC_* and
// TRACE_FLOW_*.
#define TRACE_NONCE() TRACE_INTERNAL_NONCE()

// Writes an instant event representing a single moment in time (a probe).
// Use |TRACE_SCOPE_*| constants to specify the scope of the event.
#define TRACE_INSTANT(category, name, scope, args...) \
  TRACE_INTERNAL_INSTANT(category, name, scope, TRACE_INTERNAL_MAKE_ARGS(args))

// Writes a counter event with the specified id.  The arguments to this
// event are numeric samples which may be presented by the visualizer as a
// stack area chart.
#define TRACE_COUNTER(category, name, id, k1, v1, args...) \
  TRACE_INTERNAL_COUNTER(category, name, id,               \
                         TRACE_INTERNAL_MAKE_ARGS(k1, v1, ##args))

// Writes a duration event which ends when the current scope exits.
#define TRACE_DURATION(category, name, args...) \
  TRACE_INTERNAL_DURATION(category, name, TRACE_INTERNAL_MAKE_ARGS(args))

// Writes a duration begin event only.
#define TRACE_DURATION_BEGIN(category, name, args...) \
  TRACE_INTERNAL_DURATION_BEGIN(category, name, TRACE_INTERNAL_MAKE_ARGS(args))

// Writes a duration end event only.
#define TRACE_DURATION_END(category, name, args...) \
  TRACE_INTERNAL_DURATION_END(category, name, TRACE_INTERNAL_MAKE_ARGS(args))

// Writes an asynchronous begin event with the specified id.
#define TRACE_ASYNC_BEGIN(category, name, id, args...) \
  TRACE_INTERNAL_ASYNC_BEGIN(category, name, id, TRACE_INTERNAL_MAKE_ARGS(args))

// Writes an asynchronous instant event with the specified id.
#define TRACE_ASYNC_INSTANT(category, name, id, args...) \
  TRACE_INTERNAL_ASYNC_INSTANT(category, name, id,       \
                               TRACE_INTERNAL_MAKE_ARGS(args))

// Writes an asynchronous end event with the specified id.
#define TRACE_ASYNC_END(category, name, id, args...) \
  TRACE_INTERNAL_ASYNC_END(category, name, id, TRACE_INTERNAL_MAKE_ARGS(args))

// Writes a flow begin event with the specified id.
#define TRACE_FLOW_BEGIN(category, name, id, args...) \
  TRACE_INTERNAL_FLOW_BEGIN(category, name, id, TRACE_INTERNAL_MAKE_ARGS(args))

// Writes a flow step event with the specified id.
#define TRACE_FLOW_STEP(category, name, id, args...) \
  TRACE_INTERNAL_FLOW_STEP(category, name, id, TRACE_INTERNAL_MAKE_ARGS(args))

// Writes a flow end event with the specified id.
#define TRACE_FLOW_END(category, name, id, args...) \
  TRACE_INTERNAL_FLOW_END(category, name, id, TRACE_INTERNAL_MAKE_ARGS(args))

// Writes a description of a kernel object indicated by |handle|,
// including its koid, name, and the supplied arguments.
#define TRACE_HANDLE(handle, args...) \
  TRACE_INTERNAL_HANDLE(handle, TRACE_INTERNAL_MAKE_ARGS(args))

#endif  // GARNET_LIB_TRACE_EVENT_H_
