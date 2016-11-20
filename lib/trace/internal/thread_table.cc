// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/tracing/lib/trace/internal/string_table.h"

#include "apps/tracing/lib/trace/internal/fields.h"
#include "apps/tracing/lib/trace/internal/trace_engine.h"
#include "lib/mtl/handles/object_info.h"
#include "magenta/syscalls.h"

namespace tracing {
namespace internal {
namespace {

std::atomic<uint32_t> g_current_generation;
mx_koid_t g_process_koid;

struct ThreadInfo {
  mx_koid_t koid;
  uint32_t generation;
  ThreadTable::ThreadRef ref;
};
thread_local ThreadInfo g_thread_info;

}  // namespace

ThreadTable::ThreadTable()
    : generation_(
          g_current_generation.fetch_add(1u, std::memory_order_relaxed) + 1u) {
  if (g_process_koid)
    g_process_koid = mtl::GetKoid(mx_process_self());
}

ThreadTable::~ThreadTable() {}

ThreadTable::ThreadRef ThreadTable::RegisterCurrentThread(TraceEngine* engine) {
  ThreadInfo& thread_info = g_thread_info;

  if (thread_info.generation != generation_) {
    // TODO(jeffbrown): This is a quick hack to create a unique value per
    // thread.  Replace this with a request to get the current thread's koid
    // once that is supported by magenta.
    if (!thread_info.koid)
      thread_info.koid = reinterpret_cast<mx_koid_t>(&g_thread_info);

    thread_info.generation = generation_;
    ThreadIndex index = next_index_.fetch_add(1u, std::memory_order_relaxed);
    if (index > ThreadRefFields::kMaxIndex) {
      // Prevent wrapping.
      next_index_.store(ThreadRefFields::kMaxIndex + 1u,
                        std::memory_order_relaxed);
      thread_info.ref =
          ThreadRef::MakeInlined(g_process_koid, thread_info.koid);
    } else {
      engine->WriteThreadRecord(index, g_process_koid, thread_info.koid);
      thread_info.ref = ThreadRef::MakeIndexed(index);
    }
  }
  return thread_info.ref;
}

}  // namespace internal
}  // namespace tracing
