// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <assert.h>
#include <err.h>
#include <string.h>
#include <trace.h>

#include <arch/arch_ops.h>
#include <arch/mmu.h>
#include <arch/x86.h>
#include <arch/x86/descriptor.h>
#include <arch/x86/feature.h>
#include <arch/x86/mmu.h>
#include <arch/x86/mmu_mem_types.h>
#include <kernel/mp.h>
#include <vm/arch_vm_aspace.h>
#include <vm/physmap.h>
#include <vm/pmm.h>
#include <vm/vm.h>
#include <zircon/types.h>
#include <zxcpp/new.h>

#define LOCAL_TRACE 0

/* Default address width including virtual/physical address.
 * newer versions fetched below */
uint8_t g_vaddr_width = 48;
uint8_t g_paddr_width = 32;

/* True if the system supports 1GB pages */
static bool supports_huge_pages = false;

/* top level kernel page tables, initialized in start.S */
volatile pt_entry_t pml4[NO_OF_PT_ENTRIES] __ALIGNED(PAGE_SIZE);
volatile pt_entry_t pdp[NO_OF_PT_ENTRIES] __ALIGNED(PAGE_SIZE); /* temporary */
volatile pt_entry_t pte[NO_OF_PT_ENTRIES] __ALIGNED(PAGE_SIZE);

/* top level pdp needed to map the -512GB..0 space */
volatile pt_entry_t pdp_high[NO_OF_PT_ENTRIES] __ALIGNED(PAGE_SIZE);

/* a big pile of page tables needed to map 64GB of memory into kernel space using 2MB pages */
volatile pt_entry_t linear_map_pdp[(64ULL * GB) / (2 * MB)] __ALIGNED(PAGE_SIZE);

/* which of the above variables is the top level page table */
#define KERNEL_PT pml4

// Static relocated base to prepare for KASLR. Used at early boot and by gdb
// script to know the target relocated address.
// TODO(thgarnie): Move to a dynamicly generated base address
#if DISABLE_KASLR
uint64_t kernel_relocated_base = KERNEL_BASE - KERNEL_LOAD_OFFSET;
#else
uint64_t kernel_relocated_base = 0xffffffff00000000;
#endif

/* kernel base top level page table in physical space */
static const paddr_t kernel_pt_phys =
    (vaddr_t)KERNEL_PT - (vaddr_t)__code_start + KERNEL_LOAD_OFFSET;

// Valid EPT MMU flags.
static const uint kValidEptFlags =
    ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE | ARCH_MMU_FLAG_PERM_EXECUTE;

paddr_t x86_kernel_cr3(void) {
    return kernel_pt_phys;
}

/**
 * @brief  check if the virtual address is canonical
 */
bool x86_is_vaddr_canonical(vaddr_t vaddr) {
    uint64_t max_vaddr_lohalf, min_vaddr_hihalf;

    /* get max address in lower-half canonical addr space */
    /* e.g. if width is 48, then 0x00007FFF_FFFFFFFF */
    max_vaddr_lohalf = ((uint64_t)1ull << (g_vaddr_width - 1)) - 1;

    /* get min address in higher-half canonical addr space */
    /* e.g. if width is 48, then 0xFFFF8000_00000000*/
    min_vaddr_hihalf = ~max_vaddr_lohalf;

    /* Check to see if the address in a canonical address */
    if ((vaddr > max_vaddr_lohalf) && (vaddr < min_vaddr_hihalf))
        return false;

    return true;
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

    max_paddr = ((uint64_t)1ull << g_paddr_width) - 1;

    return paddr <= max_paddr;
}

/**
 * @brief  invalidate all TLB entries, including global entries
 */
static void x86_tlb_global_invalidate() {
    /* See Intel 3A section 4.10.4.1 */
    ulong cr4 = x86_get_cr4();
    if (likely(cr4 & X86_CR4_PGE)) {
        x86_set_cr4(cr4 & ~X86_CR4_PGE);
        x86_set_cr4(cr4);
    } else {
        x86_set_cr3(x86_get_cr3());
    }
}

