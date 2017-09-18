// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/trace/writer.h"

#include <zircon/compiler.h>

#include "lib/fxl/logging.h"

namespace tracing {
namespace internal {

__WEAK void ReleaseEngine() {
  FXL_DCHECK(false);
}

}  // namespace internal

namespace writer {

__WEAK bool StartTracing(zx::vmo buffer,
                         zx::eventpair fence,
                         std::vector<std::string> enabled_categories,
                         TraceFinishedCallback finished_callback) {
  return false;
}

__WEAK void StopTracing() {}

__WEAK bool IsTracingEnabled() {
  return false;
}

__WEAK bool IsTracingEnabledForCategory(const char* category) {
  return false;
}

__WEAK std::vector<std::string> GetEnabledCategories() {
  return std::vector<std::string>();
}

__WEAK TraceState GetTraceState() {
  return TraceState::kFinished;
}

__WEAK TraceHandlerKey AddTraceHandler(TraceHandler handler) {
  return 0u;
}

__WEAK void RemoveTraceHandler(TraceHandlerKey handler_key) {}

__WEAK TraceWriter TraceWriter::Prepare() {
  return TraceWriter(nullptr);
}

__WEAK StringRef TraceWriter::RegisterString(const char* constant,
                                             bool check_category) {
  FXL_DCHECK(false);
  return StringRef::MakeEmpty();
}

__WEAK StringRef TraceWriter::RegisterStringCopy(const std::string& string) {
  FXL_DCHECK(false);
  return StringRef::MakeEmpty();
}

__WEAK ThreadRef TraceWriter::RegisterCurrentThread() {
  FXL_DCHECK(false);
  return ThreadRef::MakeUnknown();
}

__WEAK ThreadRef TraceWriter::RegisterThread(zx_koid_t process_koid,
                                             zx_koid_t thread_koid) {
  FXL_DCHECK(false);
  return ThreadRef::MakeUnknown();
}

__WEAK void TraceWriter::WriteProcessDescription(
    zx_koid_t process_koid,
    const std::string& process_name) {
  FXL_DCHECK(false);
}

__WEAK void TraceWriter::WriteThreadDescription(
    zx_koid_t process_koid,
    zx_koid_t thread_koid,
    const std::string& thread_name) {
  FXL_DCHECK(false);
}

__WEAK Payload TraceWriter::WriteKernelObjectRecordBase(zx_handle_t handle,
                                                        size_t argument_count,
                                                        size_t payload_size) {
  FXL_DCHECK(false);
  return Payload(nullptr);
}

__WEAK void TraceWriter::WriteContextSwitchRecord(
    Ticks event_time,
    CpuNumber cpu_number,
    ThreadState outgoing_thread_state,
    const ThreadRef& outgoing_thread_ref,
    const ThreadRef& incoming_thread_ref) {
  FXL_DCHECK(engine_);
}

__WEAK void TraceWriter::WriteLogRecord(Ticks event_time,
                                        const ThreadRef& thread_ref,
                                        const char* log_message,
                                        size_t log_message_length) {
  FXL_DCHECK(engine_);
}

__WEAK Payload TraceWriter::WriteEventRecordBase(EventType type,
                                                 Ticks event_time,
                                                 const ThreadRef& thread_ref,
                                                 const StringRef& category_ref,
                                                 const StringRef& name_ref,
                                                 size_t argument_count,
                                                 size_t payload_size) {
  FXL_DCHECK(false);
  return Payload(nullptr);
}

}  // namespace writer
}  // namespace tracing
