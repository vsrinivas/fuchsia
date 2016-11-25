// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_TRACING_LIB_TRACE_H_
#define APPS_TRACING_LIB_TRACE_H_

#include "apps/tracing/lib/trace/writer.h"

// Converts a |mx_koid_t| koid value into one which can be passed as an
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

// Writes an instant event representing a single moment in time (a probe).
// Use |TRACE_SCOPE_*| constants to specify the scope of the event.
#define TRACE_INSTANT0(category, name, scope) \
  TRACE_INTERNAL_INSTANT(category, name, scope)
#define TRACE_INSTANT1(category, name, scope, k1, v1) \
  TRACE_INTERNAL_INSTANT(category, name, scope,       \
                         TRACE_INTERNAL_MAKE_ARGS1(k1, v1))
#define TRACE_INSTANT2(category, name, scope, k1, v1, k2, v2) \
  TRACE_INTERNAL_INSTANT(category, name, scope,               \
                         TRACE_INTERNAL_MAKE_ARGS2(k1, v1, k2, v2))
#define TRACE_INSTANT3(category, name, scope, k1, v1, k2, v2, k3, v3) \
  TRACE_INTERNAL_INSTANT(category, name, scope,                       \
                         TRACE_INTERNAL_MAKE_ARGS3(k1, v1, k2, v2, k3, v3))
#define TRACE_INSTANT4(category, name, scope, k1, v1, k2, v2, k3, v3, k4, v4) \
  TRACE_INTERNAL_INSTANT(                                                     \
      category, name, scope,                                                  \
      TRACE_INTERNAL_MAKE_ARGS4(k1, v1, k2, v2, k3, v3, k4, v4))

// Writes a counter event with the specified id.  The arguments to this
// event are numeric samples which may be presented by the visualizer as a
// stack area chart.
#define TRACE_COUNTER1(category, name, id, k1, v1) \
  TRACE_INTERNAL_COUNTER(category, name, id, TRACE_INTERNAL_MAKE_ARGS1(k1, v1))
#define TRACE_COUNTER2(category, name, id, k1, v1, k2, v2) \
  TRACE_INTERNAL_COUNTER(category, name, id,               \
                         TRACE_INTERNAL_MAKE_ARGS2(k1, v1, k2, v2))
#define TRACE_COUNTER3(category, name, id, k1, v1, k2, v2, k3, v3) \
  TRACE_INTERNAL_COUNTER(category, name, id,                       \
                         TRACE_INTERNAL_MAKE_ARGS3(k1, v1, k2, v2, k3, v3))
#define TRACE_COUNTER4(category, name, id, k1, v1, k2, v2, k3, v3, k4, v4) \
  TRACE_INTERNAL_COUNTER(                                                  \
      category, name, id,                                                  \
      TRACE_INTERNAL_MAKE_ARGS4(k1, v1, k2, v2, k3, v3, k4, v4))

// Writes a duration event which ends when the current scope exits.
#define TRACE_DURATION0(category, name) TRACE_INTERNAL_DURATION(category, name)
#define TRACE_DURATION1(category, name, k1, v1) \
  TRACE_INTERNAL_DURATION(category, name, TRACE_INTERNAL_MAKE_ARGS1(k1, v1))
#define TRACE_DURATION2(category, name, k1, v1, k2, v2) \
  TRACE_INTERNAL_DURATION(category, name,               \
                          TRACE_INTERNAL_MAKE_ARGS2(k1, v1, k2, v2))
#define TRACE_DURATION3(category, name, k1, v1, k2, v2, k3, v3) \
  TRACE_INTERNAL_DURATION(category, name,                       \
                          TRACE_INTERNAL_MAKE_ARGS3(k1, v1, k2, v2, k3, v3))
#define TRACE_DURATION4(category, name, k1, v1, k2, v2, k3, v3, k4, v4) \
  TRACE_INTERNAL_DURATION(category, name, TRACE_INTERNAL_MAKE_ARGS4(    \
                                              k1, v1, k2, v2, k3, v3, k4, v4))

