// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
#include "arch/x86/mmu.h"

#include <align.h>
#include <assert.h>
#include <lib/arch/sysreg.h>
#include <lib/arch/x86/boot-cpuid.h>
#include <lib/arch/x86/system.h>
#include <lib/counters.h>
#include <lib/zircon-internal/macros.h>
#include <string.h>
#include <trace.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <new>

#include <arch/arch_ops.h>
#include <arch/x86.h>
#include <arch/x86/descriptor.h>
#include <arch/x86/feature.h>
#include <arch/x86/hypervisor/invalidate.h>
#include <arch/x86/mmu_mem_types.h>
#include <kernel/mp.h>
#include <vm/arch_vm_aspace.h>
#include <vm/physmap.h>
#include <vm/pmm.h>
#include <vm/vm.h>

#define LOCAL_TRACE 0

// Count of the number of batches of TLB invalidations initiated on each CPU
KCOUNTER(tlb_invalidations_sent, "mmu.tlb_invalidation_batches_sent")
// Count of the number of batches of TLB invalidation requests received on each CPU
// Includes tlb_invalidations_full_global_received and tlb_invalidations_full_nonglobal_received
KCOUNTER(tlb_invalidations_received, "mmu.tlb_invalidation_batches_received")
// Count of the number of TLB invalidation requests for all entries on each CPU
KCOUNTER(tlb_invalidations_full_global_received, "mmu.tlb_invalidation_full_global_received")
// Count of the number of TLB invalidation requests for all non-global entries on each CPU
KCOUNTER(tlb_invalidations_full_nonglobal_received, "mmu.tlb_invalidation_full_nonglobal_received")
// Count of the number of times an EPT TLB invalidation got performed.
KCOUNTER(ept_tlb_invalidations, "mmu.ept_tlb_invalidations")

/* Default address width including virtual/physical address.
 * newer versions fetched below */
static uint8_t g_max_vaddr_width = 48;
uint8_t g_max_paddr_width = 32;

/* True if the system supports 1GB pages */
static bool supports_huge_pages = false;

/* top level kernel page tables, initialized in start.S */
volatile pt_entry_t pml4[NO_OF_PT_ENTRIES] __ALIGNED(PAGE_SIZE);
volatile pt_entry_t pdp[NO_OF_PT_ENTRIES] __ALIGNED(PAGE_SIZE); /* temporary */
volatile pt_entry_t pte[NO_OF_PT_ENTRIES] __ALIGNED(PAGE_SIZE);

/* top level pdp needed to map the -512GB..0 space */
volatile pt_entry_t pdp_high[NO_OF_PT_ENTRIES] __ALIGNED(PAGE_SIZE);

#if __has_feature(address_sanitizer)
volatile pt_entry_t kasan_shadow_pt[NO_OF_PT_ENTRIES] __ALIGNED(PAGE_SIZE);  // Leaf page tables
volatile pt_entry_t kasan_shadow_pd[NO_OF_PT_ENTRIES] __ALIGNED(PAGE_SIZE);  // Page directories
// TODO(fxbug.dev/30033): Share this with the vm::zero_page
volatile uint8_t kasan_zero_page[PAGE_SIZE] __ALIGNED(PAGE_SIZE);
#endif

/* a big pile of page tables needed to map 64GB of memory into kernel space using 2MB pages */
volatile pt_entry_t linear_map_pdp[(64ULL * GB) / (2 * MB)] __ALIGNED(PAGE_SIZE);

/* which of the above variables is the top level page table */
#define KERNEL_PT pml4

// Width of the PCID identifier
#define X86_PCID_BITS (12)
// When this bit is set in the source operand of a MOV CR3, TLB entries and paging structure
// caches for the active PCID may be preserved. If the bit is clear, entries will be cleared.
// See Intel Volume 3A, 4.10.4.1
#define X86_PCID_CR3_SAVE_ENTRIES (63)

// Static relocated base to prepare for KASLR. Used at early boot and by gdb
// script to know the target relocated address.
// TODO(thgarnie): Move to a dynamically generated base address
#if DISABLE_KASLR
uint64_t kernel_relocated_base = KERNEL_BASE - KERNEL_LOAD_OFFSET;
#else
uint64_t kernel_relocated_base = 0xffffffff00000000;
#endif

