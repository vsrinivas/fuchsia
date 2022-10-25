// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2014 Google Inc. All rights reserved
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
#include "arch/arm64/mmu.h"

#include <align.h>
#include <assert.h>
#include <bits.h>
#include <debug.h>
#include <inttypes.h>
#include <lib/arch/intrin.h>
#include <lib/counters.h>
#include <lib/fit/defer.h>
#include <lib/heap.h>
#include <lib/instrumentation/asan.h>
#include <lib/ktrace.h>
#include <lib/lazy_init/lazy_init.h>
#include <lib/page_cache.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <trace.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <arch/arm64/hypervisor/el2_state.h>
#include <arch/aspace.h>
#include <fbl/auto_lock.h>
#include <fbl/bits.h>
#include <kernel/auto_preempt_disabler.h>
#include <kernel/mutex.h>
#include <ktl/algorithm.h>
#include <lk/init.h>
#include <vm/arch_vm_aspace.h>
#include <vm/physmap.h>
#include <vm/pmm.h>
#include <vm/vm.h>

#include "asid_allocator.h"

#include <ktl/enforce.h>

#define LOCAL_TRACE 0
#define TRACE_CONTEXT_SWITCH 0

/* ktraces just local to this file */
#define LOCAL_KTRACE_ENABLE 0

