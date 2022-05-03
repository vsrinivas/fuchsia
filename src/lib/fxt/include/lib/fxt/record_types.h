// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// FXT Record constants for FXT as defined in
// docs/reference/tracing/trace-format.md

#ifndef SRC_LIB_FXT_INCLUDE_LIB_FXT_RECORD_TYPES_H_
#define SRC_LIB_FXT_INCLUDE_LIB_FXT_RECORD_TYPES_H_

#include <zircon/syscalls/object.h>

#include <cstdint>
#include <type_traits>

namespace fxt {
// Enumerates all known record types.
enum class RecordType {
  kMetadata = 0,
  kInitialization = 1,
  kString = 2,
  kThread = 3,
  kEvent = 4,
  kBlob = 5,
  kKernelObject = 7,
  kContextSwitch = 8,
  kLog = 9,

  // The kLargeRecord uses a 32-bit size field.
  kLargeRecord = 15,
};

enum class LargeRecordType {
  kBlob = 0,
};

// MetadataType enumerates all known trace metadata types.
enum class MetadataType {
  kProviderInfo = 1,
  kProviderSection = 2,
  kProviderEvent = 3,
  kTraceInfo = 4,
};

// Enumerates all provider events.
enum class ProviderEventType {
  kBufferOverflow = 0,
};

// Enumerates all known trace info types.
enum class TraceInfoType {
  kMagicNumber = 0,
};

// The four byte value present in a magic number record.
constexpr uint32_t kMagicValue = 0x16547846;

// Enumerates all known argument types.
enum class ArgumentType {
  kNull = 0,
  kInt32 = 1,
  kUint32 = 2,
  kInt64 = 3,
  kUint64 = 4,
  kDouble = 5,
  kString = 6,
  kPointer = 7,
  kKoid = 8,
  kBool = 9,
};

// EventType enumerates all known trace event types.
enum class EventType {
  kInstant = 0,
  kCounter = 1,
  kDurationBegin = 2,
  kDurationEnd = 3,
  kDurationComplete = 4,
  kAsyncBegin = 5,
  kAsyncInstant = 6,
  kAsyncEnd = 7,
  kFlowBegin = 8,
  kFlowStep = 9,
  kFlowEnd = 10,
};

// Specifies the scope of instant events.
enum class EventScope {
  kThread = 0,
  kProcess = 1,
  kGlobal = 2,
};

// Trace provider id in a trace session.
using ProviderId = uint32_t;

// Thread states used to describe context switches.
enum class ThreadState {
  kNew = ZX_THREAD_STATE_NEW,
  kRunning = ZX_THREAD_STATE_RUNNING,
  kSuspended = ZX_THREAD_STATE_SUSPENDED,
  kBlocked = ZX_THREAD_STATE_BLOCKED,
  kDying = ZX_THREAD_STATE_DYING,
  kDead = ZX_THREAD_STATE_DEAD,
};

using ArgumentHeader = uint64_t;
using RecordHeader = uint64_t;

}  // namespace fxt

#endif  // SRC_LIB_FXT_INCLUDE_LIB_FXT_RECORD_TYPES_H_