/* kernel base top level page table in physical space */
static const paddr_t kernel_pt_phys =
    (vaddr_t)KERNEL_PT - (vaddr_t)__code_start + KERNEL_LOAD_OFFSET;

paddr_t x86_kernel_cr3(void) { return kernel_pt_phys; }

/**
 * @brief  check if the virtual address is canonical
 */
bool x86_is_vaddr_canonical(vaddr_t vaddr) {
  // If N is the number of address bits in use for a virtual address, then the
  // address is canonical if bits [N - 1, 63] are all either 0 (the low half of
  // the valid addresses) or 1 (the high half).
  return ((vaddr & kX86CanonicalAddressMask) == 0) ||
         ((vaddr & kX86CanonicalAddressMask) == kX86CanonicalAddressMask);
}

/**
 * @brief  check if the virtual address is aligned and canonical
 */
static bool x86_mmu_check_vaddr(vaddr_t vaddr) {
  /* Check to see if the address is PAGE aligned */
  if (!IS_ALIGNED(vaddr, PAGE_SIZE))
    return false;

  return x86_is_vaddr_canonical(vaddr);
}

/**
 * @brief  check if the physical address is valid and aligned
 */
bool x86_mmu_check_paddr(paddr_t paddr) {
  uint64_t max_paddr;

  /* Check to see if the address is PAGE aligned */
  if (!IS_ALIGNED(paddr, PAGE_SIZE))
    return false;

  max_paddr = ((uint64_t)1ull << g_max_paddr_width) - 1;

  return paddr <= max_paddr;
}

/**
 * @brief  invalidate all TLB entries, excluding global entries
 */
static void x86_tlb_nonglobal_invalidate() {
  // Read CR3 and immediately write it back.
  arch::X86Cr3::Read().Write();
}

/**
 * @brief  invalidate all TLB entries, including global entries
 */
static void x86_tlb_global_invalidate() {
  /* See Intel 3A section 4.10.4.1 */
  auto cr4 = arch::X86Cr4::Read();
  if (likely(cr4.pge())) {
    cr4.set_pge(false).Write();
    cr4.set_pge(true).Write();
  } else {
    x86_tlb_nonglobal_invalidate();
  }
}

static void maybe_invvpid(InvVpid invalidation, uint16_t vpid, zx_vaddr_t address) {
  if (vpid != MMU_X86_UNUSED_VPID) {
    invvpid(invalidation, vpid, address);
  }
}

/* Task used for invalidating a TLB entry on each CPU */
struct TlbInvalidatePage_context {
  ulong target_cr3;
  const PendingTlbInvalidation* pending;
  uint16_t vpid;
};
static void TlbInvalidatePage_task(void* raw_context) {
  DEBUG_ASSERT(arch_ints_disabled());
  TlbInvalidatePage_context* context = (TlbInvalidatePage_context*)raw_context;

  kcounter_add(tlb_invalidations_received, 1);

  if (context->target_cr3 != arch::X86Cr3::Read().base() && !context->pending->contains_global) {
    /* This invalidation doesn't apply to this CPU, ignore it */
    return;
  }

  if (context->pending->full_shootdown) {
    if (context->pending->contains_global) {
      kcounter_add(tlb_invalidations_full_global_received, 1);
      x86_tlb_global_invalidate();
      maybe_invvpid(InvVpid::SINGLE_CONTEXT, context->vpid, 0);
    } else {
      kcounter_add(tlb_invalidations_full_nonglobal_received, 1);
      x86_tlb_nonglobal_invalidate();
      maybe_invvpid(InvVpid::SINGLE_CONTEXT_RETAIN_GLOBALS, context->vpid, 0);
    }
    return;
  }

  for (uint i = 0; i < context->pending->count; ++i) {
    const auto& item = context->pending->item[i];
    switch (static_cast<PageTableLevel>(item.page_level())) {
      case PageTableLevel::PML4_L:
        panic("PML4_L invld found; should not be here\n");
      case PageTableLevel::PDP_L:
      case PageTableLevel::PD_L:
      case PageTableLevel::PT_L:
        __asm__ volatile("invlpg %0" ::"m"(*(uint8_t*)item.addr()));
        maybe_invvpid(InvVpid::INDIVIDUAL_ADDRESS, context->vpid, item.addr());
        break;
    }
  }
}

