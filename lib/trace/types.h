// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_TRACING_LIB_TRACE_TYPES_H_
#define APPS_TRACING_LIB_TRACE_TYPES_H_

#include <magenta/syscalls/object.h>
#include <magenta/types.h>

#include <stdint.h>

namespace tracing {

// Enumerates all known record types.
enum class RecordType {
  kMetadata = 0,
  kInitialization = 1,
  kString = 2,
  kThread = 3,
  kEvent = 4
};

// MetadataType enumerates all known trace metadata types.
enum class MetadataType {
  kProviderInfo = 1,
  kProviderSection = 2,
};

// Enumerates all known argument types.
enum class ArgumentType {
  kNull = 0,
  kInt32 = 1,
  kInt64 = 2,
  kDouble = 3,
  kString = 4,
  kPointer = 5,
  kKoid = 6,
};

// EventType enumerates all known trace event types.
enum class EventType {
  kDurationBegin = 1,
  kDurationEnd = 2,
  kAsyncStart = 3,
  kAsyncInstant = 4,
  kAsyncEnd = 5,
};

// String index in a string table and in encoded form.
// These are stored as 16-bit values in the trace.
using StringIndex = uint32_t;
using EncodedStringRef = uint32_t;

// Thread index in a thread table and in encoded form.
// These are stored as 8-bit values in the trace.
using ThreadIndex = uint32_t;
using EncodedThreadRef = uint32_t;

// Trace provider id in a trace session.
using ProviderId = uint32_t;

// Represents a kernel object id value.
// This structure is used to distinguish koids from other 64-bit integers.
struct Koid {
  Koid() : value(0u) {}
  explicit Koid(mx_koid_t value) : value(value) {}

  explicit operator bool() const { return value; }

  mx_koid_t value;
};

// Represents a process koid and thread koid pair.
struct ProcessThread {
  ProcessThread() : process_koid(0u), thread_koid(0u) {}
  explicit ProcessThread(mx_koid_t process_koid, mx_koid_t thread_koid)
      : process_koid(process_koid), thread_koid(thread_koid) {}

  explicit operator bool() const { return thread_koid && process_koid; }

  mx_koid_t process_koid;
  mx_koid_t thread_koid;
};

}  // namepsace tracing

#endif  // APPS_TRACING_LIB_TRACE_TYPES_H_
