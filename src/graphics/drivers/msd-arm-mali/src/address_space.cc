// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "address_space.h"

#include "magma_util/macros.h"

enum LpaeEntryType : mali_pte_t {
  kLpaeEntryTypeMask = 0x3,

  // Address translation entry - points to a 4kB physical page.
  kLpaeEntryTypeAte = (1 << 0),
  kLpaeEntryTypeInvalid = (2 << 0),

  // Page table entry - points to another page table.
  kLpaeEntryTypePte = (3 << 0),
};

enum LpaeFlags : mali_pte_t {
  kLpaeFlagWrite = (1 << 7),
  kLpaeFlagRead = (1 << 6),
  kLpaeFlagNoExecute = (1ul << 54),
  kLpaeFlagShareBoth = (2 << 8),
  kLpaeFlagShareInner = (3 << 8),
};

static uint64_t get_mmu_flags(uint64_t access_flags) {
  uint64_t mmu_flags = 0;
  if (access_flags & kAccessFlagWrite)
    mmu_flags |= kLpaeFlagWrite;
  if (access_flags & kAccessFlagRead)
    mmu_flags |= kLpaeFlagRead;
  if (access_flags & kAccessFlagNoExecute)
    mmu_flags |= kLpaeFlagNoExecute;

  uint8_t attribute_slot = (access_flags & kAccessFlagShareBoth)
                               ? AddressSpace::kOuterCacheableAttributeSlot
                               : AddressSpace::kNormalMemoryAttributeSlot;

  mmu_flags |= attribute_slot << 2;

  if (access_flags & kAccessFlagShareBoth)
    mmu_flags |= kLpaeFlagShareBoth;
  else if (access_flags & kAccessFlagShareInner)
    mmu_flags |= kLpaeFlagShareInner;
  return mmu_flags;
}

AddressSpace::Owner::~Owner() = default;

std::unique_ptr<AddressSpace> AddressSpace::Create(Owner* owner, bool cache_coherent) {
  auto page_directory =
      AddressSpace::PageTable::Create(owner, kPageDirectoryLevels - 1, cache_coherent);
  if (!page_directory)
    return DRETP(nullptr, "failed to create root page table");
  return std::unique_ptr<AddressSpace>(
      new AddressSpace(owner, cache_coherent, std::move(page_directory)));
}

AddressSpace::~AddressSpace() { owner_->GetAddressSpaceObserver()->ReleaseSpaceMappings(this); }

bool AddressSpace::Insert(uint64_t addr, magma::PlatformBusMapper::BusMapping* bus_mapping,
                          uint64_t offset, uint64_t length, uint64_t flags) {
  DASSERT(magma::is_page_aligned(addr));
  DASSERT(magma::is_page_aligned(offset));
  DASSERT(magma::is_page_aligned(length));

  uint64_t start_page_index = offset / PAGE_SIZE;
  uint64_t num_pages = length / PAGE_SIZE;

  if ((addr / PAGE_SIZE) + num_pages > (1l << (kVirtualAddressSize - PAGE_SHIFT)))
    return DRETF(false, "Virtual address too large");

  std::vector<uint64_t>& bus_addr_array = bus_mapping->Get();

  if (start_page_index < bus_mapping->page_offset())
    return DRETF(false, "invalid bus mapping start_page_index %lu < bus_mapping page_offset %lu",
                 start_page_index, bus_mapping->page_offset());

  if (start_page_index + num_pages > bus_mapping->page_offset() + bus_mapping->page_count())
    return DRETF(false, "invalid bus mapping");

  // TODO(fxbug.dev/12966): ensure the range isn't currently in use.

  for (uint64_t i = 0; i < num_pages; i++) {
    // TODO(fxbug.dev/12978): optimize walk to not get page table every time.
    uint64_t page_index = i + addr / PAGE_SIZE;
    PageTable* page_table = root_page_directory_->GetPageTableLevel0(page_index, true);
    if (!page_table)
      return DRETF(false, "Faied to get page table");

    mali_pte_t pte = bus_addr_array[start_page_index - bus_mapping->page_offset() + i] |
                     get_mmu_flags(flags) | kLpaeEntryTypeAte;
    page_table->WritePte(page_index, pte);
  }

  // No one should be using the (lack of) mapping, so asynchronous flush is
  // fine.
  owner_->GetAddressSpaceObserver()->FlushAddressMappingRange(this, addr, length, false);
  return true;
}

