// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FS_JOURNAL_INTERNAL_OPERATION_TRACKER_H_
#define FS_JOURNAL_INTERNAL_OPERATION_TRACKER_H_

#include <algorithm>
#include <vector>

#include <range/interval-tree.h>

namespace fs {
namespace internal {

// Tracks a collection of live operations.
//
// This class is thread-compatible.
// This class is movable, but not copyable.
class OperationTracker {
 public:
  using Range = range::Range<uint64_t>;
  OperationTracker() = default;
  OperationTracker(const OperationTracker&) = delete;
  OperationTracker& operator=(const OperationTracker&) = delete;
  OperationTracker(OperationTracker&& other) = default;
  OperationTracker& operator=(OperationTracker&& other) = default;

  // Tracks operations contained within the range.
  void Insert(Range range) { operations_.insert(range); }

  // Removes the overlapping portion of tracked operations which overlap with the input range.
  // This method does not remove the non-overlapping portion.
  // For example:
  //   Insert([0, 100))
  //   Remove([50, 150)
  // Removes/returns [50, 100), but leaves [0, 50) in the tracker.
  //
  // Returns a vector of Ranges describing the overlapping portions of these operations.
  std::vector<Range> Remove(Range range);

  // Returns true if any tracked operations even partially overlap with the provided range.
  bool Overlaps(Range range) {
    return operations_.find(range) != operations_.end();
  }

  void Clear() { operations_.clear(); }

 private:
  range::IntervalTree<Range> operations_;
};

}  // namespace internal
}  // namespace fs

#endif  // FS_JOURNAL_INTERNAL_OPERATION_TRACKER_H_
