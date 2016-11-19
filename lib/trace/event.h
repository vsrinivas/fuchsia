// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_TRACING_LIB_TRACE_EVENT_H_
#define APPS_TRACING_LIB_TRACE_EVENT_H_

#include "apps/tracing/lib/trace/writer.h"

// Converts a |uint64_t| koid value into one which can be passed as an
// argument to the trace macros to distinguish it from other 64-bit integers.
#define TRACE_KOID(value) ::tracing::Koid(value)

// Returns true if tracing is active and the category is enabled right now.
#define TRACE_CATEGORY_ENABLED(cat) TRACE_INTERNAL_CATEGORY_ENABLED(cat)

// Writes a duration event which ends when the current scope exits.
#define TRACE_EVENT0(cat, name) TRACE_INTERNAL_EVENT_DURATION(cat, name)
#define TRACE_EVENT1(cat, name, k1, v1) \
  TRACE_INTERNAL_EVENT_DURATION(cat, name, TRACE_INTERNAL_MAKE_ARGS1(k1, v1))
#define TRACE_EVENT2(cat, name, k1, v1, k2, v2) \
  TRACE_INTERNAL_EVENT_DURATION(cat, name,      \
                                TRACE_INTERNAL_MAKE_ARGS2(k1, v1, k2, v2))
#define TRACE_EVENT3(cat, name, k1, v1, k2, v2, k3, v3) \
  TRACE_INTERNAL_EVENT_DURATION(                        \
      cat, name, TRACE_INTERNAL_MAKE_ARGS3(k1, v1, k2, v2, k3, v3))
#define TRACE_EVENT4(cat, name, k1, v1, k2, v2, k3, v3, k4, v4) \
  TRACE_INTERNAL_EVENT_DURATION(                                \
      cat, name, TRACE_INTERNAL_MAKE_ARGS4(k1, v1, k2, v2, k3, v3, k4, v4))

// Writes an asynchronous begin event with the specified id.
#define TRACE_EVENT_ASYNC_BEGIN0(cat, name, id) \
  TRACE_INTERNAL_EVENT_ASYNC_BEGIN(cat, name, id)
#define TRACE_EVENT_ASYNC_BEGIN1(cat, name, id, k1, v1) \
  TRACE_INTERNAL_EVENT_ASYNC_BEGIN(cat, name, id,       \
                                   TRACE_INTERNAL_MAKE_ARGS1(k1, v1))
#define TRACE_EVENT_ASYNC_BEGIN2(cat, name, id, k1, v1, k2, v2) \
  TRACE_INTERNAL_EVENT_ASYNC_BEGIN(cat, name, id,               \
                                   TRACE_INTERNAL_MAKE_ARGS2(k1, v1, k2, v2))
#define TRACE_EVENT_ASYNC_BEGIN3(cat, name, id, k1, v1, k2, v2, k3, v3) \
  TRACE_INTERNAL_EVENT_ASYNC_BEGIN(                                     \
      cat, name, id, TRACE_INTERNAL_MAKE_ARGS3(k1, v1, k2, v2, k3, v3))
#define TRACE_EVENT_ASYNC_BEGIN4(cat, name, id, k1, v1, k2, v2, k3, v3, k4, \
                                 v4)                                        \
  TRACE_INTERNAL_EVENT_ASYNC_BEGIN(                                         \
      cat, name, id,                                                        \
      TRACE_INTERNAL_MAKE_ARGS4(k1, v1, k2, v2, k3, v3, k4, v4))

// Writes an asynchronous instant event with the specified id.
#define TRACE_EVENT_ASYNC_INSTANT0(cat, name, id) \
  TRACE_INTERNAL_EVENT_ASYNC_INSTANT(cat, name, id)
#define TRACE_EVENT_ASYNC_INSTANT1(cat, name, id, k1, v1) \
  TRACE_INTERNAL_EVENT_ASYNC_INSTANT(cat, name, id,       \
                                     TRACE_INTERNAL_MAKE_ARGS1(k1, v1))
#define TRACE_EVENT_ASYNC_INSTANT2(cat, name, id, k1, v1, k2, v2) \
  TRACE_INTERNAL_EVENT_ASYNC_INSTANT(                             \
      cat, name, id, TRACE_INTERNAL_MAKE_ARGS2(k1, v1, k2, v2))
#define TRACE_EVENT_ASYNC_INSTANT3(cat, name, id, k1, v1, k2, v2, k3, v3) \
  TRACE_INTERNAL_EVENT_ASYNC_INSTANT(                                     \
      cat, name, id, TRACE_INTERNAL_MAKE_ARGS3(k1, v1, k2, v2, k3, v3))
#define TRACE_EVENT_ASYNC_INSTANT4(cat, name, id, k1, v1, k2, v2, k3, v3, k4, \
                                   v4)                                        \
  TRACE_INTERNAL_EVENT_ASYNC_INSTANT(                                         \
      cat, name, id,                                                          \
      TRACE_INTERNAL_MAKE_ARGS4(k1, v1, k2, v2, k3, v3, k4, v4))

// Writes an asynchronous instant event with the specified id.
#define TRACE_EVENT_ASYNC_END0(cat, name, id) \
  TRACE_INTERNAL_EVENT_ASYNC_END(cat, name, id)
#define TRACE_EVENT_ASYNC_END1(cat, name, id, k1, v1) \
  TRACE_INTERNAL_EVENT_ASYNC_END(cat, name, id,       \
                                 TRACE_INTERNAL_MAKE_ARGS1(k1, v1))
#define TRACE_EVENT_ASYNC_END2(cat, name, id, k1, v1, k2, v2) \
  TRACE_INTERNAL_EVENT_ASYNC_END(cat, name, id,               \
                                 TRACE_INTERNAL_MAKE_ARGS2(k1, v1, k2, v2))
#define TRACE_EVENT_ASYNC_END3(cat, name, id, k1, v1, k2, v2, k3, v3) \
  TRACE_INTERNAL_EVENT_ASYNC_END(                                     \
      cat, name, id, TRACE_INTERNAL_MAKE_ARGS3(k1, v1, k2, v2, k3, v3))
#define TRACE_EVENT_ASYNC_END4(cat, name, id, k1, v1, k2, v2, k3, v3, k4, v4) \
  TRACE_INTERNAL_EVENT_ASYNC_END(                                             \
      cat, name, id,                                                          \
      TRACE_INTERNAL_MAKE_ARGS4(k1, v1, k2, v2, k3, v3, k4, v4))

#endif  // APPS_TRACING_LIB_TRACE_EVENT_H_
