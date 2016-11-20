// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_TRACING_LIB_TRACE_H_
#define APPS_TRACING_LIB_TRACE_H_

#include "apps/tracing/lib/trace/writer.h"

// Converts a |uint64_t| koid value into one which can be passed as an
// argument to the trace macros to distinguish it from other 64-bit integers.
#define TRACE_KOID(value) ::tracing::Koid(value)

// Returns true if tracing is active and the category is enabled right now.
#define TRACE_CATEGORY_ENABLED(category) \
  TRACE_INTERNAL_CATEGORY_ENABLED(category)

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

#endif  // APPS_TRACING_LIB_TRACE_H_
