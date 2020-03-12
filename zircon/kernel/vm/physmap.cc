// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "vm/physmap.h"

#include <trace.h>

#include <fbl/alloc_checker.h>
#include <fbl/function.h>
#include <ktl/unique_ptr.h>
#include <vm/pmm.h>

#include "vm_priv.h"

#define LOCAL_TRACE VM_GLOBAL_TRACE(0)

namespace {

// Protect the region [ |base|, |base| + |size| ) from the physmap.
void physmap_protect_region(vaddr_t base, size_t size) {
  DEBUG_ASSERT(base % PAGE_SIZE == 0);
  DEBUG_ASSERT(size % PAGE_SIZE == 0);
  const size_t page_count = size / PAGE_SIZE;
  LTRACEF("base=0x%" PRIx64 "; page_count=0x%" PRIx64 "\n", base, page_count);

  // Ideally, we'd drop the PERM_READ, but MMU code doesn't support that.
  constexpr uint mmu_flags = ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_UNCACHED_DEVICE;
  zx_status_t status =
      VmAspace::kernel_aspace()->arch_aspace().Protect(base, page_count, mmu_flags);
  DEBUG_ASSERT(status == ZX_OK);
}

}  // namespace

void physmap_for_each_gap(fbl::Function<void(vaddr_t base, size_t size)> func,
                          pmm_arena_info_t* arenas, size_t num_arenas) {
  // Iterate over the arenas and invoke |func| for the gaps between them.
  //
  // |gap_base| is the base address of the last identified gap.
  vaddr_t gap_base = PHYSMAP_BASE;
  for (unsigned i = 0; i < num_arenas; ++i) {
    const vaddr_t arena_base = reinterpret_cast<vaddr_t>(paddr_to_physmap(arenas[i].base));
    DEBUG_ASSERT(arena_base >= gap_base && arena_base % PAGE_SIZE == 0);

    const size_t arena_size = arenas[i].size;
    DEBUG_ASSERT(arena_size > 0 && arena_size % PAGE_SIZE == 0);

    LTRACEF("gap_base=%" PRIx64 "; arena_base=%" PRIx64 "; arena_size=%" PRIx64 "\n", gap_base,
            arena_base, arena_size);

    const size_t gap_size = arena_base - gap_base;
    if (gap_size > 0) {
      func(gap_base, gap_size);
    }

    gap_base = arena_base + arena_size;
  }

  // Don't forget the last gap.
  const vaddr_t physmap_end = PHYSMAP_BASE + PHYSMAP_SIZE;
  const size_t gap_size = physmap_end - gap_base;
  if (gap_size > 0) {
    func(gap_base, gap_size);
  }
}

void physmap_protect_non_arena_regions() {
  // Create a buffer to hold the pmm_arena_info_t objects.
  const size_t num_arenas = pmm_num_arenas();
  fbl::AllocChecker ac;
  auto arenas = ktl::unique_ptr<pmm_arena_info_t[]>(new (&ac) pmm_arena_info_t[num_arenas]);
  ASSERT(ac.check());
  const size_t size = num_arenas * sizeof(pmm_arena_info_t);

  // Fetch them.
  zx_status_t status = pmm_get_arena_info(num_arenas, 0, arenas.get(), size);
  ASSERT(status == ZX_OK);

  physmap_for_each_gap(physmap_protect_region, arenas.get(), num_arenas);
}
