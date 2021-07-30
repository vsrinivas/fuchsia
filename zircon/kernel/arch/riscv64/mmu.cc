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
#include <lib/heap.h>
#include <lib/ktrace.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <trace.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <arch/aspace.h>
#include <bitmap/raw-bitmap.h>
#include <bitmap/storage.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <kernel/mutex.h>
#include <ktl/algorithm.h>
#include <vm/arch_vm_aspace.h>
#include <vm/physmap.h>
#include <vm/pmm.h>
#include <vm/vm.h>
#include <arch/riscv64/sbi.h>

#define LOCAL_TRACE 0
#define TRACE_CONTEXT_SWITCH 1

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

// The main translation table.
pte_t riscv64_kernel_translation_table[RISCV64_MMU_PT_ENTRIES] __ALIGNED(PAGE_SIZE);

pte_t* riscv64_get_kernel_ptable() { return riscv64_kernel_translation_table; }

namespace {

class AsidAllocator {
 public:
  AsidAllocator() { bitmap_.Reset(MMU_RISCV64_MAX_USER_ASID + 1); }
  ~AsidAllocator() = default;

  zx_status_t Alloc(uint16_t* asid);
  zx_status_t Free(uint16_t asid);

 private:
  DISALLOW_COPY_ASSIGN_AND_MOVE(AsidAllocator);

  DECLARE_MUTEX(AsidAllocator) lock_;
  uint16_t last_ TA_GUARDED(lock_) = MMU_RISCV64_FIRST_USER_ASID - 1;

  bitmap::RawBitmapGeneric<bitmap::FixedStorage<MMU_RISCV64_MAX_USER_ASID + 1>> bitmap_
      TA_GUARDED(lock_);

  static_assert(MMU_RISCV64_ASID_BITS <= 16, "");
};

zx_status_t AsidAllocator::Alloc(uint16_t* asid) {
  uint16_t new_asid;

  // use the bitmap allocator to allocate ids in the range of
  // [MMU_RISCV64_FIRST_USER_ASID, MMU_RISCV64_MAX_USER_ASID]
  // start the search from the last found id + 1 and wrap when hitting the end of the range
  {
    Guard<Mutex> al{&lock_};

    size_t val;
    bool notfound = bitmap_.Get(last_ + 1, MMU_RISCV64_MAX_USER_ASID + 1, &val);
    if (unlikely(notfound)) {
      // search again from the start
      notfound = bitmap_.Get(MMU_RISCV64_FIRST_USER_ASID, MMU_RISCV64_MAX_USER_ASID + 1, &val);
      if (unlikely(notfound)) {
        TRACEF("RISCV64: out of ASIDs\n");
        return ZX_ERR_NO_MEMORY;
      }
    }
    bitmap_.SetOne(val);

    DEBUG_ASSERT(val <= UINT16_MAX);

    new_asid = (uint16_t)val;
    last_ = new_asid;
  }

  LTRACEF("new asid %#x\n", new_asid);

  *asid = new_asid;

  return ZX_OK;
}

zx_status_t AsidAllocator::Free(uint16_t asid) {
  LTRACEF("free asid %#x\n", asid);

  Guard<Mutex> al{&lock_};

  bitmap_.ClearOne(asid);

  return ZX_OK;
}

AsidAllocator asid;

}  // namespace

// given a va address and the level, compute the index in the current PT
static inline uint vaddr_to_index(vaddr_t va, uint level) {
  // levels count down from PT_LEVELS - 1
  DEBUG_ASSERT(level < RISCV64_MMU_PT_LEVELS);

  // canonicalize the address
  va &= RISCV64_MMU_CANONICAL_MASK;

  uint index = ((va >> PAGE_SIZE_SHIFT) >> (level * RISCV64_MMU_PT_SHIFT)) & (RISCV64_MMU_PT_ENTRIES - 1);
  LTRACEF_LEVEL(3, "canonical va %#lx, level %u = index %#x\n", va, level, index);

  return index;
}

