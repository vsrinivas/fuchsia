// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/counters.h>
#include <lib/instrumentation/asan.h>
#include <stdlib.h>
#include <string.h>
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

extern volatile pt_entry_t kasan_shadow_pt[];
extern volatile pt_entry_t kasan_shadow_pd[];

namespace {

paddr_t get_or_allocate_page_table(volatile pt_entry_t* table, size_t i,
                                   volatile pt_entry_t* initial_value) {
  paddr_t pd_page_paddr;
  vm_page_t* pd_page;
  pt_entry_t pdp_entry = table[i];

  // The table is either empty or has X86_KERNEL_KASAN_PD_INITIAL_FLAGS
  // In either case, it doesn't have Write permission.
  if (pdp_entry & X86_MMU_PG_RW) {
    return pdp_entry & ~X86_FLAGS_MASK;
  }

  zx_status_t status = pmm_alloc_page(0, &pd_page, &pd_page_paddr);
  DEBUG_ASSERT(status == ZX_OK);
  kcounter_add(asan_allocated_shadow_page_tables, 1);
  __unsanitized_memcpy(paddr_to_physmap(pd_page_paddr), const_cast<pt_entry_t*>(initial_value),
                       PAGE_SIZE);
  return pd_page_paddr;
}

// asan_remap_shadow updates the kASAN shadow map to allow poisoning in the region [start,
// start+size)
void asan_remap_shadow_internal(volatile pt_entry_t* pdp, uintptr_t start, size_t size) {
  const vaddr_t start_shadow = reinterpret_cast<vaddr_t>(addr2shadow(start));
  const vaddr_t end_shadow = reinterpret_cast<vaddr_t>(addr2shadow(start + size - 1));
  printf("KASAN enabling shadow for region %lx size %lx, start_shadow=%lx end_shadow=%lx\n", start,
         size, start_shadow, end_shadow);
  const size_t pdp_map_start = VADDR_TO_PDP_INDEX(start_shadow);
  const size_t pdp_map_end = VADDR_TO_PDP_INDEX(end_shadow);
  const size_t pd_map_start = VADDR_TO_PD_INDEX(start_shadow);
  const size_t pd_map_end = VADDR_TO_PD_INDEX(end_shadow);
  const size_t pt_map_start = VADDR_TO_PT_INDEX(start_shadow);
  const size_t pt_map_end = VADDR_TO_PT_INDEX(end_shadow);

  // TODO(fxbug.dev/50371): When pmm_alloc_page allows getting high memory, use high pages where
  // possible for page tables and asan shadow pages.
  for (size_t i = pdp_map_start; i <= pdp_map_end; i++) {
    paddr_t new_pdp_entry = get_or_allocate_page_table(pdp, i, kasan_shadow_pd);
    pt_entry_t* pd = reinterpret_cast<pt_entry_t*>(paddr_to_physmap(new_pdp_entry));
    new_pdp_entry |= X86_KERNEL_KASAN_PD_FLAGS;

    for (size_t j = 0; j < NO_OF_PT_ENTRIES; j++) {
      // The first and last page directories might have entries that do not
      // belong to this request. Skip them.
      if ((i == pdp_map_start && j < pd_map_start) || (i == pdp_map_end && j > pd_map_end)) {
        continue;
      }

      pt_entry_t new_pd_entry = get_or_allocate_page_table(pd, j, kasan_shadow_pt);
      pt_entry_t* pt = reinterpret_cast<pt_entry_t*>(paddr_to_physmap(new_pd_entry));
      new_pd_entry |= X86_KERNEL_KASAN_PD_FLAGS;

      // Allocate and fill in leaf page tables for shadow map.
      for (size_t k = 0; k < NO_OF_PT_ENTRIES; k++) {
        // The first and last page tables might have entries that do not belong
        // to this request. Skip them.
        if ((i == pdp_map_start && j == pd_map_start && k < pt_map_start) ||
            (i == pdp_map_end && j == pd_map_end && k > pt_map_end)) {
          continue;
        }

        if (pt[k] & X86_MMU_PG_RW) {
          // already mapped.
          continue;
        }

        vm_page_t* rw_page;
        paddr_t rw_page_paddr;
        zx_status_t status = pmm_alloc_page(0, &rw_page, &rw_page_paddr);
        DEBUG_ASSERT(status == ZX_OK);
        kcounter_add(asan_allocated_shadow_pages, 1);
        arch_zero_page(paddr_to_physmap(rw_page_paddr));
        pt[k] = rw_page_paddr | X86_KERNEL_KASAN_RW_PT_FLAGS | X86_MMU_PG_NX;
      }
      pd[j] = new_pd_entry;
    }
    pdp[i] = new_pdp_entry;
  }
  // Walk the entire area we just added and invalidate TLB entries. Shootdown not required since
  // we are single-threaded at this point in boot.
  uint8_t* startp = addr2shadow(start);
  uint8_t* endp = addr2shadow(start + size);
  for (long i = 0; i < endp - startp; i += PAGE_SIZE) {
    asm volatile("invlpg (%0)" ::"r"(&(startp[i])));
  }
}

}  // namespace

void asan_remap_shadow(uintptr_t start, size_t size) {
  asan_remap_shadow_internal(pdp_high, start, size);
}

void arch_asan_reallocate_shadow() {
  for (size_t i = 0; i < pmm_num_arenas(); i++) {
    pmm_arena_info_t arena = {};
    pmm_get_arena_info(1, i, &arena, sizeof(arena));
    uintptr_t address = reinterpret_cast<uintptr_t>(paddr_to_physmap(arena.base));
    asan_remap_shadow(address, arena.size);
  }
}
