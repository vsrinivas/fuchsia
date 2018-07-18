// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "pts_manager.h"

void PtsManager::InsertPts(uint64_t offset, uint64_t pts) {
  std::lock_guard<std::mutex> lock(lock_);
  pts_list_[offset] = pts;

  // Erase the oldest PTS, assuming they probably won't be used anymore.
  while (pts_list_.size() > 100) {
    pts_list_.erase(pts_list_.begin());
  }
}

bool PtsManager::LookupPts(uint64_t offset, uint64_t* pts_out) {
  std::lock_guard<std::mutex> lock(lock_);
  auto it = pts_list_.upper_bound(offset);
  // Check if this offset is < any element in the list.
  if (it == pts_list_.begin())
    return false;
  // Decrement to find the pts corresponding to the last offset <= |offset|.
  --it;
  *pts_out = it->second;
  return true;
}
