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

__WEAK StringRef TraceWriter::RegisterString(const char* constant,
                                             bool check_category) {
  FTL_DCHECK(false);
  return StringRef::MakeEmpty();
}

__WEAK StringRef TraceWriter::RegisterStringCopy(const std::string& string) {
  FTL_DCHECK(false);
  return StringRef::MakeEmpty();
}

__WEAK ThreadRef TraceWriter::RegisterCurrentThread() {
  FTL_DCHECK(false);
  return ThreadRef::MakeUnknown();
}

__WEAK ThreadRef TraceWriter::RegisterThread(mx_koid_t process_koid,
                                             mx_koid_t thread_koid) {
  FTL_DCHECK(false);
  return ThreadRef::MakeUnknown();
}

__WEAK void TraceWriter::WriteProcessDescription(
    mx_koid_t process_koid,
    const std::string& process_name) {
  FTL_DCHECK(false);
}

__WEAK void TraceWriter::WriteThreadDescription(
    mx_koid_t process_koid,
    mx_koid_t thread_koid,
    const std::string& thread_name) {
  FTL_DCHECK(false);
}

__WEAK Payload TraceWriter::WriteKernelObjectRecordBase(mx_handle_t handle,
                                                        size_t argument_count,
                                                        size_t payload_size) {
  FTL_DCHECK(false);
  return Payload(nullptr);
}

__WEAK Payload TraceWriter::WriteEventRecordBase(EventType type,
                                                 Ticks event_time,
                                                 const ThreadRef& thread_ref,
                                                 const StringRef& category_ref,
                                                 const StringRef& name_ref,
                                                 size_t argument_count,
                                                 size_t payload_size) {
  FTL_DCHECK(false);
  return Payload(nullptr);
}

}  // namespace writer
}  // namespace tracing
