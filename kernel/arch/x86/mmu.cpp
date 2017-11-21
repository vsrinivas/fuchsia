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
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
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

/* kernel base top level page table in physical space */
static const paddr_t kernel_pt_phys = (vaddr_t)KERNEL_PT - KERNEL_BASE + KERNEL_LOAD_OFFSET;

/* valid EPT MMU flags */
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

static page_table_levels lower_level(page_table_levels level) {
    DEBUG_ASSERT(level != 0);
    return (page_table_levels)(level - 1);
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
static bool x86_mmu_check_paddr(paddr_t paddr) {
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

/* Task used for invalidating a TLB entry on each CPU */
struct TlbInvalidatePage_context {
    ulong target_cr3;
    vaddr_t vaddr;
    enum page_table_levels level;
    bool global_page;
};
static void TlbInvalidatePage_task(void* raw_context) {
    DEBUG_ASSERT(arch_ints_disabled());
    TlbInvalidatePage_context* context = (TlbInvalidatePage_context*)raw_context;

    ulong cr3 = x86_get_cr3();
    if (context->target_cr3 != cr3 && !context->global_page) {
        /* This invalidation doesn't apply to this CPU, ignore it */
        return;
    }

    switch (context->level) {
    case PML4_L:
        x86_tlb_global_invalidate();
        break;
    case PDP_L:
    case PD_L:
    case PT_L:
        __asm__ volatile("invlpg %0" ::"m"(*(uint8_t*)context->vaddr));
        break;
    }
}

/**
 * @brief Invalidate a single page at a given page table level
 *
 * @param pt The page table we're invalidating for (if NULL, assume for current one)
 * @param vaddr The virtual address we are invalidating the TLB entry for
 * @param level The page table level that maps this vaddr
 * @param global_page True if we are invalidating a global mapping
 *
 * TODO(ZX-979): Optimize this.  This is horribly inefficient.
 * We should also change this to pool invalidations from a single
 * "transaction" and then only execute a single mp_sync_exec for that
 * transaction, rather than one per page.
 */
static void x86_tlb_invalidate_page(X86PageTableBase* pt, vaddr_t vaddr,
                                    enum page_table_levels level, bool global_page) {
    ulong cr3 = pt ? pt->phys() : x86_get_cr3();
    struct TlbInvalidatePage_context task_context = {
        .target_cr3 = cr3, .vaddr = vaddr, .level = level, .global_page = global_page,
    };

    /* Target only CPUs this aspace is active on.  It may be the case that some
     * other CPU will become active in it after this load, or will have left it
     * just before this load.  In the former case, it is becoming active after
     * the write to the page table, so it will see the change.  In the latter
     * case, it will get a spurious request to flush. */
    mp_ipi_target_t target;
    cpu_mask_t target_mask = 0;
    if (global_page || pt == nullptr) {
        target = MP_IPI_TARGET_ALL;
    } else {
        target = MP_IPI_TARGET_MASK;
        target_mask = static_cast<X86ArchVmAspace*>(pt->ctx())->active_cpus();
    }

    mp_sync_exec(target, target_mask, TlbInvalidatePage_task, &task_context);
}

bool X86PageTableMmu::supports_page_size(page_table_levels level) {
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

X86PageTableBase::PtFlags X86PageTableMmu::terminal_flags(page_table_levels level,
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

X86PageTableBase::PtFlags X86PageTableMmu::split_flags(page_table_levels level,
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

void X86PageTableMmu::TlbInvalidatePage(page_table_levels level, X86PageTableBase* pt,
                                        vaddr_t vaddr, bool global_page) {
    x86_tlb_invalidate_page(pt, vaddr, level, global_page);
}

uint X86PageTableMmu::pt_flags_to_mmu_flags(PtFlags flags, page_table_levels level) {
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

bool X86PageTableEpt::supports_page_size(page_table_levels level) {
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

X86PageTableBase::PtFlags X86PageTableEpt::terminal_flags(page_table_levels level,
                                                          uint flags) {
    DEBUG_ASSERT((flags & ARCH_MMU_FLAG_CACHED) == ARCH_MMU_FLAG_CACHED);
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

X86PageTableBase::PtFlags X86PageTableEpt::split_flags(page_table_levels level,
                                                       X86PageTableBase::PtFlags flags) {
    DEBUG_ASSERT(level != PML4_L && level != PT_L);
    // We don't need to relocate any flags on split for EPT.
    return flags;
}

void X86PageTableEpt::TlbInvalidatePage(page_table_levels level, X86PageTableBase* pt,
                                        vaddr_t vaddr, bool global_page) {
    // TODO(ZX-981): Implement this.
}

uint X86PageTableEpt::pt_flags_to_mmu_flags(PtFlags flags, page_table_levels level) {
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

struct MappingCursor {
public:
    /**
   * @brief Update the cursor to skip over a not-present page table entry.
   */
    void SkipEntry(page_table_levels level) {
        const size_t ps = X86PageTableBase::page_size(level);
        // Calculate the amount the cursor should skip to get to the next entry at
        // this page table level.
        const size_t skipped_size = ps - (vaddr & (ps - 1));
        // If our endpoint was in the middle of this range, clamp the
        // amount we remove from the cursor
        const size_t _size = (size > skipped_size) ? skipped_size : size;

        size -= _size;
        vaddr += _size;
    }

    paddr_t paddr;
    vaddr_t vaddr;
    size_t size;
};

void X86PageTableBase::UpdateEntry(page_table_levels level, vaddr_t vaddr, volatile pt_entry_t* pte,
                                   paddr_t paddr, PtFlags flags) {
    DEBUG_ASSERT(pte);
    DEBUG_ASSERT(IS_PAGE_ALIGNED(paddr));

    pt_entry_t olde = *pte;

    /* set the new entry */
    *pte = paddr | flags | X86_MMU_PG_P;

    /* attempt to invalidate the page */
    if (IS_PAGE_PRESENT(olde)) {
        TlbInvalidatePage(level, this, vaddr, is_kernel_address(vaddr));
    }
}

void X86PageTableBase::UnmapEntry(page_table_levels level, vaddr_t vaddr, volatile pt_entry_t* pte) {
    DEBUG_ASSERT(pte);

    pt_entry_t olde = *pte;

    *pte = 0;

    /* attempt to invalidate the page */
    if (IS_PAGE_PRESENT(olde)) {
        TlbInvalidatePage(level, this, vaddr, is_kernel_address(vaddr));
    }
}

/**
 * @brief Allocating a new page table
 */
static volatile pt_entry_t* _map_alloc_page(void) {
    vm_page_t* p;

    pt_entry_t* page_ptr = static_cast<pt_entry_t*>(pmm_alloc_kpage(nullptr, &p));
    if (!page_ptr)
        return nullptr;

    arch_zero_page(page_ptr);
    p->state = VM_PAGE_STATE_MMU;

    return page_ptr;
}

/*
 * @brief Split the given large page into smaller pages
 */
zx_status_t X86PageTableBase::SplitLargePage(page_table_levels level, vaddr_t vaddr,
                                             volatile pt_entry_t* pte) {
    DEBUG_ASSERT_MSG(level != PT_L, "tried splitting PT_L");
    LTRACEF_LEVEL(2, "splitting table %p at level %d\n", pte, level);

    DEBUG_ASSERT(IS_PAGE_PRESENT(*pte) && IS_LARGE_PAGE(*pte));
    volatile pt_entry_t* m = _map_alloc_page();
    if (m == nullptr) {
        return ZX_ERR_NO_MEMORY;
    }

    paddr_t paddr_base = paddr_from_pte(level, *pte);
    PtFlags flags = split_flags(level, *pte & X86_LARGE_FLAGS_MASK);

    DEBUG_ASSERT(page_aligned(level, vaddr));
    vaddr_t new_vaddr = vaddr;
    paddr_t new_paddr = paddr_base;
    size_t ps = page_size(lower_level(level));
    for (int i = 0; i < NO_OF_PT_ENTRIES; i++) {
        volatile pt_entry_t* e = m + i;
        // If this is a PDP_L (i.e. huge page), flags will include the
        // PS bit still, so the new PD entries will be large pages.
        UpdateEntry(lower_level(level), new_vaddr, e, new_paddr, flags);
        new_vaddr += ps;
        new_paddr += ps;
    }
    DEBUG_ASSERT(new_vaddr == vaddr + page_size(level));

    flags = intermediate_flags();
    UpdateEntry(level, vaddr, pte, X86_VIRT_TO_PHYS(m), flags);
    pages_++;
    return ZX_OK;
}

/*
 * @brief given a page table entry, return a pointer to the next page table one level down
 */
static inline volatile pt_entry_t* get_next_table_from_entry(pt_entry_t entry) {
    if (!IS_PAGE_PRESENT(entry) || IS_LARGE_PAGE(entry))
        return nullptr;

    return reinterpret_cast<volatile pt_entry_t*>(X86_PHYS_TO_VIRT(entry & X86_PG_FRAME));
}

/**
 * @brief  Walk the page table structures returning the entry and level that maps the address.
 *
 * @param table The top-level paging structure's virtual address
 * @param vaddr The virtual address to retrieve the mapping for
 * @param ret_level The level of the table that defines the found mapping
 * @param mapping The mapping that was found
 *
 * @return ZX_OK if mapping is found
 * @return ZX_ERR_NOT_FOUND if mapping is not found
 */
zx_status_t X86PageTableBase::GetMapping(volatile pt_entry_t* table, vaddr_t vaddr,
                                         page_table_levels level,
                                         page_table_levels* ret_level,
                                         volatile pt_entry_t** mapping) {
    DEBUG_ASSERT(table);
    DEBUG_ASSERT(ret_level);
    DEBUG_ASSERT(mapping);

    if (level == PT_L) {
        return GetMappingL0(table, vaddr, ret_level, mapping);
    }

    LTRACEF_LEVEL(2, "table %p\n", table);

    uint index = vaddr_to_index(level, vaddr);
    volatile pt_entry_t* e = table + index;
    pt_entry_t pt_val = *e;
    if (!IS_PAGE_PRESENT(pt_val))
        return ZX_ERR_NOT_FOUND;

    /* if this is a large page, stop here */
    if (IS_LARGE_PAGE(pt_val)) {
        *mapping = e;
        *ret_level = level;
        return ZX_OK;
    }

    volatile pt_entry_t* next_table = get_next_table_from_entry(pt_val);
    return GetMapping(next_table, vaddr, lower_level(level), ret_level, mapping);
}

zx_status_t X86PageTableBase::GetMappingL0(volatile pt_entry_t* table, vaddr_t vaddr,
                                           page_table_levels* ret_level,
                                           volatile pt_entry_t** mapping) {
    /* do the final page table lookup */
    uint index = vaddr_to_index(PT_L, vaddr);
    volatile pt_entry_t* e = table + index;
    if (!IS_PAGE_PRESENT(*e))
        return ZX_ERR_NOT_FOUND;

    *mapping = e;
    *ret_level = PT_L;
    return ZX_OK;
}

/**
 * @brief Unmaps the range specified by start_cursor.
 *
 * Level must be MAX_PAGING_LEVEL when invoked.
 *
 * @param table The top-level paging structure's virtual address.
 * @param start_cursor A cursor describing the range of address space to
 * unmap within table
 * @param new_cursor A returned cursor describing how much work was not
 * completed.  Must be non-null.
 *
 * @return true if at least one page was unmapped at this level
 */
bool X86PageTableBase::RemoveMapping(volatile pt_entry_t* table, page_table_levels level,
                                     const MappingCursor& start_cursor, MappingCursor* new_cursor) {
    DEBUG_ASSERT(table);
    LTRACEF("L: %d, %016" PRIxPTR " %016zx\n", level, start_cursor.vaddr,
            start_cursor.size);
    DEBUG_ASSERT(x86_mmu_check_vaddr(start_cursor.vaddr));

    if (level == PT_L) {
        return RemoveMappingL0(table, start_cursor, new_cursor);
    }

    *new_cursor = start_cursor;

    bool unmapped = false;
    size_t ps = page_size(level);
    uint index = vaddr_to_index(level, new_cursor->vaddr);
    for (; index != NO_OF_PT_ENTRIES && new_cursor->size != 0; ++index) {
        volatile pt_entry_t* e = table + index;
        pt_entry_t pt_val = *e;
        // If the page isn't even mapped, just skip it
        if (!IS_PAGE_PRESENT(pt_val)) {
            new_cursor->SkipEntry(level);
            DEBUG_ASSERT(new_cursor->size <= start_cursor.size);
            continue;
        }

        if (IS_LARGE_PAGE(pt_val)) {
            bool vaddr_level_aligned = page_aligned(level, new_cursor->vaddr);
            // If the request covers the entire large page, just unmap it
            if (vaddr_level_aligned && new_cursor->size >= ps) {
                UnmapEntry(level, new_cursor->vaddr, e);
                unmapped = true;

                new_cursor->vaddr += ps;
                new_cursor->size -= ps;
                DEBUG_ASSERT(new_cursor->size <= start_cursor.size);
                continue;
            }
            // Otherwise, we need to split it
            vaddr_t page_vaddr = new_cursor->vaddr & ~(ps - 1);
            zx_status_t status = SplitLargePage(level, page_vaddr, e);
            if (status != ZX_OK) {
                // If split fails, just unmap the whole thing, and let a
                // subsequent page fault clean it up.
                UnmapEntry(level, new_cursor->vaddr, e);
                unmapped = true;

                new_cursor->SkipEntry(level);
                DEBUG_ASSERT(new_cursor->size <= start_cursor.size);
            }
            pt_val = *e;
        }

        MappingCursor cursor;
        volatile pt_entry_t* next_table = get_next_table_from_entry(pt_val);
        bool lower_unmapped = RemoveMapping(next_table, lower_level(level),
                                            *new_cursor, &cursor);

        // If we were requesting to unmap everything in the lower page table,
        // we know we can unmap the lower level page table.  Otherwise, if
        // we unmapped anything in the lower level, check to see if that
        // level is now empty.
        bool unmap_page_table =
            page_aligned(level, new_cursor->vaddr) && new_cursor->size >= ps;
        if (!unmap_page_table && lower_unmapped) {
            uint lower_idx;
            for (lower_idx = 0; lower_idx < NO_OF_PT_ENTRIES; ++lower_idx) {
                if (IS_PAGE_PRESENT(next_table[lower_idx])) {
                    break;
                }
            }
            if (lower_idx == NO_OF_PT_ENTRIES) {
                unmap_page_table = true;
            }
        }
        if (unmap_page_table) {
            paddr_t ptable_phys = X86_VIRT_TO_PHYS(next_table);
            LTRACEF("L: %d free pt v %#" PRIxPTR " phys %#" PRIxPTR "\n",
                    level, (uintptr_t)next_table, ptable_phys);

            UnmapEntry(level, new_cursor->vaddr, e);
            vm_page_t* page = paddr_to_vm_page(ptable_phys);

            DEBUG_ASSERT(page);
            DEBUG_ASSERT_MSG(page->state == VM_PAGE_STATE_MMU,
                             "page %p state %u, paddr %#" PRIxPTR "\n", page, page->state,
                             X86_VIRT_TO_PHYS(next_table));

            pmm_free_page(page);
            pages_--;
            unmapped = true;
        }
        *new_cursor = cursor;
        DEBUG_ASSERT(new_cursor->size <= start_cursor.size);

        DEBUG_ASSERT(new_cursor->size == 0 || page_aligned(level, new_cursor->vaddr));
    }

    return unmapped;
}

// Base case of RemoveMapping for smallest page size.
bool X86PageTableBase::RemoveMappingL0(volatile pt_entry_t* table,
                                       const MappingCursor& start_cursor,
                                       MappingCursor* new_cursor) {
    LTRACEF("%016" PRIxPTR " %016zx\n", start_cursor.vaddr, start_cursor.size);
    DEBUG_ASSERT(IS_PAGE_ALIGNED(start_cursor.size));

    *new_cursor = start_cursor;

    bool unmapped = false;
    uint index = vaddr_to_index(PT_L, new_cursor->vaddr);
    for (; index != NO_OF_PT_ENTRIES && new_cursor->size != 0; ++index) {
        volatile pt_entry_t* e = table + index;
        if (IS_PAGE_PRESENT(*e)) {
            UnmapEntry(PT_L, new_cursor->vaddr, e);
            unmapped = true;
        }

        new_cursor->vaddr += PAGE_SIZE;
        new_cursor->size -= PAGE_SIZE;
        DEBUG_ASSERT(new_cursor->size <= start_cursor.size);
    }
    return unmapped;
}

/**
 * @brief Creates mappings for the range specified by start_cursor
 *
 * Level must be MAX_PAGING_LEVEL when invoked.
 *
 * @param table The top-level paging structure's virtual address.
 * @param start_cursor A cursor describing the range of address space to
 * act on within table
 * @param new_cursor A returned cursor describing how much work was not
 * completed.  Must be non-null.
 *
 * @return ZX_OK if successful
 * @return ZX_ERR_ALREADY_EXISTS if the range overlaps an existing mapping
 * @return ZX_ERR_NO_MEMORY if intermediate page tables could not be allocated
 */
zx_status_t X86PageTableBase::AddMapping(volatile pt_entry_t* table, uint mmu_flags,
                                         page_table_levels level, const MappingCursor& start_cursor,
                                         MappingCursor* new_cursor) {
    DEBUG_ASSERT(table);
    DEBUG_ASSERT(x86_mmu_check_vaddr(start_cursor.vaddr));
    DEBUG_ASSERT(x86_mmu_check_paddr(start_cursor.paddr));

    zx_status_t ret = ZX_OK;
    *new_cursor = start_cursor;

    if (level == PT_L) {
        return AddMappingL0(table, mmu_flags, start_cursor, new_cursor);
    }

    // Disable thread safety analysis, since Clang has trouble noticing that
    // lock_ is held when RemoveMapping is called.
    auto abort = fbl::MakeAutoCall([&]() TA_NO_THREAD_SAFETY_ANALYSIS {
        if (level == MAX_PAGING_LEVEL) {
            MappingCursor cursor = start_cursor;
            MappingCursor result;
            // new_cursor->size should be how much is left to be mapped still
            cursor.size -= new_cursor->size;
            if (cursor.size > 0) {
                RemoveMapping(table, MAX_PAGING_LEVEL, cursor, &result);
                DEBUG_ASSERT(result.size == 0);
            }
        }
    });

    X86PageTableBase::IntermediatePtFlags interm_flags = intermediate_flags();
    X86PageTableBase::PtFlags term_flags = terminal_flags(level, mmu_flags);

    size_t ps = page_size(level);
    bool level_supports_large_pages = supports_page_size(level);
    uint index = vaddr_to_index(level, new_cursor->vaddr);
    for (; index != NO_OF_PT_ENTRIES && new_cursor->size != 0; ++index) {
        volatile pt_entry_t* e = table + index;
        pt_entry_t pt_val = *e;
        // See if there's a large page in our way
        if (IS_PAGE_PRESENT(pt_val) && IS_LARGE_PAGE(pt_val)) {
            return ZX_ERR_ALREADY_EXISTS;
        }

        // Check if this is a candidate for a new large page
        bool level_valigned = page_aligned(level, new_cursor->vaddr);
        bool level_paligned = page_aligned(level, new_cursor->paddr);
        if (level_supports_large_pages && !IS_PAGE_PRESENT(pt_val) && level_valigned &&
            level_paligned && new_cursor->size >= ps) {

            UpdateEntry(level, new_cursor->vaddr, table + index,
                        new_cursor->paddr, term_flags | X86_MMU_PG_PS);

            new_cursor->paddr += ps;
            new_cursor->vaddr += ps;
            new_cursor->size -= ps;
            DEBUG_ASSERT(new_cursor->size <= start_cursor.size);
        } else {
            // See if we need to create a new table
            if (!IS_PAGE_PRESENT(pt_val)) {
                volatile pt_entry_t* m = _map_alloc_page();
                if (m == nullptr) {
                    return ZX_ERR_NO_MEMORY;
                }

                LTRACEF_LEVEL(2, "new table %p at level %d\n", m, level);

                UpdateEntry(level, new_cursor->vaddr, e,
                            X86_VIRT_TO_PHYS(m), interm_flags);
                pt_val = *e;
                pages_++;
            }

            MappingCursor cursor;
            ret = AddMapping(get_next_table_from_entry(pt_val), mmu_flags,
                             lower_level(level), *new_cursor, &cursor);
            *new_cursor = cursor;
            DEBUG_ASSERT(new_cursor->size <= start_cursor.size);
            if (ret != ZX_OK) {
                return ret;
            }
        }
    }
    abort.cancel();
    return ZX_OK;
}

// Base case of AddMapping for smallest page size.
zx_status_t X86PageTableBase::AddMappingL0(volatile pt_entry_t* table, uint mmu_flags,
                                           const MappingCursor& start_cursor,
                                           MappingCursor* new_cursor) {
    DEBUG_ASSERT(IS_PAGE_ALIGNED(start_cursor.size));

    *new_cursor = start_cursor;

    X86PageTableBase::PtFlags term_flags = terminal_flags(PT_L, mmu_flags);

    uint index = vaddr_to_index(PT_L, new_cursor->vaddr);
    for (; index != NO_OF_PT_ENTRIES && new_cursor->size != 0; ++index) {
        volatile pt_entry_t* e = table + index;
        if (IS_PAGE_PRESENT(*e)) {
            return ZX_ERR_ALREADY_EXISTS;
        }

        UpdateEntry(PT_L, new_cursor->vaddr, e, new_cursor->paddr, term_flags);

        new_cursor->paddr += PAGE_SIZE;
        new_cursor->vaddr += PAGE_SIZE;
        new_cursor->size -= PAGE_SIZE;
        DEBUG_ASSERT(new_cursor->size <= start_cursor.size);
    }

    return ZX_OK;
}

/**
 * @brief Changes the permissions/caching of the range specified by start_cursor
 *
 * Level must be MAX_PAGING_LEVEL when invoked.
 *
 * @param table The top-level paging structure's virtual address.
 * @param start_cursor A cursor describing the range of address space to
 * act on within table
 * @param new_cursor A returned cursor describing how much work was not
 * completed.  Must be non-null.
 */
zx_status_t X86PageTableBase::UpdateMapping(volatile pt_entry_t* table, uint mmu_flags,
                                            page_table_levels level, const MappingCursor& start_cursor,
                                            MappingCursor* new_cursor) {
    DEBUG_ASSERT(table);
    LTRACEF("L: %d, %016" PRIxPTR " %016zx\n", level, start_cursor.vaddr,
            start_cursor.size);
    DEBUG_ASSERT(x86_mmu_check_vaddr(start_cursor.vaddr));

    if (level == PT_L) {
        return UpdateMappingL0(table, mmu_flags, start_cursor, new_cursor);
    }

    zx_status_t ret = ZX_OK;
    *new_cursor = start_cursor;

    X86PageTableBase::PtFlags term_flags = terminal_flags(level, mmu_flags);

    size_t ps = page_size(level);
    uint index = vaddr_to_index(level, new_cursor->vaddr);
    for (; index != NO_OF_PT_ENTRIES && new_cursor->size != 0; ++index) {
        volatile pt_entry_t* e = table + index;
        pt_entry_t pt_val = *e;
        // Skip unmapped pages (we may encounter these due to demand paging)
        if (!IS_PAGE_PRESENT(pt_val)) {
            new_cursor->SkipEntry(level);
            continue;
        }

        if (IS_LARGE_PAGE(pt_val)) {
            bool vaddr_level_aligned = page_aligned(level, new_cursor->vaddr);
            // If the request covers the entire large page, just change the
            // permissions
            if (vaddr_level_aligned && new_cursor->size >= ps) {
                UpdateEntry(level, new_cursor->vaddr, e,
                            paddr_from_pte(level, pt_val),
                            term_flags | X86_MMU_PG_PS);

                new_cursor->vaddr += ps;
                new_cursor->size -= ps;
                DEBUG_ASSERT(new_cursor->size <= start_cursor.size);
                continue;
            }
            // Otherwise, we need to split it
            vaddr_t page_vaddr = new_cursor->vaddr & ~(ps - 1);
            ret = SplitLargePage(level, page_vaddr, e);
            if (ret != ZX_OK) {
                // If we failed to split the table, just unmap it.  Subsequent
                // page faults will bring it back in.
                MappingCursor cursor;
                cursor.vaddr = new_cursor->vaddr;
                cursor.size = ps;

                MappingCursor tmp_cursor;
                RemoveMapping(table, level, cursor, &tmp_cursor);

                new_cursor->SkipEntry(level);
            }
            pt_val = *e;
        }

        MappingCursor cursor;
        volatile pt_entry_t* next_table = get_next_table_from_entry(pt_val);
        ret = UpdateMapping(next_table, mmu_flags, lower_level(level),
                            *new_cursor, &cursor);
        *new_cursor = cursor;
        if (ret != ZX_OK) {
            // Currently this can't happen
            ASSERT(false);
        }
        DEBUG_ASSERT(new_cursor->size <= start_cursor.size);
        DEBUG_ASSERT(new_cursor->size == 0 || page_aligned(level, new_cursor->vaddr));
    }
    return ZX_OK;
}

// Base case of UpdateMapping for smallest page size.
zx_status_t X86PageTableBase::UpdateMappingL0(volatile pt_entry_t* table,
                                              uint mmu_flags,
                                              const MappingCursor& start_cursor,
                                              MappingCursor* new_cursor) {
    LTRACEF("%016" PRIxPTR " %016zx\n", start_cursor.vaddr, start_cursor.size);
    DEBUG_ASSERT(IS_PAGE_ALIGNED(start_cursor.size));

    *new_cursor = start_cursor;

    X86PageTableBase::PtFlags term_flags = terminal_flags(PT_L, mmu_flags);

    uint index = vaddr_to_index(PT_L, new_cursor->vaddr);
    for (; index != NO_OF_PT_ENTRIES && new_cursor->size != 0; ++index) {
        volatile pt_entry_t* e = table + index;
        pt_entry_t pt_val = *e;
        // Skip unmapped pages (we may encounter these due to demand paging)
        if (IS_PAGE_PRESENT(pt_val)) {
            UpdateEntry(PT_L, new_cursor->vaddr, e, paddr_from_pte(PT_L, pt_val), term_flags);
        }

        new_cursor->vaddr += PAGE_SIZE;
        new_cursor->size -= PAGE_SIZE;
        DEBUG_ASSERT(new_cursor->size <= start_cursor.size);
    }
    DEBUG_ASSERT(new_cursor->size == 0 || page_aligned(PT_L, new_cursor->vaddr));
    return ZX_OK;
}

zx_status_t X86PageTableBase::UnmapPages(vaddr_t vaddr, const size_t count,
                                         size_t* unmapped) {
    LTRACEF("aspace %p, vaddr %#" PRIxPTR ", count %#zx\n", this, vaddr, count);

    canary_.Assert();
    fbl::AutoLock a(&lock_);

    if (!x86_mmu_check_vaddr(vaddr))
        return ZX_ERR_INVALID_ARGS;
    if (count == 0)
        return ZX_OK;

    DEBUG_ASSERT(virt_);

    MappingCursor start = {
        .paddr = 0, .vaddr = vaddr, .size = count * PAGE_SIZE,
    };

    MappingCursor result;
    RemoveMapping(virt_, MAX_PAGING_LEVEL, start, &result);
    DEBUG_ASSERT(result.size == 0);

    if (unmapped)
        *unmapped = count;

    return ZX_OK;
}

zx_status_t X86PageTableBase::MapPages(vaddr_t vaddr, paddr_t paddr,
                                       const size_t count, uint mmu_flags,
                                       size_t* mapped) {
    canary_.Assert();
    fbl::AutoLock a(&lock_);

    LTRACEF("aspace %p, vaddr %#" PRIxPTR " paddr %#" PRIxPTR " count %#zx mmu_flags 0x%x\n",
            this, vaddr, paddr, count, mmu_flags);

    if ((!x86_mmu_check_paddr(paddr)))
        return ZX_ERR_INVALID_ARGS;
    if (!x86_mmu_check_vaddr(vaddr))
        return ZX_ERR_INVALID_ARGS;
    if (count == 0)
        return ZX_OK;

    if (!(mmu_flags & ARCH_MMU_FLAG_PERM_READ))
        return ZX_ERR_INVALID_ARGS;

    DEBUG_ASSERT(virt_);

    MappingCursor start = {
        .paddr = paddr, .vaddr = vaddr, .size = count * PAGE_SIZE,
    };
    MappingCursor result;
    zx_status_t status = AddMapping(virt_, mmu_flags, MAX_PAGING_LEVEL, start, &result);
    if (status != ZX_OK) {
        dprintf(SPEW, "Add mapping failed with err=%d\n", status);
        return status;
    }
    DEBUG_ASSERT(result.size == 0);

    if (mapped)
        *mapped = count;

    return ZX_OK;
}

zx_status_t X86PageTableBase::ProtectPages(vaddr_t vaddr, size_t count, uint mmu_flags) {
    canary_.Assert();
    fbl::AutoLock a(&lock_);

    LTRACEF("aspace %p, vaddr %#" PRIxPTR " count %#zx mmu_flags 0x%x\n",
            this, vaddr, count, mmu_flags);

    if (!x86_mmu_check_vaddr(vaddr))
        return ZX_ERR_INVALID_ARGS;
    if (count == 0)
        return ZX_OK;

    if (!(mmu_flags & ARCH_MMU_FLAG_PERM_READ))
        return ZX_ERR_INVALID_ARGS;

    MappingCursor start = {
        .paddr = 0, .vaddr = vaddr, .size = count * PAGE_SIZE,
    };
    MappingCursor result;
    zx_status_t status = UpdateMapping(virt_, mmu_flags, MAX_PAGING_LEVEL, start, &result);
    if (status != ZX_OK) {
        return status;
    }
    DEBUG_ASSERT(result.size == 0);
    return ZX_OK;
}

zx_status_t X86PageTableBase::QueryVaddr(vaddr_t vaddr, paddr_t* paddr, uint* mmu_flags) {
    canary_.Assert();
    fbl::AutoLock a(&lock_);

    page_table_levels ret_level;

    LTRACEF("aspace %p, vaddr %#" PRIxPTR ", paddr %p, mmu_flags %p\n", this, vaddr, paddr,
            mmu_flags);

    volatile pt_entry_t* last_valid_entry;
    zx_status_t status = GetMapping(virt_, vaddr, MAX_PAGING_LEVEL, &ret_level, &last_valid_entry);
    if (status != ZX_OK)
        return status;

    DEBUG_ASSERT(last_valid_entry);
    LTRACEF("last_valid_entry (%p) 0x%" PRIxPTE ", level %d\n", last_valid_entry, *last_valid_entry,
            ret_level);

    /* based on the return level, parse the page table entry */
    if (paddr) {
        switch (ret_level) {
        case PDP_L: /* 1GB page */
            *paddr = paddr_from_pte(PDP_L, *last_valid_entry);
            *paddr |= vaddr & PAGE_OFFSET_MASK_HUGE;
            break;
        case PD_L: /* 2MB page */
            *paddr = paddr_from_pte(PD_L, *last_valid_entry);
            *paddr |= vaddr & PAGE_OFFSET_MASK_LARGE;
            break;
        case PT_L: /* 4K page */
            *paddr = paddr_from_pte(PT_L, *last_valid_entry);
            *paddr |= vaddr & PAGE_OFFSET_MASK_4KB;
            break;
        default:
            panic("arch_mmu_query: unhandled frame level\n");
        }

        LTRACEF("paddr %#" PRIxPTR "\n", *paddr);
    }

    /* converting arch-specific flags to mmu flags */
    if (mmu_flags) {
        *mmu_flags = pt_flags_to_mmu_flags(*last_valid_entry, ret_level);
    }

    return ZX_OK;
}

void x86_mmu_early_init() {
    x86_mmu_percpu_init();

    x86_mmu_mem_type_init();

    // Unmap the lower identity mapping.
    pml4[0] = 0;
    x86_tlb_invalidate_page(nullptr, 0, PML4_L, false);

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

zx_status_t X86PageTableBase::Init(void* ctx) {
    /* allocate a top level page table for the new address space */
    paddr_t pa;
    vm_page_t* p;
    virt_ = (pt_entry_t*)pmm_alloc_kpage(&pa, &p);
    if (!virt_) {
        TRACEF("error allocating top level page directory\n");
        return ZX_ERR_NO_MEMORY;
    }
    phys_ = pa;
    p->state = VM_PAGE_STATE_MMU;

    // TODO(abdulla): Remove when PMM returns pre-zeroed pages.
    arch_zero_page(virt_);

    ctx_ = ctx;
    pages_ = 1;
    return ZX_OK;
}

zx_status_t X86PageTableMmu::InitKernel(void* ctx) {
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

zx_status_t X86PageTableBase::Destroy(vaddr_t base, size_t size) {
    canary_.Assert();

#if LK_DEBUGLEVEL > 1
    pt_entry_t* table = static_cast<pt_entry_t*>(virt_);
    uint start = vaddr_to_index(MAX_PAGING_LEVEL, base);
    uint end = vaddr_to_index(MAX_PAGING_LEVEL, base + size - 1);

    // Don't check start if that table is shared with another aspace.
    if (!page_aligned(MAX_PAGING_LEVEL, base)) {
        start += 1;
    }
    // Do check the end if it fills out the table entry.
    if (page_aligned(MAX_PAGING_LEVEL, base + size)) {
        end += 1;
    }

    for (uint i = start; i < end; ++i) {
        DEBUG_ASSERT(!IS_PAGE_PRESENT(table[i]));
    }
#endif

    pmm_free_page(paddr_to_vm_page(phys_));
    phys_ = 0;
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

    return pt_->Destroy(base_, size_);
}

zx_status_t X86ArchVmAspace::Unmap(vaddr_t vaddr, size_t count, size_t* unmapped) {
    if (!IsValidVaddr(vaddr))
        return ZX_ERR_INVALID_ARGS;

    return pt_->UnmapPages(vaddr, count, unmapped);
}

zx_status_t X86ArchVmAspace::Map(vaddr_t vaddr, paddr_t paddr, size_t count,
                                 uint mmu_flags, size_t* mapped) {
    if (!IsValidVaddr(vaddr))
        return ZX_ERR_INVALID_ARGS;

    if (flags_ & ARCH_ASPACE_FLAG_GUEST) {
        if (mmu_flags & ~kValidEptFlags)
            return ZX_ERR_INVALID_ARGS;
    }
    return pt_->MapPages(vaddr, paddr, count, mmu_flags, mapped);
}

zx_status_t X86ArchVmAspace::Protect(vaddr_t vaddr, size_t count, uint mmu_flags) {
    if (!IsValidVaddr(vaddr))
        return ZX_ERR_INVALID_ARGS;

    if (flags_ & ARCH_ASPACE_FLAG_GUEST) {
        if (mmu_flags & ~kValidEptFlags)
            return ZX_ERR_INVALID_ARGS;
    }
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
