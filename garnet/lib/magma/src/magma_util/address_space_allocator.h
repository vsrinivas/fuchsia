// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ADDRESS_SPACE_ALLOCATOR_H
#define ADDRESS_SPACE_ALLOCATOR_H

#include "magma_util/macros.h"

namespace magma {

class AddressSpaceAllocator {
 public:
  // Constructs an address space ranging from address base to address base + size.
  AddressSpaceAllocator(uint64_t base, size_t size) : base_(base), size_(size) {
    DASSERT(size > 0);
    DASSERT(size <= UINT64_MAX - base);
  }

  virtual ~AddressSpaceAllocator() = default;

  uint64_t base() const { return base_; }
  size_t size() const { return size_; }

  // Allocates an address for a region of the given size and alignment, where alignment
  // is specified by 2 << align_pow2.
  // If alignment is less than a page than page alignment will be used.
  // On success returns true and addr_out is set; otherwise returns false.
  virtual bool Alloc(size_t size, uint8_t align_pow2, uint64_t* addr_out) = 0;

  // Frees an address that was previously allocated.
  virtual bool Free(uint64_t addr) = 0;

  // Returns true and the size of the region if mapped; otherwise returns false.
  virtual bool GetSize(uint64_t addr, size_t* size_out) = 0;

 private:
  uint64_t base_;
  size_t size_;
};

}  // namespace magma

#endif  // ADDRESS_SPACE_ALLOCATOR_H
