// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Helpers for event macros.
// Note: This file is only intended to be included from event.h.

#ifndef APPS_TRACING_LIB_TRACE_INTERNAL_EVENT_HELPERS_H_
#define APPS_TRACING_LIB_TRACE_INTERNAL_EVENT_HELPERS_H_

#include "apps/tracing/lib/trace/writer.h"

#define TRACE_INTERNAL_ENABLED() ::tracing::writer::IsTracingEnabled()
#define TRACE_INTERNAL_CATEGORY_ENABLED(category) \
  ::tracing::writer::IsTracingEnabledForCategory(category)

#define TRACE_INTERNAL_NONCE() ::tracing::internal::TraceNonce()

#define TRACE_INTERNAL_SCOPE_LABEL__(token) __trace_scope_##token
#define TRACE_INTERNAL_SCOPE_LABEL_(token) TRACE_INTERNAL_SCOPE_LABEL__(token)
#define TRACE_INTERNAL_SCOPE_LABEL() TRACE_INTERNAL_SCOPE_LABEL_(__COUNTER__)

#define TRACE_INTERNAL_WRITER __trace_writer
#define TRACE_INTERNAL_EVENT_CATEGORY_REF __trace_event_category_ref

#define TRACE_INTERNAL_MAKE_ARGS(args...) \
  ::tracing::writer::MakeArgumentList(TRACE_INTERNAL_WRITER, ##args)

#define TRACE_INTERNAL_SIMPLE(stmt)                                         \
  do {                                                                      \
    auto TRACE_INTERNAL_WRITER = ::tracing::writer::TraceWriter::Prepare(); \
    if (TRACE_INTERNAL_WRITER) {                                            \
      stmt;                                                                 \
    }                                                                       \
  } while (0)

// TODO(jeffbrown): Determine whether we should try to do anything to
// further reduce the code expansion here.  The current goal is to avoid
// evaluating and expanding arguments unless the category is enabled.
#define TRACE_INTERNAL_EVENT(category, stmt)                                \
  do {                                                                      \
    auto TRACE_INTERNAL_WRITER = ::tracing::writer::TraceWriter::Prepare(); \
    if (TRACE_INTERNAL_WRITER) {                                            \
      auto TRACE_INTERNAL_EVENT_CATEGORY_REF =                              \
          TRACE_INTERNAL_WRITER.RegisterString(category, true);             \
      if (!TRACE_INTERNAL_EVENT_CATEGORY_REF.is_empty()) {                  \
        stmt;                                                               \
      }                                                                     \
    }                                                                       \
  } while (0)

#define TRACE_INTERNAL_INSTANT(category, name, scope, args)                   \
  TRACE_INTERNAL_EVENT(                                                       \
      category, ::tracing::internal::WriteInstantEventRecord(                 \
                    TRACE_INTERNAL_WRITER, TRACE_INTERNAL_EVENT_CATEGORY_REF, \
                    name, scope, args))

#define TRACE_INTERNAL_COUNTER(category, name, id, args)                      \
  TRACE_INTERNAL_EVENT(                                                       \
      category, ::tracing::internal::WriteCounterEventRecord(                 \
                    TRACE_INTERNAL_WRITER, TRACE_INTERNAL_EVENT_CATEGORY_REF, \
                    name, id, args))

#define TRACE_INTERNAL_DURATION_BEGIN(category, name, args)                   \
  TRACE_INTERNAL_EVENT(                                                       \
      category, ::tracing::internal::WriteDurationBeginEventRecord(           \
                    TRACE_INTERNAL_WRITER, TRACE_INTERNAL_EVENT_CATEGORY_REF, \
                    name, args))

#define TRACE_INTERNAL_DURATION_END(category, name, args)                     \
  TRACE_INTERNAL_EVENT(                                                       \
      category, ::tracing::internal::WriteDurationEndEventRecord(             \
                    TRACE_INTERNAL_WRITER, TRACE_INTERNAL_EVENT_CATEGORY_REF, \
                    name, args))

#define TRACE_INTERNAL_DURATION_SCOPE(scope_label, scope_category, scope_name, \
                                      args)                                    \
  ::tracing::internal::DurationEventScope scope_label(scope_category,          \
                                                      scope_name);             \
  TRACE_INTERNAL_DURATION_BEGIN(scope_label.category(), scope_label.name(),    \
                                args)

