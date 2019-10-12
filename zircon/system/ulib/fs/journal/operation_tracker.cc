// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <vector>

#include <fs/journal/internal/operation_tracker.h>
#include <range/interval-tree.h>

namespace fs {
namespace internal {

using Range = OperationTracker::Range;

// Removes all tracked operations which overlap with the input range.
//
// Returns a vector of Ranges describing the overlapping regions.
std::vector<Range> OperationTracker::Remove(Range range) {
  std::vector<Range> overlap_regions;

  for (auto iter = operations_.find(range); iter != operations_.end(); iter++) {
    Range overlap(std::max(iter->second.Start(), range.Start()),
                  std::min(iter->second.End(), range.End()));
    overlap_regions.push_back(std::move(overlap));
  }

  for (const auto& overlap : overlap_regions) {
    operations_.erase(overlap);
  }

  return overlap_regions;
}

}  // namespace internal
}  // namespace fs
