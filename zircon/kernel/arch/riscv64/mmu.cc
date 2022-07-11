// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <align.h>
#include <assert.h>
#include <bits.h>
#include <debug.h>
#include <inttypes.h>
#include <lib/counters.h>
#include <lib/fit/defer.h>
#include <lib/heap.h>
#include <lib/ktrace.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <trace.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <arch/aspace.h>
#include <fbl/auto_lock.h>
#include <kernel/mutex.h>
#include <ktl/algorithm.h>
#include <vm/arch_vm_aspace.h>
#include <vm/physmap.h>
#include <vm/pmm.h>
#include <vm/vm.h>
#include <arch/riscv64/sbi.h>

#include "asid_allocator.h"

#define LOCAL_TRACE 0
#define TRACE_CONTEXT_SWITCH 0

/* ktraces just local to this file */
#define LOCAL_KTRACE_ENABLE 0

#define LOCAL_KTRACE(string, args...)                                                         \
  ktrace_probe(LocalTrace<LOCAL_KTRACE_ENABLE>, TraceContext::Cpu, KTRACE_STRING_REF(string), \
               ##args)

// Static relocated base to prepare for KASLR. Used at early boot and by gdb
// script to know the target relocated address.
// TODO(fxbug.dev/24762): Choose it randomly.
#if DISABLE_KASLR
uint64_t kernel_relocated_base = KERNEL_BASE;
#else
uint64_t kernel_relocated_base = 0xffffffff10000000;
#endif

// The main translation table for the kernel. Globally declared because it's reached
// from assembly.
pte_t riscv64_kernel_translation_table[RISCV64_MMU_PT_ENTRIES] __ALIGNED(PAGE_SIZE);
// Physical address of the above table, saved in start.S.
paddr_t riscv64_kernel_translation_table_phys;

// Global accessor for the kernel page table
pte_t* riscv64_get_kernel_ptable() { return riscv64_kernel_translation_table; }

namespace {

KCOUNTER(cm_flush_all, "mmu.consistency_manager.flush_all")
KCOUNTER(cm_flush_all_replacing, "mmu.consistency_manager.flush_all_replacing")
KCOUNTER(cm_single_tlb_invalidates, "mmu.consistency_manager.single_tlb_invalidate")
KCOUNTER(cm_flush, "mmu.consistency_manager.flush")

AsidAllocator asid;

KCOUNTER(vm_mmu_protect_make_execute_calls, "vm.mmu.protect.make_execute_calls")
KCOUNTER(vm_mmu_protect_make_execute_pages, "vm.mmu.protect.make_execute_pages")

// given a va address and the level, compute the index in the current PT
inline uint vaddr_to_index(vaddr_t va, uint level) {
  // levels count down from PT_LEVELS - 1
  DEBUG_ASSERT(level < RISCV64_MMU_PT_LEVELS);

  // canonicalize the address
  va &= RISCV64_MMU_CANONICAL_MASK;

  uint index = ((va >> PAGE_SIZE_SHIFT) >> (level * RISCV64_MMU_PT_SHIFT)) & (RISCV64_MMU_PT_ENTRIES - 1);
  LTRACEF_LEVEL(3, "canonical va %#lx, level %u = index %#x\n", va, level, index);

  return index;
}

uintptr_t page_size_per_level(uint level) {
  // levels count down from PT_LEVELS - 1
  DEBUG_ASSERT(level < RISCV64_MMU_PT_LEVELS);

  return 1UL << (PAGE_SIZE_SHIFT + level * RISCV64_MMU_PT_SHIFT);
}

uintptr_t page_mask_per_level(uint level) {
  return page_size_per_level(level) - 1;
}

// Convert user level mmu flags to flags that go in L1 descriptors.
pte_t mmu_flags_to_pte_attr(uint flags, bool global) {
  pte_t attr = RISCV64_PTE_V | RISCV64_PTE_A | RISCV64_PTE_D;

  attr |= (flags & ARCH_MMU_FLAG_PERM_USER) ? RISCV64_PTE_U : 0;
  attr |= (flags & ARCH_MMU_FLAG_PERM_READ) ? RISCV64_PTE_R : 0;
  attr |= (flags & ARCH_MMU_FLAG_PERM_WRITE) ? RISCV64_PTE_W : 0;
  attr |= (flags & ARCH_MMU_FLAG_PERM_EXECUTE) ? RISCV64_PTE_X : 0;
  attr |= (global) ? RISCV64_PTE_G : 0;

  return attr;
}

bool is_pte_valid(pte_t pte) {
  return pte & RISCV64_PTE_V;
}

void update_pte(volatile pte_t* pte, pte_t newval) { *pte = newval; }

int first_used_page_table_entry(const volatile pte_t* page_table) {
  const int count = 1U << (PAGE_SIZE_SHIFT - 3);

  for (int i = 0; i < count; i++) {
    pte_t pte = page_table[i];
    if (pte & RISCV64_PTE_V) {
      return i;
    }
  }
  return -1;
}

bool page_table_is_clear(const volatile pte_t* page_table) {
  const int index = first_used_page_table_entry(page_table);
  const bool clear = index == -1;
  if (clear) {
    LTRACEF("page table at %p is clear\n", page_table);
  } else {
    LTRACEF("page_table at %p still in use, index %d is %#" PRIx64 "\n", page_table, index,
            page_table[index]);
  }
  return clear;
}

Riscv64AspaceType AspaceTypeFromFlags(uint mmu_flags) {
  // Kernel/Guest flags are mutually exclusive. Ensure at most 1 is set.
  DEBUG_ASSERT(((mmu_flags & ARCH_ASPACE_FLAG_KERNEL) != 0) +
                   ((mmu_flags & ARCH_ASPACE_FLAG_GUEST) != 0) <=
               1);
  if (mmu_flags & ARCH_ASPACE_FLAG_KERNEL) {
    return Riscv64AspaceType::kKernel;
  }
  if (mmu_flags & ARCH_ASPACE_FLAG_GUEST) {
    return Riscv64AspaceType::kGuest;
  }
  return Riscv64AspaceType::kUser;
}

ktl::string_view Riscv64AspaceTypeName(Riscv64AspaceType type) {
  switch (type) {
    case Riscv64AspaceType::kKernel:
      return "kernel";
    case Riscv64AspaceType::kUser:
      return "user";
    case Riscv64AspaceType::kGuest:
      return "guest";
    case Riscv64AspaceType::kHypervisor:
      return "hypervisor";
  }
  __UNREACHABLE;
}

}  // namespace

// A consistency manager that tracks TLB updates, walker syncs and free pages in an effort to
// minimize MBs (by delaying and coalescing TLB invalidations) and switching to full ASID
// invalidations if too many TLB invalidations are requested.
class Riscv64ArchVmAspace::ConsistencyManager {
 public:
  ConsistencyManager(Riscv64ArchVmAspace& aspace) TA_REQ(aspace.lock_) : aspace_(aspace) {}
  ~ConsistencyManager() {
    Flush();
    if (!list_is_empty(&to_free_)) {
      pmm_free(&to_free_);
    }
  }

  // Queue a TLB entry for flushing. This may get turned into a complete ASID flush.
  void FlushEntry(vaddr_t va, bool terminal) {
    AssertHeld(aspace_.lock_);
    // Check we have queued too many entries already.
    if (num_pending_tlbs_ >= kMaxPendingTlbs) {
      // Most of the time we will now prefer to invalidate the entire ASID, the exception is if
      // this aspace is using the global ASID.
      if (aspace_.asid_ != MMU_RISCV64_GLOBAL_ASID) {
        // Keep counting entries so that we can track how many TLB invalidates we saved by grouping.
        num_pending_tlbs_++;
        return;
      }
      // Flush what pages we've cached up until now and reset counter to zero.
      Flush();
    }

    // va must be page aligned so we can safely throw away the bottom bit.
    DEBUG_ASSERT(IS_PAGE_ALIGNED(va));
    DEBUG_ASSERT(aspace_.IsValidVaddr(va));

    pending_tlbs_[num_pending_tlbs_].terminal = terminal;
    pending_tlbs_[num_pending_tlbs_].va_shifted = va >> 1;
    num_pending_tlbs_++;
  }

  // Performs any pending synchronization of TLBs and page table walkers. Includes the MB to ensure
  // TLB flushes have completed prior to returning to user.
  void Flush() TA_REQ(aspace_.lock_) {
    cm_flush.Add(1);
    if (num_pending_tlbs_ == 0) {
      return;
    }
    // Need a mb to synchronize any page table updates prior to flushing the TLBs.
    mb();

    // Check if we should just be performing a full ASID invalidation.
    if (num_pending_tlbs_ > kMaxPendingTlbs) {
      cm_flush_all.Add(1);
      cm_flush_all_replacing.Add(num_pending_tlbs_);
      aspace_.FlushAsid();
    } else {
      for (size_t i = 0; i < num_pending_tlbs_; i++) {
        const vaddr_t va = pending_tlbs_[i].va_shifted << 1;
        DEBUG_ASSERT(aspace_.IsValidVaddr(va));
        aspace_.FlushTLBEntry(va, pending_tlbs_[i].terminal);
      }
      cm_single_tlb_invalidates.Add(num_pending_tlbs_);
    }

    // mb to ensure TLB flushes happen prior to returning to user.
    mb();
    num_pending_tlbs_ = 0;
  }

  // Queue a page for freeing that is dependent on TLB flushing. This is for pages that were
  // previously installed as page tables and they should not be reused until the non-terminal TLB
  // flush has occurred.
  void FreePage(vm_page_t* page) { list_add_tail(&to_free_, &page->queue_node); }

 private:
  // Maximum number of TLB entries we will queue before switching to ASID invalidation.
  static constexpr size_t kMaxPendingTlbs = 0;

  // Pending TLBs to flush are stored as 63 bits, with the bottom bit stolen to store the terminal
  // flag. 63 bits is more than enough as these entries are page aligned at the minimum.
  struct {
    bool terminal : 1;
    uint64_t va_shifted : 63;
  } pending_tlbs_[kMaxPendingTlbs];
  size_t num_pending_tlbs_ = 0;

  // vm_page_t's to release to the PMM after the TLB invalidation occurs.
  list_node to_free_ = LIST_INITIAL_VALUE(to_free_);

  // The aspace we are invalidating TLBs for.
  const Riscv64ArchVmAspace& aspace_;
};

uint Riscv64ArchVmAspace::MmuFlagsFromPte(pte_t pte) {
  uint mmu_flags = 0;
  mmu_flags |= (pte & RISCV64_PTE_U) ? ARCH_MMU_FLAG_PERM_USER : 0;
  mmu_flags |= (pte & RISCV64_PTE_R) ? ARCH_MMU_FLAG_PERM_READ : 0;
  mmu_flags |= (pte & RISCV64_PTE_W) ? ARCH_MMU_FLAG_PERM_WRITE : 0;
  mmu_flags |= (pte & RISCV64_PTE_X) ? ARCH_MMU_FLAG_PERM_EXECUTE : 0;
  return mmu_flags;
}

zx_status_t Riscv64ArchVmAspace::Query(vaddr_t vaddr, paddr_t* paddr, uint* mmu_flags) {
  Guard<Mutex> al{&lock_};
  return QueryLocked(vaddr, paddr, mmu_flags);
}

zx_status_t Riscv64ArchVmAspace::QueryLocked(vaddr_t vaddr, paddr_t* paddr, uint* mmu_flags) {
  uint level = RISCV64_MMU_PT_LEVELS - 1;

  canary_.Assert();
  LTRACEF("aspace %p, vaddr 0x%lx\n", this, vaddr);

  DEBUG_ASSERT(tt_virt_);

  DEBUG_ASSERT(IsValidVaddr(vaddr));
  if (!IsValidVaddr(vaddr)) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  const volatile pte_t* page_table = tt_virt_;

  while (true) {
    ulong index = vaddr_to_index(vaddr, level);
    const pte_t pte = page_table[index];
    const paddr_t pte_addr = RISCV64_PTE_PPN(pte);

    LTRACEF("va %#" PRIxPTR ", index %lu, level %u, pte %#" PRIx64 "\n",
            vaddr, index, level, pte);

    if (!(pte & RISCV64_PTE_V)) {
      return ZX_ERR_NOT_FOUND;
    }

    if (pte & RISCV64_PTE_PERM_MASK) {
      if (paddr) {
        *paddr = pte_addr + (vaddr & page_mask_per_level(level));
      }
      if (mmu_flags) {
        *mmu_flags = MmuFlagsFromPte(pte);
      }
      LTRACEF("va 0x%lx, paddr 0x%lx, flags 0x%x\n", vaddr, paddr ? *paddr : ~0UL,
              mmu_flags ? *mmu_flags : ~0U);
      return ZX_OK;
    }

    page_table = static_cast<const volatile pte_t*>(paddr_to_physmap(pte_addr));
    level--;
  }
}

zx_status_t Riscv64ArchVmAspace::AllocPageTable(paddr_t* paddrp) {
  // Allocate a page from the pmm via function pointer passed to us in Init().
  // The default is pmm_alloc_page so test and explicitly call it to avoid any unnecessary
  // virtual functions.
  vm_page_t* page;
  zx_status_t status;
  if (likely(!test_page_alloc_func_)) {
    status = pmm_alloc_page(0, &page, paddrp);
  } else {
    status = test_page_alloc_func_(0, &page, paddrp);
  }
  if (status != ZX_OK) {
    return status;
  }

  page->set_state(vm_page_state::MMU);
  pt_pages_++;

  LOCAL_KTRACE("page table alloc");

  LTRACEF("allocated 0x%lx\n", *paddrp);

  if (!is_physmap_phys_addr(*paddrp)) {
    while(1) __asm__("nop");
  }
  return ZX_OK;
}

void Riscv64ArchVmAspace::FreePageTable(void* vaddr, paddr_t paddr, ConsistencyManager& cm) {
  LTRACEF("vaddr %p paddr 0x%lx\n", vaddr, paddr);

  LOCAL_KTRACE("page table free");

  vm_page_t* page = paddr_to_vm_page(paddr);
  if (!page) {
    panic("bad page table paddr 0x%lx\n", paddr);
  }
  DEBUG_ASSERT(page->state() == vm_page_state::MMU);
  cm.FreePage(page);

  pt_pages_--;
}

zx_status_t Riscv64ArchVmAspace::SplitLargePage(vaddr_t vaddr, uint level,
                                              vaddr_t pt_index, volatile pte_t* page_table,
					      ConsistencyManager& cm) {
  const pte_t pte = page_table[pt_index];
  DEBUG_ASSERT(pte & RISCV64_PTE_PERM_MASK);

  paddr_t paddr;
  zx_status_t ret = AllocPageTable(&paddr);
  if (ret) {
    TRACEF("failed to allocate page table\n");
    return ret;
  }

  const auto new_page_table = static_cast<volatile pte_t*>(paddr_to_physmap(paddr));
  const auto attrs = pte & (RISCV64_PTE_PERM_MASK | RISCV64_PTE_V);

  const size_t next_size = page_size_per_level(level - 1);
  for (uint64_t i = 0, mapped_paddr = RISCV64_PTE_PPN(pte);
       i < RISCV64_MMU_PT_ENTRIES; i++, mapped_paddr += next_size) {
    // directly write to the pte, no need to update since this is
    // a completely new table
    new_page_table[i] = RISCV64_PTE_PPN_TO_PTE(mapped_paddr) | attrs;
  }

  // Ensure all zeroing becomes visible prior to page table installation.
  wmb();

  update_pte(&page_table[pt_index], RISCV64_PTE_PPN_TO_PTE(paddr) | RISCV64_PTE_V);
  LTRACEF("pte %p[%#" PRIxPTR "] = %#" PRIx64 "\n", page_table, pt_index, page_table[pt_index]);

  // no need to update the page table count here since we're replacing a block entry with a table
  // entry.

  cm.FlushEntry(vaddr, false);

  return ZX_OK;
}

// use the appropriate TLB flush instruction to globally flush the modified entry
// terminal is set when flushing at the final level of the page table.
void Riscv64ArchVmAspace::FlushTLBEntry(vaddr_t vaddr, bool terminal) const {
  unsigned long hart_mask = mask_all_but_one(riscv64_curr_hart_id());
  if (terminal) {
    __asm__ __volatile__ ("sfence.vma  %0, %1" :: "r"(vaddr), "r"(asid_) : "memory");
    sbi_remote_sfence_vma_asid(&hart_mask, 0, vaddr, PAGE_SIZE, asid_);
  } else {
    __asm("sfence.vma  zero, %0" :: "r"(asid_) : "memory");
    sbi_remote_sfence_vma_asid(&hart_mask, 0, 0, -1, asid_);
  }
}

void Riscv64ArchVmAspace::FlushAsid() const {
  __asm("sfence.vma  zero, %0" :: "r"(asid_) : "memory");
  unsigned long hart_mask = mask_all_but_one(riscv64_curr_hart_id());
  sbi_remote_sfence_vma_asid(&hart_mask, 0, 0, -1, asid_);
}

ssize_t Riscv64ArchVmAspace::UnmapPageTable(vaddr_t vaddr, vaddr_t vaddr_rel, size_t size,
                                          uint level, volatile pte_t* page_table,
					  ConsistencyManager& cm) {
  const vaddr_t block_size = page_size_per_level(level);
  const vaddr_t block_mask = block_size - 1;

  LTRACEF("vaddr 0x%lx, vaddr_rel 0x%lx, size 0x%lx, level %u, page_table %p\n",
          vaddr, vaddr_rel, size, level, page_table);

  size_t unmap_size = 0;
  while (size) {
    const vaddr_t vaddr_rem = vaddr_rel & block_mask;
    const size_t chunk_size = ktl::min(size, block_size - vaddr_rem);
    const vaddr_t index = vaddr_to_index(vaddr_rel, level);

    pte_t pte = page_table[index];

    // If the input range partially covers a large page, attempt to split.
    if (level > 0 && (pte & RISCV64_PTE_V) && (pte & RISCV64_PTE_PERM_MASK) &&
        chunk_size != block_size) {
      zx_status_t s = SplitLargePage(vaddr, level, index, page_table, cm);
      // If the split failed then we just fall through and unmap the entire large page.
      if (likely(s == ZX_OK)) {
        pte = page_table[index];
      }
    }
    if (level > 0 && (pte & RISCV64_PTE_V) && !(pte & RISCV64_PTE_PERM_MASK)) {
      const paddr_t page_table_paddr = RISCV64_PTE_PPN(pte);
      volatile pte_t* next_page_table =
	  static_cast<volatile pte_t*>(paddr_to_physmap(page_table_paddr));

      // Recurse a level.
      UnmapPageTable(vaddr, vaddr_rem, chunk_size, level - 1, next_page_table, cm);

      // if we unmapped an entire page table leaf and/or the unmap made the level below us empty,
      // free the page table
      if (chunk_size == block_size || page_table_is_clear(next_page_table)) {
        LTRACEF("pte %p[0x%lx] = 0 (was page table phys %#lx)\n", page_table, index, page_table_paddr);
        update_pte(&page_table[index], 0);

        // We can safely defer TLB flushing as the consistency manager will not return the backing
        // page to the PMM until after the tlb is flushed.
        cm.FlushEntry(vaddr, false);
        FreePageTable(const_cast<pte_t*>(next_page_table), page_table_paddr, cm);
      }
    } else if (is_pte_valid(pte)) {
      LTRACEF("pte %p[0x%lx] = 0 (was phys %#lx)\n", page_table, index, RISCV64_PTE_PPN(page_table[index]));
      update_pte(&page_table[index], 0);

      cm.FlushEntry(vaddr, true);
    } else {
      LTRACEF("pte %p[0x%lx] already clear\n", page_table, index);
    }
    vaddr += chunk_size;
    vaddr_rel += chunk_size;
    size -= chunk_size;
    unmap_size += chunk_size;
  }

  return unmap_size;
}

ssize_t Riscv64ArchVmAspace::MapPageTable(vaddr_t vaddr_in, vaddr_t vaddr_rel_in, paddr_t paddr_in,
                                        size_t size_in, pte_t attrs, uint level,
                                        volatile pte_t* page_table, ConsistencyManager& cm) {
  vaddr_t vaddr = vaddr_in;
  vaddr_t vaddr_rel = vaddr_rel_in;
  paddr_t paddr = paddr_in;
  size_t size = size_in;

  const vaddr_t block_size = page_size_per_level(level);
  const vaddr_t block_mask = block_size - 1;

  LTRACEF("vaddr %#" PRIxPTR ", vaddr_rel %#" PRIxPTR ", paddr %#" PRIxPTR
          ", size %#zx, attrs %#" PRIx64 ", level %u, page_table %p\n",
          vaddr, vaddr_rel, paddr, size, attrs, level, page_table);

  if ((vaddr_rel | paddr | size) & (PAGE_MASK)) {
    TRACEF("not page aligned\n");
    return ZX_ERR_INVALID_ARGS;
  }

  auto cleanup = fit::defer([&]() {
    AssertHeld(lock_);
    UnmapPageTable(vaddr_in, vaddr_rel_in, size_in - size, level, page_table, cm);
  });

  size_t mapped_size = 0;
  while (size) {
    const vaddr_t vaddr_rem = vaddr_rel & block_mask;
    const size_t chunk_size = ktl::min(size, block_size - vaddr_rem);
    const vaddr_t index = vaddr_to_index(vaddr_rel, level);
    pte_t pte = page_table[index];

    // if we're at an unaligned address, not trying to map a block, and not at the terminal level,
    // recurse one more level of the page table tree
    if (((vaddr_rel | paddr) & block_mask) || (chunk_size != block_size) || level > 0) {
      bool allocated_page_table = false;
      paddr_t page_table_paddr = 0;
      volatile pte_t* next_page_table = nullptr;

      if (!(pte & RISCV64_PTE_V)) {
        zx_status_t ret = AllocPageTable(&page_table_paddr);
        if (ret) {
          TRACEF("failed to allocate page table\n");
          return NULL;
        }
        allocated_page_table = true;
        void* pt_vaddr = paddr_to_physmap(page_table_paddr);

        LTRACEF("allocated page table, vaddr %p, paddr 0x%lx\n", pt_vaddr, page_table_paddr);
        arch_zero_page(pt_vaddr);

        // ensure that the zeroing is observable from hardware page table walkers, as we need to
        // do this prior to writing the pte we cannot defer it using the consistency manager.
        mb();

        pte = RISCV64_PTE_PPN_TO_PTE(page_table_paddr) | RISCV64_PTE_V;
        update_pte(&page_table[index], pte);
        // We do not need to sync the walker, despite writing a new entry, as this is a
        // non-terminal entry and so is irrelevant to the walker anyway.
        LTRACEF("pte %p[%#" PRIxPTR "] = %#" PRIx64 " (paddr %#lx)\n", page_table, index, pte, paddr);
        next_page_table = static_cast<volatile pte_t*>(pt_vaddr);
      } else if (!(pte & RISCV64_PTE_PERM_MASK)) {
        page_table_paddr = RISCV64_PTE_PPN(pte);
        LTRACEF("found page table %#" PRIxPTR "\n", page_table_paddr);
        next_page_table = static_cast<volatile pte_t*>(paddr_to_physmap(page_table_paddr));
      } else {
        return ZX_ERR_ALREADY_EXISTS;
      }
      DEBUG_ASSERT(next_page_table);

      ssize_t ret =
	  MapPageTable(vaddr, vaddr_rem, paddr, chunk_size, attrs, level - 1,
                       next_page_table, cm);
      if (ret < 0) {
        if (allocated_page_table) {
          // We just allocated this page table. The unmap in err will not clean it up as the size
          // we pass in will not cause us to look at this page table. This is reasonable as if we
          // didn't allocate the page table then we shouldn't look into and potentially unmap
          // anything from that page table.
          // Since we just allocated it there should be nothing in it, otherwise the MapPageTable
          // call would not have failed.
          DEBUG_ASSERT(page_table_is_clear(next_page_table));
          page_table[index] = 0;

          // We can safely defer TLB flushing as the consistency manager will not return the backing
          // page to the PMM until after the tlb is flushed.
	  cm.FlushEntry(vaddr, false);
          FreePageTable(const_cast<pte_t*>(next_page_table), page_table_paddr, cm);
        }
        return ret;
      }
      DEBUG_ASSERT(static_cast<size_t>(ret) == chunk_size);
    } else {
      if (is_pte_valid(pte)) {
        LTRACEF("page table entry already in use, index %#" PRIxPTR ", %#" PRIx64 "\n", index, pte);
        return ZX_ERR_ALREADY_EXISTS;
      }

      pte = RISCV64_PTE_PPN_TO_PTE(paddr) | attrs;
      LTRACEF("pte %p[%#" PRIxPTR "] = %#" PRIx64 "\n", page_table, index, pte);
      page_table[index] = pte;
    }
    vaddr += chunk_size;
    vaddr_rel += chunk_size;
    paddr += chunk_size;
    size -= chunk_size;
    mapped_size += chunk_size;
  }

  cleanup.cancel();
  return mapped_size;
}

zx_status_t Riscv64ArchVmAspace::ProtectPageTable(vaddr_t vaddr_in, vaddr_t vaddr_rel_in,
                                                size_t size_in, pte_t attrs, uint level,
                                                volatile pte_t* page_table,
						ConsistencyManager& cm) {
  vaddr_t vaddr = vaddr_in;
  vaddr_t vaddr_rel = vaddr_rel_in;
  size_t size = size_in;

  const vaddr_t block_size = page_size_per_level(level);
  const vaddr_t block_mask = block_size - 1;

  LTRACEF("vaddr %#" PRIxPTR ", vaddr_rel %#" PRIxPTR ", size %#" PRIxPTR ", attrs %#" PRIx64
          ", level %u, page_table %p\n",
          vaddr, vaddr_rel, size, attrs, level, page_table);

  // vaddr_rel and size must be page aligned
  DEBUG_ASSERT(((vaddr_rel | size) & ((1UL << PAGE_SIZE_SHIFT) - 1)) == 0);

  while (size) {
    const vaddr_t vaddr_rem = vaddr_rel & block_mask;
    const size_t chunk_size = ktl::min(size, block_size - vaddr_rem);
    const vaddr_t index = vaddr_to_index(vaddr_rel, level);
    pte_t pte = page_table[index];

    // If the input range partially covers a large page, split the page.
    if (level > 0 && (pte & RISCV64_PTE_V) && (pte & RISCV64_PTE_PERM_MASK) &&
	chunk_size != block_size) {
      zx_status_t s = SplitLargePage(vaddr, level, index, page_table, cm);
      if (unlikely(s != ZX_OK)) {
        return s;
      }
      pte = page_table[index];
    }

    if (level > 0 && (pte & RISCV64_PTE_V) && !(pte & RISCV64_PTE_PERM_MASK)) {
      const paddr_t page_table_paddr = RISCV64_PTE_PPN(pte);
      volatile pte_t* next_page_table =
	  static_cast<volatile pte_t*>(paddr_to_physmap(page_table_paddr));

      // Recurse a level
      zx_status_t status =
          ProtectPageTable(vaddr, vaddr_rem, chunk_size, attrs, level - 1, next_page_table, cm);
      if (unlikely(status != ZX_OK)) {
        return status;
      }
    } else if (is_pte_valid(pte)) {
      pte = (pte & ~RISCV64_PTE_PERM_MASK) | attrs;
      LTRACEF("pte %p[%#" PRIxPTR "] = %#" PRIx64 "\n", page_table, index, pte);
      update_pte(&page_table[index], pte);

      cm.FlushEntry(vaddr, true);
    } else {
      LTRACEF("page table entry does not exist, index %#" PRIxPTR ", %#" PRIx64 "\n", index, pte);
    }
    vaddr += chunk_size;
    vaddr_rel += chunk_size;
    size -= chunk_size;
  }

  return ZX_OK;
}

void Riscv64ArchVmAspace::HarvestAccessedPageTable(vaddr_t vaddr, vaddr_t vaddr_rel_in, size_t size,
                                                   uint level, NonTerminalAction action,
                                                   volatile pte_t* page_table, ConsistencyManager& cm,
                                                   bool* unmapped_out) {
  const vaddr_t block_size = page_size_per_level(level);
  const vaddr_t block_mask = block_size - 1;

  vaddr_t vaddr_rel = vaddr_rel_in;

  // vaddr_rel and size must be page aligned
  DEBUG_ASSERT(((vaddr_rel | size) & ((1UL << PAGE_SIZE_SHIFT) - 1)) == 0);

  while (size) {
    const vaddr_t vaddr_rem = vaddr_rel & block_mask;
    const size_t chunk_size = ktl::min(size, block_size - vaddr_rem);
    const vaddr_t index = vaddr_to_index(vaddr_rel, level);

    pte_t pte = page_table[index];

    if (level > 0 && (pte & RISCV64_PTE_V) && (pte & RISCV64_PTE_PERM_MASK) && chunk_size != block_size) {
      // Ignore large pages, we do not support harvesting accessed bits from them. Having this empty
      // if block simplifies the overall logic.
    } else if (level > 0 && (pte & RISCV64_PTE_V) && !(pte & RISCV64_PTE_PERM_MASK)) {
      const paddr_t page_table_paddr = RISCV64_PTE_PPN(pte);
      volatile pte_t* next_page_table =
          static_cast<volatile pte_t*>(paddr_to_physmap(page_table_paddr));

      // Start with the assumption that we will unmap if we can.
      UnmapPageTable(vaddr, vaddr_rem, chunk_size, level - 1, next_page_table, cm);
      DEBUG_ASSERT(page_table_is_clear(next_page_table));
      update_pte(&page_table[index], 0);

      // We can safely defer TLB flushing as the consistency manager will not return the backing
      // page to the PMM until after the tlb is flushed.
      cm.FlushEntry(vaddr, false);
      FreePageTable(const_cast<pte_t*>(next_page_table), page_table_paddr, cm);
      if (unmapped_out) {
        *unmapped_out = true;
      }
    } else if (is_pte_valid(pte) && (pte & RISCV64_PTE_A)) {
      const paddr_t pte_addr = RISCV64_PTE_PPN(pte);
      const paddr_t paddr = pte_addr + vaddr_rem;

      vm_page_t* page = paddr_to_vm_page(paddr);
      // Mappings for physical VMOs do not have pages associated with them and so there's no state
      // to update on an access.
      if (likely(page)) {
        pmm_page_queues()->MarkAccessed(page);
      }

      // Modifying the access flag does not require break-before-make for correctness and as we
      // do not support hardware access flag setting at the moment we do not have to deal with
      // potential concurrent modifications.
      pte = (pte & ~RISCV64_PTE_A);
      LTRACEF("pte %p[%#" PRIxPTR "] = %#" PRIx64 "\n", page_table, index, pte);
      update_pte(&page_table[index], pte);

      cm.FlushEntry(vaddr, true);
    }
    vaddr += chunk_size;
    vaddr_rel += chunk_size;
    size -= chunk_size;
  }
}

void Riscv64ArchVmAspace::MarkAccessedPageTable(vaddr_t vaddr, vaddr_t vaddr_rel_in, size_t size,
                                              uint level, volatile pte_t* page_table, ConsistencyManager& cm) {
  const vaddr_t block_size = page_size_per_level(level);
  const vaddr_t block_mask = block_size - 1;

  vaddr_t vaddr_rel = vaddr_rel_in;

  // vaddr_rel and size must be page aligned
  DEBUG_ASSERT(((vaddr_rel | size) & ((1UL << PAGE_SIZE_SHIFT) - 1)) == 0);

  while (size) {
    const vaddr_t vaddr_rem = vaddr_rel & block_mask;
    const size_t chunk_size = ktl::min(size, block_size - vaddr_rem);
    const vaddr_t index = vaddr_to_index(vaddr_rel, level);

    pte_t pte = page_table[index];

    if (level > 0 && (pte & RISCV64_PTE_V) && (pte & RISCV64_PTE_PERM_MASK) && chunk_size != block_size) {
      // Ignore large pages as we don't support modifying their access flags. Having this empty if
      // block simplifies the overall logic.
    } else if (level > 0 && (pte & RISCV64_PTE_V) && !(pte & RISCV64_PTE_PERM_MASK)) {
      const paddr_t page_table_paddr = RISCV64_PTE_PPN(pte);
      volatile pte_t* next_page_table =
          static_cast<volatile pte_t*>(paddr_to_physmap(page_table_paddr));
      MarkAccessedPageTable(vaddr, vaddr_rem, chunk_size, level - 1,
                            next_page_table, cm);
    } else if (pte & RISCV64_PTE_V) {
      pte |= RISCV64_PTE_A;
      page_table[index] = pte;
    }
    vaddr += chunk_size;
    vaddr_rel += chunk_size;
    size -= chunk_size;
  }
}

ssize_t Riscv64ArchVmAspace::MapPages(vaddr_t vaddr, paddr_t paddr, size_t size, pte_t attrs, ConsistencyManager& cm) {
  LOCAL_KTRACE("mmu map", (vaddr & ~PAGE_MASK) | ((size >> PAGE_SIZE_SHIFT) & PAGE_MASK));
  uint level = RISCV64_MMU_PT_LEVELS - 1;
  ssize_t ret = MapPageTable(vaddr, vaddr, paddr, size, attrs, level, tt_virt_, cm);
  mb();
  return ret;
}

ssize_t Riscv64ArchVmAspace::UnmapPages(vaddr_t vaddr, size_t size, ConsistencyManager& cm) {
  LOCAL_KTRACE("mmu unmap", (vaddr & ~PAGE_MASK) | ((size >> PAGE_SIZE_SHIFT) & PAGE_MASK));
  uint level = RISCV64_MMU_PT_LEVELS - 1;
  ssize_t ret = UnmapPageTable(vaddr, vaddr, size, level, tt_virt_, cm);
  return ret;
}

zx_status_t Riscv64ArchVmAspace::ProtectPages(vaddr_t vaddr, size_t size, pte_t attrs) {
  LOCAL_KTRACE("mmu protect", (vaddr & ~PAGE_MASK) | ((size >> PAGE_SIZE_SHIFT) & PAGE_MASK));
  uint level = RISCV64_MMU_PT_LEVELS - 1;
  ConsistencyManager cm(*this);
  zx_status_t ret =
      ProtectPageTable(vaddr, vaddr, size, attrs, level, tt_virt_, cm);
  return ret;
}

void Riscv64ArchVmAspace::MmuParamsFromFlags(uint mmu_flags, pte_t* attrs) {
  if (attrs) {
    *attrs = mmu_flags_to_pte_attr(mmu_flags, asid_ == MMU_RISCV64_GLOBAL_ASID);
  }
}

zx_status_t Riscv64ArchVmAspace::MapContiguous(vaddr_t vaddr, paddr_t paddr, size_t count,
                                               uint mmu_flags, size_t* mapped) {
  canary_.Assert();
  LTRACEF("vaddr %#" PRIxPTR " paddr %#" PRIxPTR " count %zu flags %#x\n", vaddr, paddr, count,
          mmu_flags);

  DEBUG_ASSERT(tt_virt_);

  DEBUG_ASSERT(IsValidVaddr(vaddr));
  if (!IsValidVaddr(vaddr)) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  if (!(mmu_flags & ARCH_MMU_FLAG_PERM_READ)) {
    return ZX_ERR_INVALID_ARGS;
  }

  // paddr and vaddr must be aligned.
  DEBUG_ASSERT(IS_PAGE_ALIGNED(vaddr));
  DEBUG_ASSERT(IS_PAGE_ALIGNED(paddr));
  if (!IS_PAGE_ALIGNED(vaddr) || !IS_PAGE_ALIGNED(paddr)) {
    return ZX_ERR_INVALID_ARGS;
  }

  if (count == 0) {
    return ZX_OK;
  }

  ssize_t ret;
  {
    Guard<Mutex> a{&lock_};
    if (mmu_flags & ARCH_MMU_FLAG_PERM_EXECUTE) {
      Riscv64VmICacheConsistencyManager cache_cm;
      cache_cm.SyncAddr(reinterpret_cast<vaddr_t>(paddr_to_physmap(paddr)), count * PAGE_SIZE);
    }
    pte_t attrs;
    MmuParamsFromFlags(mmu_flags, &attrs);
    ConsistencyManager cm(*this);
    ret = MapPages(vaddr, paddr, count * PAGE_SIZE, attrs, cm);
  }

  if (mapped) {
    *mapped = (ret > 0) ? (ret / PAGE_SIZE) : 0u;
    DEBUG_ASSERT(*mapped <= count);
  }

  return (ret < 0) ? (zx_status_t)ret : ZX_OK;
}

zx_status_t Riscv64ArchVmAspace::Map(vaddr_t vaddr, paddr_t* phys, size_t count, uint mmu_flags,
                                     ExistingEntryAction existing_action, size_t* mapped) {
  canary_.Assert();

  DEBUG_ASSERT(tt_virt_);

  DEBUG_ASSERT(IsValidVaddr(vaddr));
  if (!IsValidVaddr(vaddr)) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  for (size_t i = 0; i < count; ++i) {
    DEBUG_ASSERT(IS_PAGE_ALIGNED(phys[i]));
    if (!IS_PAGE_ALIGNED(phys[i])) {
      return ZX_ERR_INVALID_ARGS;
    }
  }

  if (!(mmu_flags & ARCH_MMU_FLAG_PERM_READ)) {
    return ZX_ERR_INVALID_ARGS;
  }

  // vaddr must be aligned.
  DEBUG_ASSERT(IS_PAGE_ALIGNED(vaddr));
  if (!IS_PAGE_ALIGNED(vaddr)) {
    return ZX_ERR_INVALID_ARGS;
  }

  if (count == 0) {
    return ZX_OK;
  }

  size_t total_mapped = 0;
  {
    Guard<Mutex> a{&lock_};
    if (mmu_flags & ARCH_MMU_FLAG_PERM_EXECUTE) {
      Riscv64VmICacheConsistencyManager cache_cm;
      for (size_t idx = 0; idx < count; ++idx) {
        cache_cm.SyncAddr(reinterpret_cast<vaddr_t>(paddr_to_physmap(phys[idx])), PAGE_SIZE);
      }
    }
    pte_t attrs;
    MmuParamsFromFlags(mmu_flags, &attrs);

    ssize_t ret;
    size_t idx = 0;
    ConsistencyManager cm(*this);
    auto undo = fit::defer([&]() TA_NO_THREAD_SAFETY_ANALYSIS {
      if (idx > 0) {
        UnmapPages(vaddr, idx * PAGE_SIZE, cm);
      }
    });

    vaddr_t v = vaddr;
    for (; idx < count; ++idx) {
      paddr_t paddr = phys[idx];
      DEBUG_ASSERT(IS_PAGE_ALIGNED(paddr));
      ret = MapPages(v, paddr, PAGE_SIZE, attrs, cm);
      if (ret < 0) {
        zx_status_t status = static_cast<zx_status_t>(ret);
        if (status != ZX_ERR_ALREADY_EXISTS || existing_action == ExistingEntryAction::Error) {
          return status;
        }
      }

      v += PAGE_SIZE;
      total_mapped += ret / PAGE_SIZE;
    }
    undo.cancel();
  }
  DEBUG_ASSERT(total_mapped <= count);

  if (mapped) {
    *mapped = total_mapped;
  }

  return ZX_OK;
}

zx_status_t Riscv64ArchVmAspace::Unmap(vaddr_t vaddr, size_t count, size_t* unmapped) {
  canary_.Assert();
  LTRACEF("vaddr %#" PRIxPTR " count %zu\n", vaddr, count);

  DEBUG_ASSERT(tt_virt_);

  DEBUG_ASSERT(IsValidVaddr(vaddr));

  if (!IsValidVaddr(vaddr)) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  DEBUG_ASSERT(IS_PAGE_ALIGNED(vaddr));
  if (!IS_PAGE_ALIGNED(vaddr)) {
    return ZX_ERR_INVALID_ARGS;
  }

  Guard<Mutex> a{&lock_};

  ConsistencyManager cm(*this);
  ssize_t ret = UnmapPages(vaddr, count * PAGE_SIZE, cm);

  if (unmapped) {
    *unmapped = (ret > 0) ? (ret / PAGE_SIZE) : 0u;
    DEBUG_ASSERT(*unmapped <= count);
  }

  return (ret < 0) ? (zx_status_t)ret : 0;
}

zx_status_t Riscv64ArchVmAspace::Protect(vaddr_t vaddr, size_t count, uint mmu_flags) {
  canary_.Assert();

  if (!IsValidVaddr(vaddr)) {
    return ZX_ERR_INVALID_ARGS;
  }

  if (!IS_PAGE_ALIGNED(vaddr)) {
    return ZX_ERR_INVALID_ARGS;
  }

  if (!(mmu_flags & ARCH_MMU_FLAG_PERM_READ)) {
    return ZX_ERR_INVALID_ARGS;
  }

  Guard<Mutex> a{&lock_};
  if (mmu_flags & ARCH_MMU_FLAG_PERM_EXECUTE) {
    // If mappings are going to become executable then we first need to sync their caches.
    // Unfortunately this needs to be done on kernel virtual addresses to avoid taking translation
    // faults, and so we need to first query for the physical address to then get the kernel virtual
    // address in the physmap.
    // This sync could be more deeply integrated into ProtectPages, but making existing regions
    // executable is very uncommon operation and so we keep it simple.
    vm_mmu_protect_make_execute_calls.Add(1);
    Riscv64VmICacheConsistencyManager cache_cm;
    size_t pages_synced = 0;
    for (size_t idx = 0; idx < count; idx++) {
      paddr_t paddr;
      uint flags;
      if (QueryLocked(vaddr + idx * PAGE_SIZE, &paddr, &flags) == ZX_OK &&
          (flags & ARCH_MMU_FLAG_PERM_EXECUTE)) {
        cache_cm.SyncAddr(reinterpret_cast<vaddr_t>(paddr_to_physmap(paddr)), PAGE_SIZE);
        pages_synced++;
      }
    }
    vm_mmu_protect_make_execute_pages.Add(pages_synced);
  }

  int ret;
  {
    pte_t attrs;
    MmuParamsFromFlags(mmu_flags, &attrs);

    ret = ProtectPages(vaddr, count * PAGE_SIZE, attrs);
  }

  return ret;
}

zx_status_t Riscv64ArchVmAspace::HarvestAccessed(vaddr_t vaddr, size_t count,
						 NonTerminalAction action) {
  canary_.Assert();

  if (!IS_PAGE_ALIGNED(vaddr) || !IsValidVaddr(vaddr)) {
    return ZX_ERR_INVALID_ARGS;
  }

  Guard<Mutex> guard{&lock_};

  const size_t size = count * PAGE_SIZE;
  LOCAL_KTRACE("mmu harvest accessed",
               (vaddr & ~PAGE_MASK) | ((size >> PAGE_SIZE_SHIFT) & PAGE_MASK));

  ConsistencyManager cm(*this);

  HarvestAccessedPageTable(vaddr, vaddr, size, RISCV64_MMU_PT_LEVELS - 1, action,
			   tt_virt_, cm, nullptr);
  return ZX_OK;
}

zx_status_t Riscv64ArchVmAspace::MarkAccessed(vaddr_t vaddr, size_t count) {
  canary_.Assert();

  if (!IS_PAGE_ALIGNED(vaddr) || !IsValidVaddr(vaddr)) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  Guard<Mutex> a{&lock_};

  const size_t size = count * PAGE_SIZE;
  LOCAL_KTRACE("mmu mark accessed", (vaddr & ~PAGE_MASK) | ((size >> PAGE_SIZE_SHIFT) & PAGE_MASK));

  ConsistencyManager cm(*this);

  MarkAccessedPageTable(vaddr, vaddr, size, RISCV64_MMU_PT_LEVELS - 1, tt_virt_, cm);

  return ZX_OK;
}

zx_status_t Riscv64ArchVmAspace::Init() {
  canary_.Assert();
  LTRACEF("aspace %p, base %#" PRIxPTR ", size 0x%zx, type %*s\n", this, base_, size_,
	  static_cast<int>(Riscv64AspaceTypeName(type_).size()), Riscv64AspaceTypeName(type_).data());

  Guard<Mutex> a{&lock_};

  // Validate that the base + size is sane and doesn't wrap.
  DEBUG_ASSERT(size_ > PAGE_SIZE);
  DEBUG_ASSERT(base_ + size_ - 1 > base_);

  if (type_ == Riscv64AspaceType::kKernel) {
    // At the moment we can only deal with address spaces as globally defined.
    DEBUG_ASSERT(base_ == KERNEL_ASPACE_BASE);
    DEBUG_ASSERT(size_ == KERNEL_ASPACE_SIZE);

    tt_virt_ = riscv64_get_kernel_ptable();
    tt_phys_ = riscv64_kernel_translation_table_phys;
    asid_ = (uint16_t)MMU_RISCV64_GLOBAL_ASID;
  } else {
    if (type_ == Riscv64AspaceType::kUser) {
      DEBUG_ASSERT(base_ == USER_ASPACE_BASE);
      DEBUG_ASSERT(size_ == USER_ASPACE_SIZE);
      auto status = asid.Alloc();
      if (status.is_error()) {
        printf("RISC-V: out of ASIDs!\n");
        return status.status_value();
      }
      asid_ = status.value();
    } else {
      PANIC_UNIMPLEMENTED;
    }

    // allocate a top level page table to serve as the translation table
    paddr_t pa;
    zx_status_t status = AllocPageTable(&pa);
    if (status != ZX_OK) {
      return status;
    }

    volatile pte_t* va = static_cast<volatile pte_t*>(paddr_to_physmap(pa));
    tt_virt_ = va;
    tt_phys_ = pa;

    // zero the top level translation table and copy the kernel memory mapping.
    memset((void*)tt_virt_, 0, PAGE_SIZE/2);
    memcpy((void*)(tt_virt_ + RISCV64_MMU_PT_ENTRIES/2),
	   (void*)(riscv64_get_kernel_ptable() + RISCV64_MMU_PT_ENTRIES/2),
	   PAGE_SIZE/2);
  }
  pt_pages_ = 1;

  LTRACEF("tt_phys %#" PRIxPTR " tt_virt %p\n", tt_phys_, tt_virt_);

  return ZX_OK;
}

zx_status_t Riscv64ArchVmAspace::Destroy() {
  canary_.Assert();
  LTRACEF("aspace %p\n", this);

  Guard<Mutex> a{&lock_};

  // Not okay to destroy the kernel address space
  DEBUG_ASSERT(type_ != Riscv64AspaceType::kKernel);

  // Check to see if the top level page table is empty. If not the user didn't
  // properly unmap everything before destroying the aspace
  const int index = first_used_page_table_entry(tt_virt_);
  if (index != -1 && index >= (1 << (PAGE_SIZE_SHIFT - 2))) {
    panic("top level page table still in use! aspace %p tt_virt %p index %d entry %" PRIx64 "\n", this, tt_virt_, index, tt_virt_[index]);
  }

  if (pt_pages_ != 1) {
    panic("allocated page table count is wrong, aspace %p count %zu (should be 1)\n", this,
          pt_pages_);
  }

  // Flush the ASID associated with this aspace
  FlushAsid();

  // Free any ASID.
  auto status = asid.Free(asid_);
  ASSERT(status.is_ok());
  asid_ = MMU_RISCV64_UNUSED_ASID;

  // Free the top level page table
  vm_page_t* page = paddr_to_vm_page(tt_phys_);
  DEBUG_ASSERT(page);
  pmm_free_page(page);
  pt_pages_--;

  tt_phys_ = 0;
  tt_virt_ = nullptr;

  return ZX_OK;
}

// Called during context switches between threads with different address spaces. Swaps the
// mmu context on hardware. Assumes old_aspace != aspace and optimizes as such.
void Riscv64ArchVmAspace::ContextSwitch(Riscv64ArchVmAspace* old_aspace, Riscv64ArchVmAspace* aspace) {
  uint64_t satp;
  if (likely(aspace)) {
    aspace->canary_.Assert();
    DEBUG_ASSERT(aspace->type_ == Riscv64AspaceType::kUser);

    // Load the user space SATP with the translation table and user space ASID.
    satp = ((uint64_t)RISCV64_SATP_MODE_SV39 << RISCV64_SATP_MODE_SHIFT) |
            ((uint64_t)aspace->asid_ << RISCV64_SATP_ASID_SHIFT) |
            (aspace->tt_phys_ >> PAGE_SIZE_SHIFT);
  } else {
    // Switching to the null aspace, which means kernel address space only.
    satp = ((uint64_t)RISCV64_SATP_MODE_SV39 << RISCV64_SATP_MODE_SHIFT) |
            (riscv64_kernel_translation_table_phys >> PAGE_SIZE_SHIFT);
  }
  if (TRACE_CONTEXT_SWITCH) {
    TRACEF("old aspace %p aspace %p satp %#" PRIx64 "\n", old_aspace, aspace, satp);
  }

  riscv64_csr_write(RISCV64_CSR_SATP, satp);
  mb();
}

void arch_zero_page(void* _ptr) {
  memset(_ptr, 0, PAGE_SIZE);
}

Riscv64ArchVmAspace::Riscv64ArchVmAspace(vaddr_t base, size_t size, Riscv64AspaceType type, page_alloc_fn_t paf)
    : test_page_alloc_func_(paf), type_(type), base_(base), size_(size) {}

Riscv64ArchVmAspace::Riscv64ArchVmAspace(vaddr_t base, size_t size, uint mmu_flags, page_alloc_fn_t paf)
	: Riscv64ArchVmAspace(base, size, AspaceTypeFromFlags(mmu_flags), paf) {}

Riscv64ArchVmAspace::~Riscv64ArchVmAspace() {
  // Destroy() will have freed the final page table if it ran correctly, and further validated that
  // everything else was freed.
  DEBUG_ASSERT(pt_pages_ == 0);
}

vaddr_t Riscv64ArchVmAspace::PickSpot(vaddr_t base, uint prev_region_mmu_flags, vaddr_t end,
                                      uint next_region_mmu_flags, vaddr_t align, size_t size,
                                      uint mmu_flags) {
  canary_.Assert();
  return PAGE_ALIGN(base);
}

void Riscv64VmICacheConsistencyManager::SyncAddr(vaddr_t start, size_t len) {
  // Validate we are operating on a kernel address range.
  DEBUG_ASSERT(is_kernel_address(start));
  // use the physmap to clean the range to PoU, which is the point of where the instruction cache
  // pulls from. Cleaning to PoU is potentially cheaper than cleaning to PoC, which is the default
  // of arch_clean_cache_range.
  // TODO(revest): Flush
  // We can batch the icache invalidate and just perform it once at the end.
  need_invalidate_ = true;
}
void Riscv64VmICacheConsistencyManager::Finish() {
  if (!need_invalidate_) {
    return;
  }
  // TODO(revest): Flush
  need_invalidate_ = false;
}