/**
 * @brief Execute a queued TLB invalidation
 *
 * @param pt The page table we're invalidating for (if nullptr, assume for current one)
 * @param pending The planned invalidation
 */
static void x86_tlb_invalidate_page(const X86PageTableBase* pt, PendingTlbInvalidation* pending) {
  if (pending->count == 0 && !pending->full_shootdown) {
    return;
  }

  kcounter_add(tlb_invalidations_sent, 1);

  auto aspace = static_cast<X86ArchVmAspace*>(pt->ctx());
  ulong cr3 = pt ? pt->phys() : x86_get_cr3();
  uint16_t vpid = aspace->arch_vpid();
  struct TlbInvalidatePage_context task_context = {
      .target_cr3 = cr3,
      .pending = pending,
      .vpid = vpid,
  };

  // TODO(fxbug.dev/95763): Consider whether it is better to invalidate a VPID
  // on context switch, or whether it is better to target all CPUs here.
  if (vpid != MMU_X86_UNUSED_VPID) {
    pending->contains_global = true;
  }

  /* Target only CPUs this aspace is active on.  It may be the case that some
   * other CPU will become active in it after this load, or will have left it
   * just before this load.  In the former case, it is becoming active after
   * the write to the page table, so it will see the change.  In the latter
   * case, it will get a spurious request to flush. */
  mp_ipi_target_t target;
  cpu_mask_t target_mask = 0;
  if (pending->contains_global || pt == nullptr) {
    target = MP_IPI_TARGET_ALL;
  } else {
    target = MP_IPI_TARGET_MASK;
    target_mask = aspace->active_cpus();
  }

  mp_sync_exec(target, target_mask, TlbInvalidatePage_task, &task_context);
  pending->clear();
}

#if 0  // TODO(mcgrathr): remove this if it isn't going to be used
bool x86_enable_pcid() {
  DEBUG_ASSERT(arch_ints_disabled());
  if (!g_x86_feature_pcid_good) {
    return false;
  }

  arch::X86Cr4::Read().set_pcide(true).Write();
  return true;
}
#endif

bool X86PageTableMmu::check_paddr(paddr_t paddr) { return x86_mmu_check_paddr(paddr); }

bool X86PageTableMmu::check_vaddr(vaddr_t vaddr) { return x86_mmu_check_vaddr(vaddr); }

bool X86PageTableMmu::supports_page_size(PageTableLevel level) {
  DEBUG_ASSERT(level != PageTableLevel::PT_L);
  switch (level) {
    case PageTableLevel::PD_L:
      return true;
    case PageTableLevel::PDP_L:
      return supports_huge_pages;
    case PageTableLevel::PML4_L:
      return false;
    default:
      panic("Unreachable case in supports_page_size\n");
  }
}

IntermediatePtFlags X86PageTableMmu::intermediate_flags() { return X86_MMU_PG_RW | X86_MMU_PG_U; }