#define TRACE_INTERNAL_DURATION(category, name, args)                         \
  TRACE_INTERNAL_DURATION_SCOPE(TRACE_INTERNAL_SCOPE_LABEL(), category, name, \
                                args)

#define TRACE_INTERNAL_ASYNC_BEGIN(category, name, id, args)                  \
  TRACE_INTERNAL_EVENT(                                                       \
      category, ::tracing::internal::WriteAsyncBeginEventRecord(              \
                    TRACE_INTERNAL_WRITER, TRACE_INTERNAL_EVENT_CATEGORY_REF, \
                    name, id, args))

#define TRACE_INTERNAL_ASYNC_INSTANT(category, name, id, args)                \
  TRACE_INTERNAL_EVENT(                                                       \
      category, ::tracing::internal::WriteAsyncInstantEventRecord(            \
                    TRACE_INTERNAL_WRITER, TRACE_INTERNAL_EVENT_CATEGORY_REF, \
                    name, id, args))

#define TRACE_INTERNAL_ASYNC_END(category, name, id, args)                    \
  TRACE_INTERNAL_EVENT(                                                       \
      category, ::tracing::internal::WriteAsyncEndEventRecord(                \
                    TRACE_INTERNAL_WRITER, TRACE_INTERNAL_EVENT_CATEGORY_REF, \
                    name, id, args))

#define TRACE_INTERNAL_FLOW_BEGIN(category, name, id, args)                   \
  TRACE_INTERNAL_EVENT(                                                       \
      category, ::tracing::internal::WriteFlowBeginEventRecord(               \
                    TRACE_INTERNAL_WRITER, TRACE_INTERNAL_EVENT_CATEGORY_REF, \
                    name, id, args))

#define TRACE_INTERNAL_FLOW_STEP(category, name, id, args)                    \
  TRACE_INTERNAL_EVENT(                                                       \
      category, ::tracing::internal::WriteFlowStepEventRecord(                \
                    TRACE_INTERNAL_WRITER, TRACE_INTERNAL_EVENT_CATEGORY_REF, \
                    name, id, args))

#define TRACE_INTERNAL_FLOW_END(category, name, id, args)                     \
  TRACE_INTERNAL_EVENT(                                                       \
      category, ::tracing::internal::WriteFlowEndEventRecord(                 \
                    TRACE_INTERNAL_WRITER, TRACE_INTERNAL_EVENT_CATEGORY_REF, \
                    name, id, args))

#define TRACE_INTERNAL_HANDLE(handle, args) \
  TRACE_INTERNAL_SIMPLE(                    \
      TRACE_INTERNAL_WRITER.WriteKernelObjectRecord(handle, args))

