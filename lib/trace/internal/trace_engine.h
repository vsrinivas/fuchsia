// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_TRACING_LIB_TRACE_INTERNAL_TRACE_ENGINE_H_
#define APPS_TRACING_LIB_TRACE_INTERNAL_TRACE_ENGINE_H_

#include "stdint.h"

#include <atomic>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "apps/tracing/lib/trace/writer.h"
#include "lib/ftl/macros.h"
#include "lib/mtl/vmo/shared_vmo.h"
#include "lib/ftl/strings/string_view.h"

namespace tracing {
namespace internal {

// Manages a single tracing session.
// The trace engine uses thread local state to maintain string and thread
// tables but it can tolerate having multiple instances alive at the same
// time though the performance of older instances will degrade.
class TraceEngine final {
 public:
  using Payload = ::tracing::writer::Payload;
  using StringRef = ::tracing::writer::StringRef;
  using ThreadRef = ::tracing::writer::ThreadRef;

  ~TraceEngine();

  // Creates and initializes the trace engine.
  // If the list of enabled categories is empty, then all are enabled.
  // Returns nullptr if the engine could not be created.
  static std::unique_ptr<TraceEngine> Create(
      mx::vmo vmo,
      std::vector<std::string> enabled_categories);

  // Returns true if the specified category is enabled.
  bool IsCategoryEnabled(const char* category) const;

  // Registers the string and returns its string ref.
  // If |check_category| is true, returns an empty string ref if the string
  // is not one of the enabled categories.
  StringRef RegisterString(const char* string, bool check_category);

  // Registers the current thread and returns its thread ref.
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

  uint32_t const generation_;

  ftl::RefPtr<mtl::SharedVmo> const shared_vmo_;
  uintptr_t const buffer_start_;
  uintptr_t const buffer_end_;
  std::atomic<uintptr_t> buffer_current_;

  // We must keep both the vector and the set since the set contains
  // string views into the strings which are backed by the vector.
  std::vector<std::string> const enabled_categories_;
  std::set<ftl::StringView> enabled_category_set_;

  std::atomic<StringIndex> next_string_index_{1u};
  std::atomic<ThreadIndex> next_thread_index_{1u};

  FTL_DISALLOW_COPY_AND_ASSIGN(TraceEngine);
};

}  // namespace internal
}  // namespace tracing

#endif  // APPS_TRACING_LIB_TRACE_INTERNAL_TRACE_ENGINE_H_