PtFlags X86PageTableMmu::terminal_flags(PageTableLevel level, uint flags) {
  PtFlags terminal_flags = 0;

  if (flags & ARCH_MMU_FLAG_PERM_WRITE) {
    terminal_flags |= X86_MMU_PG_RW;
  }
  if (flags & ARCH_MMU_FLAG_PERM_USER) {
    terminal_flags |= X86_MMU_PG_U;
  }
  if (use_global_mappings_) {
    terminal_flags |= X86_MMU_PG_G;
  }
  if (!(flags & ARCH_MMU_FLAG_PERM_EXECUTE)) {
    terminal_flags |= X86_MMU_PG_NX;
  }

  if (level != PageTableLevel::PT_L) {
    switch (flags & ARCH_MMU_FLAG_CACHE_MASK) {
      case ARCH_MMU_FLAG_CACHED:
        terminal_flags |= X86_MMU_LARGE_PAT_WRITEBACK;
        break;
      case ARCH_MMU_FLAG_UNCACHED_DEVICE:
      case ARCH_MMU_FLAG_UNCACHED:
        terminal_flags |= X86_MMU_LARGE_PAT_UNCACHABLE;
        break;
      case ARCH_MMU_FLAG_WRITE_COMBINING:
        terminal_flags |= X86_MMU_LARGE_PAT_WRITE_COMBINING;
        break;
      default:
        PANIC_UNIMPLEMENTED;
    }
  } else {
    switch (flags & ARCH_MMU_FLAG_CACHE_MASK) {
      case ARCH_MMU_FLAG_CACHED:
        terminal_flags |= X86_MMU_PTE_PAT_WRITEBACK;
        break;
      case ARCH_MMU_FLAG_UNCACHED_DEVICE:
      case ARCH_MMU_FLAG_UNCACHED:
        terminal_flags |= X86_MMU_PTE_PAT_UNCACHABLE;
        break;
      case ARCH_MMU_FLAG_WRITE_COMBINING:
        terminal_flags |= X86_MMU_PTE_PAT_WRITE_COMBINING;
        break;
      default:
        PANIC_UNIMPLEMENTED;
    }
  }

  return terminal_flags;
}

PtFlags X86PageTableMmu::split_flags(PageTableLevel level, PtFlags flags) {
  DEBUG_ASSERT(level != PageTableLevel::PML4_L && level != PageTableLevel::PT_L);
  DEBUG_ASSERT(flags & X86_MMU_PG_PS);
  if (level == PageTableLevel::PD_L) {
    // Note: Clear PS before the check below; the PAT bit for a PTE is the
    // the same as the PS bit for a higher table entry.
    flags &= ~X86_MMU_PG_PS;

    /* If the larger page had the PAT flag set, make sure it's
     * transferred to the different index for a PTE */
    if (flags & X86_MMU_PG_LARGE_PAT) {
      flags &= ~X86_MMU_PG_LARGE_PAT;
      flags |= X86_MMU_PG_PTE_PAT;
    }
  }
  return flags;
}

void X86PageTableMmu::TlbInvalidate(PendingTlbInvalidation* pending) {
  x86_tlb_invalidate_page(this, pending);
}

uint X86PageTableMmu::pt_flags_to_mmu_flags(PtFlags flags, PageTableLevel level) {
  uint mmu_flags = ARCH_MMU_FLAG_PERM_READ;

  if (flags & X86_MMU_PG_RW) {
    mmu_flags |= ARCH_MMU_FLAG_PERM_WRITE;
  }
  if (flags & X86_MMU_PG_U) {
    mmu_flags |= ARCH_MMU_FLAG_PERM_USER;
  }
  if (!(flags & X86_MMU_PG_NX)) {
    mmu_flags |= ARCH_MMU_FLAG_PERM_EXECUTE;
  }

  if (level != PageTableLevel::PT_L) {
    switch (flags & X86_MMU_LARGE_PAT_MASK) {
      case X86_MMU_LARGE_PAT_WRITEBACK:
        mmu_flags |= ARCH_MMU_FLAG_CACHED;
        break;
      case X86_MMU_LARGE_PAT_UNCACHABLE:
        mmu_flags |= ARCH_MMU_FLAG_UNCACHED;
        break;
      case X86_MMU_LARGE_PAT_WRITE_COMBINING:
        mmu_flags |= ARCH_MMU_FLAG_WRITE_COMBINING;
        break;
      default:
        PANIC_UNIMPLEMENTED;
    }
  } else {
    switch (flags & X86_MMU_PTE_PAT_MASK) {
      case X86_MMU_PTE_PAT_WRITEBACK:
        mmu_flags |= ARCH_MMU_FLAG_CACHED;
        break;
      case X86_MMU_PTE_PAT_UNCACHABLE:
        mmu_flags |= ARCH_MMU_FLAG_UNCACHED;
        break;
      case X86_MMU_PTE_PAT_WRITE_COMBINING:
        mmu_flags |= ARCH_MMU_FLAG_WRITE_COMBINING;
        break;
      default:
        PANIC_UNIMPLEMENTED;
    }
  }
  return mmu_flags;
}

