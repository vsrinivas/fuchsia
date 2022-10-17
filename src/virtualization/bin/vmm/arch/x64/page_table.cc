// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/vmm/arch/x64/page_table.h"

#include <page_tables/x86/constants.h>

namespace {

constexpr size_t kMaxSize = 1ul << PML4_SHIFT;
constexpr size_t kMinSize = 4 * (1ul << PT_SHIFT);
constexpr size_t kPtesPerPage = PAGE_SIZE / sizeof(uint64_t);

using Page = cpp20::span<const uint8_t>;

// Create all page tables for a given page size.
//
// @param phys_mem     The guest physical memory to write the page table to.
// @param l1_page_size The size of pages at this level.
// @param l1_pte_off   The offset of this page table, relative to the start of
//                     memory.
// @param aspace_off   The address space offset, used to keep track of mapped
//                     address space.
// @param has_page     Whether this level of the page table has associated
//                     pages.
// @param map_flags    Flags added to any descriptors directly mapping pages.
uintptr_t CreatePageTableLevel(const PhysMem& phys_mem, size_t l1_page_size, uintptr_t l1_pte_off,
                               uint64_t* aspace_off, bool has_page, uint64_t map_flags) {
  const size_t size = phys_mem.size() - *aspace_off;
  const size_t l1_ptes = (size + l1_page_size - 1) / l1_page_size;
  const bool has_l0_aspace = size % l1_page_size != 0;
  const size_t l1_pages = (l1_ptes + kPtesPerPage - 1) / kPtesPerPage;

  uintptr_t l0_pte_off = l1_pte_off + l1_pages * PAGE_SIZE;
  for (size_t i = 0; i < l1_ptes; i++) {
    uint64_t pte;
    if (has_page && (!has_l0_aspace || i < l1_ptes - 1)) {
      pte = *aspace_off | X86_MMU_PG_P | X86_MMU_PG_RW | map_flags;
      *aspace_off += l1_page_size;
    } else {
      if (i > 0 && (i % kPtesPerPage == 0)) {
        l0_pte_off += PAGE_SIZE;
      }
      pte = l0_pte_off | X86_MMU_PG_P | X86_MMU_PG_RW;
    }
    phys_mem.write<uint64_t>(l1_pte_off + i * sizeof(uint64_t), pte);
  }

  return l0_pte_off;
}

}  // namespace

zx::result<> CreatePageTable(const PhysMem& phys_mem) {
  if (phys_mem.size() % PAGE_SIZE != 0) {
    return zx::error(ZX_ERR_INVALID_ARGS);
  }
  if (phys_mem.size() > kMaxSize || phys_mem.size() < kMinSize) {
    return zx::error(ZX_ERR_OUT_OF_RANGE);
  }

  uint64_t aspace_off = 0;
  uintptr_t next_off = 0;
  next_off = CreatePageTableLevel(phys_mem, 1ul << PML4_SHIFT, next_off, &aspace_off, false, 0);
  next_off =
      CreatePageTableLevel(phys_mem, 1ul << PDP_SHIFT, next_off, &aspace_off, true, X86_MMU_PG_PS);
  next_off =
      CreatePageTableLevel(phys_mem, 1ul << PD_SHIFT, next_off, &aspace_off, true, X86_MMU_PG_PS);
  next_off = CreatePageTableLevel(phys_mem, 1ul << PT_SHIFT, next_off, &aspace_off, true, 0);
  return zx::ok();
}

// Returns the page address for a given page table entry.
//
// If the page address is for a large page, we additionally calculate the offset
// to the correct guest physical page that backs the large page.
zx_gpaddr_t PageAddress(zx_gpaddr_t pt_addr, size_t level, zx_vaddr_t guest_vaddr) {
  zx_gpaddr_t off = 0;
  if (IS_LARGE_PAGE(pt_addr)) {
    if (level == 1) {
      off = guest_vaddr & PAGE_OFFSET_MASK_HUGE;
    } else if (level == 2) {
      off = guest_vaddr & PAGE_OFFSET_MASK_LARGE;
    }
  }
  return (pt_addr & X86_PG_FRAME) + (off & X86_PG_FRAME);
}

// Returns the page for a given guest virtual address.
zx::result<Page> FindPage(const PhysMem& phys_mem, zx_gpaddr_t pt_addr, zx_vaddr_t guest_vaddr) {
  const size_t num_entries = PAGE_SIZE / sizeof(zx_gpaddr_t);
  const size_t indices[X86_PAGING_LEVELS] = {
      VADDR_TO_PML4_INDEX(guest_vaddr),
      VADDR_TO_PDP_INDEX(guest_vaddr),
      VADDR_TO_PD_INDEX(guest_vaddr),
      VADDR_TO_PT_INDEX(guest_vaddr),
  };
  cpp20::span<zx_gpaddr_t> pt;
  for (size_t level = 0; level <= X86_PAGING_LEVELS; level++) {
    pt = phys_mem.span<zx_gpaddr_t>(PageAddress(pt_addr, level - 1, guest_vaddr), num_entries);
    if (level == X86_PAGING_LEVELS || IS_LARGE_PAGE(pt_addr)) {
      break;
    }
    pt_addr = pt[indices[level]];
    if (!IS_PAGE_PRESENT(pt_addr)) {
      return zx::error(ZX_ERR_NOT_FOUND);
    }
  }
  return zx::ok(Page(reinterpret_cast<uint8_t*>(pt.data()), pt.size_bytes()));
}

zx::result<> ReadInstruction(const PhysMem& phys_mem, zx_gpaddr_t cr3_addr, zx_vaddr_t rip_addr,
                             InstructionSpan span) {
  auto page = FindPage(phys_mem, cr3_addr, rip_addr);
  if (page.is_error()) {
    return page.take_error();
  }

  size_t page_offset = rip_addr & PAGE_OFFSET_MASK_4KB;
  size_t limit = std::min(span.size(), PAGE_SIZE - page_offset);
  auto begin = page->begin() + page_offset;
  std::copy_n(begin, limit, span.begin());

  // If the read is not split across pages, return.
  if (limit == span.size()) {
    return zx::ok();
  }

  page = FindPage(phys_mem, cr3_addr, rip_addr + limit);
  if (page.is_error()) {
    return page.take_error();
  }

  begin = page->begin();
  std::copy_n(begin, span.size() - limit, span.begin() + limit);
  return zx::ok();
}
