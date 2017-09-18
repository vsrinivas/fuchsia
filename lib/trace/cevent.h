// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/tracing/lib/trace/ctypes.h"
#include "apps/tracing/lib/trace/internal/cevent_helpers.h"

#define TA_NULL(n) { .type = CTRACE_ARGUMENTTYPE_KNULL, .name = n, { .u64 = 0 } }
#define TA_I32(n, x) { .type = CTRACE_ARGUMENT_INT32, .name = n, { .i32 = (x) } }
#define TA_U32(n, x) { .type = CTRACE_ARGUMENT_UINT32, .name = n, { .u32 = (x) } }
#define TA_I64(n, x) { .type = CTRACE_ARGUMENT_INT64, .name = n, { .i64 = (x) } }
#define TA_U64(n, x) { .type = CTRACE_ARGUMENT_UINT64, .name = n, { .u64 = (x) } }
#define TA_DOUBLE(n, x) { .type = CTRACE_ARGUMENT_DOUBLE, .name = n, { .d = (x) } }
#define TA_STR(n, x) { .type = CTRACE_ARGUMENT_STRING, .name = n, { .s = (x) } }
#define TA_PTR(n, x) { .type = CTRACE_ARGUMENT_POINTER, .name = n, { .p = (x) } }
#define TA_KOID(n, x) { .type = CTRACE_ARGUMENT_KOID, .name = n, { .koid = (x) } }

// Helper macro for client code to construct Ctrace_ArgList objects.
#define CTRACE_MAKE_LOCAL_ARGS(var, ...) \
  CTRACE_INTERNAL_MAKE_LOCAL_ARGS(var, __VA_ARGS__)

// Returns true if tracing is active.
#define CTRACE_ENABLED() CTRACE_INTERNAL_ENABLED()

// Returns true if tracing is active and the category is enabled right now.
#define CTRACE_CATEGORY_ENABLED(category) \
  CTRACE_INTERNAL_CATEGORY_ENABLED(category)

// Returns an uint64 that can be used as identifier for CTRACE_ASYNC_* and
// CTRACE_FLOW_*.
#define CTRACE_NONCE() CTRACE_INTERNAL_NONCE()

// Writes an instant event representing a single moment in time (a probe).
// Use |CTRACE_SCOPE_*| constants to specify the scope of the event.
#define CTRACE_INSTANT(category, name, scope, args...) \
  CTRACE_INTERNAL_INSTANT(category, name, scope, (args))

// Writes a counter event with the specified id.  The arguments to this
// event are numeric samples which may be presented by the visualizer as a
// stack area chart.
#define CTRACE_COUNTER(category, name, id, arg1, args...) \
  CTRACE_INTERNAL_COUNTER(category, name, id, (arg1, ##args))

// Writes a duration event which ends when the current scope exits.
#define CTRACE_DURATION(category, name, args...) \
  CTRACE_INTERNAL_DURATION(category, name, (args))

// Writes a duration begin event only.
#define CTRACE_DURATION_BEGIN(category, name, args...) \
  CTRACE_INTERNAL_DURATION_BEGIN(category, name, (args))

// Writes a duration end event only.
#define CTRACE_DURATION_END(category, name, args...) \
  CTRACE_INTERNAL_DURATION_END(category, name, (args))

// Writes an asynchronous begin event with the specified id.
#define CTRACE_ASYNC_BEGIN(category, name, id, args...) \
  CTRACE_INTERNAL_ASYNC_BEGIN(category, name, id, (args))

// Writes an asynchronous instant event with the specified id.
#define CTRACE_ASYNC_INSTANT(category, name, id, args...) \
  CTRACE_INTERNAL_ASYNC_INSTANT(category, name, id, (args))

// Writes an asynchronous end event with the specified id.
#define CTRACE_ASYNC_END(category, name, id, args...) \
  CTRACE_INTERNAL_ASYNC_END(category, name, id, (args))

// Writes a flow begin event with the specified id.
#define CTRACE_FLOW_BEGIN(category, name, id, args...) \
  CTRACE_INTERNAL_FLOW_BEGIN(category, name, id, (args))

// Writes a flow step event with the specified id.
#define CTRACE_FLOW_STEP(category, name, id, args...) \
  CTRACE_INTERNAL_FLOW_STEP(category, name, id, (args))

// Writes a flow end event with the specified id.
#define CTRACE_FLOW_END(category, name, id, args...) \
  CTRACE_INTERNAL_FLOW_END(category, name, id, (args))

// Writes a description of a kernel object indicated by |handle|,
// including its koid, name, and the supplied arguments.
#define CTRACE_HANDLE(handle, args...) \
  CTRACE_INTERNAL_HANDLE(handle, (args))