bool X86PageTableEpt::allowed_flags(uint flags) {
  if (!(flags & ARCH_MMU_FLAG_PERM_READ)) {
    return false;
  }
  return true;
}

bool X86PageTableEpt::check_paddr(paddr_t paddr) { return x86_mmu_check_paddr(paddr); }

bool X86PageTableEpt::check_vaddr(vaddr_t vaddr) { return x86_mmu_check_vaddr(vaddr); }

bool X86PageTableEpt::supports_page_size(PageTableLevel level) {
  DEBUG_ASSERT(level != PageTableLevel::PT_L);
  switch (level) {
    case PageTableLevel::PD_L:
      return true;
    case PageTableLevel::PDP_L:
      return supports_huge_pages;
    case PageTableLevel::PML4_L:
      return false;
    default:
      panic("Unreachable case in supports_page_size\n");
  }
}

PtFlags X86PageTableEpt::intermediate_flags() { return X86_EPT_R | X86_EPT_W | X86_EPT_X; }

PtFlags X86PageTableEpt::terminal_flags(PageTableLevel level, uint flags) {
  PtFlags terminal_flags = 0;

  if (flags & ARCH_MMU_FLAG_PERM_READ) {
    terminal_flags |= X86_EPT_R;
  }
  if (flags & ARCH_MMU_FLAG_PERM_WRITE) {
    terminal_flags |= X86_EPT_W;
  }
  if (flags & ARCH_MMU_FLAG_PERM_EXECUTE) {
    terminal_flags |= X86_EPT_X;
  }

  switch (flags & ARCH_MMU_FLAG_CACHE_MASK) {
    case ARCH_MMU_FLAG_CACHED:
      terminal_flags |= X86_EPT_WB;
      break;
    case ARCH_MMU_FLAG_UNCACHED_DEVICE:
    case ARCH_MMU_FLAG_UNCACHED:
      terminal_flags |= X86_EPT_UC;
      break;
    case ARCH_MMU_FLAG_WRITE_COMBINING:
      terminal_flags |= X86_EPT_WC;
      break;
    default:
      PANIC_UNIMPLEMENTED;
  }

  return terminal_flags;
}

PtFlags X86PageTableEpt::split_flags(PageTableLevel level, PtFlags flags) {
  DEBUG_ASSERT(level != PageTableLevel::PML4_L && level != PageTableLevel::PT_L);
  // We don't need to relocate any flags on split for EPT.
  return flags;
}

void X86PageTableEpt::TlbInvalidate(PendingTlbInvalidation* pending) {
  if (pending->count == 0 && !pending->full_shootdown) {
    return;
  }

  kcounter_add(ept_tlb_invalidations, 1);

  // Target all CPUs with a context invalidation since we do not know what CPUs have this EPT
  // active. We cannot use active_cpus() is only updated by ContextSwitch, which does not get called
  // for guests, and also EPT mappings persist even if a guest is not presently executing. In
  // general unmap operations on EPTs should be extremely rare and not in any common path, so this
  // inefficiency is not disastrous in the short term. Similarly, since this is an infrequent
  // operation, we do not attempt to invalidate any individual entries, but just blow away the whole
  // context.
  // TODO: Track what CPUs the VCPUs using this EPT are migrated to and only IPI that subset.
  uint64_t eptp = ept_pointer_from_pml4(static_cast<X86ArchVmAspace*>(ctx())->arch_table_phys());
  broadcast_invept(eptp);
  pending->clear();
}

uint X86PageTableEpt::pt_flags_to_mmu_flags(PtFlags flags, PageTableLevel level) {
  uint mmu_flags = 0;

  if (flags & X86_EPT_R) {
    mmu_flags |= ARCH_MMU_FLAG_PERM_READ;
  }
  if (flags & X86_EPT_W) {
    mmu_flags |= ARCH_MMU_FLAG_PERM_WRITE;
  }
  if (flags & X86_EPT_X) {
    mmu_flags |= ARCH_MMU_FLAG_PERM_EXECUTE;
  }

  switch (flags & X86_EPT_MEMORY_TYPE_MASK) {
    case X86_EPT_WB:
      mmu_flags |= ARCH_MMU_FLAG_CACHED;
      break;
    case X86_EPT_UC:
      mmu_flags |= ARCH_MMU_FLAG_UNCACHED;
      break;
    case X86_EPT_WC:
      mmu_flags |= ARCH_MMU_FLAG_WRITE_COMBINING;
      break;
    default:
      PANIC_UNIMPLEMENTED;
  }

  return mmu_flags;
}

