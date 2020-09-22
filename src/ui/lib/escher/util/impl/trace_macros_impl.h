// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_UTIL_IMPL_TRACE_MACROS_IMPL_H_
#define SRC_UI_LIB_ESCHER_UTIL_IMPL_TRACE_MACROS_IMPL_H_

#include "src/lib/fxl/build_config.h"
#include "src/ui/lib/escher/util/tracer.h"

#ifdef OS_FUCHSIA
#error Should only be included on Linux.
#endif

#include <atomic>

// Keep overhead low by using temporary variables. The variable names are based
// on line numbers in order to prevent name collisions.
#define TRACE_INTERNAL_EVENT_UID3(a, b) trace_event_unique_##a##b
#define TRACE_INTERNAL_EVENT_UID2(a, b) TRACE_INTERNAL_EVENT_UID3(a, b)
#define TRACE_INTERNAL_EVENT_UID(name_prefix) TRACE_INTERNAL_EVENT_UID2(name_prefix, __LINE__)

// For this simple implementation, all categories are always enabled.
//
// TODO(fxbug.dev/7176): Add support for additional trace arguments. Currently, they are
// silently dropped.
#define TRACE_INTERNAL_DURATION(category, name, args...)                     \
  escher::impl::TraceEndOnScopeClose TRACE_INTERNAL_EVENT_UID(profileScope); \
  escher::impl::AddTraceEvent(TRACE_EVENT_PHASE_BEGIN, category, name);      \
  TRACE_INTERNAL_EVENT_UID(profileScope).Initialize(category, name);

#define TRACE_EVENT_PHASE_BEGIN ('B')
#define TRACE_EVENT_PHASE_END ('E')

namespace escher {
namespace impl {

// Used by TRACE_* macros. Do not use directly.
class TraceEndOnScopeClose {
 public:
  TraceEndOnScopeClose() {}
  ~TraceEndOnScopeClose();

  void Initialize(const char* category_enabled, const char* name);

 private:
  const char* category_ = nullptr;
  const char* name_ = nullptr;
};

static inline void AddTraceEvent(char phase, const char* category, const char* name) {
  if (auto tracer = GetTracer()) {
    tracer->AddTraceEvent(phase, category, name);
  }
}

}  // namespace impl
}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_UTIL_IMPL_TRACE_MACROS_IMPL_H_
