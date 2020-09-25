// Copyright 2017 The Fuchsia Authors
// Copyright (c) 2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_VM_INCLUDE_VM_PHYSMAP_H_
#define ZIRCON_KERNEL_VM_INCLUDE_VM_PHYSMAP_H_

#include <arch/defines.h>
#include <arch/kernel_aspace.h>

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
#include <zircon/compiler.h>

#include <fbl/function.h>
#include <vm/vm.h>

__BEGIN_CDECLS

// check to see if an address is in the physmap virtually and physically
static inline bool is_physmap_addr(const void* addr) {
  return ((uintptr_t)addr >= PHYSMAP_BASE && (uintptr_t)addr - PHYSMAP_BASE < PHYSMAP_SIZE);
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

  return (void*)(pa - PHYSMAP_BASE_PHYS + PHYSMAP_BASE);
}

// given a pointer into the physmap, reverse back to a physical address
static inline paddr_t physmap_to_paddr(const void* addr) {
  DEBUG_ASSERT_MSG(is_physmap_addr(addr), "vaddr %p\n", addr);

  return (uintptr_t)addr - PHYSMAP_BASE + PHYSMAP_BASE_PHYS;
}

__END_CDECLS

struct pmm_arena_info;

// Invokes |func| on each non-arena backed region of the physmap in ascending order of base address.
//
// No locks are held while calling |func|.
void physmap_for_each_gap(fbl::Function<void(vaddr_t base, size_t size)> func,
                          pmm_arena_info* arenas, size_t num_arenas);

// Protects all the regions of the physmap that are not backed by a PMM arena.
//
// Should not be called before the VM is up and running.
//
// Why does this function exist?
//
// The physmap is mapped early in boot and contains a contiguous mapping of a large portion of the
// physical address space, which may include device memory regions (think MMIO).  If the device
// memory remains mapped, hardware based memory prefetching might attempt to read from device
// memory.  That would be bad.  Ideally, we wouldn't map the device memory in the first place, but
// that's easier said that done (fxbug.dev/47856).
//
// The second best thing is to unmap the non-arena memory.  There are two problems with that
// approach.  One, on arm64 the physmap was mapped using 1GB pages.  However, the arm64 MMU Unmap
// code does not yet know how to deal with (i.e. split) 1GB pages (fxbug.dev/47920).  Two, Unmap attempts
// to free pages by returning them to the PMM.  However, the pages backing the phsymap's page tables
// didn't come from the PMM.  They came from the bootalloc.
//
// So that leaves us with the third best approach: change the protection bits on the non-arena
// regions to prevent caching.
//
// TODO(fxbug.dev/47856): Change the way the physmap is initially mapped.  Ideally, we would parse the
// boot data (ZBI) early on and only map the parts of the physmap that coorespond to normal memory.
// As it stands, we are still suseptible to problems arising from hardware prefetching device memory
// from the physmap.
void physmap_protect_non_arena_regions();

#endif  // !__ASSEMBLER__

#endif  // ZIRCON_KERNEL_VM_INCLUDE_VM_PHYSMAP_H_
