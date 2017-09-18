// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Note: We only define in C things needed by the C API.
// As such there are two types.h files: This one and types.h for C++.

#pragma once

#include <zircon/compiler.h>

#include <stdint.h>

__BEGIN_CDECLS

// Enumerates all known argument types.
typedef enum {
  CTRACE_ARGUMENT_NULL = 0,
  CTRACE_ARGUMENT_INT32 = 1,
  CTRACE_ARGUMENT_UINT32 = 2,
  CTRACE_ARGUMENT_INT64 = 3,
  CTRACE_ARGUMENT_UINT64 = 4,
  CTRACE_ARGUMENT_DOUBLE = 5,
  CTRACE_ARGUMENT_STRING = 6,
  CTRACE_ARGUMENT_POINTER = 7,
  CTRACE_ARGUMENT_KOID = 8,
} ctrace_argument_t;

// Specifies the scope of instant events.
typedef enum {
  // The event is only relevant to the thread it occurred on.
  CTRACE_SCOPE_THREAD = 0,
  // The event is only relevant to the process in which it occurred.
  CTRACE_SCOPE_PROCESS = 1,
  // The event is globally relevant.
  CTRACE_SCOPE_GLOBAL = 2,
} ctrace_scope_t;

// The encoded form of strings. See writer.{h,cc}.
typedef uint32_t ctrace_encoded_stringref_t;

// The encoded form of threads. See writer.{h,cc}.
typedef uint32_t ctrace_encoded_threadref_t;

__END_CDECLS
