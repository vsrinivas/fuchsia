// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "database.h"

#include <algorithm>

#include "lib/fxl/logging.h"

namespace btlib {
namespace att {
namespace {

bool StartLessThan(const AttributeGrouping& grp, const Handle handle) {
  return grp.start_handle() < handle;
}

}  // namespace

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
  auto iter = std::lower_bound(groupings_.begin(), groupings_.end(),
                               start_handle, StartLessThan);

  if (iter == groupings_.end() || iter->start_handle() != start_handle)
    return false;

  groupings_.erase(iter);
  return true;
}

ErrorCode Database::ReadByGroupType(
    Handle start_handle,
    Handle end_handle,
    const common::UUID& group_type,
    uint16_t max_data_list_size,
    uint8_t* out_value_size,
    std::list<AttributeGrouping*>* out_results) {
  FXL_DCHECK(out_results);
  FXL_DCHECK(out_value_size);

  // Should be large enough to accomodate at least one entry with a non-empty
  // value (NOTE: in production this will be at least equal to
  // l2cap::kMinLEMTU). Smaller values are allowed for unit tests.
  FXL_DCHECK(max_data_list_size > sizeof(AttributeGroupDataEntry));

  if (start_handle == kInvalidHandle || start_handle > end_handle)
    return ErrorCode::kInvalidHandle;

  std::list<AttributeGrouping*> results;

  // Find the first grouping with start >= |start_handle|
  auto iter = std::lower_bound(groupings_.begin(), groupings_.end(),
                               start_handle, StartLessThan);
  if (iter == groupings_.end() || iter->start_handle() > end_handle)
    return ErrorCode::kAttributeNotFound;

  // "If the attributes with the requested type within the handle range have
  // attribute values with different lengths, then multiple Read By Group Type
  // Requests must be made." (see Vol 3, Part F, 3.4.4.9).
  //
  // |value_size| is determined by the first match.
  size_t value_size;
  size_t entry_size;
  for (; iter != groupings_.end(); ++iter) {
    // Exit the loop if the grouping is out of range.
    if (iter->start_handle() > end_handle)
      break;

    if (!iter->active() || !iter->complete())
      continue;

    if (iter->group_type() != group_type)
      continue;

    // TODO(armansito): Compare against actual connection security level here.
    // We currently do not allow security at the service declaration level, so
    // groupings are always readable.
    FXL_DCHECK(iter->attributes()[0].read_reqs().allowed_without_security());

    if (results.empty()) {
      value_size = iter->decl_value().size();

      // The actual size of the attribute group data entry that this attribute
      // would produce. This is both bounded by |max_data_list_size| and the
      // maximum value size that a Read By Group Type Response can accomodate.
      entry_size = std::min(
          value_size, static_cast<size_t>(kMaxReadByGroupTypeValueLength));
      entry_size = std::min(entry_size + sizeof(AttributeGroupDataEntry),
                            static_cast<size_t>(max_data_list_size));
    } else if (iter->decl_value().size() != value_size ||
               entry_size > max_data_list_size) {
      // Stop the search if the value size is different or it wouldn't fit
      // inside the PDU.
      break;
    }

    results.push_back(&*iter);
    max_data_list_size -= entry_size;
  }

  if (results.empty())
    return ErrorCode::kAttributeNotFound;

  // Return the potentially truncated value size.
  *out_value_size = entry_size - sizeof(AttributeGroupDataEntry);
  *out_results = std::move(results);

  return ErrorCode::kNoError;
}

}  // namespace att
}  // namespace btlib
