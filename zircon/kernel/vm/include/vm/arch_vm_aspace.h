// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_VM_INCLUDE_VM_ARCH_VM_ASPACE_H_
#define ZIRCON_KERNEL_VM_INCLUDE_VM_ARCH_VM_ASPACE_H_

#include <sys/types.h>
#include <zircon/types.h>

#include <fbl/function.h>
#include <fbl/macros.h>
#include <vm/page.h>

// Flags
const uint ARCH_MMU_FLAG_CACHED = (0u << 0);
const uint ARCH_MMU_FLAG_UNCACHED = (1u << 0);
const uint ARCH_MMU_FLAG_UNCACHED_DEVICE =
    (2u << 0);  // Only exists on some arches, otherwise UNCACHED
const uint ARCH_MMU_FLAG_WRITE_COMBINING =
    (3u << 0);  // Only exists on some arches, otherwise UNCACHED
const uint ARCH_MMU_FLAG_CACHE_MASK = (3u << 0);
const uint ARCH_MMU_FLAG_PERM_USER = (1u << 2);
const uint ARCH_MMU_FLAG_PERM_READ = (1u << 3);
const uint ARCH_MMU_FLAG_PERM_WRITE = (1u << 4);
const uint ARCH_MMU_FLAG_PERM_EXECUTE = (1u << 5);
const uint ARCH_MMU_FLAG_PERM_RWX_MASK =
    (ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE | ARCH_MMU_FLAG_PERM_EXECUTE);
const uint ARCH_MMU_FLAG_NS = (1u << 6);       // NON-SECURE
const uint ARCH_MMU_FLAG_INVALID = (1u << 7);  // Indicates that flags are not specified

const uint ARCH_ASPACE_FLAG_KERNEL = (1u << 0);
const uint ARCH_ASPACE_FLAG_GUEST = (1u << 1);

// per arch base class api to encapsulate the mmu routines on an aspace
class ArchVmAspaceInterface {
 public:
  ArchVmAspaceInterface() = default;
  virtual ~ArchVmAspaceInterface() = default;

  // Function pointer to allocate a single page that the mmu routine uses to allocate
  // page tables.
  using page_alloc_fn_t = zx_status_t (*)(uint alloc_flags, vm_page** p, paddr_t* pa);

  virtual zx_status_t Init() = 0;

  // ::Destroy expects the aspace to be fully unmapped, as any mapped regions
  // indicate incomplete cleanup at the higher layers.
  virtual zx_status_t Destroy() = 0;

  // main methods

  // Map a physically contiguous region into the virtual address space
  virtual zx_status_t MapContiguous(vaddr_t vaddr, paddr_t paddr, size_t count, uint mmu_flags,
                                    size_t* mapped) = 0;
  // Map the given array of pages into the virtual address space starting at
  // |vaddr|, in the order they appear in |phys|.
  // If any address in the range [vaddr, vaddr + count * PAGE_SIZE) is already
  // mapped when this is called, this returns ZX_ERR_ALREADY_EXISTS.
  virtual zx_status_t Map(vaddr_t vaddr, paddr_t* phys, size_t count, uint mmu_flags,
                          size_t* mapped) = 0;

  // Unmap the given virtual address range
  virtual zx_status_t Unmap(vaddr_t vaddr, size_t count, size_t* unmapped) = 0;

  // Change the page protections on the given virtual address range
  //
  // If this requires splitting a large page and the next level page table
  // allocation fails, it will instead unmap the entire large page and rely
  // on subsequent page faults to reestablish the mapping.
  //
  // TODO: Handle allocation failure more gracefully.
  virtual zx_status_t Protect(vaddr_t vaddr, size_t count, uint mmu_flags) = 0;

  virtual zx_status_t Query(vaddr_t vaddr, paddr_t* paddr, uint* mmu_flags) = 0;

  virtual vaddr_t PickSpot(vaddr_t base, uint prev_region_mmu_flags, vaddr_t end,
                           uint next_region_mmu_flags, vaddr_t align, size_t size,
                           uint mmu_flags) = 0;

  // Walks the given range of pages and for any pages that are mapped and have their access bit set
  //  * Calls the provided callback with the page information
  //  * If callback returns true, clears the accessed bit
  // The callback may be invoked whilst the aspace is holding arbitrary mutexes and spinlocks and
  // the callback there must not
  //  * Acquire additional mutexes
  //  * Call any aspace functions
  // TODO: It is possible that HarvestAccessed only gets called from a single code path, at which
  // point we could consider removing the dynamic callback function pointer with a static global
  // callback routine.
  using HarvestCallback = fbl::Function<bool(paddr_t paddr, vaddr_t vaddr, uint mmu_flags)>;
  virtual zx_status_t HarvestAccessed(vaddr_t vaddr, size_t count,
                                      const HarvestCallback& accessed_callback) = 0;

  // Marks any pages in the given virtual address range as being accessed.
  virtual zx_status_t MarkAccessed(vaddr_t vaddr, size_t count) = 0;

  // Walks the page tables backing the given range and frees any page tables based on the accessed
  // flags. The accessed flags examined depends on |HasNonTerminalAccessedFlag| and when true this
  // reclaims any page table entries that have not been accessed, and clears any existing access
  // bits. If false this reclaims page tables where all entries do not have accessed flags, but
  // does not clear any accessed bits. To clear accessed flags on terminal entries |HarvestAccessed|
  // must be used.
  virtual zx_status_t FreeUnaccessed(vaddr_t vaddr, size_t count) = 0;

  // Physical address of the backing data structure used for translation.
  //
  // This should be treated as an opaque value outside of
  // architecture-specific components.
  virtual paddr_t arch_table_phys() const = 0;
};

#endif  // ZIRCON_KERNEL_VM_INCLUDE_VM_ARCH_VM_ASPACE_H_
