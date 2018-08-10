// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "pts_manager.h"

#include <lib/fxl/logging.h>

void PtsManager::InsertPts(uint64_t offset, uint64_t pts) {
  std::lock_guard<std::mutex> lock(lock_);

  // caller should not insert duplicates
  FXL_DCHECK(offset_to_result_.find(offset) == offset_to_result_.end());
  // caller should set offsets in order
  FXL_DCHECK(offset_to_result_.empty() ||
             offset > (*offset_to_result_.rbegin()).first);

  offset_to_result_.emplace(
      std::make_pair(offset, LookupResult(false, true, pts)));

  // Erase the oldest PTS, assuming they probably won't be used anymore.
  while (offset_to_result_.size() > 100) {
    offset_to_result_.erase(offset_to_result_.begin());
  }
}

void PtsManager::SetEndOfStreamOffset(uint64_t end_of_stream_offset) {
  std::lock_guard<std::mutex> lock(lock_);

  // caller should not insert duplicates
  FXL_DCHECK(offset_to_result_.find(end_of_stream_offset) ==
             offset_to_result_.end());
  // caller should set offsets in order
  FXL_DCHECK(offset_to_result_.empty() ||
             end_of_stream_offset > (*offset_to_result_.rbegin()).first);

  // caller should only set end of stream offset once
  FXL_DCHECK(offset_to_result_.empty() ||
             !(*offset_to_result_.rbegin()).second.is_end_of_stream());

  offset_to_result_.emplace(
      std::make_pair(end_of_stream_offset, LookupResult(true, false, 0)));
}

const PtsManager::LookupResult PtsManager::Lookup(uint64_t offset) {
  std::lock_guard<std::mutex> lock(lock_);
  auto it = offset_to_result_.upper_bound(offset);
  // Check if this offset is < any element in the list.
  if (it == offset_to_result_.begin())
    return PtsManager::LookupResult(false, false, 0);
  // Decrement to find the pts corresponding to the last offset <= |offset|.
  --it;
  return it->second;
}
