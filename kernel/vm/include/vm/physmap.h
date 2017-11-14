// Copyright 2017 The Fuchsia Authors
// Copyright (c) 2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <arch/defines.h>

// The kernel physmap is a region of the kernel where all of useful physical memory
// is mapped in one large chunk. It's up to the individual architecture to decide
// how much to map but it's usually a fairly large chunk at the base of the kernel
// address space.

#define PHYSMAP_BASE (KERNEL_ASPACE_BASE)
#define PHYSMAP_SIZE (ARCH_PHYSMAP_SIZE)
#define PHYSMAP_BASE_PHYS (0)

#ifndef __ASSEMBLER__

#include <arch.h>
#include <assert.h>
#include <inttypes.h>
#include <vm/vm.h>
#include <zircon/compiler.h>

__BEGIN_CDECLS

// check to see if an address is in the physmap virtually and physically
static inline bool is_physmap_addr(const void* addr) {
    return ((uintptr_t)addr >= PHYSMAP_BASE &&
            (uintptr_t)addr - PHYSMAP_BASE < PHYSMAP_SIZE);
}

static inline bool is_physmap_phys_addr(paddr_t pa) {
    return (
#if PHYSMAP_BASE_PHYS != 0
            pa >= PHYSMAP_BASE_PHYS &&
#endif
            pa - PHYSMAP_BASE_PHYS < PHYSMAP_SIZE);
}

// physical to virtual, returning pointer in the big kernel map
static inline void* paddr_to_physmap(paddr_t pa) {
    DEBUG_ASSERT_MSG(is_physmap_phys_addr(pa), "paddr %#" PRIxPTR "\n", pa);

    return (void *)(pa - PHYSMAP_BASE_PHYS + PHYSMAP_BASE);
}

// given a pointer into the physmap, reverse back to a physical address
static inline paddr_t physmap_to_paddr(const void* addr) {
    DEBUG_ASSERT_MSG(is_physmap_addr(addr), "vaddr %p\n", addr);

    return (uintptr_t)addr - PHYSMAP_BASE + PHYSMAP_BASE_PHYS;
}

__END_CDECLS

#endif // !__ASSEMBLER__