static uintptr_t page_size_per_level(uint level) {
  // levels count down from PT_LEVELS - 1
  DEBUG_ASSERT(level < RISCV64_MMU_PT_LEVELS);

  return 1UL << (PAGE_SIZE_SHIFT + level * RISCV64_MMU_PT_SHIFT);
}

static uintptr_t page_mask_per_level(uint level) {
  return page_size_per_level(level) - 1;
}

// Convert user level mmu flags to flags that go in L1 descriptors.
static pte_t mmu_flags_to_pte_attr(uint flags) {
  pte_t attr = RISCV64_PTE_V;

  if ((flags & ARCH_MMU_FLAG_CACHED) ||
      (flags & ARCH_MMU_FLAG_WRITE_COMBINING) ||
      (flags & ARCH_MMU_FLAG_UNCACHED) ||
      (flags & ARCH_MMU_FLAG_UNCACHED_DEVICE) ||
      (flags & ARCH_MMU_FLAG_NS)) {
    PANIC_UNIMPLEMENTED;
  }

  attr |= (flags & ARCH_MMU_FLAG_PERM_USER) ? RISCV64_PTE_U : 0;
  attr |= (flags & ARCH_MMU_FLAG_PERM_READ) ? RISCV64_PTE_R : 0;
  attr |= (flags & ARCH_MMU_FLAG_PERM_WRITE) ? RISCV64_PTE_W : 0;
  attr |= (flags & ARCH_MMU_FLAG_PERM_EXECUTE) ? RISCV64_PTE_X : 0;

  return attr;
}

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
  ulong index;
  uint level = RISCV64_MMU_PT_LEVELS - 1;
  pte_t pte;
  pte_t pte_addr;
  volatile pte_t* page_table;

  canary_.Assert();
  LTRACEF("aspace %p, vaddr 0x%lx\n", this, vaddr);

  DEBUG_ASSERT(tt_virt_);

  DEBUG_ASSERT(IsValidVaddr(vaddr));
  if (!IsValidVaddr(vaddr)) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  page_table = tt_virt_;

  while (true) {
    index = vaddr_to_index(vaddr, level);
    pte = page_table[index];
    pte_addr = RISCV64_PTE_PPN(pte);

    LTRACEF("va %#" PRIxPTR ", index %lu, level %u, pte %#" PRIx64 "\n",
            vaddr, index, level, pte);

    if (!(pte & RISCV64_PTE_V)) {
      return ZX_ERR_NOT_FOUND;
    }

    if (pte & RISCV64_PTE_PERM_MASK) {
      break;
    }

    page_table = static_cast<volatile pte_t*>(paddr_to_physmap(pte_addr));
    level--;
  }

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

  page->set_state(VM_PAGE_STATE_MMU);
  pt_pages_++;

  LOCAL_KTRACE("page table alloc");

  LTRACEF("allocated 0x%lx\n", *paddrp);
   return ZX_OK;
 }

void Riscv64ArchVmAspace::FreePageTable(void* vaddr, paddr_t paddr) {
  LTRACEF("vaddr %p paddr 0x%lx\n", vaddr, paddr);

  vm_page_t* page;

  LOCAL_KTRACE("page table free");

  page = paddr_to_vm_page(paddr);
  if (!page) {
    panic("bad page table paddr 0x%lx\n", paddr);
  }
  pmm_free_page(page);

  pt_pages_--;
}

volatile pte_t* Riscv64ArchVmAspace::GetPageTable(vaddr_t pt_index,
                                                  volatile pte_t* page_table) {
  pte_t pte;
  paddr_t paddr;
  void* vaddr;

  pte = page_table[pt_index];
  if (!(pte & RISCV64_PTE_V)) {
    zx_status_t ret = AllocPageTable(&paddr);
    if (ret) {
      TRACEF("failed to allocate page table\n");
      return NULL;
    }
    vaddr = paddr_to_physmap(paddr);

    LTRACEF("allocated page table, vaddr %p, paddr 0x%lx\n", vaddr, paddr);
    memset(vaddr, 0, PAGE_SIZE);

    // ensure that the zeroing is observable from hardware page table walkers
    mb();

    pte = RISCV64_PTE_PPN_TO_PTE(paddr) | RISCV64_PTE_V;
    page_table[pt_index] = pte;
    LTRACEF("pte %p[%#" PRIxPTR "] = %#" PRIx64 "\n", page_table, pt_index, pte);
    return static_cast<volatile pte_t*>(vaddr);
  } else if (!(pte & RISCV64_PTE_PERM_MASK)) {
    paddr = RISCV64_PTE_PPN(pte);
    LTRACEF("found page table %#" PRIxPTR "\n", paddr);
    return static_cast<volatile pte_t*>(paddr_to_physmap(paddr));
  } else {
    return NULL;
  }
}

