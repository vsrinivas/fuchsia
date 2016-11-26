// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/tracing/lib/trace/writer.h"

#include <magenta/compiler.h>

#include "lib/ftl/logging.h"

namespace tracing {
namespace internal {

__WEAK void ReleaseEngine() {
  FTL_DCHECK(false);
}

}  // namespace internal

namespace writer {

__WEAK bool StartTracing(mx::vmo buffer,
                         mx::eventpair fence,
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

__WEAK StringRef TraceWriter::RegisterString(const char* constant) {
  FTL_DCHECK(false);
  return StringRef::MakeEmpty();
}

__WEAK ThreadRef TraceWriter::RegisterCurrentThread() {
  FTL_DCHECK(false);
  return ThreadRef::MakeInlined(0u, 0u);
}

__WEAK void TraceWriter::WriteInitializationRecord(uint64_t ticks_per_second) {
  FTL_DCHECK(false);
}

__WEAK void TraceWriter::WriteStringRecord(StringIndex index,
                                           const char* value) {
  FTL_DCHECK(false);
}

__WEAK void TraceWriter::WriteThreadRecord(ThreadIndex index,
                                           mx_koid_t process_koid,
                                           mx_koid_t thread_koid) {
  FTL_DCHECK(false);
}

__WEAK Payload TraceWriter::WriteKernelObjectRecordBase(mx_handle_t handle,
                                                        size_t argument_count,
                                                        size_t payload_size) {
  FTL_DCHECK(false);
  return Payload(nullptr);
}

__WEAK CategorizedTraceWriter
CategorizedTraceWriter::Prepare(const char* category_constant) {
  return CategorizedTraceWriter(nullptr, StringRef::MakeEmpty());
}

__WEAK Payload
CategorizedTraceWriter::WriteEventRecordBase(EventType type,
                                             const char* name,
                                             size_t argument_count,
                                             size_t payload_size) {
  FTL_DCHECK(false);
  return Payload(nullptr);
}

}  // namespace writer
}  // namespace tracing