bool AddressSpace::Clear(uint64_t start, uint64_t length) {
  DASSERT(magma::is_page_aligned(start));
  DASSERT(magma::is_page_aligned(length));

  uint64_t num_pages = length >> PAGE_SHIFT;
  uint64_t start_page_index = start >> PAGE_SHIFT;

  if (start_page_index + num_pages > (1l << (kVirtualAddressSize - PAGE_SHIFT)))
    return DRETF(false, "Virtual address too large");

  std::vector<std::unique_ptr<PageTable>> empty_tables;
  for (uint64_t i = 0; i < num_pages; i++) {
    // TODO(fxbug.dev/12978): optimize walk to not get page table every time.
    uint64_t page_index = i + start_page_index;
    PageTable* page_table = root_page_directory_->GetPageTableLevel0(page_index, false);
    if (!page_table)
      continue;

    mali_pte_t pte = kLpaeEntryTypeInvalid;
    page_table->WritePte(page_index, pte);
    // Only attempt to GC children once per level 0 page table.
    bool last_entry = (page_index & kPageTableMask) == kPageTableMask;
    if (last_entry || i == num_pages - 1) {
      root_page_directory_->GarbageCollectChildren(page_index, nullptr, &empty_tables);
    }
  }

  owner_->GetAddressSpaceObserver()->FlushAddressMappingRange(this, start, length, true);

  return true;
}

bool AddressSpace::ReadPteForTesting(uint64_t addr, mali_pte_t* entry) {
  uint64_t page_index = addr >> PAGE_SHIFT;

  PageTable* page_table = root_page_directory_->GetPageTableLevel0(page_index, false);
  if (!page_table)
    return false;

  uint32_t offset = page_index & kPageTableMask;
  *entry = page_table->gpu()->entry[offset];
  return true;
}

uint64_t AddressSpace::translation_table_entry() const {
  enum LpaeAddressModes {
    kLpaeAddressModeUnmapped = 0,
    kLpaeAddressModeIdentity = 2u,
    kLpaeAddressModeTable = 3u
  };
  constexpr uint64_t kLpaeReadInner = (1u << 2);

  // If set, page table reads are coherent with main memory.
  constexpr uint64_t kLpaeReadOuter = (1u << 4);

  uint64_t flags =
      root_page_directory_->page_bus_address() | kLpaeReadInner | kLpaeAddressModeTable;
  if (cache_coherent_)
    flags |= kLpaeReadOuter;
  return flags;
}

AddressSpace::PageTable* AddressSpace::PageTable::GetPageTableLevel0(uint64_t page_number,
                                                                     bool create) {
  uint32_t shift = level_ * kPageOffsetBits;
  uint32_t offset = (page_number >> shift) & kPageTableMask;

  if (level_ == 0)
    return this;

  if (!next_levels_[offset]) {
    if (!create)
      return nullptr;

    auto directory = PageTable::Create(owner_, level_ - 1, cache_coherent_);
    if (!directory)
      return DRETP(nullptr, "failed to create page table");
    gpu_->entry[offset] = get_directory_entry(directory->page_bus_address());
    next_levels_[offset] = std::move(directory);
    if (!cache_coherent_)
      buffer_->CleanCache(offset * sizeof(gpu_->entry[0]), sizeof(gpu_->entry[0]), false);
  }
  return next_levels_[offset].get()->GetPageTableLevel0(page_number, create);
}

