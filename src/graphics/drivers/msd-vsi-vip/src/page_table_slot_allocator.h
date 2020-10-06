// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DRIVERS_MSD_VSI_VIP_SRC_PAGE_TABLE_SLOT_ALLOCATOR_H_
#define SRC_GRAPHICS_DRIVERS_MSD_VSI_VIP_SRC_PAGE_TABLE_SLOT_ALLOCATOR_H_

#include <atomic>
#include <mutex>
#include <vector>

#include "magma_util/macros.h"

// Thread-safe.
class PageTableSlotAllocator {
 public:
  PageTableSlotAllocator(uint32_t size) : slot_busy_(size), next_index_(size - 1) {}

  uint64_t size() { return slot_busy_.size(); }

  bool Alloc(uint32_t* index_out) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (uint32_t i = 0; i < slot_busy_.size(); i++) {
      uint32_t index = (next_index_ + i) % slot_busy_.size();
      if (!slot_busy_[index]) {
        slot_busy_[index] = true;
        *index_out = index;
        next_index_ = index + 1;
        return true;
      }
    }
    return false;
  }

  void Free(uint32_t index) {
    DASSERT(index < slot_busy_.size());
    DASSERT(slot_busy_[index]);
    slot_busy_[index] = false;
  }

 private:
  std::vector<std::atomic<bool>> slot_busy_;
  std::mutex mutex_;
  uint32_t next_index_;

  friend class TestPageTableSlotAllocator;
};

#endif  // SRC_GRAPHICS_DRIVERS_MSD_VSI_VIP_SRC_PAGE_TABLE_SLOT_ALLOCATOR_H_
