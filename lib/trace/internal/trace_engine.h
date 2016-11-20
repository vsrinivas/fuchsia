// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_TRACING_LIB_TRACE_INTERNAL_TRACE_ENGINE_H_
#define APPS_TRACING_LIB_TRACE_INTERNAL_TRACE_ENGINE_H_

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "apps/tracing/lib/trace/internal/string_table.h"
#include "apps/tracing/lib/trace/internal/thread_table.h"
#include "lib/mtl/vmo/shared_vmo.h"

namespace tracing {
namespace internal {

class TraceEngine final {
 public:
  using Payload = ::tracing::writer::Payload;
  using StringRef = ::tracing::writer::StringRef;
  using ThreadRef = ::tracing::writer::ThreadRef;

  ~TraceEngine();

  // Creates and initializes the trace engine.
  // Returns nullptr if the engine could not be created.
  static std::unique_ptr<TraceEngine> Create(
      mx::vmo vmo,
      std::vector<std::string> enabled_categories);

  StringRef RegisterString(const char* string);
  bool IsCategoryEnabled(const char* category) const;
  bool PrepareCategory(const char* category, StringRef* out_category_ref);
  ThreadRef RegisterCurrentThread();

  void WriteInitializationRecord(uint64_t ticks_per_second);
  void WriteStringRecord(StringIndex index, const char* string);
  void WriteThreadRecord(ThreadIndex index,
                         uint64_t process_koid,
                         uint64_t thread_koid);
  Payload WriteEventRecord(EventType type,
                           const StringRef& category_ref,
                           const char* name,
                           size_t argument_count,
                           size_t payload_size);

 private:
  explicit TraceEngine(ftl::RefPtr<mtl::SharedVmo> shared_vmo,
                       std::vector<std::string> enabled_categories);

  Payload AllocateRecord(size_t num_bytes);

  ftl::RefPtr<mtl::SharedVmo> const shared_vmo_;
  StringTable string_table_;
  ThreadTable thread_table_;

  uintptr_t const buffer_start_;
  uintptr_t const buffer_end_;
  std::atomic<uintptr_t> buffer_current_;
};

}  // namespace internal
}  // namespace tracing

#endif  // APPS_TRACING_LIB_TRACE_INTERNAL_TRACE_ENGINE_H_
