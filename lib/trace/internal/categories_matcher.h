// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_TRACING_LIB_TRACE_INTERNAL_CATEGORIES_MATCHER_H_
#define APPS_TRACING_LIB_TRACE_INTERNAL_CATEGORIES_MATCHER_H_

#include <string>
#include <vector>

#include <lib/ftl/strings/string_view.h>

namespace tracing {
namespace internal {

// A CategoriesMatcher determines whether a set of categories
// is enabled for tracing.
class CategoriesMatcher {
 public:
  // Resets to the empty state, with IsAnyCategoryEnabled
  // always returning true.
  void Reset();

  // Tokenizes the comma-separated list in |categories| storing
  // individual categories in a lookup-table.
  void SetEnabledCategories(const std::vector<std::string>& categories);

  // Returns true if any category in the comma-separated list of
  // |categories| is enabled for tracing.
  bool IsAnyCategoryEnabled(const char* categories);

 private:
  // TODO(tvoss): Switch to std::unordered_set once
  // std::hash<ftl::StringView> is available.
  std::vector<std::string> enabled_categories_;
};

}  // namespace internal
}  // namespace tracing

#endif  // APPS_TRACING_LIB_TRACE_INTERNAL_CATEGORIES_MATCHER_H_
