// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FAKE_ADDRESS_SPACE_H
#define FAKE_ADDRESS_SPACE_H

#include <map>

#include <magma_util/address_space.h>
#include <magma_util/macros.h>

// AddressSpaceBase is the base class of the FakeAllocatingAddresSpace, could be
// magma::AddressSpace or some derivative.
// AddressSpaceBase should be templated by GpuMapping; this is checked via static_assert.
template <typename GpuMapping, typename AddressSpaceBase>
class FakeAllocatingAddressSpace : public AddressSpaceBase {
 public:
  static_assert(std::is_base_of<magma::AddressSpace<GpuMapping>, AddressSpaceBase>::value,
                "AddressSpaceBase must derive from magma::AddressSpace");

  FakeAllocatingAddressSpace(magma::AddressSpaceOwner* owner, uint64_t base, uint64_t size)
      : AddressSpaceBase(owner), size_(base + size), next_addr_(base) {}

  uint64_t Size() const override { return size_; }

  bool AllocLocked(size_t size, uint8_t align_pow2, uint64_t* addr_out) override {
    DASSERT(magma::is_page_aligned(size));
    uint64_t addr = magma::round_up(next_addr_, 1ul << align_pow2);
    allocations_[addr] = Allocation{size, true, true};
    *addr_out = addr;
    next_addr_ = addr + size;
    return true;
  }

  bool FreeLocked(uint64_t addr) override {
    auto iter = allocations_.find(addr);
    if (iter == allocations_.end())
      return false;
    iter->second.allocated = false;
    return true;
  }

  bool ClearLocked(uint64_t addr, magma::PlatformBusMapper::BusMapping* bus_mapping) override {
    auto iter = allocations_.find(addr);
    if (iter == allocations_.end())
      return false;
    iter->second.clear = true;
    return true;
  }

  bool InsertLocked(uint64_t addr, magma::PlatformBusMapper::BusMapping* bus_mapping,
                    uint32_t guard_page_count) override {
    auto iter = allocations_.find(addr);
    if (iter == allocations_.end())
      return false;
    iter->second.clear = false;
    return true;
  }

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

template <typename GpuMapping, typename AddressSpaceBase>
class FakeNonAllocatingAddressSpace : public AddressSpaceBase {
 public:
  static_assert(std::is_base_of<magma::AddressSpace<GpuMapping>, AddressSpaceBase>::value,
                "AddressSpaceBase must derive from magma::AddressSpace");

  FakeNonAllocatingAddressSpace(magma::AddressSpaceOwner* owner, uint64_t size)
      : AddressSpaceBase(owner), size_(size) {}

  uint64_t Size() const override { return size_; }

  uint64_t MaxGuardPageCount() override { return 2; }

  bool AllocLocked(size_t size, uint8_t align_pow2, uint64_t* addr_out) override { return false; }
  bool FreeLocked(uint64_t addr) override { return true; }

  bool InsertLocked(uint64_t addr, magma::PlatformBusMapper::BusMapping* bus_mapping,
                    uint32_t guard_page_count) override {
    uint64_t length = (bus_mapping->page_count() + guard_page_count) * magma::page_size();

    auto iter = map_.upper_bound(addr);
    if (iter != map_.end() && (addr + length) > iter->first)
      return false;

    if (iter != map_.begin()) {
      --iter;
      if (iter->first + iter->second > addr)
        return false;
    }

    map_.insert({addr, bus_mapping->page_count() * magma::page_size()});
    return true;
  }

  bool ClearLocked(uint64_t addr, magma::PlatformBusMapper::BusMapping* bus_mapping) override {
    uint64_t length = bus_mapping->page_count() * magma::page_size();
    auto iter = map_.find(addr);
    if (iter != map_.end()) {
      if (iter->second != length) {
        return false;
      }
      map_.erase(iter);
      return true;
    }
    return false;
  }

  uint64_t inserted_size(uint64_t addr) {
    auto iter = map_.find(addr);
    DASSERT(iter != map_.end());
    return iter->second;
  }

 private:
  // Map of address, size.
  std::map<uint64_t, uint64_t> map_;
  uint64_t size_;
};

#endif  // FAKE_ADDRESS_SPACE_H
