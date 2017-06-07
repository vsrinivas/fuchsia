// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2014-2016 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <arch.h>
#include <magenta/compiler.h>
#include <sys/types.h>

// forward declare the per-address space arch-specific context object
typedef struct arch_aspace arch_aspace_t;

// put all of these inside an internal namespace to discourage older code from calling directly
namespace arch_internal {

// initialize per address space
status_t arch_mmu_init_aspace(arch_aspace_t* aspace, vaddr_t base, size_t size, uint mmu_flags);
status_t arch_mmu_destroy_aspace(arch_aspace_t* aspace);

// routines to map/unmap/update permissions/query mappings per address space
status_t arch_mmu_map(arch_aspace_t* aspace, vaddr_t vaddr, paddr_t paddr, size_t count, uint mmu_flags, size_t* mapped);
status_t arch_mmu_unmap(arch_aspace_t* aspace, vaddr_t vaddr, size_t count, size_t* unmapped);
status_t arch_mmu_protect(arch_aspace_t* aspace, vaddr_t vaddr, size_t count, uint mmu_flags);
status_t arch_mmu_query(arch_aspace_t* aspace, vaddr_t vaddr, paddr_t* paddr, uint* mmu_flags);

vaddr_t arch_mmu_pick_spot(const arch_aspace_t* aspace,
                           vaddr_t base, uint prev_region_mmu_flags,
                           vaddr_t end, uint next_region_mmu_flags,
                           vaddr_t align, size_t size, uint mmu_flags);
/* load a new user address space context.
 * aspace argument NULL should unload user space.
 */
void arch_mmu_context_switch(arch_aspace_t* old_aspace, arch_aspace_t* aspace);

} // namespace arch_internal
