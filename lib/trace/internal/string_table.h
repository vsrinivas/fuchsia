// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_TRACING_LIB_TRACE_INTERNAL_STRING_TABLE_H_
#define APPS_TRACING_LIB_TRACE_INTERNAL_STRING_TABLE_H_

#include "stdint.h"

#include <set>
#include <string>
#include <vector>

#include "apps/tracing/lib/trace/internal/fields.h"
#include "apps/tracing/lib/trace/internal/lookup_table.h"
#include "apps/tracing/lib/trace/writer.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/strings/string_view.h"

namespace tracing {
namespace internal {

class TraceEngine;

// Stores a table of registered string constants.
// The constants are assumed to outlive the table.
class StringTable final {
 public:
  using StringRef = ::tracing::writer::StringRef;

  // Initializes the string table.
  // If the list of enabled categories is empty, then all are enabled.
  explicit StringTable(std::vector<std::string> enabled_categories);
  ~StringTable();

  // Registers the string.
  StringRef RegisterString(TraceEngine* engine, const char* string);

  // If the category is enabled for tracing return true and
  // the category's |StringRef|.
  bool PrepareCategory(TraceEngine* engine,
                       const char* category,
                       StringRef* out_category_ref);

  // Returns true if the specified category is enabled.
  bool IsCategoryEnabled(const char* category) const;

 private:
  static constexpr uint32_t kMaxValues = StringRefFields::kMaxIndex;
  static constexpr uint32_t kMaxChains = kMaxValues / 4;

  // Indicates whether a StringRecord has been emitted.
  static constexpr uint32_t kRecordFlag = 1u << 0;
  // Indicates whether the string is or is not an enabled category.
  static constexpr uint32_t kCategoryEnabledFlag = 1u << 1;
  static constexpr uint32_t kCategoryDisabledFlag = 1u << 2;

  struct Hash {
    // TODO(jeffbrown): Replace with a better pointer hash function.
    uint32_t operator()(uintptr_t key) const { return key % kMaxChains; }
  };

  LookupTable<uintptr_t, Hash, kMaxChains, kMaxValues> table_;

  // We must keep both the vector and the set since the set contains
  // string views into the strings which are backed by the vector.
  std::vector<std::string> enabled_categories_;
  std::set<ftl::StringView> enabled_category_set_;

  FTL_DISALLOW_COPY_AND_ASSIGN(StringTable);
};

}  // namespace internal
}  // namespace tracing

#endif  // APPS_TRACING_LIB_TRACE_INTERNAL_STRING_TABLE_H_
