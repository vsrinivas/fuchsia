// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/tracing/lib/trace/internal/string_table.h"

#include "apps/tracing/lib/trace/internal/trace_engine.h"

namespace tracing {
namespace internal {

StringTable::StringTable(std::vector<std::string> enabled_categories)
    : enabled_categories_(std::move(enabled_categories)) {
  for (const auto& category : enabled_categories_) {
    enabled_category_set_.emplace(category);
  }
}

StringTable::~StringTable() {}

StringTable::StringRef StringTable::RegisterString(TraceEngine* engine,
                                                   const char* string) {
  if (!string || !*string)
    return StringRef::MakeEmpty();

  StringIndex index = table_.Insert(reinterpret_cast<uintptr_t>(string));
  if (!index)
    return StringRef::MakeInlined(string, strlen(string));

  std::atomic<uint32_t>& value = table_[index];
  const uint32_t flags = value.load(std::memory_order_acquire);
  if (!(flags & kRecordFlag)) {
    // We need to write the record before we return from this function so
    // that the trace doesn't contain references to unknown strings.
    // It's safe to write multiple copies of the string to the trace, so
    // we don't bother trying to prevent it here.
    engine->WriteStringRecord(index, string);
    value.fetch_or(kRecordFlag, std::memory_order_release);
  }
  return StringRef::MakeIndexed(index);
}

bool StringTable::PrepareCategory(TraceEngine* engine,
                                  const char* category,
                                  StringRef* out_category_ref) {
  if (!category || !*category)
    return false;

  StringIndex index = table_.Insert(reinterpret_cast<uintptr_t>(category));
  if (!index) {
    // Slow path, lookup table is full.
    if (!IsCategoryEnabled(category))
      return false;
    *out_category_ref = StringRef::MakeInlined(category, strlen(category));
    return true;
  }

  // Fast path, check whether we cached the flags.
  std::atomic<uint32_t>& value = table_[index];
  const uint32_t flags = value.load(std::memory_order_acquire);
  if (flags & (kCategoryEnabledFlag | kCategoryDisabledFlag)) {
    if (!(flags & kCategoryEnabledFlag))
      return false;
    *out_category_ref = StringRef::MakeIndexed(index);
    return true;
  }

  // It is safe to perform these operations more than once.
  if (!IsCategoryEnabled(category)) {
    value.fetch_or(kCategoryDisabledFlag, std::memory_order_release);
    return false;
  }
  if (!(flags & kRecordFlag))
    engine->WriteStringRecord(index, category);
  value.fetch_or(kRecordFlag | kCategoryEnabledFlag, std::memory_order_release);
  *out_category_ref = StringRef::MakeIndexed(index);
  return true;
}

bool StringTable::IsCategoryEnabled(const char* category) const {
  return enabled_category_set_.empty() ||
         enabled_category_set_.count(ftl::StringView(category)) > 0;
}

}  // namespace internal
}  // namespace tracing