zx_status_t Riscv64ArchVmAspace::SplitLargePage(vaddr_t vaddr, uint level,
                                              vaddr_t pt_index, volatile pte_t* page_table) {
  const pte_t pte = page_table[pt_index];
  DEBUG_ASSERT(!(pte & RISCV64_PTE_PERM_MASK));

  paddr_t paddr;
  zx_status_t ret = AllocPageTable(&paddr);
  if (ret) {
    TRACEF("failed to allocate page table\n");
    return ret;
  }

  const auto new_page_table = static_cast<volatile pte_t*>(paddr_to_physmap(paddr));
  const auto attrs = pte & RISCV64_PTE_PERM_MASK;

  const size_t next_size = page_size_per_level(level - 1);
  for (uint64_t i = 0, mapped_paddr = RISCV64_PTE_PPN(pte);
       i < RISCV64_MMU_PT_ENTRIES; i++, mapped_paddr += next_size) {
    new_page_table[i] = RISCV64_PTE_PPN_TO_PTE(mapped_paddr) | attrs;
  }

  // Ensure all zeroing becomes visible prior to page table installation.
  mb();

  page_table[pt_index] = RISCV64_PTE_PPN_TO_PTE(paddr);
  LTRACEF("pte %p[%#" PRIxPTR "] = %#" PRIx64 "\n", page_table, pt_index, page_table[pt_index]);

  // ensure that the update is observable from hardware page table walkers before TLB
  // operations can occur.
  mb();

  FlushTLBEntry(vaddr, false);

  return ZX_OK;
}

static bool page_table_is_clear(volatile pte_t* page_table) {
  int i;
  int count = 1U << (PAGE_SIZE_SHIFT - 3);
  pte_t pte;

  for (i = 0; i < count; i++) {
    pte = page_table[i];
    if (pte & RISCV64_PTE_V) {
      LTRACEF("page_table at %p still in use, index %d is %#" PRIx64 "\n", page_table, i, pte);
      return false;
    }
  }

  LTRACEF("page table at %p is clear\n", page_table);
  return true;
}

// use the appropriate TLB flush instruction to globally flush the modified entry
// terminal is set when flushing at the final level of the page table.
void Riscv64ArchVmAspace::FlushTLBEntry(vaddr_t vaddr, bool terminal) {
  __asm__ __volatile__ ("sfence.vma  %0, %1" :: "r"(vaddr), "r"(asid_) : "memory");
  unsigned long hart_mask = -1; // TODO(all): Flush vaddr only, on other harts only 
  sbi_remote_sfence_vma_asid(&hart_mask, 0, 0, -1, asid_);
}

