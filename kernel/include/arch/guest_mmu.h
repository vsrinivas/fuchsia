// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <arch/guest_paspace.h>

#define ARCH_MMU_FLAG_GUEST_PASPACE     (1u << 8) /* guest physical address space */

/* initialize per address space */
status_t guest_mmu_init_paspace(guest_paspace_t* paspace, size_t size) __NONNULL((1));
status_t guest_mmu_destroy_paspace(guest_paspace_t* paspace) __NONNULL((1));

/* routines to map/unmap/update permissions/query mappings per address space */
status_t guest_mmu_map(guest_paspace_t* aspace, vaddr_t vaddr, paddr_t paddr, size_t count, uint mmu_flags, size_t* mapped) __NONNULL((1));
status_t guest_mmu_unmap(guest_paspace_t* aspace, vaddr_t vaddr, size_t count, size_t* unmapped) __NONNULL((1));
status_t guest_mmu_protect(guest_paspace_t* aspace, vaddr_t vaddr, size_t count, uint mmu_flags) __NONNULL((1));
status_t guest_mmu_query(guest_paspace_t* aspace, vaddr_t vaddr, paddr_t* paddr, uint* mmu_flags) __NONNULL((1));

vaddr_t guest_mmu_pick_spot(const guest_paspace_t* aspace,
                            vaddr_t base, uint prev_region_mmu_flags,
                            vaddr_t end, uint next_region_mmu_flags,
                            vaddr_t align, size_t size, uint mmu_flags) __NONNULL((1));
