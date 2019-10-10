// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ALLOCATING_ADDRESS_SPACE_H
#define ALLOCATING_ADDRESS_SPACE_H

#include <magma_util/simple_allocator.h>

#include "address_space.h"
#include "gpu_mapping.h"

// Address space built from a simple allocator and a platform iommu.
class AllocatingAddressSpace : public AddressSpace {
 public:
  explicit AllocatingAddressSpace(magma::AddressSpaceOwner* owner) : AddressSpace(owner) {}

  bool Init(uint64_t base, size_t size, std::unique_ptr<magma::PlatformIommu> iommu) {
    DASSERT(!allocator_);
    allocator_ = magma::SimpleAllocator::Create(base, size);
    if (!allocator_)
      return DRETF(false, "SimpleAllocator::Create failed");

    AddressSpace::Init(std::move(iommu));

    return true;
  }

  uint64_t Size() const override {
    DASSERT(allocator_);
    return allocator_->size();
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
