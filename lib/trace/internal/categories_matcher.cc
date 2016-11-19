// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/tracing/lib/trace/internal/categories_matcher.h"

namespace tracing {
namespace internal {
namespace {

struct Comparator {
  bool operator()(const std::string& lhs, const ftl::StringView& rhs) const {
    return lhs.compare(0, lhs.size(), rhs.data(), rhs.size()) < 0;
  }

  bool operator()(const ftl::StringView& lhs, const std::string& rhs) const {
    return rhs.compare(0, rhs.size(), lhs.data(), lhs.size()) > 0;
  }
};

const Comparator g_comparator;

}  // namespace

void CategoriesMatcher::Reset() {
  enabled_categories_.clear();
}

void CategoriesMatcher::SetEnabledCategories(
    const std::vector<std::string>& categories) {
  enabled_categories_ = categories;
  std::sort(enabled_categories_.begin(), enabled_categories_.end());
}

bool CategoriesMatcher::IsAnyCategoryEnabled(const char* categories) {
  if (!categories)
    return false;

  if (enabled_categories_.empty())
    return true;

  const char* last = categories;
  const char* current = categories;

  auto begin = enabled_categories_.begin();
  auto end = enabled_categories_.end();

  while (*current != '\0') {
    if (*current == ',') {
      if (last < current) {
        if (std::binary_search(
                begin, end, ftl::StringView(last, std::distance(last, current)),
                g_comparator))
          return true;
        last = current + 1;
      } else {
        last++;
      }
    }
    ++current;
  }

  // We need to do a final check as |cat| might not
  // conveniently end in a ','.
  return std::binary_search(begin, end,
                            ftl::StringView(last, std::distance(last, current)),
                            g_comparator);
}

}  // namespace internal
}  // namespace tracing
