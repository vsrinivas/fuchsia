// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MOCK_ADDRESS_SPACE_H
#define MOCK_ADDRESS_SPACE_H

#include <map>

#include "address_space.h"
#include "magma_util/macros.h"

class MockAllocatingAddressSpace : public AddressSpace {
 public:
  MockAllocatingAddressSpace(AddressSpace::Owner* owner, uint64_t base, uint64_t size)
      : AddressSpace(owner, ADDRESS_SPACE_PPGTT), size_(size), next_addr_(base) {}

  uint64_t Size() const override { return size_; }

  bool AllocLocked(size_t size, uint8_t align_pow2, uint64_t* addr_out) override;
  bool FreeLocked(uint64_t addr) override;
  bool ClearLocked(uint64_t addr, uint64_t page_count) override;
  bool InsertLocked(uint64_t addr, magma::PlatformBusMapper::BusMapping* bus_mapping) override;

  bool is_allocated(uint64_t addr) {
    auto iter = allocations_.find(addr);
    if (iter == allocations_.end())
      return false;
    return iter->second.allocated;
  }

  bool is_clear(uint64_t addr) {
    auto iter = allocations_.find(addr);
    DASSERT(iter != allocations_.end());
    return iter->second.clear;
  }

  uint64_t allocated_size(uint64_t addr) {
    auto iter = allocations_.find(addr);
    DASSERT(iter != allocations_.end());
    return iter->second.size;
  }

 private:
  uint64_t size_;
  uint64_t next_addr_;

  struct Allocation {
    uint64_t size;
    bool allocated;
    bool clear;
  };
  std::map<uint64_t, Allocation> allocations_;
};

using MockAddressSpace = MockAllocatingAddressSpace;

class MockNonAllocatingAddressSpace : public AddressSpace {
 public:
  MockNonAllocatingAddressSpace(AddressSpace::Owner* owner, uint64_t size)
      : AddressSpace(owner, ADDRESS_SPACE_PPGTT), size_(size) {}

  uint64_t Size() const override { return size_; }

  bool AllocLocked(size_t size, uint8_t align_pow2, uint64_t* addr_out) override { return false; }
  bool FreeLocked(uint64_t addr) override { return true; }

  bool ClearLocked(uint64_t addr, uint64_t page_count) override {
    return addr + page_count * PAGE_SIZE <= Size();
  }

  bool InsertLocked(uint64_t addr, magma::PlatformBusMapper::BusMapping* bus_mapping) override {
    return addr + bus_mapping->page_count() * PAGE_SIZE <= Size();
  }

 private:
  uint64_t size_;
};

#endif  // MOCK_ADDRESS_SPACE_H
