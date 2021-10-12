// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <align.h>
#include <bits.h>
#include <lib/arch/intrin.h>
#include <lib/counters.h>
#include <lib/instrumentation/asan.h>
#include <stdlib.h>
#include <string.h>
#include <trace.h>
#include <zircon/assert.h>
#include <zircon/types.h>

#include <fbl/ref_ptr.h>
#include <kernel/mutex.h>
#include <kernel/range_check.h>
#include <ktl/move.h>
#include <vm/physmap.h>
#include <vm/pmm.h>
#include <vm/vm.h>
#include <vm/vm_address_region.h>
#include <vm/vm_aspace.h>
#include <vm/vm_object_paged.h>

#include "asan-internal.h"

#define LOCAL_TRACE 0

KCOUNTER(asan_allocated_shadow_pages, "asan.allocated_shadow_pages")
KCOUNTER(asan_repeated_shadow_pages, "asan.repeated_shadow_pages")

namespace {

fbl::RefPtr<VmAddressRegion> g_kasan_shadow_vmar;

DECLARE_SINGLETON_MUTEX(kasan_lock);

inline bool shadow_address_is_mapped(vaddr_t va) {
  InterruptDisableGuard irqd;
  asm volatile("at s1e1r, %0" ::"r"(va) : "memory");
  uint64_t par = __arm_rsr64("par_el1");

  return !BIT(par, 0);
}

}  //  namespace

void asan_map_shadow_for(uintptr_t start, size_t size) {
  // Only map shadow for kernel mappings, skipping the ones that are inside the asan shadow.
  if (start < KERNEL_ASPACE_BASE ||
      InRange(start, size, KASAN_SHADOW_OFFSET, KASAN_SHADOW_OFFSET + kAsanShadowSize)) {
    return;
  }

  Guard<Mutex> remap_guard{kasan_lock::Get()};

  uintptr_t shadow_start_aligned =
      ROUNDDOWN(reinterpret_cast<uintptr_t>(addr2shadow(start)), PAGE_SIZE);
  uintptr_t shadow_end_aligned =
      ROUNDUP(reinterpret_cast<uintptr_t>(addr2shadow(start + size - 1)), PAGE_SIZE);
  size_t pages = (shadow_end_aligned - shadow_start_aligned) / PAGE_SIZE;
  ZX_ASSERT(pages != 0);
  ZX_ASSERT(shadow_start_aligned < shadow_end_aligned);

  start = shadow_start_aligned;
  for (size_t i = 0; i < pages; i++) {
    vaddr_t vaddr = start + i * PAGE_SIZE;
    if (shadow_address_is_mapped(vaddr)) {
      // Map everything from start to start + i - 1;
      kcounter_add(asan_repeated_shadow_pages, 1);
      continue;
    }
    // MAP (start + i)
    paddr_t paddr;
    zx_status_t status;
    vm_page_t* page = nullptr;
    status = pmm_alloc_page(0, &page, &paddr);
    ZX_ASSERT_MSG(status == ZX_OK, "could not allocate page (%d)", status);
    page->set_state(vm_page_state::WIRED);
    size_t mapped = 0;
    status = g_kasan_shadow_vmar->aspace()->arch_aspace().Map(
        vaddr, &paddr, 1, ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE,
        ArchVmAspaceInterface::ExistingEntryAction::Error, &mapped);
    ZX_ASSERT_MSG(status == ZX_OK, "could not map page 0x%016lx (%d)\n", vaddr, status);
    ZX_ASSERT(mapped == 1);
    arch_zero_page(reinterpret_cast<void*>(vaddr));
    kcounter_add(asan_allocated_shadow_pages, 1);
  }
}

void arch_asan_early_init() {
  // TODO(fxbug.dev/30033): We are constructing the kasan shadow 'late' here; this is not viable
  // as a long-term solution, but will help us build out kasan support. The shadow needs to cover
  // the entire physmap.
  uintptr_t shadow_begin = reinterpret_cast<uintptr_t>(addr2shadow(KERNEL_ASPACE_BASE));
  uintptr_t shadow_end =
      reinterpret_cast<uintptr_t>(addr2shadow(KERNEL_ASPACE_BASE + KERNEL_ASPACE_SIZE - 1));

  const char* kAsanShadowName = "kasan-shadow";

  // Map a huge vmo covering all the asan shadow.
  // Shadow regions will be committed by calling MapRange into this vmo from kasan_map_shadow_for.
  fbl::RefPtr<VmAddressRegion> kasan_vmar;
  auto status = VmAspace::kernel_aspace()->RootVmar()->as_vm_address_region()->CreateSubVmar(
      shadow_begin - VmAspace::kernel_aspace()->RootVmar()->base(), shadow_end - shadow_begin + 1,
      0,
      VMAR_FLAG_CAN_MAP_SPECIFIC | VMAR_FLAG_CAN_MAP_READ | VMAR_FLAG_CAN_MAP_WRITE |
          VMAR_FLAG_SPECIFIC,
      kAsanShadowName, &kasan_vmar);
  ZX_ASSERT(status == ZX_OK);
  ZX_ASSERT(kasan_vmar);

  g_kasan_shadow_vmar = kasan_vmar;

  // Enable shadow for the physmap.
  for (size_t i = 0; i < pmm_num_arenas(); i++) {
    pmm_arena_info_t arena = {};
    pmm_get_arena_info(1, i, &arena, sizeof(arena));
    uintptr_t address = reinterpret_cast<uintptr_t>(paddr_to_physmap(arena.base));
    asan_map_shadow_for(address, arena.size);
  }
}

void arch_asan_late_init() { g_asan_initialized = true; }
