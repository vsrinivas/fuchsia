// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SIMPLE_ALLOCATOR_H
#define SIMPLE_ALLOCATOR_H

#include <list>

#include "address_space_allocator.h"

namespace magma {

class SimpleAllocator final : public AddressSpaceAllocator {
 public:
  static std::unique_ptr<SimpleAllocator> Create(uint64_t base, size_t size);

  bool Alloc(size_t size, uint8_t align_pow2, uint64_t* addr_out) override;
  bool Free(uint64_t addr) override;
  bool GetSize(uint64_t addr, size_t* size_out) override;

 private:
  SimpleAllocator(uint64_t base, size_t size);

  struct Region {
    Region(uint64_t base, size_t size);
    uint64_t base;
    size_t size;
  };

  bool CheckGap(SimpleAllocator::Region* prev, SimpleAllocator::Region* next, uint64_t align,
                size_t size, uint64_t* addr_out, bool* continue_search_out);

  DISALLOW_COPY_AND_ASSIGN(SimpleAllocator);

 private:
  std::list<SimpleAllocator::Region>::iterator FindRegion(uint64_t addr);

  std::list<SimpleAllocator::Region> regions_;
};

}  // namespace magma

#endif  // SIMPLE_ALLOCATOR_H