void x86_mmu_early_init() {
  x86_mmu_percpu_init();

  x86_mmu_mem_type_init();

  // Unmap the lower identity mapping.
  pml4[0] = 0;
  // As we are still in early init code we cannot use the general page invalidation mechanisms,
  // specifically ones that might use mp_sync_exec or kcounters, so just drop the entire tlb.
  x86_tlb_global_invalidate();

  /* get the address width from the CPU */
  auto vaddr_width =
      static_cast<uint8_t>(arch::BootCpuid<arch::CpuidAddressSizeInfo>().linear_addr_bits());
  auto paddr_width =
      static_cast<uint8_t>(arch::BootCpuid<arch::CpuidAddressSizeInfo>().phys_addr_bits());

  supports_huge_pages = x86_feature_test(X86_FEATURE_HUGE_PAGE);

  /* if we got something meaningful, override the defaults.
   * some combinations of cpu on certain emulators seems to return
   * nonsense paddr widths (1), so trim it. */
  if (paddr_width > g_max_paddr_width) {
    g_max_paddr_width = paddr_width;
  }

  if (vaddr_width > g_max_vaddr_width) {
    g_max_vaddr_width = vaddr_width;
  }

  LTRACEF("paddr_width %u vaddr_width %u\n", g_max_paddr_width, g_max_vaddr_width);
}

void x86_mmu_init() {
  ASSERT_MSG(g_max_vaddr_width >= kX86VAddrBits,
             "Maximum number of virtual address bits (%u) is less than the assumed number of bits"
             " being used (%u)\n",
             g_max_vaddr_width, kX86VAddrBits);
}

X86PageTableBase::X86PageTableBase() {}

X86PageTableBase::~X86PageTableBase() {
  DEBUG_ASSERT_MSG(!phys_, "page table dtor called before Destroy()");
}

// We disable analysis due to the write to |pages_| tripping it up.  It is safe
// to write to |pages_| since this is part of object construction.
zx_status_t X86PageTableBase::Init(void* ctx,
                                   page_alloc_fn_t test_paf) TA_NO_THREAD_SAFETY_ANALYSIS {
  test_page_alloc_func_ = test_paf;

  /* allocate a top level page table for the new address space */
  virt_ = AllocatePageTable();
  if (!virt_) {
    TRACEF("error allocating top level page directory\n");
    return ZX_ERR_NO_MEMORY;
  }

  phys_ = physmap_to_paddr(virt_);
  DEBUG_ASSERT(phys_ != 0);

  ctx_ = ctx;
  pages_ = 1;
  return ZX_OK;
}

// We disable analysis due to the write to |pages_| tripping it up.  It is safe
// to write to |pages_| since this is part of object construction.
zx_status_t X86PageTableMmu::InitKernel(void* ctx,
                                        page_alloc_fn_t test_paf) TA_NO_THREAD_SAFETY_ANALYSIS {
  test_page_alloc_func_ = test_paf;

  phys_ = kernel_pt_phys;
  virt_ = (pt_entry_t*)X86_PHYS_TO_VIRT(phys_);
  ctx_ = ctx;
  pages_ = 1;
  use_global_mappings_ = true;
  return ZX_OK;
}

zx_status_t X86PageTableMmu::AliasKernelMappings() {
  // Copy the kernel portion of it from the master kernel pt.
  memcpy(virt_ + NO_OF_PT_ENTRIES / 2, const_cast<pt_entry_t*>(&KERNEL_PT[NO_OF_PT_ENTRIES / 2]),
         sizeof(pt_entry_t) * NO_OF_PT_ENTRIES / 2);
  return ZX_OK;
}

X86ArchVmAspace::X86ArchVmAspace(vaddr_t base, size_t size, uint mmu_flags,
                                 page_alloc_fn_t test_paf)
    : test_page_alloc_func_(test_paf), flags_(mmu_flags), base_(base), size_(size) {}

