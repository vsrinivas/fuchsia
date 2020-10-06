// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ppgtt.h"

#include "magma_util/macros.h"
#include "platform_buffer.h"
#include "platform_logger.h"
#include "registers.h"

constexpr bool kLogEnable = false;

static unsigned int gen_ppat_index(CachingType caching_type) {
  switch (caching_type) {
    case CACHING_NONE:
      return 3;
    case CACHING_WRITE_THROUGH:
      return 2;
    case CACHING_LLC:
      return 4;
  }
}

static inline gen_pte_t gen_pte_encode(uint64_t bus_addr, CachingType caching_type, bool valid,
                                       bool writeable) {
  gen_pte_t pte = bus_addr;

  if (valid)
    pte |= PAGE_PRESENT;

  if (writeable)
    pte |= PAGE_RW;

  unsigned int pat_index = gen_ppat_index(caching_type);
  if (pat_index & (1 << 0))
    pte |= PAGE_PWT;
  if (pat_index & (1 << 1))
    pte |= PAGE_PCD;
  if (pat_index & (1 << 2))
    pte |= PAGE_PAT;

  return pte;
}

//////////////////////////////////////////////////////////////////////////////

std::unique_ptr<PerProcessGtt::PageTable> PerProcessGtt::PageTable::Create(
    Owner* owner, std::shared_ptr<PerProcessGtt::Page> scratch_page) {
  auto page_table = std::unique_ptr<PageTable>(new PageTable(std::move(scratch_page)));
  if (!page_table->Init(owner))
    return DRETP(nullptr, "page table init failed");
  for (uint32_t i = 0; i < kPageTableEntries; i++) {
    *page_table->page_table_entry(i) =
        gen_pte_encode(page_table->scratch_page()->bus_addr(), CACHING_NONE, true, false);
  }
  return page_table;
}

std::unique_ptr<PerProcessGtt::PageDirectory> PerProcessGtt::PageDirectory::Create(
    Owner* owner, std::shared_ptr<PerProcessGtt::PageTable> scratch_table) {
  auto dir = std::unique_ptr<PageDirectory>(new PageDirectory(std::move(scratch_table)));
  if (!dir->Init(owner))
    return DRETP(nullptr, "init failed");
  for (uint32_t i = 0; i < kPageDirectoryEntries; i++) {
    dir->page_directory_table_gpu()->entry[i] = gen_pde_encode(dir->scratch_table()->bus_addr());
  }
  return dir;
}

std::unique_ptr<PerProcessGtt::PageDirectoryPtrTable> PerProcessGtt::PageDirectoryPtrTable::Create(
    Owner* owner, std::shared_ptr<PerProcessGtt::PageDirectory> scratch_dir) {
  auto table =
      std::unique_ptr<PageDirectoryPtrTable>(new PageDirectoryPtrTable(std::move(scratch_dir)));
  if (!table->Init(owner))
    return DRETP(nullptr, "init failed");
  for (uint32_t i = 0; i < kPageDirectoryPtrEntries; i++) {
    table->page_directory_ptr_table_gpu()->entry[i] =
        gen_pdpe_encode(table->scratch_dir()->bus_addr());
  }
  return table;
}

std::unique_ptr<PerProcessGtt::Pml4Table> PerProcessGtt::Pml4Table::Create(Owner* owner) {
  auto scratch_page = std::shared_ptr<Page>(new Page());
  if (!scratch_page)
    return DRETP(nullptr, "failed to create scratch page");
  if (!scratch_page->Init(owner))
    return DRETP(nullptr, "failed to init scratch page");

  uint64_t scratch_bus_addr = scratch_page->bus_addr();

  auto scratch_table =
      std::shared_ptr<PageTable>(PageTable::Create(owner, std::move(scratch_page)));
  if (!scratch_table)
    return DRETP(nullptr, "failed to create scratch table");

  auto scratch_dir =
      std::shared_ptr<PageDirectory>(PageDirectory::Create(owner, std::move(scratch_table)));
  if (!scratch_dir)
    return DRETP(nullptr, "failed to create scratch dir");

  auto scratch_directory_ptr = PageDirectoryPtrTable::Create(owner, std::move(scratch_dir));
  if (!scratch_directory_ptr)
    return DRETP(nullptr, "failed to create scratch directory ptr");

  auto table =
      std::unique_ptr<Pml4Table>(new Pml4Table(scratch_bus_addr, std::move(scratch_directory_ptr)));
  if (!table->Init(owner))
    return DRETP(nullptr, "init failed");

  for (uint32_t i = 0; i < kPml4Entries; i++) {
    table->pml4_table_gpu()->entry[i] = gen_pml4_encode(table->scratch_directory_ptr_->bus_addr());
  }

  return table;
}

