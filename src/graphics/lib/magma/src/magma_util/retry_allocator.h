// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RETRY_ALLOCATOR_H
#define RETRY_ALLOCATOR_H

#include <lib/fit/function.h>

#include <map>

#include "macros.h"

namespace magma {

// This is an allocator that calls a user-specified allocation function on the proposed address
// range, and if that fails it tries new address ranges until it succeeds or it runs out of address
// space.
class RetryAllocator final {
 public:
  using AllocationFunction = fit::function<bool(uint64_t)>;

  static std::unique_ptr<RetryAllocator> Create(uint64_t base, uint64_t size);

  bool Alloc(size_t size, uint8_t align_pow2, AllocationFunction map_function, uint64_t* addr_out);
  bool Free(uint64_t addr, uint64_t size);

  uint64_t base() const { return base_; }
  uint64_t size() const { return size_; }

 private:
  RetryAllocator(uint64_t base, size_t size);

  // Retrys from the address to the length of the region.
  std::map<uint64_t, uint64_t> free_regions_;

  uint64_t base_;
  uint64_t size_;

  DISALLOW_COPY_AND_ASSIGN(RetryAllocator);
};

}  // namespace magma

#endif  // RETRY_ALLOCATOR_H