/*
 * Fill in the high level x86 arch aspace structure and allocating a top level page table.
 */
zx_status_t X86ArchVmAspace::Init() {
  static_assert(sizeof(cpu_mask_t) == sizeof(active_cpus_), "err");
  canary_.Assert();

  LTRACEF("aspace %p, base %#" PRIxPTR ", size 0x%zx, mmu_flags 0x%x\n", this, base_, size_,
          flags_);

  if (flags_ & ARCH_ASPACE_FLAG_KERNEL) {
    X86PageTableMmu* mmu = new (&page_table_storage_.mmu) X86PageTableMmu();
    pt_ = mmu;

    zx_status_t status = mmu->InitKernel(this, test_page_alloc_func_);
    if (status != ZX_OK) {
      return status;
    }
    LTRACEF("kernel aspace: pt phys %#" PRIxPTR ", virt %p\n", pt_->phys(), pt_->virt());
  } else if (flags_ & ARCH_ASPACE_FLAG_GUEST) {
    X86PageTableEpt* ept = new (&page_table_storage_.ept) X86PageTableEpt();
    pt_ = ept;

    zx_status_t status = ept->Init(this, test_page_alloc_func_);
    if (status != ZX_OK) {
      return status;
    }
    LTRACEF("guest paspace: pt phys %#" PRIxPTR ", virt %p\n", pt_->phys(), pt_->virt());
  } else {
    X86PageTableMmu* mmu = new (&page_table_storage_.mmu) X86PageTableMmu();
    pt_ = mmu;

    zx_status_t status = mmu->Init(this, test_page_alloc_func_);
    if (status != ZX_OK) {
      return status;
    }

    status = mmu->AliasKernelMappings();
    if (status != ZX_OK) {
      return status;
    }

    LTRACEF("user aspace: pt phys %#" PRIxPTR ", virt %p\n", pt_->phys(), pt_->virt());
  }
  ktl::atomic_init(&active_cpus_, 0);

  return ZX_OK;
}

zx_status_t X86ArchVmAspace::Destroy() {
  canary_.Assert();
  DEBUG_ASSERT(active_cpus_.load() == 0);

  if (flags_ & ARCH_ASPACE_FLAG_GUEST) {
    static_cast<X86PageTableEpt*>(pt_)->Destroy(base_, size_);
  } else {
    static_cast<X86PageTableMmu*>(pt_)->Destroy(base_, size_);
  }
  return ZX_OK;
}

zx_status_t X86ArchVmAspace::Unmap(vaddr_t vaddr, size_t count, EnlargeOperation enlarge,
                                   size_t* unmapped) {
  if (!IsValidVaddr(vaddr))
    return ZX_ERR_INVALID_ARGS;

  zx_status_t result = pt_->UnmapPages(vaddr, count, enlarge, unmapped);
  MarkAspaceModified();
  return result;
}

zx_status_t X86ArchVmAspace::MapContiguous(vaddr_t vaddr, paddr_t paddr, size_t count,
                                           uint mmu_flags, size_t* mapped) {
  if (!IsValidVaddr(vaddr))
    return ZX_ERR_INVALID_ARGS;

  zx_status_t result = pt_->MapPagesContiguous(vaddr, paddr, count, mmu_flags, mapped);
  MarkAspaceModified();
  return result;
}

zx_status_t X86ArchVmAspace::Map(vaddr_t vaddr, paddr_t* phys, size_t count, uint mmu_flags,
                                 ExistingEntryAction existing_action, size_t* mapped) {
  if (!IsValidVaddr(vaddr))
    return ZX_ERR_INVALID_ARGS;

  zx_status_t result = pt_->MapPages(vaddr, phys, count, mmu_flags, existing_action, mapped);
  MarkAspaceModified();
  return result;
}

zx_status_t X86ArchVmAspace::Protect(vaddr_t vaddr, size_t count, uint mmu_flags) {
  if (!IsValidVaddr(vaddr))
    return ZX_ERR_INVALID_ARGS;

  zx_status_t result = pt_->ProtectPages(vaddr, count, mmu_flags);
  MarkAspaceModified();
  return result;
}