std::unique_ptr<PerProcessGtt> PerProcessGtt::Create(Owner* owner) {
  auto pml4_table = Pml4Table::Create(owner);
  if (!pml4_table)
    return DRETP(nullptr, "failed to create pml4table");

  return std::unique_ptr<PerProcessGtt>(new PerProcessGtt(owner, std::move(pml4_table)));
}

PerProcessGtt::PerProcessGtt(Owner* owner, std::unique_ptr<Pml4Table> pml4_table)
    : AddressSpace(owner, ADDRESS_SPACE_PPGTT), pml4_table_(std::move(pml4_table)) {}

bool PerProcessGtt::ClearLocked(uint64_t start, magma::PlatformBusMapper::BusMapping* bus_mapping) {
  DASSERT((start & (PAGE_SIZE - 1)) == 0);
  if (start > Size())
    return DRETF(false, "invalid start");

  uint64_t length = (bus_mapping->page_count() + kOverfetchPageCount + kGuardPageCount) * PAGE_SIZE;
  if (start + length > Size())
    return DRETF(false, "invalid start + length");

  // readable, because mesa doesn't properly handle overfetching
  gen_pte_t pte = gen_pte_encode(pml4_table_->scratch_page_bus_addr(), CACHING_NONE, true, false);

  uint32_t page_table_index = (start >>= PAGE_SHIFT) & kPageTableMask;
  uint32_t page_directory_index = (start >>= kPageTableShift) & kPageDirectoryMask;
  uint32_t page_directory_ptr_index = (start >>= kPageDirectoryShift) & kPageDirectoryPtrMask;
  uint32_t pml4_index = magma::to_uint32(start >>= kPageDirectoryPtrShift);

  DLOG("start pml4 %u pdp %u pd %i pt %u", pml4_index, page_directory_ptr_index,
       page_directory_index, page_table_index);

  auto page_directory = pml4_table_->page_directory(pml4_index, page_directory_ptr_index);
  auto page_table_entry =
      page_directory ? page_directory->page_table_entry(page_directory_index, page_table_index)
                     : nullptr;

  for (uint64_t num_entries = length >> PAGE_SHIFT; num_entries > 0; num_entries--) {
    if (!page_table_entry)
      return DRETF(false, "couldn't get page table entry");

    *page_table_entry++ = pte;

    if (++page_table_index == kPageTableEntries) {
      page_table_index = 0;
      if (++page_directory_index == kPageDirectoryEntries) {
        page_directory_index = 0;
        if (++page_directory_ptr_index == kPageDirectoryPtrEntries) {
          page_directory_ptr_index = 0;
          ++pml4_index;
          DASSERT(pml4_index < kPml4Entries);
        }
        page_directory = pml4_table_->page_directory(pml4_index, page_directory_ptr_index);
      }
      page_table_entry =
          page_directory ? page_directory->page_table_entry(page_directory_index, page_table_index)
                         : nullptr;
    }
  }

  return true;
}

bool PerProcessGtt::AllocLocked(size_t size, uint8_t align_pow2, uint64_t* addr_out) {
  DASSERT(false);
  return false;
}

bool PerProcessGtt::FreeLocked(uint64_t addr) { return true; }

bool PerProcessGtt::InsertLocked(uint64_t addr, magma::PlatformBusMapper::BusMapping* bus_mapping) {
  auto& bus_addr_array = bus_mapping->Get();
  uint64_t page_count = bus_addr_array.size();

  if (kLogEnable)
    MAGMA_LOG(INFO, "ppgtt insert (%p) 0x%" PRIx64 "-0x%" PRIx64 " length 0x%" PRIx64, this, addr,
              addr + page_count * PAGE_SIZE - 1, page_count * PAGE_SIZE);

  uint32_t page_table_index = (addr >>= PAGE_SHIFT) & kPageTableMask;
  uint32_t page_directory_index = (addr >>= kPageTableShift) & kPageDirectoryMask;
  uint32_t page_directory_ptr_index = (addr >>= kPageDirectoryShift) & kPageDirectoryPtrMask;
  uint32_t pml4_index = magma::to_uint32(addr >>= kPageDirectoryPtrShift);

  DLOG("addr pml4 %u pdp %u pd %i pt %u", pml4_index, page_directory_ptr_index,
       page_directory_index, page_table_index);

  auto page_directory = pml4_table_->page_directory(pml4_index, page_directory_ptr_index);
  auto page_table_entry =
      page_directory ? page_directory->page_table_entry(page_directory_index, page_table_index)
                     : nullptr;

  for (uint64_t i = 0; i < page_count + kOverfetchPageCount + kGuardPageCount; i++) {
    gen_pte_t pte;
    if (i < page_count) {
      // buffer pages
      pte = gen_pte_encode(bus_addr_array[i], CACHING_LLC, true, true);
    } else if (i < page_count + kOverfetchPageCount) {
      // overfetch page: readable
      pte = gen_pte_encode(pml4_table_->scratch_page_bus_addr(), CACHING_NONE, true, false);
    } else {
      // guard page: also readable, because mesa doesn't properly handle overfetching
      pte = gen_pte_encode(pml4_table_->scratch_page_bus_addr(), CACHING_NONE, true, false);
    }

    if (!page_table_entry)
      return DRETF(false, "couldn't get page table entry");

    *page_table_entry++ = pte;

    if (++page_table_index == kPageTableEntries) {
      page_table_index = 0;
      if (++page_directory_index == kPageDirectoryEntries) {
        page_directory_index = 0;
        if (++page_directory_ptr_index == kPageDirectoryPtrEntries) {
          page_directory_ptr_index = 0;
          ++pml4_index;
          DASSERT(pml4_index < kPml4Entries);
        }
        page_directory = pml4_table_->page_directory(pml4_index, page_directory_ptr_index);
      }
      page_table_entry =
          page_directory ? page_directory->page_table_entry(page_directory_index, page_table_index)
                         : nullptr;
    }
  }

  return true;
}

