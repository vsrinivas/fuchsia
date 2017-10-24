// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "database.h"

#include <algorithm>

#include "lib/fxl/logging.h"

namespace bluetooth {
namespace att {

Database::Database(Handle range_start, Handle range_end)
    : range_start_(range_start), range_end_(range_end) {
  FXL_DCHECK(range_start_ < range_end_);
  FXL_DCHECK(range_start_ >= kHandleMin);
  FXL_DCHECK(range_end_ <= kHandleMax);
}

AttributeGrouping* Database::NewGrouping(const common::UUID& group_type,
                                         size_t attr_count,
                                         const common::ByteBuffer& decl_value) {
  // This method looks for a |pos| before which to insert the new grouping.
  Handle start_handle;
  decltype(groupings_)::iterator pos;

  if (groupings_.empty()) {
    if (range_end_ - range_start_ < attr_count)
      return nullptr;

    start_handle = range_start_;
    pos = groupings_.end();
  } else if (groupings_.front().start_handle() - range_start_ > attr_count) {
    // There is room at the head of the list.
    start_handle = range_start_;
    pos = groupings_.begin();
  } else if (range_end_ - groupings_.back().end_handle() > attr_count) {
    // There is room at the tail end of the list.
    start_handle = groupings_.back().end_handle() + 1;
    pos = groupings_.end();
  } else {
    // Linearly search for a gap that fits the new grouping.
    // TODO(armansito): This is suboptimal for long running cases where the
    // database is fragmented. Think about using a better algorithm.

    auto prev = groupings_.begin();
    pos = prev;
    pos++;

    for (; pos != groupings_.end(); ++pos, ++prev) {
      size_t next_avail = pos->start_handle() - prev->end_handle() - 1;
      if (attr_count < next_avail)
        break;
    }

    if (pos == groupings_.end()) {
      FXL_VLOG(1) << "att: Attribute database is out of space!";
      return nullptr;
    }

    start_handle = prev->end_handle() + 1;
  }

  auto iter =
      groupings_.emplace(pos, group_type, start_handle, attr_count, decl_value);
  FXL_DCHECK(iter != groupings_.end());

  return &*iter;
}

bool Database::RemoveGrouping(Handle start_handle) {
  auto iter =
      std::lower_bound(groupings_.begin(), groupings_.end(), start_handle,
                       [](const auto& grouping, const Handle handle) -> bool {
                         return grouping.start_handle() < handle;
                       });

  if (iter == groupings_.end() || iter->start_handle() != start_handle)
    return false;

  groupings_.erase(iter);
  return true;
}

}  // namespace att
}  // namespace bluetooth
