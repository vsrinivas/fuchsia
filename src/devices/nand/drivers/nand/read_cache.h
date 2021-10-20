// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_NAND_DRIVERS_NAND_READ_CACHE_H_
#define SRC_DEVICES_NAND_DRIVERS_NAND_READ_CACHE_H_

#include <deque>

namespace nand {

// The Read Cache is a simple FIFO cache intended to be small in size.
// This class is *not* thread-safe.
class ReadCache {
 public:
  // Instantiate new cache of size `cache_size` with entries with `data_size` bytes for pages and
  // `spare_size` bytes for spare. `cache_size` must be greater than zero.
  ReadCache(uint32_t cache_size, size_t data_size, size_t spare_size);

  // Insert an entry for `page` by copying out of `data` and `spare`.
  void Insert(uint32_t page, const void* data, const void* spare);

  // Looks up a page by `page` number. If the entry is present then the data is
  // copied to `out_data` and `out_spare`, and returns true. Returns false if entry is not present.
  bool GetPage(uint32_t page, void* out_data, void* out_spare);

  // Scans for the pages in the range of `length` starting at `first_page` and purges them from the
  // cache. Returns the number of pages purged.
  size_t PurgeRange(uint32_t first_page, uint32_t length);

 private:
  struct FifoEntry {
    std::unique_ptr<uint8_t[]> entry;
    uint32_t page;
  };

  // Size of the data portion of the cached page.
  size_t data_size_;

  // Size of the spare section of the cached page.
  size_t spare_size_;

  // Max size of the cache in entry count.
  uint32_t max_entries_;

  // The fifo to store entries and scan. Kept sorted with most recent at the end.
  std::deque<FifoEntry> fifo_;
};

}  // namespace nand

#endif  // SRC_DEVICES_NAND_DRIVERS_NAND_READ_CACHE_H_