// Writes a duration begin event only.
#define TRACE_DURATION_BEGIN0(category, name) \
  TRACE_INTERNAL_DURATION_BEGIN(category, name)
#define TRACE_DURATION_BEGIN1(category, name, k1, v1) \
  TRACE_INTERNAL_DURATION_BEGIN(category, name,       \
                                TRACE_INTERNAL_MAKE_ARGS1(k1, v1))
#define TRACE_DURATION_BEGIN2(category, name, k1, v1, k2, v2) \
  TRACE_INTERNAL_DURATION_BEGIN(category, name,               \
                                TRACE_INTERNAL_MAKE_ARGS2(k1, v1, k2, v2))
#define TRACE_DURATION_BEGIN3(category, name, k1, v1, k2, v2, k3, v3) \
  TRACE_INTERNAL_DURATION_BEGIN(                                      \
      category, name, TRACE_INTERNAL_MAKE_ARGS3(k1, v1, k2, v2, k3, v3))
#define TRACE_DURATION_BEGIN4(category, name, k1, v1, k2, v2, k3, v3, k4, v4) \
  TRACE_INTERNAL_DURATION_BEGIN(                                              \
      category, name,                                                         \
      TRACE_INTERNAL_MAKE_ARGS4(k1, v1, k2, v2, k3, v3, k4, v4))

// Writes a duration end event only.
#define TRACE_DURATION_END0(category, name) \
  TRACE_INTERNAL_DURATION_END(category, name)
#define TRACE_DURATION_END1(category, name, k1, v1) \
  TRACE_INTERNAL_DURATION_END(category, name, TRACE_INTERNAL_MAKE_ARGS1(k1, v1))
#define TRACE_DURATION_END2(category, name, k1, v1, k2, v2) \
  TRACE_INTERNAL_DURATION_END(category, name,               \
                              TRACE_INTERNAL_MAKE_ARGS2(k1, v1, k2, v2))
#define TRACE_DURATION_END3(category, name, k1, v1, k2, v2, k3, v3) \
  TRACE_INTERNAL_DURATION_END(                                      \
      category, name, TRACE_INTERNAL_MAKE_ARGS3(k1, v1, k2, v2, k3, v3))
#define TRACE_DURATION_END4(category, name, k1, v1, k2, v2, k3, v3, k4, v4) \
  TRACE_INTERNAL_DURATION_END(                                              \
      category, name,                                                       \
      TRACE_INTERNAL_MAKE_ARGS4(k1, v1, k2, v2, k3, v3, k4, v4))

// Writes an asynchronous begin event with the specified id.
#define TRACE_ASYNC_BEGIN0(category, name, id) \
  TRACE_INTERNAL_ASYNC_BEGIN(category, name, id)
#define TRACE_ASYNC_BEGIN1(category, name, id, k1, v1) \
  TRACE_INTERNAL_ASYNC_BEGIN(category, name, id,       \
                             TRACE_INTERNAL_MAKE_ARGS1(k1, v1))
#define TRACE_ASYNC_BEGIN2(category, name, id, k1, v1, k2, v2) \
  TRACE_INTERNAL_ASYNC_BEGIN(category, name, id,               \
                             TRACE_INTERNAL_MAKE_ARGS2(k1, v1, k2, v2))
#define TRACE_ASYNC_BEGIN3(category, name, id, k1, v1, k2, v2, k3, v3) \
  TRACE_INTERNAL_ASYNC_BEGIN(                                          \
      category, name, id, TRACE_INTERNAL_MAKE_ARGS3(k1, v1, k2, v2, k3, v3))
#define TRACE_ASYNC_BEGIN4(category, name, id, k1, v1, k2, v2, k3, v3, k4, v4) \
  TRACE_INTERNAL_ASYNC_BEGIN(                                                  \
      category, name, id,                                                      \
      TRACE_INTERNAL_MAKE_ARGS4(k1, v1, k2, v2, k3, v3, k4, v4))

