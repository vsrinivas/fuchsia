// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/machina/arch/x86/page_table.h"

#include <limits.h>

static const size_t kMaxSize = 512ull << 30;
static const size_t kMinSize = 4 * (4 << 10);

enum {
  X86_PTE_P = 0x01,   // P    Valid
  X86_PTE_RW = 0x02,  // R/W  Read/Write
  X86_PTE_PS = 0x80,  // PS   Page size
};

static const size_t kPml4PageSize = 512ull << 30;
static const size_t kPdpPageSize = 1 << 30;
static const size_t kPdPageSize = 2 << 20;
static const size_t kPtPageSize = 4 << 10;
static const size_t kPtesPerPage = PAGE_SIZE / sizeof(uint64_t);

// Create all page tables for a given page size.
//
// @param addr         The mapped address of where to write the page table.
//                     Must be page-aligned.
// @param size         The size of memory to map.
// @param l1_page_size The size of pages at this level.
// @param l1_pte_off   The offset of this page table, relative to the start of
//                     memory.
// @param aspace_off   The address space offset, used to keep track of mapped
//                     address space.
// @param has_page     Whether this level of the page table has associated
//                     pages.
// @param map_flags    Flags added to any descriptors directly mapping pages.
static uintptr_t create_page_table_level(uintptr_t addr,
                                         size_t size,
                                         size_t l1_page_size,
                                         uintptr_t l1_pte_off,
                                         uint64_t* aspace_off,
                                         bool has_page,
                                         uint64_t map_flags) {
  size_t l1_ptes = (size + l1_page_size - 1) / l1_page_size;
  bool has_l0_aspace = size % l1_page_size != 0;
  size_t l1_pages = (l1_ptes + kPtesPerPage - 1) / kPtesPerPage;
  uintptr_t l0_pte_off = l1_pte_off + l1_pages * PAGE_SIZE;

  uint64_t* pt = (uint64_t*)(addr + l1_pte_off);
  for (size_t i = 0; i < l1_ptes; i++) {
    if (has_page && (!has_l0_aspace || i < l1_ptes - 1)) {
      pt[i] = *aspace_off | X86_PTE_P | X86_PTE_RW | map_flags;
      *aspace_off += l1_page_size;
    } else {
      if (i > 0 && (i % kPtesPerPage == 0))
        l0_pte_off += PAGE_SIZE;
      pt[i] = l0_pte_off | X86_PTE_P | X86_PTE_RW;
    }
  }

  return l0_pte_off;
}

namespace machina {

zx_status_t create_page_table(uintptr_t addr, size_t size, uintptr_t* end_off) {
  if (size % PAGE_SIZE != 0) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (size > kMaxSize || size < kMinSize) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  uint64_t aspace_off = 0;
  *end_off = 0;
  *end_off = create_page_table_level(addr, size - aspace_off, kPml4PageSize,
                                     *end_off, &aspace_off, false, 0);
  *end_off = create_page_table_level(addr, size - aspace_off, kPdpPageSize,
                                     *end_off, &aspace_off, true, X86_PTE_PS);
  *end_off = create_page_table_level(addr, size - aspace_off, kPdPageSize,
                                     *end_off, &aspace_off, true, X86_PTE_PS);
  *end_off = create_page_table_level(addr, size - aspace_off, kPtPageSize,
                                     *end_off, &aspace_off, true, 0);
  return ZX_OK;
}

}  // namespace machina