void X86ArchVmAspace::ContextSwitch(X86ArchVmAspace* old_aspace, X86ArchVmAspace* aspace) {
  cpu_mask_t cpu_bit = cpu_num_to_mask(arch_curr_cpu_num());
  if (aspace != nullptr) {
    aspace->canary_.Assert();
    paddr_t phys = aspace->pt_phys();
    LTRACEF_LEVEL(3, "switching to aspace %p, pt %#" PRIXPTR "\n", aspace, phys);
    arch::X86Cr3::Write(phys);
    if (old_aspace != nullptr) {
      __UNUSED uint32_t prev = old_aspace->active_cpus_.fetch_and(~cpu_bit);
      // Make sure we were actually previously running on this CPU
      DEBUG_ASSERT(prev & cpu_bit);
    }
    __UNUSED uint32_t prev = aspace->active_cpus_.fetch_or(cpu_bit);
    // Should not already be running on this CPU.
    DEBUG_ASSERT(!(prev & cpu_bit));
    aspace->active_since_last_check_.store(true, ktl::memory_order_relaxed);
  } else {
    LTRACEF_LEVEL(3, "switching to kernel aspace, pt %#" PRIxPTR "\n", kernel_pt_phys);
    arch::X86Cr3::Write(kernel_pt_phys);
    if (old_aspace != nullptr) {
      __UNUSED uint32_t prev = old_aspace->active_cpus_.fetch_and(~cpu_bit);
      // Make sure we were actually previously running on this CPU
      DEBUG_ASSERT(prev & cpu_bit);
    }
  }

  // Cleanup io bitmap entries from previous thread.
  if (old_aspace)
    x86_clear_tss_io_bitmap(old_aspace->io_bitmap());

  // Set the io bitmap for this thread.
  if (aspace)
    x86_set_tss_io_bitmap(aspace->io_bitmap());
}

zx_status_t X86ArchVmAspace::Query(vaddr_t vaddr, paddr_t* paddr, uint* mmu_flags) {
  if (!IsValidVaddr(vaddr))
    return ZX_ERR_INVALID_ARGS;

  return pt_->QueryVaddr(vaddr, paddr, mmu_flags);
}

zx_status_t X86ArchVmAspace::HarvestAccessed(vaddr_t vaddr, size_t count,
                                             NonTerminalAction non_terminal_action,
                                             TerminalAction terminal_action) {
  if (!IsValidVaddr(vaddr)) {
    return ZX_ERR_INVALID_ARGS;
  }
  return pt_->HarvestAccessed(vaddr, count, non_terminal_action, terminal_action);
}

bool X86ArchVmAspace::ActiveSinceLastCheck(bool clear) {
  // Read whether any CPUs are presently executing.
  bool currently_active = active_cpus_.load(ktl::memory_order_relaxed) != 0;
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

void x86_mmu_percpu_init(void) {
  arch::X86Cr0::Read()
      .set_wp(true)   // Set write protect.
      .set_nw(false)  // Clear not-write-through.
      .set_cd(false)  // Clear cache-disable.
      .Write();

  // Set the SMEP & SMAP bits in CR4.
  arch::X86Cr4 cr4 = arch::X86Cr4::Read();
  if (x86_feature_test(X86_FEATURE_SMEP)) {
    cr4.set_smep(true);
  }
  if (g_x86_feature_has_smap) {
    cr4.set_smap(true);
  }
  cr4.Write();

  // Set NXE bit in X86_MSR_IA32_EFER.
  uint64_t efer_msr = read_msr(X86_MSR_IA32_EFER);
  efer_msr |= X86_EFER_NXE;
  write_msr(X86_MSR_IA32_EFER, efer_msr);
}

X86ArchVmAspace::~X86ArchVmAspace() {
  if (pt_) {
    pt_->~X86PageTableBase();
  }
  // TODO(fxbug.dev/30927): check that we've destroyed the aspace.
}

vaddr_t X86ArchVmAspace::PickSpot(vaddr_t base, vaddr_t end, vaddr_t align, size_t size,
                                  uint mmu_flags) {
  canary_.Assert();
  return PAGE_ALIGN(base);
}

uint32_t arch_address_tagging_features() { return 0; }
