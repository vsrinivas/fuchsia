// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2014-2016 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <arch.h>
#include <sys/types.h>
#include <magenta/compiler.h>

/* to bring in definition of arch_aspace */
#include <arch/aspace.h>

__BEGIN_CDECLS

#define ARCH_MMU_FLAG_CACHED            (0<<0)
#define ARCH_MMU_FLAG_UNCACHED          (1<<0)
#define ARCH_MMU_FLAG_UNCACHED_DEVICE   (2<<0) /* only exists on some arches, otherwise UNCACHED */
#define ARCH_MMU_FLAG_WRITE_COMBINING   (3<<0) /* only exists on some arches, otherwise UNCACHED */
#define ARCH_MMU_FLAG_CACHE_MASK        (3<<0)

#define ARCH_MMU_FLAG_PERM_USER         (1<<2)
#define ARCH_MMU_FLAG_PERM_READ         (1<<3)
#define ARCH_MMU_FLAG_PERM_WRITE        (1<<4)
#define ARCH_MMU_FLAG_PERM_EXECUTE      (1<<5)
#define ARCH_MMU_FLAG_NS                (1<<6) /* NON-SECURE */
#define ARCH_MMU_FLAG_INVALID           (1<<7) /* indicates that flags are not specified */

/* forward declare the per-address space arch-specific context object */
typedef struct arch_aspace arch_aspace_t;

#define ARCH_ASPACE_FLAG_KERNEL         (1<<0)

/* initialize per address space */
status_t arch_mmu_init_aspace(arch_aspace_t *aspace, vaddr_t base, size_t size, uint flags) __NONNULL((1));
status_t arch_mmu_destroy_aspace(arch_aspace_t *aspace) __NONNULL((1));

/* routines to map/unmap/update permissions/query mappings per address space */
int arch_mmu_map(arch_aspace_t *aspace, vaddr_t vaddr, paddr_t paddr, size_t count, uint flags) __NONNULL((1));
int arch_mmu_unmap(arch_aspace_t *aspace, vaddr_t vaddr, size_t count) __NONNULL((1));
int arch_mmu_protect(arch_aspace_t *aspace, vaddr_t vaddr, size_t count, uint flags) __NONNULL((1));
status_t arch_mmu_query(arch_aspace_t *aspace, vaddr_t vaddr, paddr_t *paddr, uint *flags) __NONNULL((1));

vaddr_t arch_mmu_pick_spot(const arch_aspace_t *aspace,
                           vaddr_t base, uint prev_region_arch_mmu_flags,
                           vaddr_t end,  uint next_region_arch_mmu_flags,
                           vaddr_t align, size_t size, uint arch_mmu_flags) __NONNULL((1));

/* load a new user address space context.
 * aspace argument NULL should unload user space.
 */
void arch_mmu_context_switch(arch_aspace_t *old_aspace, arch_aspace_t *aspace);

void arch_disable_mmu(void);

__END_CDECLS

