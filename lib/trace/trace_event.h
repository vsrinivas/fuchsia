// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_TRACING_LIB_TRACE_TRACE_EVENT_H_
#define APPS_TRACING_LIB_TRACE_TRACE_EVENT_H_

#include "lib/ftl/macros.h"
#include "apps/tracing/lib/trace/internal/trace_writer.h"

namespace tracing {

// Relies on RAII to trace begin and end of a duration event.
class ScopedDurationTracer {
 public:
  // Initializes a new instance, injecting a duration begin event with
  // the given parameters into the tracing infrastructure.
  //
  // We assume that the lifetime of |name| and |cat|
  // exceeds the lifetime of this instance.
  ScopedDurationTracer(const char* name, const char* cat)
      : is_enabled_for_tracing_(internal::IsTracingEnabled(cat)),
        name_(name),
        cat_(cat) {
    if (is_enabled_for_tracing_)
      Begin();
  }

  template <typename T>
  ScopedDurationTracer(const char* name,
                       const char* cat,
                       const char* key,
                       const T& value)
      : is_enabled_for_tracing_(internal::IsTracingEnabled(cat)),
        name_(name),
        cat_(cat) {
    if (is_enabled_for_tracing_)
      Begin(internal::MakeArgument(key, value));
  }

  template <typename T, typename U>
  ScopedDurationTracer(const char* name,
                       const char* cat,
                       const char* key0,
                       const T& value0,
                       const char* key1,
                       const U& value1)
      : is_enabled_for_tracing_(internal::IsTracingEnabled(cat)),
        name_(name),
        cat_(cat) {
    if (is_enabled_for_tracing_)
      Begin(internal::MakeArgument(key0, value0),
            internal::MakeArgument(key1, value1));
  }

  template <typename... Args>
  void Begin(Args&&... args) const {
    internal::TraceDurationBegin(name_, cat_, std::forward<Args>(args)...);
  }

  void End() const { internal::TraceDurationEnd(name_, cat_); }

  // Injects a duration end event with the parameters passed in
  // on construction into the tracing infrastructure.
  ~ScopedDurationTracer() {
    if (is_enabled_for_tracing_)
      End();
  }

 private:
  bool is_enabled_for_tracing_;
  const char* name_;
  const char* cat_;

  FTL_DISALLOW_COPY_AND_ASSIGN(ScopedDurationTracer);
};

}  // namespace tracing

#define __TRACE_EVENT_PASTE_ARGS(x, y) x##y
#define __TRACE_EVENT_PASTE_ARGS2(x, y) __TRACE_EVENT_PASTE_ARGS(x, y)

#define TRACE_KOID(value) tracing::internal::Koid(value)

#define TRACE_EVENT0(cat, name)                                              \
  tracing::ScopedDurationTracer __TRACE_EVENT_PASTE_ARGS2(_, __LINE__)(name, \
                                                                       cat)

#define TRACE_EVENT1(cat, name, k0, v0)                                 \
  tracing::ScopedDurationTracer __TRACE_EVENT_PASTE_ARGS2(_, __LINE__)( \
      name, cat, k0, v0)

#define TRACE_EVENT2(cat, name, k0, v0, k1, v1)                         \
  tracing::ScopedDurationTracer __TRACE_EVENT_PASTE_ARGS2(_, __LINE__)( \
      name, cat, k0, v0, k1, v1)

#define TRACE_EVENT_ASYNC_BEGIN0(cat, name, id)        \
  if (tracing::internal::IsTracingEnabled(cat)) {      \
    tracing::internal::TraceAsyncBegin(cat, name, id); \
  }

#define TRACE_EVENT_ASYNC_END0(cat, name, id)        \
  if (tracing::internal::IsTracingEnabled(cat)) {    \
    tracing::internal::TraceAsyncEnd(cat, name, id); \
  }

#define TRACE_EVENT_ASYNC_BEGIN1(cat, name, id, k0, v0)          \
  if (tracing::internal::IsTracingEnabled(cat)) {                \
    tracing::internal::TraceAsyncBegin(                          \
        cat, name, id, tracing::internal::MakeArgument(k0, v0)); \
  }

#define TRACE_EVENT_ASYNC_END1(cat, name, id, k0, v0)                          \
  if (tracing::internal::IsTracingEnabled(cat)) {                              \
    tracing::internal::TraceAsyncEnd(cat, name, id,                            \
                                     tracing::internal::MakeArgument(k0, v0)); \
  }

#endif  // APPS_TRACING_LIB_TRACE_TRACE_EVENT_H_