namespace tracing {
namespace internal {

uint64_t TraceNonce();

template <typename... Args>
void WriteInstantEventRecord(::tracing::writer::TraceWriter& writer,
                             const ::tracing::writer::StringRef& category_ref,
                             const char* name,
                             ::tracing::EventScope scope,
                             Args&&... args) {
  writer.WriteInstantEventRecord(
      ::tracing::GetTicksNow(), writer.RegisterCurrentThread(), category_ref,
      writer.RegisterString(name), scope, std::forward<Args>(args)...);
}

template <typename... Args>
void WriteCounterEventRecord(::tracing::writer::TraceWriter& writer,
                             const ::tracing::writer::StringRef& category_ref,
                             const char* name,
                             uint64_t id,
                             Args&&... args) {
  writer.WriteCounterEventRecord(
      ::tracing::GetTicksNow(), writer.RegisterCurrentThread(), category_ref,
      writer.RegisterString(name), id, std::forward<Args>(args)...);
}

template <typename... Args>
void WriteDurationBeginEventRecord(
    ::tracing::writer::TraceWriter& writer,
    const ::tracing::writer::StringRef& category_ref,
    const char* name,
    Args&&... args) {
  writer.WriteDurationBeginEventRecord(
      ::tracing::GetTicksNow(), writer.RegisterCurrentThread(), category_ref,
      writer.RegisterString(name), std::forward<Args>(args)...);
}

template <typename... Args>
void WriteDurationEndEventRecord(
    ::tracing::writer::TraceWriter& writer,
    const ::tracing::writer::StringRef& category_ref,
    const char* name,
    Args&&... args) {
  writer.WriteDurationEndEventRecord(
      ::tracing::GetTicksNow(), writer.RegisterCurrentThread(), category_ref,
      writer.RegisterString(name), std::forward<Args>(args)...);
}

template <typename... Args>
void WriteAsyncBeginEventRecord(
    ::tracing::writer::TraceWriter& writer,
    const ::tracing::writer::StringRef& category_ref,
    const char* name,
    uint64_t id,
    Args&&... args) {
  writer.WriteAsyncBeginEventRecord(
      ::tracing::GetTicksNow(), writer.RegisterCurrentThread(), category_ref,
      writer.RegisterString(name), id, std::forward<Args>(args)...);
}

template <typename... Args>
void WriteAsyncInstantEventRecord(
    ::tracing::writer::TraceWriter& writer,
    const ::tracing::writer::StringRef& category_ref,
    const char* name,
    uint64_t id,
    Args&&... args) {
  writer.WriteAsyncInstantEventRecord(
      ::tracing::GetTicksNow(), writer.RegisterCurrentThread(), category_ref,
      writer.RegisterString(name), id, std::forward<Args>(args)...);
}

template <typename... Args>
void WriteAsyncEndEventRecord(::tracing::writer::TraceWriter& writer,
                              const ::tracing::writer::StringRef& category_ref,
                              const char* name,
                              uint64_t id,
                              Args&&... args) {
  writer.WriteAsyncEndEventRecord(
      ::tracing::GetTicksNow(), writer.RegisterCurrentThread(), category_ref,
      writer.RegisterString(name), id, std::forward<Args>(args)...);
}

template <typename... Args>
void WriteFlowBeginEventRecord(::tracing::writer::TraceWriter& writer,
                               const ::tracing::writer::StringRef& category_ref,
                               const char* name,
                               uint64_t id,
                               Args&&... args) {
  writer.WriteFlowBeginEventRecord(
      ::tracing::GetTicksNow(), writer.RegisterCurrentThread(), category_ref,
      writer.RegisterString(name), id, std::forward<Args>(args)...);
}

template <typename... Args>
void WriteFlowStepEventRecord(::tracing::writer::TraceWriter& writer,
                              const ::tracing::writer::StringRef& category_ref,
                              const char* name,
                              uint64_t id,
                              Args&&... args) {
  writer.WriteFlowStepEventRecord(
      ::tracing::GetTicksNow(), writer.RegisterCurrentThread(), category_ref,
      writer.RegisterString(name), id, std::forward<Args>(args)...);
}

template <typename... Args>
void WriteFlowEndEventRecord(::tracing::writer::TraceWriter& writer,
                             const ::tracing::writer::StringRef& category_ref,
                             const char* name,
                             uint64_t id,
                             Args&&... args) {
  writer.WriteFlowEndEventRecord(
      ::tracing::GetTicksNow(), writer.RegisterCurrentThread(), category_ref,
      writer.RegisterString(name), id, std::forward<Args>(args)...);
}

// When destroyed, writes a duration end event.
// Note: This implementation only acquires the |TraceWriter| at the end of
// the scope rather than holding it for the entire duration so we don't
// inadvertently delay trace shutdown if the code within the scope blocks
// for a long time.
class DurationEventScope {
 public:
  explicit DurationEventScope(const char* category, const char* name)
      : category_(category), name_(name) {}

  ~DurationEventScope() {
    TRACE_INTERNAL_DURATION_END(category_, name_, TRACE_INTERNAL_MAKE_ARGS());
  }

  const char* category() const { return category_; }
  const char* name() const { return name_; }

 private:
  const char* const category_;
  const char* const name_;

  FXL_DISALLOW_COPY_AND_ASSIGN(DurationEventScope);
};

}  // namespace internal
}  // namespace tracing

#endif  // APPS_TRACING_LIB_TRACE_INTERNAL_EVENT_HELPERS_H_