#define LOCAL_KTRACE(string, args...)                                                         \
  ktrace_probe(LocalTrace<LOCAL_KTRACE_ENABLE>, TraceContext::Cpu, KTRACE_STRING_REF(string), \
               ##args)

using LocalTraceDuration =
    TraceDuration<TraceEnabled<LOCAL_KTRACE_ENABLE>, KTRACE_GRP_VM, TraceContext::Thread>;

// Use one of the ignored bits for a software simulated accessed flag for non-terminal entries.
// TODO: Once the hardware setting of the terminal AF is supported usage of this for non-terminal AF
// will have to become optional as we rely on the software terminal fault to set the non-terminal
// bits.
#define MMU_PTE_ATTR_RES_SOFTWARE_AF BM(55, 1, 1)
// Ensure we picked a bit that is actually part of the software controlled bits.
static_assert((MMU_PTE_ATTR_RES_SOFTWARE & MMU_PTE_ATTR_RES_SOFTWARE_AF) ==
              MMU_PTE_ATTR_RES_SOFTWARE_AF);

static_assert(((long)KERNEL_BASE >> MMU_KERNEL_SIZE_SHIFT) == -1, "");
static_assert(((long)KERNEL_ASPACE_BASE >> MMU_KERNEL_SIZE_SHIFT) == -1, "");
static_assert(MMU_KERNEL_SIZE_SHIFT <= 48, "");
static_assert(MMU_KERNEL_SIZE_SHIFT >= 25, "");

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
pte_t arm64_kernel_translation_table[MMU_KERNEL_PAGE_TABLE_ENTRIES_TOP] __ALIGNED(
    MMU_KERNEL_PAGE_TABLE_ENTRIES_TOP * 8);
// Physical address of the above table, saved in start.S.
paddr_t arm64_kernel_translation_table_phys;

// Global accessor for the kernel page table
pte_t* arm64_get_kernel_ptable() { return arm64_kernel_translation_table; }

namespace {

KCOUNTER(cm_flush_all, "mmu.consistency_manager.flush_all")
KCOUNTER(cm_flush_all_replacing, "mmu.consistency_manager.flush_all_replacing")
KCOUNTER(cm_single_tlb_invalidates, "mmu.consistency_manager.single_tlb_invalidate")
KCOUNTER(cm_flush, "mmu.consistency_manager.flush")

lazy_init::LazyInit<AsidAllocator> asid;

KCOUNTER(vm_mmu_protect_make_execute_calls, "vm.mmu.protect.make_execute_calls")
KCOUNTER(vm_mmu_protect_make_execute_pages, "vm.mmu.protect.make_execute_pages")
KCOUNTER(vm_mmu_page_table_alloc, "vm.mmu.pt.alloc")
KCOUNTER(vm_mmu_page_table_free, "vm.mmu.pt.free")
KCOUNTER(vm_mmu_page_table_reclaim, "vm.mmu.pt.reclaim")

page_cache::PageCache page_cache;

zx_status_t CacheAllocPage(vm_page_t** p, paddr_t* pa) {
  if (!page_cache) {
    return pmm_alloc_page(PMM_ALLOC_FLAG_ANY, p, pa);
  }

  zx::result result = page_cache.Allocate(1);
  if (result.is_error()) {
    return result.error_value();
  }

  vm_page_t* page = list_remove_head_type(&result->page_list, vm_page_t, queue_node);
  DEBUG_ASSERT(page != nullptr);
  DEBUG_ASSERT(result->page_list.is_empty());

  *p = page;
  *pa = page->paddr();
  return ZX_OK;
}

void CacheFreePages(list_node_t* list) {
  if (!page_cache) {
    pmm_free(list);
  }
  page_cache.Free(ktl::move(*list));
}

void CacheFreePage(vm_page_t* p) {
  if (!page_cache) {
    pmm_free_page(p);
  }

  page_cache::PageCache::PageList list;
  list_add_tail(&list, &p->queue_node);

  page_cache.Free(ktl::move(list));
}

void InitializePageCache(uint32_t level) {
  ASSERT(level < LK_INIT_LEVEL_THREADING);

  const size_t reserve_pages = 8;
  zx::result<page_cache::PageCache> result = page_cache::PageCache::Create(reserve_pages);

  ASSERT(result.is_ok());
  page_cache = ktl::move(result.value());
}

// Initialize the cache after the percpu data structures are initialized.
LK_INIT_HOOK(arm64_mmu_page_cache_init, InitializePageCache, LK_INIT_LEVEL_KERNEL + 1)

// Convert user level mmu flags to flags that go in L1 descriptors.
// Hypervisor flag modifies behavior to work for single translation regimes
// such as the mapping of kernel pages with ArmAspaceType::kHypervisor in EL2.
pte_t mmu_flags_to_s1_pte_attr(uint flags, bool hypervisor = false) {
  pte_t attr = MMU_PTE_ATTR_AF;

  switch (flags & ARCH_MMU_FLAG_CACHE_MASK) {
    case ARCH_MMU_FLAG_CACHED:
      attr |= MMU_PTE_ATTR_NORMAL_MEMORY | MMU_PTE_ATTR_SH_INNER_SHAREABLE;
      break;
    case ARCH_MMU_FLAG_WRITE_COMBINING:
      attr |= MMU_PTE_ATTR_NORMAL_UNCACHED | MMU_PTE_ATTR_SH_INNER_SHAREABLE;
      break;
    case ARCH_MMU_FLAG_UNCACHED:
      attr |= MMU_PTE_ATTR_STRONGLY_ORDERED;
      break;
    case ARCH_MMU_FLAG_UNCACHED_DEVICE:
      attr |= MMU_PTE_ATTR_DEVICE;
      break;
    default:
      PANIC_UNIMPLEMENTED;
  }

  switch (flags & (ARCH_MMU_FLAG_PERM_USER | ARCH_MMU_FLAG_PERM_WRITE)) {
    case 0:
      attr |= MMU_PTE_ATTR_AP_P_RO_U_NA;
      break;
    case ARCH_MMU_FLAG_PERM_WRITE:
      attr |= MMU_PTE_ATTR_AP_P_RW_U_NA;
      break;
    case ARCH_MMU_FLAG_PERM_USER:
      attr |= MMU_PTE_ATTR_AP_P_RO_U_RO;
      break;
    case ARCH_MMU_FLAG_PERM_USER | ARCH_MMU_FLAG_PERM_WRITE:
      attr |= MMU_PTE_ATTR_AP_P_RW_U_RW;
      break;
  }

  if (hypervisor) {
    // For single translation regimes such as the hypervisor pages, only
    // the XN bit applies.
    if ((flags & ARCH_MMU_FLAG_PERM_EXECUTE) == 0) {
      attr |= MMU_PTE_ATTR_XN;
    }
  } else {
    if (flags & ARCH_MMU_FLAG_PERM_EXECUTE) {
      if (flags & ARCH_MMU_FLAG_PERM_USER) {
        // User executable page, marked privileged execute never.
        attr |= MMU_PTE_ATTR_PXN;
      } else {
        // Privileged executable page, marked user execute never.
        attr |= MMU_PTE_ATTR_UXN;
      }
    } else {
      // All non executable pages are marked both privileged and user execute never.
      attr |= MMU_PTE_ATTR_UXN | MMU_PTE_ATTR_PXN;
    }
  }

  if (flags & ARCH_MMU_FLAG_NS) {
    attr |= MMU_PTE_ATTR_NON_SECURE;
  }

  return attr;
}

uint s1_pte_attr_to_mmu_flags(pte_t pte, bool hypervisor = false) {
  uint mmu_flags = 0;
  switch (pte & MMU_PTE_ATTR_ATTR_INDEX_MASK) {
    case MMU_PTE_ATTR_STRONGLY_ORDERED:
      mmu_flags |= ARCH_MMU_FLAG_UNCACHED;
      break;
    case MMU_PTE_ATTR_DEVICE:
      mmu_flags |= ARCH_MMU_FLAG_UNCACHED_DEVICE;
      break;
    case MMU_PTE_ATTR_NORMAL_UNCACHED:
      mmu_flags |= ARCH_MMU_FLAG_WRITE_COMBINING;
      break;
    case MMU_PTE_ATTR_NORMAL_MEMORY:
      mmu_flags |= ARCH_MMU_FLAG_CACHED;
      break;
    default:
      PANIC_UNIMPLEMENTED;
  }

  mmu_flags |= ARCH_MMU_FLAG_PERM_READ;
  switch (pte & MMU_PTE_ATTR_AP_MASK) {
    case MMU_PTE_ATTR_AP_P_RW_U_NA:
      mmu_flags |= ARCH_MMU_FLAG_PERM_WRITE;
      break;
    case MMU_PTE_ATTR_AP_P_RW_U_RW:
      mmu_flags |= ARCH_MMU_FLAG_PERM_USER | ARCH_MMU_FLAG_PERM_WRITE;
      break;
    case MMU_PTE_ATTR_AP_P_RO_U_NA:
      break;
    case MMU_PTE_ATTR_AP_P_RO_U_RO:
      mmu_flags |= ARCH_MMU_FLAG_PERM_USER;
      break;
  }

  if (hypervisor) {
    // Single translation regimes such as the hypervisor only support the XN bit.
    if ((pte & MMU_PTE_ATTR_XN) == 0) {
      mmu_flags |= ARCH_MMU_FLAG_PERM_EXECUTE;
    }
  } else {
    // Based on whether or not this is a user page, check UXN or PXN bit to determine
    // if it's an executable page.
    if (mmu_flags & ARCH_MMU_FLAG_PERM_USER) {
      if ((pte & MMU_PTE_ATTR_UXN) == 0) {
        mmu_flags |= ARCH_MMU_FLAG_PERM_EXECUTE;
      }
    } else if ((pte & MMU_PTE_ATTR_PXN) == 0) {
      // Privileged page, check the PXN bit.
      mmu_flags |= ARCH_MMU_FLAG_PERM_EXECUTE;
    }

    // TODO: fxbug.dev/88451
    // Add additional asserts here that the translation table entries are correctly formed
    // with regards to UXN and PXN bits and possibly other unhandled and/or ambiguous bits.
  }

  if (pte & MMU_PTE_ATTR_NON_SECURE) {
    mmu_flags |= ARCH_MMU_FLAG_NS;
  }

  return mmu_flags;
}

pte_t mmu_flags_to_s2_pte_attr(uint flags) {
  pte_t attr = MMU_PTE_ATTR_AF;

  switch (flags & ARCH_MMU_FLAG_CACHE_MASK) {
    case ARCH_MMU_FLAG_CACHED:
      attr |= MMU_S2_PTE_ATTR_NORMAL_MEMORY | MMU_PTE_ATTR_SH_INNER_SHAREABLE;
      break;
    case ARCH_MMU_FLAG_WRITE_COMBINING:
      attr |= MMU_S2_PTE_ATTR_NORMAL_UNCACHED | MMU_PTE_ATTR_SH_INNER_SHAREABLE;
      break;
    case ARCH_MMU_FLAG_UNCACHED:
      attr |= MMU_S2_PTE_ATTR_STRONGLY_ORDERED;
      break;
    case ARCH_MMU_FLAG_UNCACHED_DEVICE:
      attr |= MMU_S2_PTE_ATTR_DEVICE;
      break;
    default:
      PANIC_UNIMPLEMENTED;
  }

  if (flags & ARCH_MMU_FLAG_PERM_WRITE) {
    attr |= MMU_S2_PTE_ATTR_S2AP_RW;
  } else {
    attr |= MMU_S2_PTE_ATTR_S2AP_RO;
  }
  if (!(flags & ARCH_MMU_FLAG_PERM_EXECUTE)) {
    attr |= MMU_S2_PTE_ATTR_XN;
  }

  return attr;
}

uint s2_pte_attr_to_mmu_flags(pte_t pte) {
  uint mmu_flags = 0;

  switch (pte & MMU_S2_PTE_ATTR_ATTR_INDEX_MASK) {
    case MMU_S2_PTE_ATTR_STRONGLY_ORDERED:
      mmu_flags |= ARCH_MMU_FLAG_UNCACHED;
      break;
    case MMU_S2_PTE_ATTR_DEVICE:
      mmu_flags |= ARCH_MMU_FLAG_UNCACHED_DEVICE;
      break;
    case MMU_S2_PTE_ATTR_NORMAL_UNCACHED:
      mmu_flags |= ARCH_MMU_FLAG_WRITE_COMBINING;
      break;
    case MMU_S2_PTE_ATTR_NORMAL_MEMORY:
      mmu_flags |= ARCH_MMU_FLAG_CACHED;
      break;
    default:
      PANIC_UNIMPLEMENTED;
  }

  mmu_flags |= ARCH_MMU_FLAG_PERM_READ;
  switch (pte & MMU_PTE_ATTR_AP_MASK) {
    case MMU_S2_PTE_ATTR_S2AP_RO:
      break;
    case MMU_S2_PTE_ATTR_S2AP_RW:
      mmu_flags |= ARCH_MMU_FLAG_PERM_WRITE;
      break;
    default:
      PANIC_UNIMPLEMENTED;
  }

  if (!(pte & MMU_S2_PTE_ATTR_XN)) {
    mmu_flags |= ARCH_MMU_FLAG_PERM_EXECUTE;
  }

  return mmu_flags;
}

bool is_pte_valid(pte_t pte) {
  return (pte & MMU_PTE_DESCRIPTOR_MASK) != MMU_PTE_DESCRIPTOR_INVALID;
}

void update_pte(volatile pte_t* pte, pte_t newval) { *pte = newval; }

int first_used_page_table_entry(const volatile pte_t* page_table, uint page_size_shift) {
  const unsigned int count = 1U << (page_size_shift - 3);

  for (unsigned int i = 0; i < count; i++) {
    pte_t pte = page_table[i];
    if (pte != MMU_PTE_DESCRIPTOR_INVALID) {
      // Although the descriptor isn't exactly the INVALID value, it might have been corrupted and
      // also not a valid entry. Some forms of corruption are indistinguishable from valid entries,
      // so this is really just checking for scenarios where the low type bits got set to INVALID,
      // but the rest of the entry did not.
      //
      // TODO(fxbug.dev/79118): Once fxbug.dev/79118 is resolved this can be removed.
      ASSERT_MSG(is_pte_valid(pte),
                 "page_table at %p has malformed invalid entry %#" PRIx64 " at %u\n", page_table,
                 pte, i);
      return i;
    }
  }
  return -1;
}

bool page_table_is_clear(const volatile pte_t* page_table, uint page_size_shift) {
  const int index = first_used_page_table_entry(page_table, page_size_shift);
  const bool clear = index == -1;
  if (clear) {
    LTRACEF("page table at %p is clear\n", page_table);
  } else {
    LTRACEF("page_table at %p still in use, index %d is %#" PRIx64 "\n", page_table, index,
            page_table[index]);
  }
  return clear;
}

ArmAspaceType AspaceTypeFromFlags(uint mmu_flags) {
  // Kernel/Guest flags are mutually exclusive. Ensure at most 1 is set.
  DEBUG_ASSERT(((mmu_flags & ARCH_ASPACE_FLAG_KERNEL) != 0) +
                   ((mmu_flags & ARCH_ASPACE_FLAG_GUEST) != 0) <=
               1);
  if (mmu_flags & ARCH_ASPACE_FLAG_KERNEL) {
    return ArmAspaceType::kKernel;
  }
  if (mmu_flags & ARCH_ASPACE_FLAG_GUEST) {
    return ArmAspaceType::kGuest;
  }
  return ArmAspaceType::kUser;
}

ktl::string_view ArmAspaceTypeName(ArmAspaceType type) {
  switch (type) {
    case ArmAspaceType::kKernel:
      return "kernel";
    case ArmAspaceType::kUser:
      return "user";
    case ArmAspaceType::kGuest:
      return "guest";
    case ArmAspaceType::kHypervisor:
      return "hypervisor";
  }
  __UNREACHABLE;
}

}  // namespace

// A consistency manager that tracks TLB updates, walker syncs and free pages in an effort to
// minimize DSBs (by delaying and coalescing TLB invalidations) and switching to full ASID
// invalidations if too many TLB invalidations are requested.
class ArmArchVmAspace::ConsistencyManager {
 public:
  ConsistencyManager(ArmArchVmAspace& aspace) TA_REQ(aspace.lock_) : aspace_(aspace) {}
  ~ConsistencyManager() {
    Flush();
    if (!list_is_empty(&to_free_)) {
      CacheFreePages(&to_free_);
    }
  }

  // Queue a TLB entry for flushing. This may get turned into a complete ASID flush.
  void FlushEntry(vaddr_t va, bool terminal) {
    AssertHeld(aspace_.lock_);
    // Check we have queued too many entries already.
    if (num_pending_tlbs_ >= kMaxPendingTlbs) {
      // Most of the time we will now prefer to invalidate the entire ASID, the exception is if
      // this aspace is using the global ASID, since we cannot perform a global TLB invalidation
      // for all ASIDs. Note that there is an instruction to invalidate the entire TLB, but it is
      // only available in EL2, and we are in EL1.
      if (aspace_.asid_ != MMU_ARM64_GLOBAL_ASID) {
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

  // Performs any pending synchronization of TLBs and page table walkers. Includes the DSB to ensure
  // TLB flushes have completed prior to returning to user.
  void Flush() TA_REQ(aspace_.lock_) {
    cm_flush.Add(1);
    if (num_pending_tlbs_ == 0) {
      return;
    }
    // Need a DSB to synchronize any page table updates prior to flushing the TLBs.
    __dsb(ARM_MB_ISHST);

    // Check if we should just be performing a full ASID invalidation.
    if (num_pending_tlbs_ > kMaxPendingTlbs || aspace_.type_ == ArmAspaceType::kHypervisor) {
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

    // DSB to ensure TLB flushes happen prior to returning to user.
    __dsb(ARM_MB_ISH);

    // Local flushes that the kernel may observe prior to Context Synchronization Event
    // should go ahead and get an ISB to force it.
    if (aspace_.type_ == ArmAspaceType::kKernel) {
      __isb(ARM_MB_SY);
    }

    num_pending_tlbs_ = 0;
  }

  // Queue a page for freeing that is dependent on TLB flushing. This is for pages that were
  // previously installed as page tables and they should not be reused until the non-terminal TLB
  // flush has occurred.
  void FreePage(vm_page_t* page) { list_add_tail(&to_free_, &page->queue_node); }

 private:
  // Maximum number of TLB entries we will queue before switching to ASID invalidation.
  static constexpr size_t kMaxPendingTlbs = 16;

  // Pending TLBs to flush are stored as 63 bits, with the bottom bit stolen to store the terminal
  // flag. 63 bits is more than enough as these entries are page aligned at the minimum.
  FBL_BITFIELD_DEF_START(PendingTlbs, uint64_t)
  FBL_BITFIELD_MEMBER(terminal, 0, 1);
  FBL_BITFIELD_MEMBER(va_shifted, 1, 63);
  FBL_BITFIELD_DEF_END();

  PendingTlbs pending_tlbs_[kMaxPendingTlbs];

  size_t num_pending_tlbs_ = 0;

  // vm_page_t's to release to the PMM after the TLB invalidation occurs.
  list_node to_free_ = LIST_INITIAL_VALUE(to_free_);

  // The aspace we are invalidating TLBs for.
  const ArmArchVmAspace& aspace_;
};

uint ArmArchVmAspace::MmuFlagsFromPte(pte_t pte) {
  switch (type_) {
    case ArmAspaceType::kUser:
    case ArmAspaceType::kKernel:
      return s1_pte_attr_to_mmu_flags(pte);
    case ArmAspaceType::kHypervisor:
      return s1_pte_attr_to_mmu_flags(pte, true);
    case ArmAspaceType::kGuest:
      return s2_pte_attr_to_mmu_flags(pte);
  }
  __UNREACHABLE;
}

zx_status_t ArmArchVmAspace::Query(vaddr_t vaddr, paddr_t* paddr, uint* mmu_flags) {
  Guard<CriticalMutex> al{&lock_};
  return QueryLocked(vaddr, paddr, mmu_flags);
}

zx_status_t ArmArchVmAspace::QueryLocked(vaddr_t vaddr, paddr_t* paddr, uint* mmu_flags) {
  vaddr_t vaddr_rem;

  canary_.Assert();
  LTRACEF("aspace %p, vaddr 0x%lx\n", this, vaddr);

  DEBUG_ASSERT(tt_virt_);

  DEBUG_ASSERT(IsValidVaddr(vaddr));
  if (!IsValidVaddr(vaddr)) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  const volatile pte_t* page_table = tt_virt_;
  uint32_t index_shift = top_index_shift_;
  vaddr_rem = vaddr - vaddr_base_;
  while (true) {
    const ulong index = vaddr_rem >> index_shift;
    vaddr_rem -= (vaddr_t)index << index_shift;
    const pte_t pte = page_table[index];
    const uint descriptor_type = pte & MMU_PTE_DESCRIPTOR_MASK;
    const paddr_t pte_addr = pte & MMU_PTE_OUTPUT_ADDR_MASK;

    LTRACEF("va %#" PRIxPTR ", index %lu, index_shift %u, rem %#" PRIxPTR ", pte %#" PRIx64 "\n",
            vaddr, index, index_shift, vaddr_rem, pte);

    if (descriptor_type == MMU_PTE_DESCRIPTOR_INVALID) {
      return ZX_ERR_NOT_FOUND;
    }

    if (descriptor_type == ((index_shift > page_size_shift_) ? MMU_PTE_L012_DESCRIPTOR_BLOCK
                                                             : MMU_PTE_L3_DESCRIPTOR_PAGE)) {
      if (paddr) {
        *paddr = pte_addr + vaddr_rem;
      }
      if (mmu_flags) {
        *mmu_flags = MmuFlagsFromPte(pte);
      }
      LTRACEF("va 0x%lx, paddr 0x%lx, flags 0x%x\n", vaddr, paddr ? *paddr : ~0UL,
              mmu_flags ? *mmu_flags : ~0U);
      return ZX_OK;
    }

    if (index_shift <= page_size_shift_ || descriptor_type != MMU_PTE_L012_DESCRIPTOR_TABLE) {
      PANIC_UNIMPLEMENTED;
    }

    page_table = static_cast<const volatile pte_t*>(paddr_to_physmap(pte_addr));
    index_shift -= page_size_shift_ - 3;
  }
}

zx_status_t ArmArchVmAspace::AllocPageTable(paddr_t* paddrp) {
  LTRACEF("page_size_shift %u\n", page_size_shift_);

  // currently we only support allocating a single page
  DEBUG_ASSERT(page_size_shift_ == PAGE_SIZE_SHIFT);

  // Allocate a page from the pmm via function pointer passed to us in Init().
  // The default is CacheAllocPage so test and explicitly call it to avoid any unnecessary
  // virtual functions.
  vm_page_t* page;
  zx_status_t status;
  if (likely(!test_page_alloc_func_)) {
    status = CacheAllocPage(&page, paddrp);
  } else {
    status = test_page_alloc_func_(0, &page, paddrp);
  }
  if (status != ZX_OK) {
    return status;
  }

  page->set_state(vm_page_state::MMU);
  pt_pages_++;
  kcounter_add(vm_mmu_page_table_alloc, 1);

  LOCAL_KTRACE("page table alloc");

  LTRACEF("allocated %#lx\n", *paddrp);
  return ZX_OK;
}

void ArmArchVmAspace::FreePageTable(void* vaddr, paddr_t paddr, ConsistencyManager& cm,
                                    Reclaim reclaim) {
  LTRACEF("vaddr %p paddr %#lx page_size_shift %u\n", vaddr, paddr, page_size_shift_);

  // currently we only support freeing a single page
  DEBUG_ASSERT(page_size_shift_ == PAGE_SIZE_SHIFT);

  LOCAL_KTRACE("page table free");

  vm_page_t* page = paddr_to_vm_page(paddr);
  if (!page) {
    panic("bad page table paddr %#lx\n", paddr);
  }
  DEBUG_ASSERT(page->state() == vm_page_state::MMU);
  cm.FreePage(page);

  pt_pages_--;
  kcounter_add(vm_mmu_page_table_free, 1);
  if (reclaim == Reclaim::Yes) {
    kcounter_add(vm_mmu_page_table_reclaim, 1);
  }
}

zx_status_t ArmArchVmAspace::SplitLargePage(vaddr_t vaddr, const uint index_shift, vaddr_t pt_index,
                                            volatile pte_t* page_table, ConsistencyManager& cm) {
  DEBUG_ASSERT(index_shift > page_size_shift_);

  const pte_t pte = page_table[pt_index];
  DEBUG_ASSERT((pte & MMU_PTE_DESCRIPTOR_MASK) == MMU_PTE_L012_DESCRIPTOR_BLOCK);

  paddr_t paddr;
  zx_status_t ret = AllocPageTable(&paddr);
  if (ret) {
    TRACEF("failed to allocate page table\n");
    return ret;
  }

  const uint next_shift = (index_shift - (page_size_shift_ - 3));

  const auto new_page_table = static_cast<volatile pte_t*>(paddr_to_physmap(paddr));
  const auto new_desc_type =
      (next_shift == page_size_shift_) ? MMU_PTE_L3_DESCRIPTOR_PAGE : MMU_PTE_L012_DESCRIPTOR_BLOCK;
  const auto attrs = (pte & ~(MMU_PTE_OUTPUT_ADDR_MASK | MMU_PTE_DESCRIPTOR_MASK)) | new_desc_type;

  const uint next_size = 1U << next_shift;
  for (uint64_t i = 0, mapped_paddr = pte & MMU_PTE_OUTPUT_ADDR_MASK;
       i < MMU_KERNEL_PAGE_TABLE_ENTRIES; i++, mapped_paddr += next_size) {
    // directly write to the pte, no need to update since this is
    // a completely new table
    new_page_table[i] = mapped_paddr | attrs;
  }

  // Ensure all new entries of the table becomes visible prior to page table installation.
  __dsb(ARM_MB_ISHST);

  update_pte(&page_table[pt_index], paddr | MMU_PTE_L012_DESCRIPTOR_TABLE);
  LTRACEF("pte %p[%#" PRIxPTR "] = %#" PRIx64 "\n", page_table, pt_index, page_table[pt_index]);

  // no need to update the page table count here since we're replacing a block entry with a table
  // entry.

  cm.FlushEntry(vaddr, false);

  return ZX_OK;
}

// use the appropriate TLB flush instruction to globally flush the modified entry
// terminal is set when flushing at the final level of the page table.
void ArmArchVmAspace::FlushTLBEntry(vaddr_t vaddr, bool terminal) const {
  switch (type_) {
    case ArmAspaceType::kUser: {
      // flush this address for the specific asid
      if (terminal) {
        ARM64_TLBI(vale1is, vaddr >> 12 | (vaddr_t)asid_ << 48);
      } else {
        ARM64_TLBI(vae1is, vaddr >> 12 | (vaddr_t)asid_ << 48);
      }
      return;
    }
    case ArmAspaceType::kKernel: {
      DEBUG_ASSERT(asid_ == MMU_ARM64_GLOBAL_ASID);
      // flush this address on all ASIDs
      if (terminal) {
        ARM64_TLBI(vaale1is, vaddr >> 12);
      } else {
        ARM64_TLBI(vaae1is, vaddr >> 12);
      }
      return;
    }
    case ArmAspaceType::kGuest: {
      uint64_t vttbr = arm64_vttbr(asid_, tt_phys_);
      __UNUSED zx_status_t status = arm64_el2_tlbi_ipa(vttbr, vaddr, terminal);
      DEBUG_ASSERT(status == ZX_OK);
      return;
    }
    case ArmAspaceType::kHypervisor:
      PANIC("Unsupported.");
      return;
  }
  __UNREACHABLE;
}

void ArmArchVmAspace::FlushAsid() const {
  switch (type_) {
    case ArmAspaceType::kUser: {
      DEBUG_ASSERT(asid_ != MMU_ARM64_GLOBAL_ASID);
      ARM64_TLBI_ASID(aside1is, asid_);
      return;
    }
    case ArmAspaceType::kKernel: {
      // The alle1is instruction that invalidates the TLBs for all ASIDs is only available in EL2,
      // and not EL1.
      panic("FlushAsid not available for kernel address space");
      return;
    }
    case ArmAspaceType::kGuest: {
      uint64_t vttbr = arm64_vttbr(asid_, tt_phys_);
      zx_status_t status = arm64_el2_tlbi_vmid(vttbr);
      DEBUG_ASSERT(status == ZX_OK);
      return;
    }
    case ArmAspaceType::kHypervisor: {
      // Flush all TLB entries in EL2.
      zx_status_t status = arm64_el2_tlbi_el2();
      DEBUG_ASSERT(status == ZX_OK);
      return;
    }
  }
  __UNREACHABLE;
}

ssize_t ArmArchVmAspace::UnmapPageTable(vaddr_t vaddr, vaddr_t vaddr_rel, size_t size,
                                        EnlargeOperation enlarge, const uint index_shift,
                                        volatile pte_t* page_table, ConsistencyManager& cm,
                                        Reclaim reclaim) {
  const vaddr_t block_size = 1UL << index_shift;
  const vaddr_t block_mask = block_size - 1;

  LTRACEF(
      "vaddr 0x%lx, vaddr_rel 0x%lx, size 0x%lx, index shift %u, page_size_shift %u, page_table "
      "%p\n",
      vaddr, vaddr_rel, size, index_shift, page_size_shift_, page_table);

  size_t unmap_size = 0;
  while (size) {
    const vaddr_t vaddr_rem = vaddr_rel & block_mask;
    const size_t chunk_size = ktl::min(size, block_size - vaddr_rem);
    const vaddr_t index = vaddr_rel >> index_shift;

    pte_t pte = page_table[index];

    // If the input range partially covers a large page, attempt to split.
    if (index_shift > page_size_shift_ &&
        (pte & MMU_PTE_DESCRIPTOR_MASK) == MMU_PTE_L012_DESCRIPTOR_BLOCK &&
        chunk_size != block_size) {
      zx_status_t s = SplitLargePage(vaddr, index_shift, index, page_table, cm);
      // If the split failed then check if we are allowed to unmap extra, and if so just fall
      // through and unmap the entire large page.
      if (likely(s == ZX_OK)) {
        pte = page_table[index];
      } else if (enlarge == EnlargeOperation::No) {
        return s;
      }
    }
    if (index_shift > page_size_shift_ &&
        (pte & MMU_PTE_DESCRIPTOR_MASK) == MMU_PTE_L012_DESCRIPTOR_TABLE) {
      const paddr_t page_table_paddr = pte & MMU_PTE_OUTPUT_ADDR_MASK;
      volatile pte_t* next_page_table =
          static_cast<volatile pte_t*>(paddr_to_physmap(page_table_paddr));

      // Recurse a level.
      ssize_t result =
          UnmapPageTable(vaddr, vaddr_rem, chunk_size, enlarge,
                         index_shift - (page_size_shift_ - 3), next_page_table, cm, reclaim);
      if (result < 0) {
        return result;
      }

      // if we unmapped an entire page table leaf and/or the unmap made the level below us empty,
      // free the page table
      if (chunk_size == block_size || page_table_is_clear(next_page_table, page_size_shift_)) {
        LTRACEF("pte %p[0x%lx] = 0 (was page table phys %#lx)\n", page_table, index,
                page_table_paddr);
        update_pte(&page_table[index], MMU_PTE_DESCRIPTOR_INVALID);

        // We can safely defer TLB flushing as the consistency manager will not return the backing
        // page to the PMM until after the tlb is flushed.
        cm.FlushEntry(vaddr, false);
        FreePageTable(const_cast<pte_t*>(next_page_table), page_table_paddr, cm, reclaim);
      }
    } else if (is_pte_valid(pte)) {
      LTRACEF("pte %p[0x%lx] = 0 (was phys %#lx)\n", page_table, index,
              page_table[index] & MMU_PTE_OUTPUT_ADDR_MASK);
      update_pte(&page_table[index], MMU_PTE_DESCRIPTOR_INVALID);
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

ssize_t ArmArchVmAspace::MapPageTable(vaddr_t vaddr_in, vaddr_t vaddr_rel_in, paddr_t paddr_in,
                                      size_t size_in, pte_t attrs, const uint index_shift,
                                      volatile pte_t* page_table, ConsistencyManager& cm) {
  vaddr_t vaddr = vaddr_in;
  vaddr_t vaddr_rel = vaddr_rel_in;
  paddr_t paddr = paddr_in;
  size_t size = size_in;

  const vaddr_t block_size = 1UL << index_shift;
  const vaddr_t block_mask = block_size - 1;
  LTRACEF("vaddr %#" PRIxPTR ", vaddr_rel %#" PRIxPTR ", paddr %#" PRIxPTR
          ", size %#zx, attrs %#" PRIx64 ", index shift %u, page_size_shift %u, page_table %p\n",
          vaddr, vaddr_rel, paddr, size, attrs, index_shift, page_size_shift_, page_table);

  if ((vaddr_rel | paddr | size) & ((1UL << page_size_shift_) - 1)) {
    TRACEF("not page aligned\n");
    return ZX_ERR_INVALID_ARGS;
  }

  auto cleanup = fit::defer([&]() {
    AssertHeld(lock_);
    // Unmapping what we have just mapped in should never fail, and we should not have to enlarge
    // the unmap for it to succeed.
    ssize_t result = UnmapPageTable(vaddr_in, vaddr_rel_in, size_in - size, EnlargeOperation::No,
                                    index_shift, page_table, cm);
    ASSERT(result >= 0);
  });

  size_t mapped_size = 0;
  while (size) {
    const vaddr_t vaddr_rem = vaddr_rel & block_mask;
    const size_t chunk_size = ktl::min(size, block_size - vaddr_rem);
    const vaddr_t index = vaddr_rel >> index_shift;
    pte_t pte = page_table[index];

    // if we're at an unaligned address, not trying to map a block, and not at the terminal level,
    // recurse one more level of the page table tree
    if (((vaddr_rel | paddr) & block_mask) || (chunk_size != block_size) ||
        (index_shift > MMU_PTE_DESCRIPTOR_BLOCK_MAX_SHIFT)) {
      // Lookup the next level page table, allocating if required.
      bool allocated_page_table = false;
      paddr_t page_table_paddr = 0;
      volatile pte_t* next_page_table = nullptr;

      switch (pte & MMU_PTE_DESCRIPTOR_MASK) {
        case MMU_PTE_DESCRIPTOR_INVALID: {
          zx_status_t ret = AllocPageTable(&page_table_paddr);
          if (ret) {
            TRACEF("failed to allocate page table\n");
            return ret;
          }
          allocated_page_table = true;
          void* pt_vaddr = paddr_to_physmap(page_table_paddr);

          LTRACEF("allocated page table, vaddr %p, paddr 0x%lx\n", pt_vaddr, page_table_paddr);
          arch_zero_page(pt_vaddr);

          // ensure that the zeroing is observable from hardware page table walkers, as we need to
          // do this prior to writing the pte we cannot defer it using the consistency manager.
          __dsb(ARM_MB_ISHST);

          // When new pages are mapped they they have their AF set, under the assumption they are
          // being mapped due to being accessed, and this lets us avoid an accessed fault. Since new
          // terminal mappings start with the AF flag set, we then also need to start non-terminal
          // mappings as having the AF set.
          pte = page_table_paddr | MMU_PTE_L012_DESCRIPTOR_TABLE | MMU_PTE_ATTR_RES_SOFTWARE_AF;
          update_pte(&page_table[index], pte);
          // We do not need to sync the walker, despite writing a new entry, as this is a
          // non-terminal entry and so is irrelevant to the walker anyway.
          LTRACEF("pte %p[%#" PRIxPTR "] = %#" PRIx64 "\n", page_table, index, pte);
          next_page_table = static_cast<volatile pte_t*>(pt_vaddr);
          break;
        }
        case MMU_PTE_L012_DESCRIPTOR_TABLE:
          // Similar to creating a page table, if we end up mapping a page lower down in this
          // hierarchy then it will start off as accessed. As such we set the accessed flag on the
          // way down.
          pte |= MMU_PTE_ATTR_RES_SOFTWARE_AF;
          update_pte(&page_table[index], pte);
          page_table_paddr = pte & MMU_PTE_OUTPUT_ADDR_MASK;
          LTRACEF("found page table %#" PRIxPTR "\n", page_table_paddr);
          next_page_table = static_cast<volatile pte_t*>(paddr_to_physmap(page_table_paddr));
          break;
        case MMU_PTE_L012_DESCRIPTOR_BLOCK:
          return ZX_ERR_ALREADY_EXISTS;

        default:
          PANIC_UNIMPLEMENTED;
      }
      DEBUG_ASSERT(next_page_table);

      ssize_t ret = MapPageTable(vaddr, vaddr_rem, paddr, chunk_size, attrs,
                                 index_shift - (page_size_shift_ - 3), next_page_table, cm);
      if (ret < 0) {
        if (allocated_page_table) {
          // We just allocated this page table. The unmap in err will not clean it up as the size
          // we pass in will not cause us to look at this page table. This is reasonable as if we
          // didn't allocate the page table then we shouldn't look into and potentially unmap
          // anything from that page table.
          // Since we just allocated it there should be nothing in it, otherwise the MapPageTable
          // call would not have failed.
          DEBUG_ASSERT(page_table_is_clear(next_page_table, page_size_shift_));
          update_pte(&page_table[index], MMU_PTE_DESCRIPTOR_INVALID);

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

      pte = paddr | attrs;
      if (index_shift > page_size_shift_) {
        pte |= MMU_PTE_L012_DESCRIPTOR_BLOCK;
      } else {
        pte |= MMU_PTE_L3_DESCRIPTOR_PAGE;
      }
      LTRACEF("pte %p[%#" PRIxPTR "] = %#" PRIx64 " (paddr %#lx)\n", page_table, index, pte, paddr);
      update_pte(&page_table[index], pte);
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

zx_status_t ArmArchVmAspace::ProtectPageTable(vaddr_t vaddr_in, vaddr_t vaddr_rel_in,
                                              size_t size_in, pte_t attrs, const uint index_shift,
                                              volatile pte_t* page_table, ConsistencyManager& cm) {
  vaddr_t vaddr = vaddr_in;
  vaddr_t vaddr_rel = vaddr_rel_in;
  size_t size = size_in;

  const vaddr_t block_size = 1UL << index_shift;
  const vaddr_t block_mask = block_size - 1;

  LTRACEF("vaddr %#" PRIxPTR ", vaddr_rel %#" PRIxPTR ", size %#" PRIxPTR ", attrs %#" PRIx64
          ", index shift %u, page_size_shift %u, page_table %p\n",
          vaddr, vaddr_rel, size, attrs, index_shift, page_size_shift_, page_table);

  // vaddr_rel and size must be page aligned
  DEBUG_ASSERT(((vaddr_rel | size) & ((1UL << page_size_shift_) - 1)) == 0);

  while (size) {
    const vaddr_t vaddr_rem = vaddr_rel & block_mask;
    const size_t chunk_size = ktl::min(size, block_size - vaddr_rem);
    const vaddr_t index = vaddr_rel >> index_shift;

    pte_t pte = page_table[index];

    // If the input range partially covers a large page, split the page.
    if (index_shift > page_size_shift_ &&
        (pte & MMU_PTE_DESCRIPTOR_MASK) == MMU_PTE_L012_DESCRIPTOR_BLOCK &&
        chunk_size != block_size) {
      zx_status_t s = SplitLargePage(vaddr, index_shift, index, page_table, cm);
      if (unlikely(s != ZX_OK)) {
        return s;
      }
      pte = page_table[index];
    }

    if (index_shift > page_size_shift_ &&
        (pte & MMU_PTE_DESCRIPTOR_MASK) == MMU_PTE_L012_DESCRIPTOR_TABLE) {
      const paddr_t page_table_paddr = pte & MMU_PTE_OUTPUT_ADDR_MASK;
      volatile pte_t* next_page_table =
          static_cast<volatile pte_t*>(paddr_to_physmap(page_table_paddr));

      // Recurse a level.
      zx_status_t status =
          ProtectPageTable(vaddr, vaddr_rem, chunk_size, attrs,
                           index_shift - (page_size_shift_ - 3), next_page_table, cm);
      if (unlikely(status != ZX_OK)) {
        return status;
      }
    } else if (is_pte_valid(pte)) {
      pte = (pte & ~MMU_PTE_PERMISSION_MASK) | attrs;
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

size_t ArmArchVmAspace::HarvestAccessedPageTable(
    size_t* entry_limit, vaddr_t vaddr, vaddr_t vaddr_rel_in, size_t size, const uint index_shift,
    NonTerminalAction non_terminal_action, TerminalAction terminal_action,
    volatile pte_t* page_table, ConsistencyManager& cm, bool* unmapped_out) {
  const vaddr_t block_size = 1UL << index_shift;
  const vaddr_t block_mask = block_size - 1;

  vaddr_t vaddr_rel = vaddr_rel_in;

  // vaddr_rel and size must be page aligned
  DEBUG_ASSERT(((vaddr_rel | size) & ((1UL << page_size_shift_) - 1)) == 0);

  size_t harvested_size = 0;

  while (size > 0 && *entry_limit > 0) {
    LocalTraceDuration trace{"page_table_loop"_stringref};

    const vaddr_t vaddr_rem = vaddr_rel & block_mask;
    const vaddr_t index = vaddr_rel >> index_shift;

    size_t chunk_size = ktl::min(size, block_size - vaddr_rem);

    pte_t pte = page_table[index];

    if (index_shift > page_size_shift_ &&
        (pte & MMU_PTE_DESCRIPTOR_MASK) == MMU_PTE_L012_DESCRIPTOR_BLOCK &&
        chunk_size != block_size) {
      // Ignore large pages, we do not support harvesting accessed bits from them. Having this empty
      // if block simplifies the overall logic.
    } else if (index_shift > page_size_shift_ &&
               (pte & MMU_PTE_DESCRIPTOR_MASK) == MMU_PTE_L012_DESCRIPTOR_TABLE) {
      const paddr_t page_table_paddr = pte & MMU_PTE_OUTPUT_ADDR_MASK;
      volatile pte_t* next_page_table =
          static_cast<volatile pte_t*>(paddr_to_physmap(page_table_paddr));

      // Start with the assumption that we will unmap if we can.
      bool do_unmap = non_terminal_action == NonTerminalAction::FreeUnaccessed;
      // Check for our emulated non-terminal AF so we can potentially skip the recursion.
      // TODO: make this optional when hardware AF is supported (see todo on
      // MMU_PTE_ATTR_RES_SOFTWARE_AF for details)
      if (pte & MMU_PTE_ATTR_RES_SOFTWARE_AF) {
        bool unmapped = false;
        chunk_size = HarvestAccessedPageTable(
            entry_limit, vaddr, vaddr_rem, chunk_size, index_shift - (page_size_shift_ - 3),
            non_terminal_action, terminal_action, next_page_table, cm, &unmapped);
        // This was accessed so we don't necessarily want to unmap it, unless our recursive call
        // caused the page table to be empty, in which case we are obligated to.
        do_unmap = (unmapped && page_table_is_clear(next_page_table, page_size_shift_));
        // If we processed till the end of sub page table, and we are not retaining page tables,
        // then we can clear the AF as we know we will not have to process entries from this one
        // again.
        if (!do_unmap && (vaddr_rel + chunk_size) >> index_shift != index &&
            non_terminal_action != NonTerminalAction::Retain) {
          pte &= ~MMU_PTE_ATTR_RES_SOFTWARE_AF;
          update_pte(&page_table[index], pte);
        }
      }
      if (do_unmap) {
        // Unmapping an exact block, which should not need enlarging and hence should never be able
        // to fail.
        ssize_t result =
            UnmapPageTable(vaddr, vaddr_rem, chunk_size, EnlargeOperation::No,
                           index_shift - (page_size_shift_ - 3), next_page_table, cm, Reclaim::Yes);
        ASSERT(result >= 0);
        DEBUG_ASSERT(page_table_is_clear(next_page_table, page_size_shift_));
        update_pte(&page_table[index], MMU_PTE_DESCRIPTOR_INVALID);

        // We can safely defer TLB flushing as the consistency manager will not return the backing
        // page to the PMM until after the tlb is flushed.
        cm.FlushEntry(vaddr, false);
        FreePageTable(const_cast<pte_t*>(next_page_table), page_table_paddr, cm, Reclaim::Yes);
        if (unmapped_out) {
          *unmapped_out = true;
        }
      }
    } else if (is_pte_valid(pte) && (pte & MMU_PTE_ATTR_AF)) {
      const paddr_t pte_addr = pte & MMU_PTE_OUTPUT_ADDR_MASK;
      const paddr_t paddr = pte_addr + vaddr_rem;

      vm_page_t* page = paddr_to_vm_page(paddr);
      // Mappings for physical VMOs do not have pages associated with them and so there's no state
      // to update on an access.
      if (likely(page)) {
        pmm_page_queues()->MarkAccessedDeferredCount(page);

        if (terminal_action == TerminalAction::UpdateAgeAndHarvest) {
          // Modifying the access flag does not require break-before-make for correctness and as we
          // do not support hardware access flag setting at the moment we do not have to deal with
          // potential concurrent modifications.
          pte = (pte & ~MMU_PTE_ATTR_AF);
          LTRACEF("pte %p[%#" PRIxPTR "] = %#" PRIx64 "\n", page_table, index, pte);
          update_pte(&page_table[index], pte);

          cm.FlushEntry(vaddr, true);
        }
      }
    }
    vaddr += chunk_size;
    vaddr_rel += chunk_size;
    size -= chunk_size;

    harvested_size += chunk_size;

    // Each iteration of this loop examines a PTE at the current level. The
    // total number of PTEs examined is limited to avoid holding the aspace lock
    // for too long. However, the remaining limit balance is updated at the end
    // of the loop to ensure that harvesting makes progress, even if the initial
    // limit is too small to reach a terminal PTE.
    if (*entry_limit > 0) {
      *entry_limit -= 1;
    }
  }

  return harvested_size;
}

void ArmArchVmAspace::MarkAccessedPageTable(vaddr_t vaddr, vaddr_t vaddr_rel_in, size_t size,
                                            const uint index_shift, volatile pte_t* page_table,
                                            ConsistencyManager& cm) {
  const vaddr_t block_size = 1UL << index_shift;
  const vaddr_t block_mask = block_size - 1;

  vaddr_t vaddr_rel = vaddr_rel_in;

  // vaddr_rel and size must be page aligned
  DEBUG_ASSERT(((vaddr_rel | size) & ((1UL << page_size_shift_) - 1)) == 0);

  while (size) {
    const vaddr_t vaddr_rem = vaddr_rel & block_mask;
    const size_t chunk_size = ktl::min(size, block_size - vaddr_rem);
    const vaddr_t index = vaddr_rel >> index_shift;

    pte_t pte = page_table[index];

    if (index_shift > page_size_shift_ &&
        (pte & MMU_PTE_DESCRIPTOR_MASK) == MMU_PTE_L012_DESCRIPTOR_BLOCK &&
        chunk_size != block_size) {
      // Ignore large pages as we don't support modifying their access flags. Having this empty if
      // block simplifies the overall logic.
    } else if (index_shift > page_size_shift_ &&
               (pte & MMU_PTE_DESCRIPTOR_MASK) == MMU_PTE_L012_DESCRIPTOR_TABLE) {
      // Set the software bit we use to represent that this page table has been accessed.
      pte |= MMU_PTE_ATTR_RES_SOFTWARE_AF;
      update_pte(&page_table[index], pte);
      const paddr_t page_table_paddr = pte & MMU_PTE_OUTPUT_ADDR_MASK;
      volatile pte_t* next_page_table =
          static_cast<volatile pte_t*>(paddr_to_physmap(page_table_paddr));
      MarkAccessedPageTable(vaddr, vaddr_rem, chunk_size, index_shift - (page_size_shift_ - 3),
                            next_page_table, cm);
    } else if (is_pte_valid(pte) && (pte & MMU_PTE_ATTR_AF) == 0) {
      pte |= MMU_PTE_ATTR_AF;
      update_pte(&page_table[index], pte);
    }
    vaddr += chunk_size;
    vaddr_rel += chunk_size;
    size -= chunk_size;
  }
}

ssize_t ArmArchVmAspace::MapPages(vaddr_t vaddr, paddr_t paddr, size_t size, pte_t attrs,
                                  vaddr_t vaddr_base, ConsistencyManager& cm) {
  vaddr_t vaddr_rel = vaddr - vaddr_base;
  vaddr_t vaddr_rel_max = 1UL << top_size_shift_;

  LTRACEF("vaddr %#" PRIxPTR ", paddr %#" PRIxPTR ", size %#" PRIxPTR ", attrs %#" PRIx64
          ", asid %#x\n",
          vaddr, paddr, size, attrs, asid_);

  if (vaddr_rel > vaddr_rel_max - size || size > vaddr_rel_max) {
    TRACEF("vaddr %#" PRIxPTR ", size %#" PRIxPTR " out of range vaddr %#" PRIxPTR
           ", size %#" PRIxPTR "\n",
           vaddr, size, vaddr_base, vaddr_rel_max);
    return ZX_ERR_INVALID_ARGS;
  }

  LOCAL_KTRACE("mmu map", (vaddr & ~PAGE_MASK) | ((size >> PAGE_SIZE_SHIFT) & PAGE_MASK));
  ssize_t ret = MapPageTable(vaddr, vaddr_rel, paddr, size, attrs, top_index_shift_, tt_virt_, cm);
  return ret;
}

ssize_t ArmArchVmAspace::UnmapPages(vaddr_t vaddr, size_t size, EnlargeOperation enlarge,
                                    vaddr_t vaddr_base, ConsistencyManager& cm) {
  vaddr_t vaddr_rel = vaddr - vaddr_base;
  vaddr_t vaddr_rel_max = 1UL << top_size_shift_;

  LTRACEF("vaddr 0x%lx, size 0x%lx, asid 0x%x\n", vaddr, size, asid_);

  if (vaddr_rel > vaddr_rel_max - size || size > vaddr_rel_max) {
    TRACEF("vaddr 0x%lx, size 0x%lx out of range vaddr 0x%lx, size 0x%lx\n", vaddr, size,
           vaddr_base, vaddr_rel_max);
    return ZX_ERR_INVALID_ARGS;
  }

  LOCAL_KTRACE("mmu unmap", (vaddr & ~PAGE_MASK) | ((size >> PAGE_SIZE_SHIFT) & PAGE_MASK));

  ssize_t ret = UnmapPageTable(vaddr, vaddr_rel, size, enlarge, top_index_shift_, tt_virt_, cm);
  return ret;
}

zx_status_t ArmArchVmAspace::ProtectPages(vaddr_t vaddr, size_t size, pte_t attrs,
                                          vaddr_t vaddr_base, ConsistencyManager& cm) {
  vaddr_t vaddr_rel = vaddr - vaddr_base;
  vaddr_t vaddr_rel_max = 1UL << top_size_shift_;

  LTRACEF("vaddr %#" PRIxPTR ", size %#" PRIxPTR ", attrs %#" PRIx64 ", asid %#x\n", vaddr, size,
          attrs, asid_);

  if (vaddr_rel > vaddr_rel_max - size || size > vaddr_rel_max) {
    TRACEF("vaddr %#" PRIxPTR ", size %#" PRIxPTR " out of range vaddr %#" PRIxPTR
           ", size %#" PRIxPTR "\n",
           vaddr, size, vaddr_base, vaddr_rel_max);
    return ZX_ERR_INVALID_ARGS;
  }

  LOCAL_KTRACE("mmu protect", (vaddr & ~PAGE_MASK) | ((size >> PAGE_SIZE_SHIFT) & PAGE_MASK));

  zx_status_t ret = ProtectPageTable(vaddr, vaddr_rel, size, attrs, top_index_shift_, tt_virt_, cm);
  return ret;
}

pte_t ArmArchVmAspace::MmuParamsFromFlags(uint mmu_flags) {
  pte_t attrs = 0;
  switch (type_) {
    case ArmAspaceType::kUser:
      attrs = mmu_flags_to_s1_pte_attr(mmu_flags);
      // User pages are marked non global
      attrs |= MMU_PTE_ATTR_NON_GLOBAL;
      break;
    case ArmAspaceType::kKernel:
      attrs = mmu_flags_to_s1_pte_attr(mmu_flags);
      break;
    case ArmAspaceType::kGuest:
      attrs = mmu_flags_to_s2_pte_attr(mmu_flags);
      break;
    case ArmAspaceType::kHypervisor:
      attrs = mmu_flags_to_s1_pte_attr(mmu_flags, true);
      break;
  }
  return attrs;
}

zx_status_t ArmArchVmAspace::MapContiguous(vaddr_t vaddr, paddr_t paddr, size_t count,
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
    Guard<CriticalMutex> a{&lock_};
    ASSERT(updates_enabled_);
    if (mmu_flags & ARCH_MMU_FLAG_PERM_EXECUTE) {
      ArmVmICacheConsistencyManager cache_cm;
      cache_cm.SyncAddr(reinterpret_cast<vaddr_t>(paddr_to_physmap(paddr)), count * PAGE_SIZE);
    }
    pte_t attrs = MmuParamsFromFlags(mmu_flags);

    ConsistencyManager cm(*this);
    ret = MapPages(vaddr, paddr, count * PAGE_SIZE, attrs, vaddr_base_, cm);
    MarkAspaceModified();
  }

  if (mapped) {
    *mapped = (ret > 0) ? (ret / PAGE_SIZE) : 0u;
    DEBUG_ASSERT(*mapped <= count);
  }

#if __has_feature(address_sanitizer)
  if (ret >= 0 && type_ == ArmAspaceType::kKernel) {
    asan_map_shadow_for(vaddr, ret);
  }
#endif  // __has_feature(address_sanitizer)

  return (ret < 0) ? (zx_status_t)ret : ZX_OK;
}

zx_status_t ArmArchVmAspace::Map(vaddr_t vaddr, paddr_t* phys, size_t count, uint mmu_flags,
                                 ExistingEntryAction existing_action, size_t* mapped) {
  canary_.Assert();
  LTRACEF("vaddr %#" PRIxPTR " count %zu flags %#x\n", vaddr, count, mmu_flags);

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
    Guard<CriticalMutex> a{&lock_};
    ASSERT(updates_enabled_);
    if (mmu_flags & ARCH_MMU_FLAG_PERM_EXECUTE) {
      ArmVmICacheConsistencyManager cache_cm;
      for (size_t idx = 0; idx < count; ++idx) {
        cache_cm.SyncAddr(reinterpret_cast<vaddr_t>(paddr_to_physmap(phys[idx])), PAGE_SIZE);
      }
    }
    pte_t attrs = MmuParamsFromFlags(mmu_flags);

    ssize_t ret;
    size_t idx = 0;
    ConsistencyManager cm(*this);
    auto undo = fit::defer([&]() TA_NO_THREAD_SAFETY_ANALYSIS {
      if (idx > 0) {
        UnmapPages(vaddr, idx * PAGE_SIZE, EnlargeOperation::No, vaddr_base_, cm);
      }
    });

    vaddr_t v = vaddr;
    for (; idx < count; ++idx) {
      paddr_t paddr = phys[idx];
      DEBUG_ASSERT(IS_PAGE_ALIGNED(paddr));
      ret = MapPages(v, paddr, PAGE_SIZE, attrs, vaddr_base_, cm);
      MarkAspaceModified();
      if (ret < 0) {
        zx_status_t status = static_cast<zx_status_t>(ret);
        if (status != ZX_ERR_ALREADY_EXISTS || existing_action == ExistingEntryAction::Error) {
          return status;
        }
      }

      v += PAGE_SIZE;
      if (ret > 0) {
        total_mapped += ret / PAGE_SIZE;
      }
    }
    undo.cancel();
  }
  DEBUG_ASSERT(total_mapped <= count);
  DEBUG_ASSERT(existing_action != ExistingEntryAction::Error || total_mapped == count);

  if (mapped) {
    // For ExistingEntryAction::Error, we should have mapped all the addresses we were asked to.
    // For ExistingEntryAction::Skip, we might have mapped less if we encountered existing entries,
    // but skipped entries contribute towards the total as well.
    *mapped = count;
  }

#if __has_feature(address_sanitizer)
  if (type_ == ArmAspaceType::kKernel) {
    asan_map_shadow_for(vaddr, total_mapped * PAGE_SIZE);
  }
#endif  // __has_feature(address_sanitizer)

  return ZX_OK;
}

zx_status_t ArmArchVmAspace::Unmap(vaddr_t vaddr, size_t count, EnlargeOperation enlarge,
                                   size_t* unmapped) {
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

  Guard<CriticalMutex> a{&lock_};

  ASSERT(updates_enabled_);
  ssize_t ret;
  {
    ConsistencyManager cm(*this);
    ret = UnmapPages(vaddr, count * PAGE_SIZE, enlarge, vaddr_base_, cm);
    MarkAspaceModified();
  }

  if (unmapped) {
    *unmapped = (ret > 0) ? (ret / PAGE_SIZE) : 0u;
    DEBUG_ASSERT(*unmapped <= count);
  }

  return (ret < 0) ? (zx_status_t)ret : 0;
}

zx_status_t ArmArchVmAspace::Protect(vaddr_t vaddr, size_t count, uint mmu_flags) {
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

  Guard<CriticalMutex> a{&lock_};
  ASSERT(updates_enabled_);
  if (mmu_flags & ARCH_MMU_FLAG_PERM_EXECUTE) {
    // If mappings are going to become executable then we first need to sync their caches.
    // Unfortunately this needs to be done on kernel virtual addresses to avoid taking translation
    // faults, and so we need to first query for the physical address to then get the kernel virtual
    // address in the physmap.
    // This sync could be more deeply integrated into ProtectPages, but making existing regions
    // executable is very uncommon operation and so we keep it simple.
    vm_mmu_protect_make_execute_calls.Add(1);
    ArmVmICacheConsistencyManager cache_cm;
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
    pte_t attrs = MmuParamsFromFlags(mmu_flags);

    ConsistencyManager cm(*this);
    ret = ProtectPages(vaddr, count * PAGE_SIZE, attrs, vaddr_base_, cm);
    MarkAspaceModified();
  }

  return ret;
}

zx_status_t ArmArchVmAspace::HarvestAccessed(vaddr_t vaddr, size_t count,
                                             NonTerminalAction non_terminal_action,
                                             TerminalAction terminal_action) {
  VM_KTRACE_DURATION(2, "ArmArchVmAspace::HarvestAccessed", vaddr, count);
  canary_.Assert();

  if (!IS_PAGE_ALIGNED(vaddr) || !IsValidVaddr(vaddr)) {
    return ZX_ERR_INVALID_ARGS;
  }

  // Avoid preemption while "involuntarily" holding the arch aspace lock during
  // access harvesting. The harvest loop below is O(n), however, the amount of
  // work performed with the lock held and preemption disabled is limited. Other
  // O(n) operations under this lock are opt-in by the user (e.g. Map, Protect)
  // and are performed with preemption enabled.
  Guard<CriticalMutex> guard{&lock_};

  const vaddr_t vaddr_rel = vaddr - vaddr_base_;
  const vaddr_t vaddr_rel_max = 1UL << top_size_shift_;
  const size_t size = count * PAGE_SIZE;

  if (vaddr_rel > vaddr_rel_max - size || size > vaddr_rel_max) {
    TRACEF("vaddr %#" PRIxPTR ", size %#" PRIxPTR " out of range vaddr %#" PRIxPTR
           ", size %#" PRIxPTR "\n",
           vaddr, size, vaddr_base_, vaddr_rel_max);
    return ZX_ERR_INVALID_ARGS;
  }

  LOCAL_KTRACE("mmu harvest accessed",
               (vaddr & ~PAGE_MASK) | ((size >> PAGE_SIZE_SHIFT) & PAGE_MASK));

  // Limit harvesting to 32 entries per iteration with the arch aspace lock held
  // to avoid delays in accessed faults in the same aspace running in parallel.
  //
  // This limit is derived from the following observations:
  // 1. Worst case runtime to harvest a terminal PTE on a low-end A53 is ~780ns.
  // 2. Real workloads can result in harvesting thousands of terminal PTEs in a
  //    single aspace.
  // 3. An access fault handler will spin up to 150us on the aspace adaptive
  //    mutex before blocking.
  // 4. Unnecessarily blocking is costly when the system is heavily loaded,
  //    especially during accessed faults, which tend to occur multiple times in
  //    quick succession within and across threads in the same process.
  //
  // To achieve optimal contention between access harvesting and access faults,
  // it is important to avoid exhausting the 150us mutex spin phase by holding
  // the aspace mutex for too long. The selected entry limit results in a worst
  // case harvest time of about 1/6 of the mutex spin phase.
  //
  //   Ti = worst case runtime per top-level harvest iteration.
  //   Te = worst case runtime per terminal entry harvest.
  //   L  = max entries per top-level harvest iteration.
  //
  //   Ti = Te * L = 780ns * 32 = 24.96us
  //
  const size_t kMaxEntriesPerIteration = 32;

  ConsistencyManager cm(*this);
  size_t remaining_size = size;
  vaddr_t current_vaddr = vaddr;
  vaddr_t current_vaddr_rel = vaddr_rel;

  while (remaining_size) {
    LocalTraceDuration trace{"harvest_loop"_stringref};
    size_t entry_limit = kMaxEntriesPerIteration;
    const size_t harvested_size = HarvestAccessedPageTable(
        &entry_limit, current_vaddr, current_vaddr_rel, remaining_size, top_index_shift_,
        non_terminal_action, terminal_action, tt_virt_, cm, nullptr);
    DEBUG_ASSERT(harvested_size > 0);
    DEBUG_ASSERT(harvested_size <= remaining_size);

    remaining_size -= harvested_size;
    current_vaddr += harvested_size;
    current_vaddr_rel += harvested_size;

    // Release and re-acquire the lock to let contending threads have a chance
    // to acquire the arch aspace lock between iterations. Use arch::Yield() to
    // give other CPUs spinning on the aspace mutex a slight edge in acquiring
    // the mutex. Reenable preemption to flush any pending preemptions that may
    // have pended during the critical section.
    guard.CallUnlocked([this] {
      while (pending_access_faults_.load() != 0) {
        arch::Yield();
      }
    });
  }

  return ZX_OK;
}

zx_status_t ArmArchVmAspace::MarkAccessed(vaddr_t vaddr, size_t count) {
  VM_KTRACE_DURATION(2, "ArmArchVmAspace::MarkAccessed", vaddr, count);
  canary_.Assert();

  if (!IS_PAGE_ALIGNED(vaddr) || !IsValidVaddr(vaddr)) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  AutoPendingAccessFault pending_access_fault{this};
  Guard<CriticalMutex> a{&lock_};

  const vaddr_t vaddr_rel = vaddr - vaddr_base_;
  const vaddr_t vaddr_rel_max = 1UL << top_size_shift_;
  const size_t size = count * PAGE_SIZE;

  if (vaddr_rel > vaddr_rel_max - size || size > vaddr_rel_max) {
    TRACEF("vaddr %#" PRIxPTR ", size %#" PRIxPTR " out of range vaddr %#" PRIxPTR
           ", size %#" PRIxPTR "\n",
           vaddr, size, vaddr_base_, vaddr_rel_max);
    return ZX_ERR_OUT_OF_RANGE;
  }

  LOCAL_KTRACE("mmu mark accessed", (vaddr & ~PAGE_MASK) | ((size >> PAGE_SIZE_SHIFT) & PAGE_MASK));

  ConsistencyManager cm(*this);

  MarkAccessedPageTable(vaddr, vaddr_rel, size, top_index_shift_, tt_virt_, cm);
  MarkAspaceModified();

  return ZX_OK;
}

bool ArmArchVmAspace::ActiveSinceLastCheck(bool clear) {
  // Read whether any CPUs are presently executing.
  bool currently_active = num_active_cpus_.load(ktl::memory_order_relaxed) != 0;
  // Exchange the current notion of active, with the previously active information. This is the only
  // time a |false| value can potentially be written to active_since_last_check_, and doing an
  // exchange means we can never 'lose' a |true| value.
  bool previously_active =
      clear ? active_since_last_check_.exchange(currently_active, ktl::memory_order_relaxed)
            : active_since_last_check_.load(ktl::memory_order_relaxed);
  // Return whether we had previously been active. It is not necessary to also consider whether we
  // are currently active, since activating would also have active_since_last_check_ to true. In the
  // scenario where we race and currently_active is true, but we observe previously_active to be
  // false, this means that as of the start of this function ::ContextSwitch had not completed, and
  // so this aspace is still not actually active.
  return previously_active;
}

zx_status_t ArmArchVmAspace::Init() {
  canary_.Assert();
  LTRACEF("aspace %p, base %#" PRIxPTR ", size 0x%zx, type %*s\n", this, base_, size_,
          static_cast<int>(ArmAspaceTypeName(type_).size()), ArmAspaceTypeName(type_).data());

  Guard<CriticalMutex> a{&lock_};

  // Validate that the base + size is sane and doesn't wrap.
  DEBUG_ASSERT(size_ > PAGE_SIZE);
  DEBUG_ASSERT(base_ + size_ - 1 > base_);

  if (type_ == ArmAspaceType::kKernel) {
    // At the moment we can only deal with address spaces as globally defined.
    DEBUG_ASSERT(base_ == ~0UL << MMU_KERNEL_SIZE_SHIFT);
    DEBUG_ASSERT(size_ == 1UL << MMU_KERNEL_SIZE_SHIFT);

    vaddr_base_ = ~0UL << MMU_KERNEL_SIZE_SHIFT;
    top_size_shift_ = MMU_KERNEL_SIZE_SHIFT;
    top_index_shift_ = MMU_KERNEL_TOP_SHIFT;
    page_size_shift_ = MMU_KERNEL_PAGE_SIZE_SHIFT;

    tt_virt_ = arm64_kernel_translation_table;
    tt_phys_ = arm64_kernel_translation_table_phys;
    asid_ = (uint16_t)MMU_ARM64_GLOBAL_ASID;
  } else {
    if (type_ == ArmAspaceType::kUser) {
      DEBUG_ASSERT(base_ + size_ <= 1UL << MMU_USER_SIZE_SHIFT);

      vaddr_base_ = 0;
      top_size_shift_ = MMU_USER_SIZE_SHIFT;
      top_index_shift_ = MMU_USER_TOP_SHIFT;
      page_size_shift_ = MMU_USER_PAGE_SIZE_SHIFT;

      auto status = asid->Alloc();
      if (status.is_error()) {
        printf("ARM: out of ASIDs!\n");
        return status.status_value();
      }
      asid_ = status.value();
    } else if (type_ == ArmAspaceType::kGuest) {
      DEBUG_ASSERT(base_ + size_ <= 1UL << MMU_GUEST_SIZE_SHIFT);

      vaddr_base_ = 0;
      top_size_shift_ = MMU_GUEST_SIZE_SHIFT;
      top_index_shift_ = MMU_GUEST_TOP_SHIFT;
      page_size_shift_ = MMU_GUEST_PAGE_SIZE_SHIFT;
    } else {
      DEBUG_ASSERT(type_ == ArmAspaceType::kHypervisor);
      DEBUG_ASSERT(base_ + size_ <= 1UL << MMU_IDENT_SIZE_SHIFT);

      vaddr_base_ = 0;
      top_size_shift_ = MMU_IDENT_SIZE_SHIFT;
      top_index_shift_ = MMU_IDENT_TOP_SHIFT;
      page_size_shift_ = MMU_IDENT_PAGE_SIZE_SHIFT;
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

    // zero the top level translation table.
    arch_zero_page(const_cast<pte_t*>(tt_virt_));
    __dsb(ARM_MB_ISHST);
  }
  pt_pages_ = 1;
  kcounter_add(vm_mmu_page_table_alloc, 1);

  LTRACEF("tt_phys %#" PRIxPTR " tt_virt %p\n", tt_phys_, tt_virt_);

  return ZX_OK;
}

zx_status_t ArmArchVmAspace::DebugFindFirstLeafMapping(vaddr_t* out_pt, vaddr_t* out_vaddr,
                                                       pte_t* out_pte) const {
  canary_.Assert();

  DEBUG_ASSERT(tt_virt_);
  DEBUG_ASSERT(out_vaddr);
  DEBUG_ASSERT(out_pte);

  const unsigned int count = 1U << (page_size_shift_ - 3);
  const volatile pte_t* page_table = tt_virt_;
  uint32_t index_shift = top_index_shift_;
  vaddr_t vaddr = 0;
  while (true) {
    uint64_t index = 0;
    pte_t pte;
    // Walk the page table until we find an entry.
    for (index = 0; index < count; index++) {
      pte = page_table[index];
      if (pte != MMU_PTE_DESCRIPTOR_INVALID) {
        break;
      }
    }
    if (index == count) {
      return ZX_ERR_NOT_FOUND;
    }
    // Update the virtual address for the index at the current level.
    vaddr += (index << index_shift);

    const uint descriptor_type = pte & MMU_PTE_DESCRIPTOR_MASK;
    const paddr_t pte_addr = pte & MMU_PTE_OUTPUT_ADDR_MASK;

    // If we have found a leaf mapping, return it.
    if (descriptor_type == ((index_shift > page_size_shift_) ? MMU_PTE_L012_DESCRIPTOR_BLOCK
                                                             : MMU_PTE_L3_DESCRIPTOR_PAGE)) {
      *out_vaddr = vaddr;
      *out_pte = pte;
      *out_pt = reinterpret_cast<vaddr_t>(page_table);
      return ZX_OK;
    }

    // Assume this entry could be corrupted and validate the next table address is valid, and return
    // graceful errors on invalid descriptor types.
    if (!is_physmap_phys_addr(pte_addr) || index_shift <= page_size_shift_ ||
        descriptor_type != MMU_PTE_L012_DESCRIPTOR_TABLE) {
      *out_vaddr = vaddr;
      *out_pte = pte;
      *out_pt = reinterpret_cast<vaddr_t>(page_table);
      return ZX_ERR_BAD_STATE;
    }

    page_table = static_cast<const volatile pte_t*>(paddr_to_physmap(pte_addr));
    index_shift -= page_size_shift_ - 3;
  }
}

void ArmArchVmAspace::AssertEmptyLocked() const {
  // Check to see if the top level page table is empty. If not the user didn't
  // properly unmap everything before destroying the aspace
  if (const int index = first_used_page_table_entry(tt_virt_, page_size_shift_); index != -1) {
    vaddr_t pt_addr = 0;
    vaddr_t entry_vaddr = 0;
    pte_t pte = 0;
    // Attempt to walk the page table and find the first leaf most mapping that we can. This
    // represents (at least one of) the entries that is holding this page table alive.
    //
    // TODO(fxbug.dev/79118): Once fxbug.dev/79118 is resolved this call, and the entire called
    // method, can be removed.
    zx_status_t status = DebugFindFirstLeafMapping(&pt_addr, &entry_vaddr, &pte);
    panic(
        "top level page table still in use! aspace %p pt_pages_ %zu tt_virt %p index %d entry "
        "%" PRIx64 ". Leaf query status %d pt_addr %zu vaddr %zu entry %" PRIx64 "\n",
        this, pt_pages_, tt_virt_, index, tt_virt_[index], status, pt_addr, entry_vaddr, pte);
  }

  if (pt_pages_ != 1) {
    panic("allocated page table count is wrong, aspace %p count %zu (should be 1)\n", this,
          pt_pages_);
  }
}

void ArmArchVmAspace::DisableUpdates() {
  canary_.Assert();

  Guard<CriticalMutex> a{&lock_};
  updates_enabled_ = false;
  AssertEmptyLocked();
}

zx_status_t ArmArchVmAspace::Destroy() {
  canary_.Assert();
  LTRACEF("aspace %p\n", this);

  Guard<CriticalMutex> a{&lock_};

  // Not okay to destroy the kernel address space
  DEBUG_ASSERT(type_ != ArmAspaceType::kKernel);

  if (!tt_virt_) {
    // Initialization must not have succeeded.
    DEBUG_ASSERT(!tt_phys_);
    return ZX_OK;
  }

  AssertEmptyLocked();

  // Need a DSB to synchronize any page table updates prior to flushing the TLBs.
  __dsb(ARM_MB_ISH);

  // Flush the ASID or VMID associated with this aspace
  FlushAsid();

  // Need a DSB to ensure all other cpus have fully processed the TLB flush.
  __dsb(ARM_MB_ISH);

  // Free any ASID.
  if (type_ == ArmAspaceType::kUser) {
    auto status = asid->Free(asid_);
    ASSERT(status.is_ok());
    asid_ = MMU_ARM64_UNUSED_ASID;
  }

  // Free the top level page table.
  vm_page_t* page = paddr_to_vm_page(tt_phys_);
  DEBUG_ASSERT(page);
  CacheFreePage(page);
  pt_pages_--;
  kcounter_add(vm_mmu_page_table_free, 1);

  tt_phys_ = 0;
  tt_virt_ = nullptr;

  return ZX_OK;
}

// Called during context switches between threads with different address spaces. Swaps the
// mmu context on hardware. Assumes old_aspace != aspace and optimizes as such.
void ArmArchVmAspace::ContextSwitch(ArmArchVmAspace* old_aspace, ArmArchVmAspace* aspace) {
  uint64_t tcr;
  uint64_t ttbr;
  if (likely(aspace)) {
    aspace->canary_.Assert();
    DEBUG_ASSERT(aspace->type_ == ArmAspaceType::kUser);

    // Load the user space TTBR with the translation table and user space ASID.
    ttbr = ((uint64_t)aspace->asid_ << 48) | aspace->tt_phys_;
    __arm_wsr64("ttbr0_el1", ttbr);
    __isb(ARM_MB_SY);

    // If we're switching away from the kernel aspace, load TCR with the user flags.
    tcr = MMU_TCR_FLAGS_USER;
    if (unlikely(!old_aspace)) {
      __arm_wsr64("tcr_el1", tcr);
      __isb(ARM_MB_SY);
    } else {
      __UNUSED uint32_t prev = old_aspace->num_active_cpus_.fetch_sub(1, ktl::memory_order_relaxed);
      DEBUG_ASSERT(prev > 0);
    }
    __UNUSED uint32_t prev = aspace->num_active_cpus_.fetch_add(1, ktl::memory_order_relaxed);
    DEBUG_ASSERT(prev < SMP_MAX_CPUS);
    aspace->active_since_last_check_.store(true, ktl::memory_order_relaxed);

  } else {
    // Switching to the null aspace, which means kernel address space only.
    // Load a null TTBR0 and disable page table walking for user space.
    tcr = MMU_TCR_FLAGS_KERNEL;
    __arm_wsr64("tcr_el1", tcr);
    __isb(ARM_MB_SY);

    ttbr = 0;  // MMU_ARM64_UNUSED_ASID
    __arm_wsr64("ttbr0_el1", ttbr);
    __isb(ARM_MB_SY);

    if (likely(old_aspace != nullptr)) {
      __UNUSED uint32_t prev = old_aspace->num_active_cpus_.fetch_sub(1, ktl::memory_order_relaxed);
      DEBUG_ASSERT(prev > 0);
    }
  }
  if (TRACE_CONTEXT_SWITCH) {
    TRACEF("old aspace %p aspace %p ttbr %#" PRIx64 ", tcr %#" PRIx64 "\n", old_aspace, aspace,
           ttbr, tcr);
  }
}

void arch_zero_page(void* _ptr) {
  uintptr_t ptr = (uintptr_t)_ptr;

  uint32_t zva_size = arm64_zva_size;
  uintptr_t end_ptr = ptr + PAGE_SIZE;
  do {
    __asm volatile("dc zva, %0" ::"r"(ptr));
    ptr += zva_size;
  } while (ptr != end_ptr);
}

zx_status_t arm64_mmu_translate(vaddr_t va, paddr_t* pa, bool user, bool write) {
  // disable interrupts around this operation to make the at/par instruction combination atomic
  uint64_t par;
  {
    InterruptDisableGuard irqd;

    if (user) {
      if (write) {
        __asm__ volatile("at s1e0w, %0" ::"r"(va) : "memory");
      } else {
        __asm__ volatile("at s1e0r, %0" ::"r"(va) : "memory");
      }
    } else {
      if (write) {
        __asm__ volatile("at s1e1w, %0" ::"r"(va) : "memory");
      } else {
        __asm__ volatile("at s1e1r, %0" ::"r"(va) : "memory");
      }
    }

    par = __arm_rsr64("par_el1");
  }

  // if bit 0 is clear, the translation succeeded
  if (BIT(par, 0)) {
    return ZX_ERR_NOT_FOUND;
  }

  // physical address is stored in bits [51..12], naturally aligned
  *pa = BITS(par, 51, 12) | (va & (PAGE_SIZE - 1));

  return ZX_OK;
}

ArmArchVmAspace::ArmArchVmAspace(vaddr_t base, size_t size, ArmAspaceType type, page_alloc_fn_t paf)
    : test_page_alloc_func_(paf), type_(type), base_(base), size_(size) {}

ArmArchVmAspace::ArmArchVmAspace(vaddr_t base, size_t size, uint mmu_flags, page_alloc_fn_t paf)
    : ArmArchVmAspace(base, size, AspaceTypeFromFlags(mmu_flags), paf) {}

ArmArchVmAspace::~ArmArchVmAspace() {
  // Destroy() will have freed the final page table if it ran correctly, and further validated that
  // everything else was freed.
  DEBUG_ASSERT(pt_pages_ == 0);
}

vaddr_t ArmArchVmAspace::PickSpot(vaddr_t base, vaddr_t end, vaddr_t align, size_t size,
                                  uint mmu_flags) {
  canary_.Assert();
  return PAGE_ALIGN(base);
}

void ArmVmICacheConsistencyManager::SyncAddr(vaddr_t start, size_t len) {
  // Validate we are operating on a kernel address range.
  DEBUG_ASSERT(is_kernel_address(start));
  // use the physmap to clean the range to PoU, which is the point of where the instruction cache
  // pulls from. Cleaning to PoU is potentially cheaper than cleaning to PoC, which is the default
  // of arch_clean_cache_range.
  arm64_clean_cache_range_pou(start, len);
  // We can batch the icache invalidate and just perform it once at the end.
  need_invalidate_ = true;
}
void ArmVmICacheConsistencyManager::Finish() {
  if (!need_invalidate_) {
    return;
  }
  // Under the assumption our icache is VIPT then as we do not know all the virtual aliases of the
  // sections we cleaned our only option is to dump the entire icache.
  asm volatile("ic ialluis" ::: "memory");
  __isb(ARM_MB_SY);
  need_invalidate_ = false;
}

void arm64_mmu_early_init() {
  // after we've probed the feature set, initialize the asid allocator
  asid.Initialize();
}

uint32_t arch_address_tagging_features() {
  static_assert(MMU_TCR_FLAGS_USER & MMU_TCR_TBI0, "Expected TBI enabled.");
  return ZX_ARM64_FEATURE_ADDRESS_TAGGING_TBI;
}
