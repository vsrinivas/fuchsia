// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ALLOCATING_ADDRESS_SPACE_H
#define ALLOCATING_ADDRESS_SPACE_H

#include <magma_util/simple_allocator.h>

#include "address_space.h"
#include "gpu_mapping.h"

// An address space built from a simple allocator and a platform iommu.
// The region used for allocation is specified in Init; the region between
// address 0 and the allocating base may be used as a non-allocating region.
class PartialAllocatingAddressSpace : public AddressSpace {
 public:
  explicit PartialAllocatingAddressSpace(magma::AddressSpaceOwner* owner, uint64_t size,
                                         std::shared_ptr<magma::PlatformIommu> iommu)
      : AddressSpace(owner, size, std::move(iommu)) {}

  bool Init(uint64_t base, size_t allocating_size) {
    DASSERT(!allocator_);
    DASSERT(allocating_size <= Size());

    allocator_ = magma::SimpleAllocator::Create(base, allocating_size);
    if (!allocator_)
      return DRETF(false, "SimpleAllocator::Create failed");

    return true;
  }

  bool AllocLocked(size_t size, uint8_t align_pow2, uint64_t* addr_out) override {
    DASSERT(allocator_);
    return allocator_->Alloc(size, align_pow2, addr_out);
  }

  bool FreeLocked(uint64_t addr) override {
    DASSERT(allocator_);
    return allocator_->Free(addr);
  }

 private:
  std::unique_ptr<magma::SimpleAllocator> allocator_;
};

#endif  // ALLOCATING_ADDRESS_SPACE_H
