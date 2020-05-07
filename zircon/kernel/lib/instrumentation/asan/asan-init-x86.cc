// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/counters.h>
#include <lib/instrumentation/asan.h>
#include <stdlib.h>
#include <trace.h>
#include <zircon/assert.h>
#include <zircon/types.h>

#include <arch/x86/mmu.h>
#include <arch/x86/page_tables/constants.h>
#include <vm/physmap.h>
#include <vm/pmm.h>
#include <vm/vm.h>
#include <vm/vm_address_region.h>
#include <vm/vm_aspace.h>
#include <vm/vm_object_paged.h>

#include "asan-internal.h"

KCOUNTER(asan_allocated_shadow_pages, "asan.allocated_shadow_pages")
KCOUNTER(asan_allocated_shadow_page_tables, "asan.allocated_shadow_page_tables")

namespace {

// Check whether a physical address is within a valid region of the physmap.
inline bool is_gap(paddr_t paddr) {
  // Each page of the shadow map stores information for 8 pages (1 byte-per-8-byte encoding)
  // We can only avoid allocating a shadow map page if all 8 pages are in a 'gap' (invalid)
  for (int i = 0; i < (1 << kAsanShift); i++) {
    if (paddr_to_vm_page(paddr + i * PAGE_SIZE) != nullptr) {
      return false;
    }
  }
  return true;
}

}  // namespace

void arch_asan_reallocate_shadow(uintptr_t physmap_shadow_begin, uintptr_t physmap_shadow_end) {
  // At boot, asan shadow is mapped to a RO Zero Page.
  // We need to carve out space for the places that are actually going to be
  // poisoned. The heap and all pmm allocations come from the physmap.
  // We provide RW shadow pages for the entire physmap, leaving the rest as RO.
  //
  // The end result after calling this function, is that most of the kernel root
  // vmar shadow will be mapped as RO against the same Page Directories, and the
  // physmap shadow will be mapped as RW.
  //
  // TODO(30033): Handle globals and thread stacks. Currently the shadow only
  //              covers the physmap.
  const size_t pdp_asan_physmap_start = VADDR_TO_PDP_INDEX(physmap_shadow_begin);
  const size_t pdp_asan_physmap_end = VADDR_TO_PDP_INDEX(physmap_shadow_end);
  const size_t pdp_entries = pdp_asan_physmap_end - pdp_asan_physmap_start;

  paddr_t current_paddr = 0;
  // TODO(fxb/50371): When pmm_alloc_page allows getting high memory, use high pages where possible
  // for page tables and asan shadow pages.
  for (size_t i = 0; i < pdp_entries; i++) {
    vm_page_t* pd_page;
    paddr_t pd_page_paddr;

    zx_status_t status = pmm_alloc_page(0, &pd_page, &pd_page_paddr);
    DEBUG_ASSERT(status == ZX_OK);
    kcounter_add(asan_allocated_shadow_page_tables, 1);
    pt_entry_t* pd = reinterpret_cast<pt_entry_t*>(paddr_to_physmap(pd_page_paddr));
    for (size_t j = 0; j < NO_OF_PT_ENTRIES; j++) {
      // Allocate a page table for this entry.
      vm_page_t* pt_page;
      paddr_t pt_page_paddr;
      status = pmm_alloc_page(0, &pt_page, &pt_page_paddr);
      DEBUG_ASSERT(status == ZX_OK);
      kcounter_add(asan_allocated_shadow_page_tables, 1);
      pt_entry_t* pt = reinterpret_cast<pt_entry_t*>(paddr_to_physmap(pt_page_paddr));
      // Allocate and fill in leaf page tables for shadow map.
      // For shadow map pages that 'cover' addresses in a gap, we just point the shadow at the zero
      // page. Otherwise we allocate a page for the shadow.
      for (int k = 0; k < NO_OF_PT_ENTRIES; k++) {
        if (is_gap(current_paddr)) {
          pt[k] = vm_get_zero_page_paddr() | X86_KERNEL_KASAN_INITIAL_PT_FLAGS | X86_MMU_PG_NX;
        } else {
          vm_page_t* rw_page;
          paddr_t rw_page_paddr;
          status = pmm_alloc_page(0, &rw_page, &rw_page_paddr);
          DEBUG_ASSERT(status == ZX_OK);
          kcounter_add(asan_allocated_shadow_pages, 1);
          arch_zero_page(paddr_to_physmap(rw_page_paddr));
          pt[k] = rw_page_paddr | X86_KERNEL_KASAN_RW_PT_FLAGS | X86_MMU_PG_NX;
        }
        current_paddr += PAGE_SIZE << kAsanShift;  // 1 shadow page covers 8 pages
      }
      // Install leaf page table in page directory.
      pd[j] = pt_page_paddr | X86_KERNEL_KASAN_PD_FLAGS;
    }

    DEBUG_ASSERT(pdp_asan_physmap_start + i <= NO_OF_PT_ENTRIES);
    pdp_high[pdp_asan_physmap_start + i] =
        pd_page_paddr | X86_KERNEL_KASAN_PD_FLAGS | X86_MMU_PG_NX;
  }
  // Invalidate required since we are changing page frame addresses. Shootdown not required since
  // we are single-threaded at this point in boot.
  x86_set_cr3(x86_get_cr3());
}
