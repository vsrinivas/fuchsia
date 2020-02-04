// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "retry_allocator.h"

#include <iterator>
#include <memory>

#include "magma_util/dlog.h"
#include "magma_util/macros.h"

namespace magma {

std::unique_ptr<RetryAllocator> RetryAllocator::Create(uint64_t base, uint64_t size) {
  return std::unique_ptr<RetryAllocator>(new RetryAllocator(base, size));
}

bool RetryAllocator::Free(uint64_t start, uint64_t size) {
  size = magma::round_up(size, magma::page_size());
  auto current_region = free_regions_.insert(std::make_pair(start, size)).first;

  // Attempt to combine with previous region.
  if (current_region != free_regions_.begin()) {
    auto prev = std::prev(current_region);
    if (prev->first + prev->second == start) {
      prev->second += size;

      free_regions_.erase(current_region);
      current_region = prev;
    }
  }

  // Attempt to combine with next region.
  auto next = std::next(current_region);
  if (next != free_regions_.end()) {
    if (next->first == current_region->first + current_region->second) {
      current_region->second += next->second;
      free_regions_.erase(next);
    }
  }
  return true;
}

bool RetryAllocator::Alloc(size_t size, uint8_t align_pow2, AllocationFunction map_function,
                           uint64_t* addr_out) {
  if (size == 0)
    return DRETF(false, "Can't allocate size 0");
  size = magma::round_up(size, magma::page_size());
  uint64_t alignment = 1 << align_pow2;
  if (alignment < magma::page_size())
    alignment = magma::page_size();
  uint64_t address = magma::round_up(base_, alignment);
  while (true) {
    if (address - base_ + size > size_)
      return DRETF(false, "Failed to find valid address");
    DASSERT(address % alignment == 0);

    auto next_region = free_regions_.upper_bound(address);
    if (next_region != free_regions_.begin()) {
      auto prev = std::prev(next_region);
      if (prev->second + prev->first >= address + size) {
        // Address is inside free region.
        if (map_function(address))
          break;
        // The address should work, but something else may be mapped there, so try the next
        // aligned address.
        address += alignment;
        continue;
      }
    }

    if (next_region == free_regions_.end())
      return DRETF(false, "failed to allocate address size %lx", size);
    // Find the first aligned location >= the base of the next region.
    address = magma::round_up(next_region->first, alignment);
  }

  // Split free region around the allocated region.
  auto it = free_regions_.upper_bound(address);
  DASSERT(it != free_regions_.begin());
  --it;
  uint64_t first_range_start = it->first;
  uint64_t first_range_length = address - first_range_start;
  uint64_t second_range_start = magma::round_up(address + size, magma::page_size());
  uint64_t second_range_length = it->first + it->second - second_range_start;
  if (first_range_length)
    it->second = first_range_length;
  else
    free_regions_.erase(it);
  if (second_range_length)
    free_regions_.insert(std::make_pair(second_range_start, second_range_length));
  *addr_out = address;
  return true;
}

RetryAllocator::RetryAllocator(uint64_t base, size_t size) : base_(base), size_(size) {
  free_regions_[base] = size;
}

}  // namespace magma
