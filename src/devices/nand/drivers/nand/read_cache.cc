// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "read_cache.h"

#include <string.h>
#include <zircon/assert.h>

namespace nand {

ReadCache::ReadCache(uint32_t cache_size, size_t data_size, size_t spare_size)
    : data_size_(data_size), spare_size_(spare_size), max_entries_(cache_size) {
  ZX_DEBUG_ASSERT(max_entries_ > 0);
}

void ReadCache::Insert(uint32_t page, const void* data, const void* spare) {
  // First purge this page if it exists. For the use case of this library it should never actually
  // occur, but for the ease of usability, make it correct. That's why it isn't being optimized
  // for at all.
  PurgeRange(page, 1);

  ReadCache::FifoEntry entry;
  if (fifo_.size() >= max_entries_) {
    // If the fifo is full we free from the head and steal the memory allocation.
    entry.entry = std::move(fifo_[0].entry);
    fifo_.pop_front();
  } else {
    // Allocate the memory for a new entry.
    entry.entry = std::make_unique<uint8_t[]>(data_size_ + spare_size_);
  }

  entry.page = page;
  // Copy in the data followed by the spare.
  memcpy(entry.entry.get(), data, data_size_);
  memcpy(&entry.entry.get()[data_size_], spare, spare_size_);

  // Insert to the tail
  fifo_.push_back(std::move(entry));
}

bool ReadCache::GetPage(uint32_t page_num, void* out_data, void* out_spare) {
  for (auto& entry : fifo_) {
    if (entry.page == page_num) {
      memcpy(out_data, entry.entry.get(), data_size_);
      memcpy(out_spare, &entry.entry.get()[data_size_], spare_size_);
      return true;
    }
  }
  return false;
}

size_t ReadCache::PurgeRange(uint32_t first_page, uint32_t length) {
  size_t purged = 0;
  for (auto it = fifo_.begin(); it != fifo_.end();) {
    if (it->page >= first_page && it->page <= first_page + (length - 1)) {
      it = fifo_.erase(it);
      ++purged;
    } else {
      ++it;
    }
  }
  return purged;
}

}  // namespace nand
