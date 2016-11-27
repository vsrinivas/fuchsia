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

#define TRACE_INTERNAL_SCOPE_LABEL__(token) __trace_scope_##token
#define TRACE_INTERNAL_SCOPE_LABEL_(token) TRACE_INTERNAL_SCOPE_LABEL__(token)
#define TRACE_INTERNAL_SCOPE_LABEL() TRACE_INTERNAL_SCOPE_LABEL_(__COUNTER__)

#define TRACE_INTERNAL_WRITER __trace_writer
#define TRACE_INTERNAL_EVENT_THREAD_REF __trace_event_thread_ref
#define TRACE_INTERNAL_EVENT_CATEGORY_REF __trace_event_category_ref
#define TRACE_INTERNAL_EVENT_NAME_REF __trace_event_name_ref
#define TRACE_INTERNAL_EVENT_SPEC                                     \
  TRACE_INTERNAL_EVENT_THREAD_REF, TRACE_INTERNAL_EVENT_CATEGORY_REF, \
      TRACE_INTERNAL_EVENT_NAME_REF

#define TRACE_INTERNAL_MAKE_ARG(key, value) \
  , MakeArgument(TRACE_INTERNAL_WRITER, key, value)
#define TRACE_INTERNAL_MAKE_ARGS1(k1, v1) TRACE_INTERNAL_MAKE_ARG(k1, v1)
#define TRACE_INTERNAL_MAKE_ARGS2(k1, v1, k2, v2) \
  TRACE_INTERNAL_MAKE_ARGS1(k1, v1) TRACE_INTERNAL_MAKE_ARG(k2, v2)
#define TRACE_INTERNAL_MAKE_ARGS3(k1, v1, k2, v2, k3, v3) \
  TRACE_INTERNAL_MAKE_ARGS2(k1, v1, k2, v2) TRACE_INTERNAL_MAKE_ARG(k3, v3)
#define TRACE_INTERNAL_MAKE_ARGS4(k1, v1, k2, v2, k3, v3, k4, v4) \
  TRACE_INTERNAL_MAKE_ARGS3(k1, v1, k2, v2, k3, v3)               \
  TRACE_INTERNAL_MAKE_ARG(k4, v4)

#define TRACE_INTERNAL_SIMPLE(stmt)                                         \
  do {                                                                      \
    auto TRACE_INTERNAL_WRITER = ::tracing::writer::TraceWriter::Prepare(); \
    if (TRACE_INTERNAL_WRITER) {                                            \
      stmt;                                                                 \
    }                                                                       \
  } while (0)

// TODO(jeffbrown): Determine whether we should try to do anything to
// reduce the code expansion here.
#define TRACE_INTERNAL_EVENT(category, name, stmt)                          \
  do {                                                                      \
    auto TRACE_INTERNAL_WRITER = ::tracing::writer::TraceWriter::Prepare(); \
    if (TRACE_INTERNAL_WRITER) {                                            \
      ::tracing::writer::StringRef TRACE_INTERNAL_EVENT_CATEGORY_REF =      \
          TRACE_INTERNAL_WRITER.RegisterString(category, true);             \
      if (!TRACE_INTERNAL_EVENT_CATEGORY_REF.is_empty()) {                  \
        ::tracing::writer::StringRef TRACE_INTERNAL_EVENT_NAME_REF =        \
            TRACE_INTERNAL_WRITER.RegisterString(name);                     \
        ::tracing::writer::ThreadRef TRACE_INTERNAL_EVENT_THREAD_REF =      \
            TRACE_INTERNAL_WRITER.RegisterCurrentThread();                  \
        stmt;                                                               \
      }                                                                     \
    }                                                                       \
  } while (0)

#define TRACE_INTERNAL_INSTANT(category, name, scope, args...)        \
  TRACE_INTERNAL_EVENT(category, name,                                \
                       TRACE_INTERNAL_WRITER.WriteInstantEventRecord( \
                           TRACE_INTERNAL_EVENT_SPEC, scope args))

#define TRACE_INTERNAL_COUNTER(category, name, id, args...)           \
  TRACE_INTERNAL_EVENT(category, name,                                \
                       TRACE_INTERNAL_WRITER.WriteCounterEventRecord( \
                           TRACE_INTERNAL_EVENT_SPEC, id args))

#define TRACE_INTERNAL_DURATION_BEGIN(category, name, args...)              \
  TRACE_INTERNAL_EVENT(category, name,                                      \
                       TRACE_INTERNAL_WRITER.WriteDurationBeginEventRecord( \
                           TRACE_INTERNAL_EVENT_SPEC args))

#define TRACE_INTERNAL_DURATION_END(category, name, args...)              \
  TRACE_INTERNAL_EVENT(category, name,                                    \
                       TRACE_INTERNAL_WRITER.WriteDurationEndEventRecord( \
                           TRACE_INTERNAL_EVENT_SPEC args))

#define TRACE_INTERNAL_DURATION_SCOPE(scope_label, scope_category, scope_name, \
                                      args...)                                 \
  ::tracing::internal::DurationEventScope scope_label(scope_category,          \
                                                      scope_name);             \
  TRACE_INTERNAL_DURATION_BEGIN(scope_label.category(), scope_label.name(),    \
                                args)

#define TRACE_INTERNAL_DURATION(category, name, args...)                      \
  TRACE_INTERNAL_DURATION_SCOPE(TRACE_INTERNAL_SCOPE_LABEL(), category, name, \
                                args)

#define TRACE_INTERNAL_ASYNC_BEGIN(category, name, id, args...)          \
  TRACE_INTERNAL_EVENT(category, name,                                   \
                       TRACE_INTERNAL_WRITER.WriteAsyncBeginEventRecord( \
                           TRACE_INTERNAL_EVENT_SPEC, id args))

#define TRACE_INTERNAL_ASYNC_INSTANT(category, name, id, args...)          \
  TRACE_INTERNAL_EVENT(category, name,                                     \
                       TRACE_INTERNAL_WRITER.WriteAsyncInstantEventRecord( \
                           TRACE_INTERNAL_EVENT_SPEC, id args))

#define TRACE_INTERNAL_ASYNC_END(category, name, id, args...)          \
  TRACE_INTERNAL_EVENT(category, name,                                 \
                       TRACE_INTERNAL_WRITER.WriteAsyncEndEventRecord( \
                           TRACE_INTERNAL_EVENT_SPEC, id args))

#define TRACE_INTERNAL_HANDLE(handle, args...) \
  TRACE_INTERNAL_SIMPLE(                       \
      TRACE_INTERNAL_WRITER.WriteKernelObjectRecord(handle args))

namespace tracing {
namespace internal {

// When destroyed, writes a duration end event.
// Note: This implementation only acquires the |TraceWriter| at the end of
// the scope rather than holding it for the entire duration so we don't
// inadvertently delay trace shutdown if the code within the scope blocks
// for a long time.
class DurationEventScope {
 public:
  explicit DurationEventScope(const char* category, const char* name)
      : category_(category), name_(name) {}

  ~DurationEventScope() { TRACE_INTERNAL_DURATION_END(category_, name_); }

  const char* category() const { return category_; }
  const char* name() const { return name_; }

 private:
  const char* const category_;
  const char* const name_;

  FTL_DISALLOW_COPY_AND_ASSIGN(DurationEventScope);
};

}  // namespace internal
}  // namespace tracing

#endif  // APPS_TRACING_LIB_TRACE_INTERNAL_EVENT_HELPERS_H_
