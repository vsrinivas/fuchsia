// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DRIVERS_MSD_ARM_MALI_SRC_ADDRESS_SPACE_H_
#define SRC_GRAPHICS_DRIVERS_MSD_ARM_MALI_SRC_ADDRESS_SPACE_H_

#include <limits.h>

#include <vector>

#include "magma_util/macros.h"
#include "platform_buffer.h"
#include "platform_bus_mapper.h"
#include "types.h"

#ifndef PAGE_SHIFT

#if PAGE_SIZE == 4096
#define PAGE_SHIFT 12
#else
#error PAGE_SHIFT not defined
#endif

#endif

class AddressSpace;
class MsdArmConnection;

class AddressSlotMapping {
 public:
  AddressSlotMapping(uint32_t slot_number, std::shared_ptr<MsdArmConnection> connection)
      : slot_number_(slot_number), connection_(connection) {}

  uint32_t slot_number() const { return slot_number_; }
  std::shared_ptr<MsdArmConnection> connection() const { return connection_; }

 private:
  uint32_t slot_number_;
  std::shared_ptr<MsdArmConnection> connection_;
};

class AddressSpaceObserver {
 public:
  virtual void FlushAddressMappingRange(AddressSpace* address_space, uint64_t start,
                                        uint64_t length, bool synchronous) = 0;

  // Tells the GPU to retry any memory lookup using this address space. Also
  // happens implicitly upon flush.
  virtual void UnlockAddressSpace(AddressSpace* address_space) = 0;

  virtual void ReleaseSpaceMappings(const AddressSpace* address_space) = 0;
};

// This should only be accessed on the connection thread (for now).
class AddressSpace {
 public:
  class Owner {
   public:
    virtual ~Owner() = 0;
    virtual AddressSpaceObserver* GetAddressSpaceObserver() = 0;
    virtual std::shared_ptr<Owner> GetSharedPtr() = 0;
    virtual magma::PlatformBusMapper* GetBusMapper() = 0;
  };

  static constexpr uint32_t kVirtualAddressSize = 48;
  static constexpr uint32_t kNormalMemoryAttributeSlot = 0;
  static constexpr uint32_t kOuterCacheableAttributeSlot = 1;

  // If cache_coherent is true, then updates to the page tables themselves
  // should be cache coherent with the GPU.
  static std::unique_ptr<AddressSpace> Create(Owner* owner, bool cache_coherent);

  ~AddressSpace();

  bool Insert(gpu_addr_t addr, magma::PlatformBusMapper::BusMapping* bus_mapping, uint64_t offset,
              uint64_t length, uint64_t flags);

  bool Clear(gpu_addr_t start, uint64_t length);
  void Unlock() { owner_->GetAddressSpaceObserver()->UnlockAddressSpace(this); }

  bool ReadPteForTesting(gpu_addr_t addr, mali_pte_t* entry);

  uint64_t translation_table_entry() const;

  std::shared_ptr<Owner> owner() const { return owner_->GetSharedPtr(); }

 private:
  static constexpr uint32_t kPageTableEntries = PAGE_SIZE / sizeof(mali_pte_t);
  static constexpr uint32_t kPageTableMask = kPageTableEntries - 1;
  static constexpr uint32_t kPageOffsetBits = 9;
  static_assert(kPageTableEntries == 1 << kPageOffsetBits, "incorrect page table entry count");
  // There are 3 levels of page directories, then an address table.
  static constexpr uint32_t kPageDirectoryLevels = 4;

  static_assert(kPageOffsetBits * kPageDirectoryLevels + PAGE_SHIFT == kVirtualAddressSize,
                "Incorrect virtual address size");

  struct PageTableGpu {
    mali_pte_t entry[kPageTableEntries];
  };

  class PageTable {
   public:
    static std::unique_ptr<PageTable> Create(Owner* owner, uint32_t level, bool cache_coherent);

    PageTableGpu* gpu() { return gpu_; }

    // Get the leaf page table for |page_number|. If |create| is false then
    // returns null instead of creating one.
    PageTable* GetPageTableLevel0(uint64_t page_number, bool create);

    void WritePte(uint64_t page_index, mali_pte_t pte);

    uint64_t page_bus_address() const { return bus_mapping_->Get()[0]; }

    // Collect empty page tables that are in the path to page_number, and
    // put them in |empty_tables|. |is_empty| is set if the page table is
    // now empty.
    void GarbageCollectChildren(uint64_t page_number, bool* is_empty,
                                std::vector<std::unique_ptr<PageTable>>* empty_tables);

   private:
    static mali_pte_t get_directory_entry(uint64_t physical_address);

    PageTable(Owner* owner, uint32_t level, bool cache_coherent,
              std::unique_ptr<magma::PlatformBuffer> buffer, PageTableGpu* gpu,
              std::unique_ptr<magma::PlatformBusMapper::BusMapping> bus_mapping);

    // The root page table has level 3, and the leaves have level 0.
    Owner* owner_;
    const uint32_t level_;
    const bool cache_coherent_;
    std::unique_ptr<magma::PlatformBuffer> buffer_;
    PageTableGpu* gpu_;
    std::unique_ptr<magma::PlatformBusMapper::BusMapping> bus_mapping_;
    std::vector<std::unique_ptr<PageTable>> next_levels_;

    friend class TestAddressSpace;
  };

  AddressSpace(Owner* owner, bool cache_coherent, std::unique_ptr<PageTable> root_page_directory);

  Owner* owner_;
  bool cache_coherent_;
  std::unique_ptr<PageTable> root_page_directory_;

  friend class TestAddressSpace;

  DISALLOW_COPY_AND_ASSIGN(AddressSpace);
};

#endif