void AddressSpace::PageTable::WritePte(uint64_t page_index, mali_pte_t pte) {
  page_index &= kPageTableMask;
  gpu_->entry[page_index] = pte;
  if (!cache_coherent_)
    buffer_->CleanCache(page_index * sizeof(gpu_->entry[0]), sizeof(gpu_->entry[0]), false);
}

void AddressSpace::PageTable::GarbageCollectChildren(
    uint64_t page_number, bool* is_empty, std::vector<std::unique_ptr<PageTable>>* empty_tables) {
  uint32_t shift = level_ * kPageOffsetBits;
  uint32_t offset = (page_number >> shift) & kPageTableMask;
  if (is_empty)
    *is_empty = false;

  bool invalidated_entry = false;
  if (level_ == 0) {
    // Caller should have already made this entry invalid.
    invalidated_entry = true;
  } else if (next_levels_[offset]) {
    bool next_level_empty = false;
    next_levels_[offset]->GarbageCollectChildren(page_number, &next_level_empty, empty_tables);
    if (next_level_empty) {
      WritePte(offset, kLpaeEntryTypeInvalid);
      // Caller should synchronize MMU before deleting empty tables.
      empty_tables->push_back(std::move(next_levels_[offset]));
      invalidated_entry = true;
    }
  }

  if (!invalidated_entry)
    return;

  for (size_t i = 0; i < kPageTableEntries; i++) {
    if (gpu_->entry[i] != kLpaeEntryTypeInvalid)
      return;
  }
  if (is_empty)
    *is_empty = true;
}

mali_pte_t AddressSpace::PageTable::get_directory_entry(uint64_t physical_address) {
  DASSERT(!(physical_address & kLpaeEntryTypeMask));
  return physical_address | kLpaeEntryTypePte;
}

std::unique_ptr<AddressSpace::PageTable> AddressSpace::PageTable::Create(Owner* owner,
                                                                         uint32_t level,
                                                                         bool cache_coherent) {
  constexpr uint32_t kPageCount = 1;

  auto buffer = magma::PlatformBuffer::Create(kPageCount * PAGE_SIZE, "page-directory");
  if (!buffer)
    return DRETP(nullptr, "couldn't create buffer");

  PageTableGpu* gpu;
  if (!buffer->MapCpu(reinterpret_cast<void**>(&gpu)))
    return DRETP(nullptr, "failed to map cpu");

  std::unique_ptr<magma::PlatformBusMapper::BusMapping> bus_mapping =
      owner->GetBusMapper()->MapPageRangeBus(buffer.get(), 0, kPageCount);
  if (!bus_mapping)
    return DRETP(nullptr, "failed to map page range bus");

  return std::unique_ptr<PageTable>(
      new PageTable(owner, level, cache_coherent, std::move(buffer), gpu, std::move(bus_mapping)));
}

AddressSpace::PageTable::PageTable(
    Owner* owner, uint32_t level, bool cache_coherent,
    std::unique_ptr<magma::PlatformBuffer> buffer, PageTableGpu* gpu,
    std::unique_ptr<magma::PlatformBusMapper::BusMapping> bus_mapping)
    : owner_(owner),
      level_(level),
      cache_coherent_(cache_coherent),
      buffer_(std::move(buffer)),
      gpu_(gpu),
      bus_mapping_(std::move(bus_mapping)) {
  if (level_ != 0)
    next_levels_.resize(kPageTableEntries);
  for (uint32_t i = 0; i < kPageTableEntries; i++)
    gpu_->entry[i] = kLpaeEntryTypeInvalid;
  if (!cache_coherent_)
    buffer_->CleanCache(0, sizeof(*gpu_), false);
}

AddressSpace::AddressSpace(Owner* owner, bool cache_coherent,
                           std::unique_ptr<PageTable> page_directory)
    : owner_(owner),
      cache_coherent_(cache_coherent),
      root_page_directory_(std::move(page_directory)) {}