gen_pte_t PerProcessGtt::get_pte(gpu_addr_t gpu_addr) {
  gpu_addr_t addr_copy = gpu_addr;

  uint32_t page_table_index = (addr_copy >>= PAGE_SHIFT) & PerProcessGtt::kPageTableMask;
  uint32_t page_directory_index =
      (addr_copy >>= PerProcessGtt::kPageTableShift) & PerProcessGtt::kPageDirectoryMask;
  uint32_t page_directory_ptr_index =
      (addr_copy >>= PerProcessGtt::kPageDirectoryShift) & PerProcessGtt::kPageDirectoryPtrMask;
  uint32_t pml4_index = magma::to_uint32(addr_copy >>= PerProcessGtt::kPageDirectoryPtrShift);

  DLOG("gpu_addr 0x%lx pml4 0x%x pdp 0x%x pd 0x%x pt 0x%x", gpu_addr, pml4_index,
       page_directory_ptr_index, page_directory_index, page_table_index);

  auto page_directory = pml4_table_->page_directory_ptr(pml4_index, false)
                            ->page_directory(page_directory_ptr_index, false);
  DASSERT(page_directory);
  auto page_table_entry =
      page_directory->page_table(page_directory_index, false)->page_table_entry(page_table_index);
  DASSERT(page_table_entry);

  return *page_table_entry;
}

//////////////////////////////////////////////////////////////////////////////

// Initialize the private page attribute registers, used to define the meaning
// of the pat bits in the page table entries.
void PerProcessGtt::InitPrivatePat(magma::RegisterIo* reg_io) {
  DASSERT(gen_ppat_index(CACHING_WRITE_THROUGH) == 2);
  DASSERT(gen_ppat_index(CACHING_NONE) == 3);
  DASSERT(gen_ppat_index(CACHING_LLC) == 4);

  uint64_t pat =
      registers::PatIndex::ppat(0, registers::PatIndex::kLruAgeFromUncore,
                                registers::PatIndex::kLlc, registers::PatIndex::kWriteBack);
  pat |= registers::PatIndex::ppat(1, registers::PatIndex::kLruAgeFromUncore,
                                   registers::PatIndex::kLlcEllc,
                                   registers::PatIndex::kWriteCombining);
  pat |=
      registers::PatIndex::ppat(2, registers::PatIndex::kLruAgeFromUncore,
                                registers::PatIndex::kLlcEllc, registers::PatIndex::kWriteThrough);
  pat |= registers::PatIndex::ppat(3, registers::PatIndex::kLruAgeFromUncore,
                                   registers::PatIndex::kEllc, registers::PatIndex::kUncacheable);
  pat |= registers::PatIndex::ppat(4, registers::PatIndex::kLruAgeFromUncore,
                                   registers::PatIndex::kLlcEllc, registers::PatIndex::kWriteBack);
  pat |= registers::PatIndex::ppat(5, registers::PatIndex::kLruAgeZero,
                                   registers::PatIndex::kLlcEllc, registers::PatIndex::kWriteBack);
  pat |= registers::PatIndex::ppat(6, registers::PatIndex::kLruAgeNoChange,
                                   registers::PatIndex::kLlcEllc, registers::PatIndex::kWriteBack);
  pat |= registers::PatIndex::ppat(7, registers::PatIndex::kLruAgeThree,
                                   registers::PatIndex::kLlcEllc, registers::PatIndex::kWriteBack);

  registers::PatIndex::write(reg_io, pat);
}