// NOTE: caller must DSB afterwards to ensure TLB entries are flushed
ssize_t Riscv64ArchVmAspace::UnmapPageTable(vaddr_t vaddr, vaddr_t vaddr_rel, size_t size,
                                          uint level, volatile pte_t* page_table) {
  volatile pte_t* next_page_table;
  vaddr_t index;
  size_t chunk_size;
  vaddr_t vaddr_rem;
  vaddr_t block_size;
  vaddr_t block_mask;
  pte_t pte;
  paddr_t page_table_paddr;
  size_t unmap_size;

  LTRACEF("vaddr 0x%lx, vaddr_rel 0x%lx, size 0x%lx, level %u, page_table %p\n",
          vaddr, vaddr_rel, size, level, page_table);

  unmap_size = 0;
  while (size) {
    block_size = page_size_per_level(level);
    block_mask = block_size - 1;
    vaddr_rem = vaddr_rel & block_mask;
    chunk_size = ktl::min(size, block_size - vaddr_rem);
    index = vaddr_to_index(vaddr_rel, level);

    pte = page_table[index];

    if (level > 0 && !(pte & RISCV64_PTE_PERM_MASK) && RISCV64_PTE_PPN(pte)) {
      page_table_paddr = RISCV64_PTE_PPN(pte);
      next_page_table = static_cast<volatile pte_t*>(paddr_to_physmap(page_table_paddr));
      UnmapPageTable(vaddr, vaddr_rem, chunk_size, level - 1, next_page_table);
      if (chunk_size == block_size || page_table_is_clear(next_page_table)) {
        LTRACEF("pte %p[0x%lx] = 0 (was page table)\n", page_table, index);
        page_table[index] = 0;

        // ensure that the update is observable from hardware page table walkers before TLB
        // operations can occur.
        mb();

        // flush the non terminal TLB entry
        FlushTLBEntry(vaddr, false);

        FreePageTable(const_cast<pte_t*>(next_page_table), page_table_paddr);
      }
    } else if (pte) {
      LTRACEF("pte %p[0x%lx] = 0\n", page_table, index);
      page_table[index] = 0;

      // ensure that the update is observable from hardware page table walkers before TLB
      // operations can occur.
      mb();

      // flush the terminal TLB entry
      FlushTLBEntry(vaddr, true);
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

// NOTE: caller must DSB afterwards to ensure TLB entries are flushed
ssize_t Riscv64ArchVmAspace::MapPageTable(vaddr_t vaddr_in, vaddr_t vaddr_rel_in, paddr_t paddr_in,
                                        size_t size_in, pte_t attrs, uint level,
                                        volatile pte_t* page_table) {
  ssize_t ret;
  volatile pte_t* next_page_table;
  vaddr_t index;
  vaddr_t vaddr = vaddr_in;
  vaddr_t vaddr_rel = vaddr_rel_in;
  paddr_t paddr = paddr_in;
  size_t size = size_in;
  size_t chunk_size;
  vaddr_t vaddr_rem;
  vaddr_t block_size;
  vaddr_t block_mask;
  pte_t pte;
  size_t mapped_size;

  LTRACEF("vaddr %#" PRIxPTR ", vaddr_rel %#" PRIxPTR ", paddr %#" PRIxPTR
          ", size %#zx, attrs %#" PRIx64 ", level %u, page_table %p\n",
          vaddr, vaddr_rel, paddr, size, attrs, level, page_table);

  if ((vaddr_rel | paddr | size) & (PAGE_MASK)) {
    TRACEF("not page aligned\n");
    return ZX_ERR_INVALID_ARGS;
  }

  mapped_size = 0;
  while (size) {
    block_size = page_size_per_level(level);
    block_mask = block_size - 1;
    vaddr_rem = vaddr_rel & block_mask;
    chunk_size = ktl::min(size, block_size - vaddr_rem);
    index = vaddr_to_index(vaddr_rel, level);

    if (((vaddr_rel | paddr) & block_mask) || (chunk_size != block_size)) {
      next_page_table = GetPageTable(index, page_table);
      if (!next_page_table) {
        goto err;
      }

      ret = MapPageTable(vaddr, vaddr_rem, paddr, chunk_size, attrs, level - 1,
                         next_page_table);
      if (ret < 0) {
        goto err;
      }
    } else {
      pte = page_table[index];
      if (pte) {
        TRACEF("page table entry already in use, index %#" PRIxPTR ", %#" PRIx64 "\n", index, pte);
        goto err;
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

  return mapped_size;

err:
  UnmapPageTable(vaddr_in, vaddr_rel_in, size_in - size, level, page_table);
  return ZX_ERR_INTERNAL;
}

// NOTE: caller must DSB afterwards to ensure TLB entries are flushed
zx_status_t Riscv64ArchVmAspace::ProtectPageTable(vaddr_t vaddr_in, vaddr_t vaddr_rel_in,
                                                size_t size_in, pte_t attrs, uint level,
                                                volatile pte_t* page_table) {
  volatile pte_t* next_page_table;
  vaddr_t index;
  vaddr_t vaddr = vaddr_in;
  vaddr_t vaddr_rel = vaddr_rel_in;
  size_t size = size_in;
  size_t chunk_size;
  vaddr_t vaddr_rem;
  vaddr_t block_size;
  vaddr_t block_mask;
  paddr_t page_table_paddr;
  pte_t pte;

  LTRACEF("vaddr %#" PRIxPTR ", vaddr_rel %#" PRIxPTR ", size %#" PRIxPTR ", attrs %#" PRIx64
          ", level %u, page_table %p\n",
          vaddr, vaddr_rel, size, attrs, level, page_table);

  // vaddr_rel and size must be page aligned
  DEBUG_ASSERT(((vaddr_rel | size) & ((1UL << PAGE_SIZE_SHIFT) - 1)) == 0);

  while (size) {
    block_size = page_size_per_level(level);
    block_mask = block_size - 1;
    vaddr_rem = vaddr_rel & block_mask;
    chunk_size = ktl::min(size, block_size - vaddr_rem);
    index = vaddr_to_index(vaddr_rel, level);
    pte = page_table[index];

    if (level > 0 && pte & RISCV64_PTE_PERM_MASK && chunk_size != block_size) {
      zx_status_t s = SplitLargePage(vaddr, level, index, page_table);
      if (likely(s == ZX_OK)) {
        pte = page_table[index];
      } else {
        // If split fails, just unmap the whole block and let a
        // subsequent page fault clean it up.
        UnmapPageTable(vaddr - vaddr_rel, 0, block_size, level, page_table);
        pte = 0;
      }
    }

    if (level > 0 && !(pte & RISCV64_PTE_PERM_MASK)) {
      page_table_paddr = RISCV64_PTE_PPN(pte);
      next_page_table = static_cast<volatile pte_t*>(paddr_to_physmap(page_table_paddr));
      ProtectPageTable(vaddr, vaddr_rem, chunk_size, attrs, level - 1, next_page_table);
    } else if (pte) {
      pte = (pte & ~RISCV64_PTE_PERM_MASK) | attrs;
      LTRACEF("pte %p[%#" PRIxPTR "] = %#" PRIx64 "\n", page_table, index, pte);
      page_table[index] = pte;

      // ensure that the update is observable from hardware page table walkers before TLB
      // operations can occur.
      mb();

      // flush the terminal TLB entry
      FlushTLBEntry(vaddr, true);
    } else {
      LTRACEF("page table entry does not exist, index %#" PRIxPTR ", %#" PRIx64 "\n", index, pte);
    }
    vaddr += chunk_size;
    vaddr_rel += chunk_size;
    size -= chunk_size;
  }

  return ZX_OK;
}

// NOTE: if this returns true, caller must DSB afterwards to ensure TLB entries are flushed
bool Riscv64ArchVmAspace::HarvestAccessedPageTable(vaddr_t vaddr, vaddr_t vaddr_rel_in, size_t size,
                                                 const uint level, volatile pte_t* page_table,
                                                 const HarvestCallback& accessed_callback) {
  const vaddr_t block_size = page_size_per_level(level);
  const vaddr_t block_mask = block_size - 1;

  vaddr_t vaddr_rel = vaddr_rel_in;

  // vaddr_rel and size must be page aligned
  DEBUG_ASSERT(((vaddr_rel | size) & ((1UL << PAGE_SIZE_SHIFT) - 1)) == 0);

  bool flushed_tlb = false;

  while (size) {
    const vaddr_t vaddr_rem = vaddr_rel & block_mask;
    const size_t chunk_size = ktl::min(size, block_size - vaddr_rem);
    const vaddr_t index = vaddr_to_index(vaddr_rel, level);

    pte_t pte = page_table[index];

    if (level > 0 && (pte & RISCV64_PTE_PERM_MASK) && chunk_size != block_size) {
      // Ignore large pages, we do not support harvesting accessed bits from them. Having this empty
      // if block simplifies the overall logic.
    } else if (level > 0 && !(pte & RISCV64_PTE_PERM_MASK)) {
      const paddr_t page_table_paddr = RISCV64_PTE_PPN(pte);
      volatile pte_t* next_page_table =
          static_cast<volatile pte_t*>(paddr_to_physmap(page_table_paddr));
      if (HarvestAccessedPageTable(vaddr, vaddr_rem, chunk_size, level - 1,
                                   next_page_table, accessed_callback)) {
        flushed_tlb = true;
      }
    } else if (pte) {
      if (pte & RISCV64_PTE_A) {
        const paddr_t pte_addr = RISCV64_PTE_PPN(pte);
        const paddr_t paddr = pte_addr + vaddr_rem;
        const uint mmu_flags = MmuFlagsFromPte(pte);
        // Invoke the callback to see if the accessed flag should be removed.
        if (accessed_callback(paddr, vaddr, mmu_flags)) {
          // Modifying the access flag does not require break-before-make for correctness and as we
          // do not support hardware access flag setting at the moment we do not have to deal with
          // potential concurrent modifications.
          pte = (pte & ~RISCV64_PTE_A);
          LTRACEF("pte %p[%#" PRIxPTR "] = %#" PRIx64 "\n", page_table, index, pte);
          page_table[index] = pte;

          // ensure that the update is observable from hardware page table walkers before TLB
          // operations can occur.
          mb();

          // flush the terminal TLB entry
          FlushTLBEntry(vaddr, true);

          // propagate back up to the caller that a tlb flush happened so they can synchronize.
          flushed_tlb = true;
        }
      }
    }
    vaddr += chunk_size;
    vaddr_rel += chunk_size;
    size -= chunk_size;
  }
  return flushed_tlb;
}

void Riscv64ArchVmAspace::MarkAccessedPageTable(vaddr_t vaddr, vaddr_t vaddr_rel_in, size_t size,
                                              uint level, volatile pte_t* page_table) {
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

    if (level > 0 && (pte & RISCV64_PTE_PERM_MASK) && chunk_size != block_size) {
      // Ignore large pages as we don't support modifying their access flags. Having this empty if
      // block simplifies the overall logic.
    } else if (level > 0 && !(pte & RISCV64_PTE_PERM_MASK)) {
      const paddr_t page_table_paddr = RISCV64_PTE_PPN(pte);
      volatile pte_t* next_page_table =
          static_cast<volatile pte_t*>(paddr_to_physmap(page_table_paddr));
      MarkAccessedPageTable(vaddr, vaddr_rem, chunk_size, level - 1,
                            next_page_table);
    } else if (pte) {
      pte |= RISCV64_PTE_A;
      page_table[index] = pte;
      // If the access bit wasn't set then we know this entry isn't cached in any TLBs and so we do
      // not need to do any TLB maintenance and can just issue a dmb to ensure the hardware walker
      // sees the new entry. If the access bit was already set then this operation is a no-op and
      // we can leave any TLB entries alone.
      mb();
    }
    vaddr += chunk_size;
    vaddr_rel += chunk_size;
    size -= chunk_size;
  }
}

// internal routine to map a run of pages
ssize_t Riscv64ArchVmAspace::MapPages(vaddr_t vaddr, paddr_t paddr, size_t size, pte_t attrs) {
  LOCAL_KTRACE("mmu map", (vaddr & ~PAGE_MASK) | ((size >> PAGE_SIZE_SHIFT) & PAGE_MASK));
  uint level = RISCV64_MMU_PT_LEVELS - 1;
  ssize_t ret = MapPageTable(vaddr, vaddr, paddr, size, attrs, level, tt_virt_);
  mb();
  return ret;
}

ssize_t Riscv64ArchVmAspace::UnmapPages(vaddr_t vaddr, size_t size) {
  LOCAL_KTRACE("mmu unmap", (vaddr & ~PAGE_MASK) | ((size >> PAGE_SIZE_SHIFT) & PAGE_MASK));
  uint level = RISCV64_MMU_PT_LEVELS - 1;
  ssize_t ret = UnmapPageTable(vaddr, vaddr, size, level, tt_virt_);
  mb();
  return ret;
}

zx_status_t Riscv64ArchVmAspace::ProtectPages(vaddr_t vaddr, size_t size, pte_t attrs) {
  LOCAL_KTRACE("mmu protect", (vaddr & ~PAGE_MASK) | ((size >> PAGE_SIZE_SHIFT) & PAGE_MASK));
  uint level = RISCV64_MMU_PT_LEVELS - 1;
  zx_status_t ret =
      ProtectPageTable(vaddr, vaddr, size, attrs, level, tt_virt_);
  mb();
  return ret;
}

void Riscv64ArchVmAspace::MmuParamsFromFlags(uint mmu_flags, pte_t* attrs) {
  if (attrs) {
    *attrs = mmu_flags_to_pte_attr(mmu_flags);
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
    pte_t attrs;
    MmuParamsFromFlags(mmu_flags, &attrs);
    ret = MapPages(vaddr, paddr, count * PAGE_SIZE, attrs);
  }

  if (mapped) {
    *mapped = (ret > 0) ? (ret / PAGE_SIZE) : 0u;
    DEBUG_ASSERT(*mapped <= count);
  }

  return (ret < 0) ? (zx_status_t)ret : ZX_OK;
}

zx_status_t Riscv64ArchVmAspace::Map(vaddr_t vaddr, paddr_t* phys, size_t count, uint mmu_flags,
                                     size_t* mapped) {
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
    Guard<Mutex> a{&lock_};
    pte_t attrs;
    MmuParamsFromFlags(mmu_flags, &attrs);

    ssize_t ret;
    size_t idx = 0;
    auto undo = fbl::MakeAutoCall([&]() TA_NO_THREAD_SAFETY_ANALYSIS {
      if (idx > 0) {
        UnmapPages(vaddr, idx * PAGE_SIZE);
      }
    });

    vaddr_t v = vaddr;
    for (; idx < count; ++idx) {
      paddr_t paddr = phys[idx];
      DEBUG_ASSERT(IS_PAGE_ALIGNED(paddr));
      // TODO: optimize by not DSBing inside each of these calls
      ret = MapPages(v, paddr, PAGE_SIZE, attrs);
      if (ret < 0) {
        return static_cast<zx_status_t>(ret);
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

  ssize_t ret;
  {
    ret = UnmapPages(vaddr, count * PAGE_SIZE);
  }

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

  int ret;
  {
    pte_t attrs;
    MmuParamsFromFlags(mmu_flags, &attrs);

    ret = ProtectPages(vaddr, count * PAGE_SIZE, attrs);
  }

  return ret;
}

zx_status_t Riscv64ArchVmAspace::HarvestAccessed(vaddr_t vaddr, size_t count,
                                                 const HarvestCallback& accessed_callback) {
  canary_.Assert();

  if (!IS_PAGE_ALIGNED(vaddr) || !IsValidVaddr(vaddr)) {
    return ZX_ERR_INVALID_ARGS;
  }

  Guard<Mutex> a{&lock_};

  const size_t size = count * PAGE_SIZE;
  LOCAL_KTRACE("mmu harvest accessed",
               (vaddr & ~PAGE_MASK) | ((size >> PAGE_SIZE_SHIFT) & PAGE_MASK));

  // It's fairly reasonable for there to be nothing to harvest, and dsb is expensive, so
  // HarvestAccessedPageTable will return true if it performed a TLB invalidation that we need to
  // synchronize with.
  if (HarvestAccessedPageTable(vaddr, vaddr, size, RISCV64_MMU_PT_LEVELS - 1, tt_virt_,
                               accessed_callback)) {
    mb();
  }

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

  MarkAccessedPageTable(vaddr, vaddr, size, RISCV64_MMU_PT_LEVELS - 1, tt_virt_);
  // MarkAccessedPageTable does not perform any TLB operations, so unlike most other top level mmu
  // functions we do not need to perform a dsb to synchronize.
  return ZX_OK;
}

zx_status_t Riscv64ArchVmAspace::Init() {
  canary_.Assert();
  LTRACEF("aspace %p, base %#" PRIxPTR ", size 0x%zx, flags 0x%x\n", this, base_, size_, flags_);

  Guard<Mutex> a{&lock_};

  // Validate that the base + size is sane and doesn't wrap.
  DEBUG_ASSERT(size_ > PAGE_SIZE);
  DEBUG_ASSERT(base_ + size_ - 1 > base_);

  if (flags_ & ARCH_ASPACE_FLAG_KERNEL) {
    // At the moment we can only deal with address spaces as globally defined.
    DEBUG_ASSERT(base_ == KERNEL_ASPACE_BASE);
    DEBUG_ASSERT(size_ == KERNEL_ASPACE_SIZE);

    tt_virt_ = riscv64_get_kernel_ptable();
    tt_phys_ = vaddr_to_paddr(const_cast<pte_t*>(tt_virt_));
    asid_ = (uint16_t)MMU_RISCV64_GLOBAL_ASID;
  } else {
    if (flags_ & ARCH_ASPACE_FLAG_GUEST) {
      PANIC_UNIMPLEMENTED;
    } else {
      DEBUG_ASSERT(base_ == USER_ASPACE_BASE);
      DEBUG_ASSERT(size_ == USER_ASPACE_SIZE);
      if (asid.Alloc(&asid_) != ZX_OK) {
        return ZX_ERR_NO_MEMORY;
      }
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
    // XXX remove when PMM starts returning pre-zeroed pages.
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

  DEBUG_ASSERT((flags_ & ARCH_ASPACE_FLAG_KERNEL) == 0);

  // XXX make sure it's not mapped

  vm_page_t* page = paddr_to_vm_page(tt_phys_);
  DEBUG_ASSERT(page);
  pmm_free_page(page);

  __asm("sfence.vma  zero, %0" :: "r"(asid_) : "memory");
  unsigned long hart_mask = -1; // TODO(all): Flush other harts only
  sbi_remote_sfence_vma_asid(&hart_mask, 0, 0, -1, asid_);
  asid.Free(asid_);
  asid_ = MMU_RISCV64_UNUSED_ASID;

  return ZX_OK;
}

void Riscv64ArchVmAspace::ContextSwitch(Riscv64ArchVmAspace* old_aspace, Riscv64ArchVmAspace* aspace) {
  if (TRACE_CONTEXT_SWITCH) {
    LTRACEF("aspace %p\n", aspace);
  }

  if (aspace) {
    aspace->canary_.Assert();
    DEBUG_ASSERT((aspace->flags_ & (ARCH_ASPACE_FLAG_KERNEL | ARCH_ASPACE_FLAG_GUEST)) == 0);

    uint64_t satp = ((uint64_t)RISCV64_SATP_MODE_SV48 << RISCV64_SATP_MODE_SHIFT) |
                     ((uint64_t)aspace->asid_ << RISCV64_SATP_ASID_SHIFT) |
                     (aspace->tt_phys_ >> PAGE_SIZE_SHIFT);

    riscv64_csr_write(RISCV64_CSR_SATP, satp);

    __asm("sfence.vma  zero, zero");
  }
}

void arch_zero_page(void* _ptr) {
  memset(_ptr, 0, PAGE_SIZE);
}

Riscv64ArchVmAspace::Riscv64ArchVmAspace(vaddr_t base, size_t size, uint mmu_flags, page_alloc_fn_t paf)
	: test_page_alloc_func_(paf), flags_(mmu_flags), base_(base), size_(size) {}

Riscv64ArchVmAspace::~Riscv64ArchVmAspace() = default;

vaddr_t Riscv64ArchVmAspace::PickSpot(vaddr_t base, uint prev_region_mmu_flags, vaddr_t end,
                                      uint next_region_mmu_flags, vaddr_t align, size_t size,
                                      uint mmu_flags) {
  canary_.Assert();
  return PAGE_ALIGN(base);
}