// Writes an asynchronous instant event with the specified id.
#define TRACE_ASYNC_INSTANT0(category, name, id) \
  TRACE_INTERNAL_ASYNC_INSTANT(category, name, id)
#define TRACE_ASYNC_INSTANT1(category, name, id, k1, v1) \
  TRACE_INTERNAL_ASYNC_INSTANT(category, name, id,       \
                               TRACE_INTERNAL_MAKE_ARGS1(k1, v1))
#define TRACE_ASYNC_INSTANT2(category, name, id, k1, v1, k2, v2) \
  TRACE_INTERNAL_ASYNC_INSTANT(category, name, id,               \
                               TRACE_INTERNAL_MAKE_ARGS2(k1, v1, k2, v2))
#define TRACE_ASYNC_INSTANT3(category, name, id, k1, v1, k2, v2, k3, v3) \
  TRACE_INTERNAL_ASYNC_INSTANT(                                          \
      category, name, id, TRACE_INTERNAL_MAKE_ARGS3(k1, v1, k2, v2, k3, v3))
#define TRACE_ASYNC_INSTANT4(category, name, id, k1, v1, k2, v2, k3, v3, k4, \
                             v4)                                             \
  TRACE_INTERNAL_ASYNC_INSTANT(                                              \
      category, name, id,                                                    \
      TRACE_INTERNAL_MAKE_ARGS4(k1, v1, k2, v2, k3, v3, k4, v4))

// Writes an asynchronous instant event with the specified id.
#define TRACE_ASYNC_END0(category, name, id) \
  TRACE_INTERNAL_ASYNC_END(category, name, id)
#define TRACE_ASYNC_END1(category, name, id, k1, v1) \
  TRACE_INTERNAL_ASYNC_END(category, name, id,       \
                           TRACE_INTERNAL_MAKE_ARGS1(k1, v1))
#define TRACE_ASYNC_END2(category, name, id, k1, v1, k2, v2) \
  TRACE_INTERNAL_ASYNC_END(category, name, id,               \
                           TRACE_INTERNAL_MAKE_ARGS2(k1, v1, k2, v2))
#define TRACE_ASYNC_END3(category, name, id, k1, v1, k2, v2, k3, v3) \
  TRACE_INTERNAL_ASYNC_END(category, name, id,                       \
                           TRACE_INTERNAL_MAKE_ARGS3(k1, v1, k2, v2, k3, v3))
#define TRACE_ASYNC_END4(category, name, id, k1, v1, k2, v2, k3, v3, k4, v4) \
  TRACE_INTERNAL_ASYNC_END(                                                  \
      category, name, id,                                                    \
      TRACE_INTERNAL_MAKE_ARGS4(k1, v1, k2, v2, k3, v3, k4, v4))

// Writes a description of a kernel object indicated by |handle|,
// including its koid, name, and the supplied arguments.
#define TRACE_HANDLE0(handle) TRACE_INTERNAL_HANDLE(handle)
#define TRACE_HANDLE1(handle, k1, v1) \
  TRACE_INTERNAL_HANDLE(handle, TRACE_INTERNAL_MAKE_ARGS1(k1, v1))
#define TRACE_HANDLE2(handle, k1, v1, k2, v2) \
  TRACE_INTERNAL_HANDLE(handle, TRACE_INTERNAL_MAKE_ARGS2(k1, v1, k2, v2))
#define TRACE_HANDLE3(handle, k1, v1, k2, v2, k3, v3) \
  TRACE_INTERNAL_HANDLE(handle,                       \
                        TRACE_INTERNAL_MAKE_ARGS3(k1, v1, k2, v2, k3, v3))
#define TRACE_HANDLE4(handle, k1, v1, k2, v2, k3, v3, k4, v4) \
  TRACE_INTERNAL_HANDLE(                                      \
      handle, TRACE_INTERNAL_MAKE_ARGS4(k1, v1, k2, v2, k3, v3, k4, v4))

#endif  // APPS_TRACING_LIB_TRACE_H_