/**
 * @brief  invalidate all TLB entries, excluding global entries
 */
static void x86_tlb_nonglobal_invalidate() {
    x86_set_cr3(x86_get_cr3());
}

/* Task used for invalidating a TLB entry on each CPU */
struct TlbInvalidatePage_context {
    ulong target_cr3;
    const PendingTlbInvalidation* pending;
};
static void TlbInvalidatePage_task(void* raw_context) {
    DEBUG_ASSERT(arch_ints_disabled());
    TlbInvalidatePage_context* context = (TlbInvalidatePage_context*)raw_context;

    ulong cr3 = x86_get_cr3();
    if (context->target_cr3 != cr3 && !context->pending->contains_global) {
        /* This invalidation doesn't apply to this CPU, ignore it */
        return;
    }

    if (context->pending->full_shootdown) {
        if (context->pending->contains_global) {
            x86_tlb_global_invalidate();
        } else {
            x86_tlb_nonglobal_invalidate();
        }
        return;
    }

    for (uint i = 0; i < context->pending->count; ++i) {
        const auto& item = context->pending->item[i];
        switch (item.page_level()) {
            case PML4_L:
                panic("PML4_L invld found; should not be here\n");
            case PDP_L:
            case PD_L:
            case PT_L:
                __asm__ volatile("invlpg %0" ::"m"(*(uint8_t*)item.addr()));
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
    if (pending->count == 0) {
        return;
    }

    ulong cr3 = pt ? pt->phys() : x86_get_cr3();
    struct TlbInvalidatePage_context task_context = {
        .target_cr3 = cr3, .pending = pending,
    };

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
        target_mask = static_cast<X86ArchVmAspace*>(pt->ctx())->active_cpus();
    }

    mp_sync_exec(target, target_mask, TlbInvalidatePage_task, &task_context);
    pending->clear();
}

bool X86PageTableMmu::check_paddr(paddr_t paddr) {
    return x86_mmu_check_paddr(paddr);
}

bool X86PageTableMmu::check_vaddr(vaddr_t vaddr) {
    return x86_mmu_check_vaddr(vaddr);
}

bool X86PageTableMmu::supports_page_size(PageTableLevel level) {
    DEBUG_ASSERT(level != PT_L);
    switch (level) {
    case PD_L:
        return true;
    case PDP_L:
        return supports_huge_pages;
    case PML4_L:
        return false;
    default:
        panic("Unreachable case in supports_page_size\n");
    }
}

X86PageTableBase::IntermediatePtFlags X86PageTableMmu::intermediate_flags() {
    return X86_MMU_PG_RW | X86_MMU_PG_U;
}

X86PageTableBase::PtFlags X86PageTableMmu::terminal_flags(PageTableLevel level,
                                                          uint flags) {
    X86PageTableBase::PtFlags terminal_flags = 0;

    if (flags & ARCH_MMU_FLAG_PERM_WRITE)
        terminal_flags |= X86_MMU_PG_RW;

    if (flags & ARCH_MMU_FLAG_PERM_USER)
        terminal_flags |= X86_MMU_PG_U;

    if (use_global_mappings_) {
        terminal_flags |= X86_MMU_PG_G;
    }

    if (!(flags & ARCH_MMU_FLAG_PERM_EXECUTE))
        terminal_flags |= X86_MMU_PG_NX;

    if (level > 0) {
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

X86PageTableBase::PtFlags X86PageTableMmu::split_flags(PageTableLevel level,
                                                       X86PageTableBase::PtFlags flags) {
    DEBUG_ASSERT(level != PML4_L && level != PT_L);
    DEBUG_ASSERT(flags & X86_MMU_PG_PS);
    if (level == PD_L) {
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

    if (flags & X86_MMU_PG_RW)
        mmu_flags |= ARCH_MMU_FLAG_PERM_WRITE;

    if (flags & X86_MMU_PG_U)
        mmu_flags |= ARCH_MMU_FLAG_PERM_USER;

    if (!(flags & X86_MMU_PG_NX))
        mmu_flags |= ARCH_MMU_FLAG_PERM_EXECUTE;

    if (level > 0) {
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
    if (flags & ~kValidEptFlags) {
        return false;
    }
    return true;
}

bool X86PageTableEpt::check_paddr(paddr_t paddr) {
    return x86_mmu_check_paddr(paddr);
}

bool X86PageTableEpt::check_vaddr(vaddr_t vaddr) {
    return x86_mmu_check_vaddr(vaddr);
}

bool X86PageTableEpt::supports_page_size(PageTableLevel level) {
    DEBUG_ASSERT(level != PT_L);
    switch (level) {
    case PD_L:
        return true;
    case PDP_L:
        return supports_huge_pages;
    case PML4_L:
        return false;
    default:
        panic("Unreachable case in supports_page_size\n");
    }
}

X86PageTableBase::PtFlags X86PageTableEpt::intermediate_flags() {
    return X86_EPT_R | X86_EPT_W | X86_EPT_X;
}

X86PageTableBase::PtFlags X86PageTableEpt::terminal_flags(PageTableLevel level,
                                                          uint flags) {
    DEBUG_ASSERT((flags & ARCH_MMU_FLAG_CACHE_MASK) == ARCH_MMU_FLAG_CACHED);
    // Only the write-back memory type is supported.
    X86PageTableBase::PtFlags terminal_flags = X86_EPT_WB;

    if (flags & ARCH_MMU_FLAG_PERM_READ)
        terminal_flags |= X86_EPT_R;

    if (flags & ARCH_MMU_FLAG_PERM_WRITE)
        terminal_flags |= X86_EPT_W;

    if (flags & ARCH_MMU_FLAG_PERM_EXECUTE)
        terminal_flags |= X86_EPT_X;

    return terminal_flags;
}

X86PageTableBase::PtFlags X86PageTableEpt::split_flags(PageTableLevel level,
                                                       X86PageTableBase::PtFlags flags) {
    DEBUG_ASSERT(level != PML4_L && level != PT_L);
    // We don't need to relocate any flags on split for EPT.
    return flags;
}


void X86PageTableEpt::TlbInvalidate(PendingTlbInvalidation* pending) {
    // TODO(ZX-981): Implement this.
    pending->clear();
}

uint X86PageTableEpt::pt_flags_to_mmu_flags(PtFlags flags, PageTableLevel level) {
    // Only the write-back memory type is supported.
    uint mmu_flags = ARCH_MMU_FLAG_CACHED;

    if (flags & X86_EPT_R)
        mmu_flags |= ARCH_MMU_FLAG_PERM_READ;

    if (flags & X86_EPT_W)
        mmu_flags |= ARCH_MMU_FLAG_PERM_WRITE;

    if (flags & X86_EPT_X)
        mmu_flags |= ARCH_MMU_FLAG_PERM_EXECUTE;

    return mmu_flags;
}

void x86_mmu_early_init() {
    x86_mmu_percpu_init();

    x86_mmu_mem_type_init();

    // Unmap the lower identity mapping.
    pml4[0] = 0;
    PendingTlbInvalidation tlb;
    tlb.enqueue(0, PML4_L, /* global */ false, /* terminal */ false);
    x86_tlb_invalidate_page(nullptr, &tlb);

    /* get the address width from the CPU */
    uint8_t vaddr_width = x86_linear_address_width();
    uint8_t paddr_width = x86_physical_address_width();

    supports_huge_pages = x86_feature_test(X86_FEATURE_HUGE_PAGE);

    /* if we got something meaningful, override the defaults.
     * some combinations of cpu on certain emulators seems to return
     * nonsense paddr widths (1), so trim it. */
    if (paddr_width > g_paddr_width)
        g_paddr_width = paddr_width;
    if (vaddr_width > g_vaddr_width)
        g_vaddr_width = vaddr_width;

    LTRACEF("paddr_width %u vaddr_width %u\n", g_paddr_width, g_vaddr_width);
}

void x86_mmu_init(void) {}

X86PageTableBase::X86PageTableBase() {
}

X86PageTableBase::~X86PageTableBase() {
    DEBUG_ASSERT_MSG(!phys_, "page table dtor called before Destroy()");
}

// We disable analysis due to the write to |pages_| tripping it up.  It is safe
// to write to |pages_| since this is part of object construction.
zx_status_t X86PageTableBase::Init(void* ctx) TA_NO_THREAD_SAFETY_ANALYSIS {
    /* allocate a top level page table for the new address space */
    paddr_t pa;
    vm_page_t* p = pmm_alloc_page(0, &pa);
    if (!p) {
        TRACEF("error allocating top level page directory\n");
        return ZX_ERR_NO_MEMORY;
    }
    virt_ = reinterpret_cast<pt_entry_t*>(paddr_to_physmap(pa));
    phys_ = pa;
    p->state = VM_PAGE_STATE_MMU;

    // TODO(abdulla): Remove when PMM returns pre-zeroed pages.
    arch_zero_page(virt_);

    ctx_ = ctx;
    pages_ = 1;
    return ZX_OK;
}

// We disable analysis due to the write to |pages_| tripping it up.  It is safe
// to write to |pages_| since this is part of object construction.
zx_status_t X86PageTableMmu::InitKernel(void* ctx) TA_NO_THREAD_SAFETY_ANALYSIS {
    phys_ = kernel_pt_phys;
    virt_ = (pt_entry_t*)X86_PHYS_TO_VIRT(phys_);
    ctx_ = ctx;
    pages_ = 1;
    use_global_mappings_ = true;
    return ZX_OK;
}

zx_status_t X86PageTableMmu::AliasKernelMappings() {
    // Copy the kernel portion of it from the master kernel pt.
    memcpy(virt_ + NO_OF_PT_ENTRIES / 2,
           const_cast<pt_entry_t*>(&KERNEL_PT[NO_OF_PT_ENTRIES / 2]),
           sizeof(pt_entry_t) * NO_OF_PT_ENTRIES / 2);
    return ZX_OK;
}

X86ArchVmAspace::X86ArchVmAspace() {}

/*
 * Fill in the high level x86 arch aspace structure and allocating a top level page table.
 */
zx_status_t X86ArchVmAspace::Init(vaddr_t base, size_t size, uint mmu_flags) {
    static_assert(sizeof(cpu_mask_t) == sizeof(active_cpus_), "err");
    canary_.Assert();

    LTRACEF("aspace %p, base %#" PRIxPTR ", size 0x%zx, mmu_flags 0x%x\n", this, base, size,
            mmu_flags);

    flags_ = mmu_flags;
    base_ = base;
    size_ = size;
    if (mmu_flags & ARCH_ASPACE_FLAG_KERNEL) {
        X86PageTableMmu* mmu = new (page_table_storage_) X86PageTableMmu();
        pt_ = mmu;

        zx_status_t status = mmu->InitKernel(this);
        if (status != ZX_OK) {
            return status;
        }
        LTRACEF("kernel aspace: pt phys %#" PRIxPTR ", virt %p\n", pt_->phys(), pt_->virt());
    } else if (mmu_flags & ARCH_ASPACE_FLAG_GUEST) {
        X86PageTableEpt* ept = new (page_table_storage_) X86PageTableEpt();
        pt_ = ept;

        zx_status_t status = ept->Init(this);
        if (status != ZX_OK) {
            return status;
        }
        LTRACEF("guest paspace: pt phys %#" PRIxPTR ", virt %p\n", pt_->phys(), pt_->virt());
    } else {
        X86PageTableMmu* mmu = new (page_table_storage_) X86PageTableMmu;
        pt_ = mmu;

        zx_status_t status = mmu->Init(this);
        if (status != ZX_OK) {
            return status;
        }

        status = mmu->AliasKernelMappings();
        if (status != ZX_OK) {
            return status;
        }

        LTRACEF("user aspace: pt phys %#" PRIxPTR ", virt %p\n", pt_->phys(), pt_->virt());
    }
    fbl::atomic_init(&active_cpus_, 0);

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

zx_status_t X86ArchVmAspace::Unmap(vaddr_t vaddr, size_t count, size_t* unmapped) {
    if (!IsValidVaddr(vaddr))
        return ZX_ERR_INVALID_ARGS;

    return pt_->UnmapPages(vaddr, count, unmapped);
}

zx_status_t X86ArchVmAspace::MapContiguous(vaddr_t vaddr, paddr_t paddr, size_t count,
                                           uint mmu_flags, size_t* mapped) {
    if (!IsValidVaddr(vaddr))
        return ZX_ERR_INVALID_ARGS;

    return pt_->MapPagesContiguous(vaddr, paddr, count, mmu_flags, mapped);
}

zx_status_t X86ArchVmAspace::Map(vaddr_t vaddr, paddr_t* phys, size_t count,
                                 uint mmu_flags, size_t* mapped) {
    if (!IsValidVaddr(vaddr))
        return ZX_ERR_INVALID_ARGS;

    return pt_->MapPages(vaddr, phys, count, mmu_flags, mapped);
}

zx_status_t X86ArchVmAspace::Protect(vaddr_t vaddr, size_t count, uint mmu_flags) {
    if (!IsValidVaddr(vaddr))
        return ZX_ERR_INVALID_ARGS;

    return pt_->ProtectPages(vaddr, count, mmu_flags);
}

void X86ArchVmAspace::ContextSwitch(X86ArchVmAspace* old_aspace, X86ArchVmAspace* aspace) {
    cpu_mask_t cpu_bit = cpu_num_to_mask(arch_curr_cpu_num());
    if (aspace != nullptr) {
        aspace->canary_.Assert();
        paddr_t phys = aspace->pt_phys();
        LTRACEF_LEVEL(3, "switching to aspace %p, pt %#" PRIXPTR "\n", aspace, phys);
        x86_set_cr3(phys);

        if (old_aspace != nullptr) {
            old_aspace->active_cpus_.fetch_and(~cpu_bit);
        }
        aspace->active_cpus_.fetch_or(cpu_bit);
    } else {
        LTRACEF_LEVEL(3, "switching to kernel aspace, pt %#" PRIxPTR "\n", kernel_pt_phys);
        x86_set_cr3(kernel_pt_phys);
        if (old_aspace != nullptr) {
            old_aspace->active_cpus_.fetch_and(~cpu_bit);
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

void x86_mmu_percpu_init(void) {
    ulong cr0 = x86_get_cr0();
    /* Set write protect bit in CR0*/
    cr0 |= X86_CR0_WP;
    // Clear Cache disable/not write-through bits
    cr0 &= ~(X86_CR0_NW | X86_CR0_CD);
    x86_set_cr0(cr0);

    /* Setting the SMEP & SMAP bit in CR4 */
    ulong cr4 = x86_get_cr4();
    if (x86_feature_test(X86_FEATURE_SMEP))
        cr4 |= X86_CR4_SMEP;
    if (x86_feature_test(X86_FEATURE_SMAP))
        cr4 |= X86_CR4_SMAP;
    x86_set_cr4(cr4);

    // Set NXE bit in X86_MSR_IA32_EFER.
    uint64_t efer_msr = read_msr(X86_MSR_IA32_EFER);
    efer_msr |= X86_EFER_NXE;
    write_msr(X86_MSR_IA32_EFER, efer_msr);
}

X86ArchVmAspace::~X86ArchVmAspace() {
    if (pt_) {
        pt_->~X86PageTableBase();
    }
    // TODO(ZX-980): check that we've destroyed the aspace.
}

vaddr_t X86ArchVmAspace::PickSpot(vaddr_t base, uint prev_region_mmu_flags,
                                  vaddr_t end, uint next_region_mmu_flags,
                                  vaddr_t align, size_t size, uint mmu_flags) {
    canary_.Assert();
    return PAGE_ALIGN(base);
}
