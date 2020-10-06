// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DRIVERS_MSD_VSI_VIP_SRC_ADDRESS_SPACE_H_
#define SRC_GRAPHICS_DRIVERS_MSD_VSI_VIP_SRC_ADDRESS_SPACE_H_

#include <limits.h>

#include <vector>

#include "gpu_mapping.h"
#include "macros.h"
#include "magma_util/address_space.h"
#include "platform_buffer.h"
#include "platform_bus_mapper.h"

#ifndef PAGE_SHIFT
#define PAGE_SHIFT 12
#endif
static_assert(PAGE_SHIFT == 12, "Need 4k page");

class AddressSpace : public magma::AddressSpace<GpuMapping> {
 public:
  class Owner : public magma::AddressSpaceOwner {
   public:
    virtual void AddressSpaceReleased(AddressSpace* address_space) = 0;
  };

  static std::unique_ptr<AddressSpace> Create(Owner* owner, uint32_t page_table_array_slot);

  AddressSpace(Owner* owner, uint32_t page_table_array_slot)
      : magma::AddressSpace<GpuMapping>(owner),
        owner_(owner),
        page_table_array_slot_(page_table_array_slot) {}

  virtual ~AddressSpace() { owner_->AddressSpaceReleased(this); }

  // Though this address space does not support allocations, this needs to be implemented
  // to avoid errors from when a gpu mapping is released and attempts to call |FreeLocked|.
  bool FreeLocked(uint64_t addr) override { return true; }

  bool InsertLocked(uint64_t addr, magma::PlatformBusMapper::BusMapping* bus_mapping) override;
  bool ClearLocked(uint64_t addr, magma::PlatformBusMapper::BusMapping* bus_mapping) override;

  uint64_t Size() const override { return 1ull << 40; }

  uint64_t bus_addr() { return root_->bus_addr(); }

  uint32_t page_table_array_slot() { return page_table_array_slot_; }

  void SetRingbufferGpuMapping(std::shared_ptr<GpuMapping> gpu_mapping) {
    DASSERT(!ringbuffer_gpu_mapping_);
    ringbuffer_gpu_mapping_ = gpu_mapping;
  }

  bool GetRingbufferGpuAddress(uint64_t* gpu_addr_out) {
    if (ringbuffer_gpu_mapping_) {
      *gpu_addr_out = ringbuffer_gpu_mapping_->gpu_addr();
      return true;
    }
    return false;
  }

 private:
  // Maximum bus address is 40 bits.
  // Only a 32bit pte is needed because bits [11..0] of a page aligned address
  // are always zero.  Address bits [39..32] are stored in pte bits [11..4].
  using pte_t = uint32_t;
  using pde_t = pte_t;

  static const AddressSpace::pte_t kInvalidPte;
  static const AddressSpace::pte_t kInvalidPde;

  static inline pte_t pte_encode_unsafe(uint64_t bus_addr, bool valid, bool writeable,
                                        bool exception) {
    // Must be a 4k page address.
    DASSERT(magma::is_page_aligned(bus_addr));
    DASSERT(fits_in_40_bits(bus_addr));
    pte_t pte =
        magma::to_uint32(bus_addr & 0xFFFFFFFF) | ((magma::to_uint32(bus_addr >> 32) & 0xFF) << 4);
    if (valid)
      pte |= 1;
    if (exception)
      pte |= (1 << 1);
    if (writeable)
      pte |= (1 << 2);
    return pte;
  }

  static inline bool pte_encode(uint64_t bus_addr, bool valid, bool writeable, bool exception,
                                pte_t* pte_out) {
    if (!fits_in_40_bits(bus_addr))
      return DRETF(false, "bus address doesn't fit in 40 bits: 0x%lx", bus_addr);
    *pte_out = pte_encode_unsafe(bus_addr, valid, writeable, exception);
    return true;
  }

  static inline bool pde_encode(uint64_t bus_addr, bool valid, pde_t* pde_out) {
    return pte_encode(bus_addr, valid, false, true, pde_out);
  }

  static constexpr uint32_t kVirtualAddressBits = 32;

  static constexpr uint64_t kPageDirectoryShift = 10;
  static constexpr uint64_t kPageDirectoryEntries = 1 << kPageDirectoryShift;
  static constexpr uint64_t kPageDirectoryMask = kPageDirectoryEntries - 1;

  static constexpr uint64_t kPageTableShift = 10;
  static constexpr uint64_t kPageTableEntries = 1 << kPageTableShift;
  static constexpr uint64_t kPageTableMask = kPageTableEntries - 1;

  class Page {
   public:
    Page(Owner* owner) : owner_(owner) {}

    bool Init(bool cached);
    void* mapping() { return mapping_; }
    uint64_t bus_addr() { return bus_mapping_->Get()[0]; }
    Owner* owner() { return owner_; }
    void Flush() { buffer_->CleanCache(0, buffer_->size(), false); }

   private:
    Owner* owner_;
    std::unique_ptr<magma::PlatformBuffer> buffer_;
    void* mapping_ = nullptr;
    std::unique_ptr<magma::PlatformBusMapper::BusMapping> bus_mapping_;
  };

  class PageTable : public Page {
   public:
    pte_t* entry(uint32_t page_index) {
      DASSERT(page_index < kPageTableEntries);
      struct PageTableGpu {
        pte_t entry[kPageTableEntries];
      };
      return &reinterpret_cast<PageTableGpu*>(mapping())->entry[page_index];
    }

    static std::unique_ptr<PageTable> Create(Owner* owner);

    PageTable(Owner* owner) : Page(owner) {}
  };

  class PageDirectory : public Page {
   public:
    pde_t* entry(uint32_t index) {
      DASSERT(index < kPageDirectoryEntries);
      struct PageDirectoryTableGpu {
        pde_t entry[kPageDirectoryEntries];
      };
      return &reinterpret_cast<PageDirectoryTableGpu*>(mapping())->entry[index];
    }

    uint32_t valid_count(uint32_t index) { return valid_counts_[index]; }

    // If |alloc| a page table will be created if one doesn't exist.
    PageTable* GetPageTable(uint32_t index, bool alloc);

    // Gets a page table, allocating one if necessary. Returns the valid count.
    pte_t* GetPageTableEntry(uint32_t page_directory_index, uint32_t page_table_index,
                             uint32_t* valid_count_out);

    // Should be called after a page table has been modified; if |valid_count| is
    // zero, the page table will be removed.
    void PageTableUpdated(uint32_t page_directory_index, uint32_t valid_count);

    static std::unique_ptr<PageDirectory> Create(Owner* owner);

    PageDirectory(Owner* owner)
        : Page(owner), page_tables_(kPageDirectoryEntries), valid_counts_(kPageDirectoryEntries) {}

   private:
    std::vector<std::unique_ptr<PageTable>> page_tables_;
    std::vector<uint32_t> valid_counts_;
  };

  bool Init();

  Owner* owner_;
  std::unique_ptr<PageDirectory> root_;

  uint32_t page_table_array_slot_;

  std::shared_ptr<GpuMapping> ringbuffer_gpu_mapping_;

  friend class TestAddressSpace;
  friend class TestAddressSpace_GarbageCollect_Test;

  DISALLOW_COPY_AND_ASSIGN(AddressSpace);
};

#endif
