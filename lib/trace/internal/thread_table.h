// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_TRACING_LIB_TRACE_INTERNAL_THREAD_TABLE_H_
#define APPS_TRACING_LIB_TRACE_INTERNAL_THREAD_TABLE_H_

#include <magenta/types.h>

#include "stdint.h"

#include <atomic>

#include "lib/ftl/macros.h"
#include "apps/tracing/lib/trace/writer.h"

namespace tracing {
namespace internal {

class TraceEngine;

// Stores a table of registered threads.
class ThreadTable final {
 public:
  using ThreadRef = ::tracing::writer::ThreadRef;

  ThreadTable();
  ~ThreadTable();

  // Registers the current thread.
  ThreadRef RegisterCurrentThread(TraceEngine* engine);

 private:
  uint32_t const generation_;
  std::atomic<ThreadIndex> next_index_{1u};

  FTL_DISALLOW_COPY_AND_ASSIGN(ThreadTable);
};

}  // namespace internal
}  // namespace tracing

#endif  // APPS_TRACING_LIB_TRACE_INTERNAL_THREAD_TABLE_H_
