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
#include <kernel/vm.h>
#include <vm/arch_vm_aspace.h>
#include <vm/pmm.h>
#include <fbl/auto_lock.h>

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
static const paddr_t kernel_pt_phys = (vaddr_t)KERNEL_PT - KERNEL_BASE;

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
struct tlb_invalidate_page_context {
    ulong target_cr3;
    vaddr_t vaddr;
    enum page_table_levels level;
    bool global_page;
};
static void tlb_invalidate_page_task(void* raw_context) {
    DEBUG_ASSERT(arch_ints_disabled());
    tlb_invalidate_page_context* context = (tlb_invalidate_page_context*)raw_context;

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
 * @param aspace The aspace we're invalidating for (if NULL, assume for current one)
 * @param vaddr The virtual address we are invalidating the TLB entry for
 * @param level The page table level that maps this vaddr
 * @param global_page True if we are invalidating a global mapping
 *
 * TODO(MG-979): Optimize this.  This is horribly inefficient.
 * We should also change this to pool invalidations from a single
 * "transaction" and then only execute a single mp_sync_exec for that
 * transaction, rather than one per page.
 */
static void x86_tlb_invalidate_page(X86ArchVmAspace* aspace, vaddr_t vaddr,
                                    enum page_table_levels level, bool global_page) {
    ulong cr3 = aspace ? aspace->pt_phys() : x86_get_cr3();
    struct tlb_invalidate_page_context task_context = {
        .target_cr3 = cr3, .vaddr = vaddr, .level = level, .global_page = global_page,
    };

    /* Target only CPUs this aspace is active on.  It may be the case that some
     * other CPU will become active in it after this load, or will have left it
     * just before this load.  In the former case, it is becoming active after
     * the write to the page table, so it will see the change.  In the latter
     * case, it will get a spurious request to flush. */
    mp_ipi_target_t target;
    mp_cpu_mask_t target_mask = 0;
    if (global_page || aspace == nullptr) {
        target = MP_IPI_TARGET_ALL;
    } else {
        target = MP_IPI_TARGET_MASK;
        target_mask = aspace->active_cpus();
    }

    mp_sync_exec(target, target_mask, tlb_invalidate_page_task, &task_context);
}

template <int Level>
struct PageTableBase {
    static constexpr page_table_levels level = static_cast<page_table_levels>(Level);
    static_assert(Level >= 0, "Level too low");
    static_assert(Level < X86_PAGING_LEVELS, "level too high");

    /**
     * @brief Return the page size for this level
     */
    static size_t page_size() {
        switch (Level) {
        case PT_L:
            return 1ULL << PT_SHIFT;
        case PD_L:
            return 1ULL << PD_SHIFT;
        case PDP_L:
            return 1ULL << PDP_SHIFT;
        case PML4_L:
            return 1ULL << PML4_SHIFT;
        default:
            panic("page_size: invalid level\n");
        }
    }

    /**
     * @brief Whether the processor supports the page size of this level
     */
    static bool supports_page_size() {
        DEBUG_ASSERT(Level != PT_L);
        switch (Level) {
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

    /**
     * @brief Whether an address is aligned to the page size of this level
     */
    static bool page_aligned(vaddr_t vaddr) {
        return (vaddr & (page_size() - 1)) == 0;
    }

    static uint vaddr_to_index(vaddr_t vaddr) {
        switch (Level) {
        case PML4_L:
            return VADDR_TO_PML4_INDEX(vaddr);
        case PDP_L:
            return VADDR_TO_PDP_INDEX(vaddr);
        case PD_L:
            return VADDR_TO_PD_INDEX(vaddr);
        case PT_L:
            return VADDR_TO_PT_INDEX(vaddr);
        default:
            panic("vaddr_to_index: invalid level\n");
        }
    }

    /*
     * @brief Convert a PTE to a physical address
     */
    static paddr_t paddr_from_pte(pt_entry_t pte) {
        DEBUG_ASSERT(IS_PAGE_PRESENT(pte));

        paddr_t pa;
        switch (Level) {
        case PDP_L:
            pa = (pte & X86_HUGE_PAGE_FRAME);
            break;
        case PD_L:
            pa = (pte & X86_LARGE_PAGE_FRAME);
            break;
        case PT_L:
            pa = (pte & X86_PG_FRAME);
            break;
        default:
            panic("paddr_from_pte at unhandled level %d\n", Level);
        }

        LTRACEF_LEVEL(2, "pte 0x%" PRIxPTE " , level %d, paddr %#" PRIxPTR "\n", pte, Level, pa);

        return pa;
    }
};

template <int Level>
struct PageTable : PageTableBase<Level> {
    using Base = PageTableBase<Level>;
    using LowerTable = PageTable<Level - 1>;
    using TopTable = PageTable<MAX_PAGING_LEVEL>;

    /**
     * @brief Return x86 arch flags for intermediate tables
     */
    static arch_flags_t intermediate_arch_flags() {
        return X86_MMU_PG_RW | X86_MMU_PG_U;
    }

    /**
     * @brief Return x86 arch flags from generic MMU flags
     *
     * These are used for page mapping entries in the table.
     */
    static arch_flags_t arch_flags(uint aspace_flags, uint flags) {
        arch_flags_t arch_flags = 0;

        if (flags & ARCH_MMU_FLAG_PERM_WRITE)
            arch_flags |= X86_MMU_PG_RW;

        if (flags & ARCH_MMU_FLAG_PERM_USER)
            arch_flags |= X86_MMU_PG_U;

        if (aspace_flags & ARCH_ASPACE_FLAG_KERNEL) {
            /* setting global flag for kernel pages */
            arch_flags |= X86_MMU_PG_G;
        }

        if (!(flags & ARCH_MMU_FLAG_PERM_EXECUTE))
            arch_flags |= X86_MMU_PG_NX;

        if (Level > 0) {
            switch (flags & ARCH_MMU_FLAG_CACHE_MASK) {
            case ARCH_MMU_FLAG_CACHED:
                arch_flags |= X86_MMU_LARGE_PAT_WRITEBACK;
                break;
            case ARCH_MMU_FLAG_UNCACHED_DEVICE:
            case ARCH_MMU_FLAG_UNCACHED:
                arch_flags |= X86_MMU_LARGE_PAT_UNCACHABLE;
                break;
            case ARCH_MMU_FLAG_WRITE_COMBINING:
                arch_flags |= X86_MMU_LARGE_PAT_WRITE_COMBINING;
                break;
            default:
                PANIC_UNIMPLEMENTED;
            }
        } else {
            switch (flags & ARCH_MMU_FLAG_CACHE_MASK) {
            case ARCH_MMU_FLAG_CACHED:
                arch_flags |= X86_MMU_PTE_PAT_WRITEBACK;
                break;
            case ARCH_MMU_FLAG_UNCACHED_DEVICE:
            case ARCH_MMU_FLAG_UNCACHED:
                arch_flags |= X86_MMU_PTE_PAT_UNCACHABLE;
                break;
            case ARCH_MMU_FLAG_WRITE_COMBINING:
                arch_flags |= X86_MMU_PTE_PAT_WRITE_COMBINING;
                break;
            default:
                PANIC_UNIMPLEMENTED;
            }
        }

        return arch_flags;
    }

    /**
     * @brief Return the x86 arch flags to split a large page into smaller pages
     */
    static arch_flags_t split_arch_flags(arch_flags_t arch_flags) {
        static_assert(Level != PT_L, "tried to split PT_L");
        // This can't easily be a static assert without duplicating
        // a bunch of code in the callers
        DEBUG_ASSERT(Level != PML4_L);
        DEBUG_ASSERT(arch_flags & X86_MMU_PG_PS);
        if (Level == PD_L) {
            // Note: Clear PS before the check below; the PAT bit for a PTE is the
            // the same as the PS bit for a higher table entry.
            arch_flags &= ~X86_MMU_PG_PS;

            /* If the larger page had the PAT flag set, make sure it's
             * transferred to the different index for a PTE */
            if (arch_flags & X86_MMU_PG_LARGE_PAT) {
                arch_flags &= ~X86_MMU_PG_LARGE_PAT;
                arch_flags |= X86_MMU_PG_PTE_PAT;
            }
        }
        return arch_flags;
    }

    /**
     * @brief Invalidate a single page at a given page table level
     */
    static void tlb_invalidate_page(X86ArchVmAspace* aspace, vaddr_t vaddr, bool global_page) {
        x86_tlb_invalidate_page(aspace, vaddr, Base::level, global_page);
    }
};

template <int Level>
struct ExtendedPageTable : PageTableBase<Level> {
    using LowerTable = ExtendedPageTable<Level - 1>;
    using TopTable = ExtendedPageTable<MAX_PAGING_LEVEL>;

    /**
     * @brief Return EPT arch flags for intermediate tables
     */
    static arch_flags_t intermediate_arch_flags() {
        return X86_EPT_R | X86_EPT_W | X86_EPT_X;
    }

    /**
     * @brief Return EPT arch flags from generic MMU flags
     *
     * These are used for page mapping entries in the table.
     */
    static arch_flags_t arch_flags(uint aspace_flags, uint flags) {
        // Only the write-back memory type is supported.
        arch_flags_t arch_flags = X86_EPT_WB;

        if (flags & ARCH_MMU_FLAG_PERM_READ)
            arch_flags |= X86_EPT_R;

        if (flags & ARCH_MMU_FLAG_PERM_WRITE)
            arch_flags |= X86_EPT_W;

        if (flags & ARCH_MMU_FLAG_PERM_EXECUTE)
            arch_flags |= X86_EPT_X;

        return arch_flags;
    }

    /**
     * @brief Return the EPT arch flags to split a large page into smaller pages
     */
    static arch_flags_t split_arch_flags(arch_flags_t arch_flags) {
        static_assert(Level != PT_L, "tried to split PT_L");
        DEBUG_ASSERT(Level != PML4_L);
        // We don't need to relocate any flags on split for EPT.
        return arch_flags;
    }

    /**
     * @brief Invalidate a single page at a given page table level
     */
    static void tlb_invalidate_page(X86ArchVmAspace* aspace, vaddr_t vaddr, bool global_page) {
        // TODO(MG-981): Implement this.
    }
};

/**
 * @brief Return generic MMU flags from x86 arch flags
 */
static uint x86_mmu_flags(arch_flags_t flags, enum page_table_levels level) {
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

/**
 * @brief Return generic MMU flags from EPT arch flags
 */
static uint ept_mmu_flags(arch_flags_t flags, enum page_table_levels level) {
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
    template <typename PageTable>
    void SkipEntry() {
        const size_t ps = PageTable::page_size();
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

template <typename PageTable>
void X86ArchVmAspace::UpdateEntry(vaddr_t vaddr, volatile pt_entry_t* pte, paddr_t paddr,
                                  arch_flags_t flags) {
    DEBUG_ASSERT(pte);
    DEBUG_ASSERT(IS_PAGE_ALIGNED(paddr));

    pt_entry_t olde = *pte;

    /* set the new entry */
    *pte = paddr | flags | X86_MMU_PG_P;

    /* attempt to invalidate the page */
    if (IS_PAGE_PRESENT(olde)) {
        PageTable::tlb_invalidate_page(this, vaddr, is_kernel_address(vaddr));
    }
}

template <typename PageTable>
void X86ArchVmAspace::UnmapEntry(X86ArchVmAspace* aspace, vaddr_t vaddr, volatile pt_entry_t* pte) {
    DEBUG_ASSERT(pte);

    pt_entry_t olde = *pte;

    *pte = 0;

    /* attempt to invalidate the page */
    if (IS_PAGE_PRESENT(olde)) {
        PageTable::tlb_invalidate_page(aspace, vaddr, is_kernel_address(vaddr));
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
template <typename PageTable>
status_t X86ArchVmAspace::SplitLargePage(vaddr_t vaddr, volatile pt_entry_t* pte) {
    static_assert(PageTable::level != PT_L, "tried splitting PT_L");
    LTRACEF_LEVEL(2, "splitting table %p at level %d\n", pte, PageTable::level);

    DEBUG_ASSERT(IS_PAGE_PRESENT(*pte) && IS_LARGE_PAGE(*pte));
    volatile pt_entry_t* m = _map_alloc_page();
    if (m == nullptr) {
        return MX_ERR_NO_MEMORY;
    }

    paddr_t paddr_base = PageTable::paddr_from_pte(*pte);
    arch_flags_t flags = PageTable::split_arch_flags(*pte & X86_LARGE_FLAGS_MASK);

    DEBUG_ASSERT(PageTable::page_aligned(vaddr));
    vaddr_t new_vaddr = vaddr;
    paddr_t new_paddr = paddr_base;
    size_t ps = PageTable::LowerTable::page_size();
    for (int i = 0; i < NO_OF_PT_ENTRIES; i++) {
        volatile pt_entry_t* e = m + i;
        // If this is a PDP_L (i.e. huge page), flags will include the
        // PS bit still, so the new PD entries will be large pages.
        UpdateEntry<typename PageTable::LowerTable>(new_vaddr, e, new_paddr, flags);
        new_vaddr += ps;
        new_paddr += ps;
    }
    DEBUG_ASSERT(new_vaddr == vaddr + PageTable::page_size());

    flags = PageTable::intermediate_arch_flags();
    UpdateEntry<PageTable>(vaddr, pte, X86_VIRT_TO_PHYS(m), flags);
    pt_pages_++;
    return MX_OK;
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
 * @return MX_OK if mapping is found
 * @return MX_ERR_NOT_FOUND if mapping is not found
 */
template <typename PageTable>
status_t X86ArchVmAspace::GetMapping(volatile pt_entry_t* table, vaddr_t vaddr,
                                     enum page_table_levels* ret_level,
                                     volatile pt_entry_t** mapping) {
    DEBUG_ASSERT(table);
    DEBUG_ASSERT(ret_level);
    DEBUG_ASSERT(mapping);

    LTRACEF_LEVEL(2, "table %p\n", table);

    uint index = PageTable::vaddr_to_index(vaddr);
    volatile pt_entry_t* e = table + index;
    pt_entry_t pt_val = *e;
    if (!IS_PAGE_PRESENT(pt_val))
        return MX_ERR_NOT_FOUND;

    /* if this is a large page, stop here */
    if (IS_LARGE_PAGE(pt_val)) {
        *mapping = e;
        *ret_level = PageTable::level;
        return MX_OK;
    }

    volatile pt_entry_t* next_table = get_next_table_from_entry(pt_val);
    return GetMapping<typename PageTable::LowerTable>(next_table, vaddr,
                                                      ret_level, mapping);
}

template <>
status_t X86ArchVmAspace::GetMapping<PageTable<PT_L>>(
    volatile pt_entry_t* table, vaddr_t vaddr,
    enum page_table_levels* ret_level,
    volatile pt_entry_t** mapping) {
    return GetMappingL0<PageTable<PT_L>>(table, vaddr, ret_level, mapping);
}

template <>
status_t X86ArchVmAspace::GetMapping<ExtendedPageTable<PT_L>>(
    volatile pt_entry_t* table, vaddr_t vaddr,
    enum page_table_levels* ret_level,
    volatile pt_entry_t** mapping) {
    return GetMappingL0<ExtendedPageTable<PT_L>>(table, vaddr, ret_level, mapping);
}

template <typename PageTable>
status_t X86ArchVmAspace::GetMappingL0(volatile pt_entry_t* table, vaddr_t vaddr,
                                       enum page_table_levels* ret_level,
                                       volatile pt_entry_t** mapping) {
    static_assert(PageTable::level == PT_L, "GetMappingL0 used with wrong level");

    /* do the final page table lookup */
    uint index = PageTable::vaddr_to_index(vaddr);
    volatile pt_entry_t* e = table + index;
    if (!IS_PAGE_PRESENT(*e))
        return MX_ERR_NOT_FOUND;

    *mapping = e;
    *ret_level = PageTable::level;
    return MX_OK;
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
template <typename PageTable>
bool X86ArchVmAspace::RemoveMapping(volatile pt_entry_t* table,
                                    const MappingCursor& start_cursor,
                                    MappingCursor* new_cursor) {
    DEBUG_ASSERT(table);
    LTRACEF("L: %d, %016" PRIxPTR " %016zx\n", PageTable::level, start_cursor.vaddr,
            start_cursor.size);
    DEBUG_ASSERT(x86_mmu_check_vaddr(start_cursor.vaddr));

    *new_cursor = start_cursor;

    bool unmapped = false;
    size_t ps = PageTable::page_size();
    uint index = PageTable::vaddr_to_index(new_cursor->vaddr);
    for (; index != NO_OF_PT_ENTRIES && new_cursor->size != 0; ++index) {
        volatile pt_entry_t* e = table + index;
        pt_entry_t pt_val = *e;
        // If the page isn't even mapped, just skip it
        if (!IS_PAGE_PRESENT(pt_val)) {
            new_cursor->SkipEntry<PageTable>();
            DEBUG_ASSERT(new_cursor->size <= start_cursor.size);
            continue;
        }

        if (IS_LARGE_PAGE(pt_val)) {
            bool vaddr_level_aligned = PageTable::page_aligned(new_cursor->vaddr);
            // If the request covers the entire large page, just unmap it
            if (vaddr_level_aligned && new_cursor->size >= ps) {
                UnmapEntry<PageTable>(this, new_cursor->vaddr, e);
                unmapped = true;

                new_cursor->vaddr += ps;
                new_cursor->size -= ps;
                DEBUG_ASSERT(new_cursor->size <= start_cursor.size);
                continue;
            }
            // Otherwise, we need to split it
            vaddr_t page_vaddr = new_cursor->vaddr & ~(ps - 1);
            status_t status = SplitLargePage<PageTable>(page_vaddr, e);
            if (status != MX_OK) {
                // If split fails, just unmap the whole thing, and let a
                // subsequent page fault clean it up.
                UnmapEntry<PageTable>(this, new_cursor->vaddr, e);
                unmapped = true;

                new_cursor->SkipEntry<PageTable>();
                DEBUG_ASSERT(new_cursor->size <= start_cursor.size);
            }
            pt_val = *e;
        }

        MappingCursor cursor;
        volatile pt_entry_t* next_table = get_next_table_from_entry(pt_val);
        bool lower_unmapped = RemoveMapping<typename PageTable::LowerTable>(
            next_table, *new_cursor, &cursor);

        // If we were requesting to unmap everything in the lower page table,
        // we know we can unmap the lower level page table.  Otherwise, if
        // we unmapped anything in the lower level, check to see if that
        // level is now empty.
        bool unmap_page_table =
            PageTable::page_aligned(new_cursor->vaddr) && new_cursor->size >= ps;
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
                    PageTable::level, (uintptr_t)next_table, ptable_phys);

            UnmapEntry<PageTable>(this, new_cursor->vaddr, e);
            vm_page_t* page = paddr_to_vm_page(ptable_phys);

            DEBUG_ASSERT(page);
            DEBUG_ASSERT_MSG(page->state == VM_PAGE_STATE_MMU,
                             "page %p state %u, paddr %#" PRIxPTR "\n", page, page->state,
                             X86_VIRT_TO_PHYS(next_table));

            pmm_free_page(page);
            pt_pages_--;
            unmapped = true;
        }
        *new_cursor = cursor;
        DEBUG_ASSERT(new_cursor->size <= start_cursor.size);

        DEBUG_ASSERT(new_cursor->size == 0 || PageTable::page_aligned(new_cursor->vaddr));
    }

    return unmapped;
}

template <>
bool X86ArchVmAspace::RemoveMapping<PageTable<PT_L>>(volatile pt_entry_t* table,
                                                     const MappingCursor& start_cursor,
                                                     MappingCursor* new_cursor) {
    return RemoveMappingL0<PageTable<PT_L>>(table, start_cursor, new_cursor);
}

template <>
bool X86ArchVmAspace::RemoveMapping<ExtendedPageTable<PT_L>>(volatile pt_entry_t* table,
                                                             const MappingCursor& start_cursor,
                                                             MappingCursor* new_cursor) {
    return RemoveMappingL0<ExtendedPageTable<PT_L>>(table, start_cursor, new_cursor);
}

// Base case of RemoveMapping for smallest page size.
template <typename PageTable>
bool X86ArchVmAspace::RemoveMappingL0(volatile pt_entry_t* table,
                                      const MappingCursor& start_cursor,
                                      MappingCursor* new_cursor) {
    static_assert(PageTable::level == PT_L, "RemoveMappingL0 used with wrong level");
    LTRACEF("%016" PRIxPTR " %016zx\n", start_cursor.vaddr, start_cursor.size);
    DEBUG_ASSERT(IS_PAGE_ALIGNED(start_cursor.size));

    *new_cursor = start_cursor;

    bool unmapped = false;
    uint index = PageTable::vaddr_to_index(new_cursor->vaddr);
    for (; index != NO_OF_PT_ENTRIES && new_cursor->size != 0; ++index) {
        volatile pt_entry_t* e = table + index;
        if (IS_PAGE_PRESENT(*e)) {
            UnmapEntry<PageTable>(this, new_cursor->vaddr, e);
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
 * @return MX_OK if successful
 * @return MX_ERR_ALREADY_EXISTS if the range overlaps an existing mapping
 * @return MX_ERR_NO_MEMORY if intermediate page tables could not be allocated
 */
template <typename PageTable>
status_t X86ArchVmAspace::AddMapping(volatile pt_entry_t* table, uint mmu_flags,
                                     const MappingCursor& start_cursor,
                                     MappingCursor* new_cursor) {
    DEBUG_ASSERT(table);
    DEBUG_ASSERT(x86_mmu_check_vaddr(start_cursor.vaddr));
    DEBUG_ASSERT(x86_mmu_check_paddr(start_cursor.paddr));

    status_t ret = MX_OK;
    *new_cursor = start_cursor;

    arch_flags_t interm_arch_flags = PageTable::intermediate_arch_flags();
    arch_flags_t arch_flags = PageTable::arch_flags(flags_, mmu_flags);

    size_t ps = PageTable::page_size();
    bool level_supports_large_pages = PageTable::supports_page_size();
    uint index = PageTable::vaddr_to_index(new_cursor->vaddr);
    for (; index != NO_OF_PT_ENTRIES && new_cursor->size != 0; ++index) {
        volatile pt_entry_t* e = table + index;
        pt_entry_t pt_val = *e;
        // See if there's a large page in our way
        if (IS_PAGE_PRESENT(pt_val) && IS_LARGE_PAGE(pt_val)) {
            ret = MX_ERR_ALREADY_EXISTS;
            goto err;
        }

        // Check if this is a candidate for a new large page
        bool level_valigned = PageTable::page_aligned(new_cursor->vaddr);
        bool level_paligned = PageTable::page_aligned(new_cursor->paddr);
        if (level_supports_large_pages && !IS_PAGE_PRESENT(pt_val) && level_valigned &&
            level_paligned && new_cursor->size >= ps) {

            UpdateEntry<PageTable>(new_cursor->vaddr, table + index,
                                   new_cursor->paddr,
                                   arch_flags | X86_MMU_PG_PS);

            new_cursor->paddr += ps;
            new_cursor->vaddr += ps;
            new_cursor->size -= ps;
            DEBUG_ASSERT(new_cursor->size <= start_cursor.size);
        } else {
            // See if we need to create a new table
            if (!IS_PAGE_PRESENT(pt_val)) {
                volatile pt_entry_t* m = _map_alloc_page();
                if (m == nullptr) {
                    ret = MX_ERR_NO_MEMORY;
                    goto err;
                }

                LTRACEF_LEVEL(2, "new table %p at level %d\n", m, PageTable::level);

                UpdateEntry<PageTable>(new_cursor->vaddr, e,
                                       X86_VIRT_TO_PHYS(m), interm_arch_flags);
                pt_val = *e;
                pt_pages_++;
            }

            MappingCursor cursor;
            ret = AddMapping<typename PageTable::LowerTable>(
                get_next_table_from_entry(pt_val), mmu_flags, *new_cursor, &cursor);
            *new_cursor = cursor;
            DEBUG_ASSERT(new_cursor->size <= start_cursor.size);
            if (ret != MX_OK) {
                goto err;
            }
        }
    }
    return MX_OK;
err:
    if (fbl::is_same<PageTable, typename PageTable::TopTable>::value) {
        MappingCursor cursor = start_cursor;
        MappingCursor result;
        // new_cursor->size should be how much is left to be mapped still
        cursor.size -= new_cursor->size;
        if (cursor.size > 0) {
            RemoveMapping<typename PageTable::TopTable>(table, cursor, &result);
            DEBUG_ASSERT(result.size == 0);
        }
    }
    return ret;
}

template <>
status_t X86ArchVmAspace::AddMapping<PageTable<PT_L>>(
    volatile pt_entry_t* table, uint mmu_flags,
    const MappingCursor& start_cursor, MappingCursor* new_cursor) {
    return AddMappingL0<PageTable<PT_L>>(table, mmu_flags, start_cursor,
                                         new_cursor);
}

template <>
status_t X86ArchVmAspace::AddMapping<ExtendedPageTable<PT_L>>(
    volatile pt_entry_t* table, uint mmu_flags,
    const MappingCursor& start_cursor, MappingCursor* new_cursor) {
    return AddMappingL0<ExtendedPageTable<PT_L>>(table, mmu_flags, start_cursor,
                                                 new_cursor);
}

// Base case of AddMapping for smallest page size.
template <typename PageTable>
status_t X86ArchVmAspace::AddMappingL0(volatile pt_entry_t* table, uint mmu_flags,
                                       const MappingCursor& start_cursor,
                                       MappingCursor* new_cursor) {
    static_assert(PageTable::level == PT_L, "AddMappingL0 used with wrong level");
    DEBUG_ASSERT(IS_PAGE_ALIGNED(start_cursor.size));

    *new_cursor = start_cursor;

    arch_flags_t arch_flags = PageTable::arch_flags(flags_, mmu_flags);

    uint index = PageTable::vaddr_to_index(new_cursor->vaddr);
    for (; index != NO_OF_PT_ENTRIES && new_cursor->size != 0; ++index) {
        volatile pt_entry_t* e = table + index;
        if (IS_PAGE_PRESENT(*e)) {
            return MX_ERR_ALREADY_EXISTS;
        }

        UpdateEntry<PageTable>(new_cursor->vaddr, e, new_cursor->paddr, arch_flags);

        new_cursor->paddr += PAGE_SIZE;
        new_cursor->vaddr += PAGE_SIZE;
        new_cursor->size -= PAGE_SIZE;
        DEBUG_ASSERT(new_cursor->size <= start_cursor.size);
    }

    return MX_OK;
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
template <typename PageTable>
status_t X86ArchVmAspace::UpdateMapping(volatile pt_entry_t* table,
                                        uint mmu_flags,
                                        const MappingCursor& start_cursor,
                                        MappingCursor* new_cursor) {
    DEBUG_ASSERT(table);
    LTRACEF("L: %d, %016" PRIxPTR " %016zx\n", PageTable::level, start_cursor.vaddr,
            start_cursor.size);
    DEBUG_ASSERT(x86_mmu_check_vaddr(start_cursor.vaddr));

    status_t ret = MX_OK;
    *new_cursor = start_cursor;

    arch_flags_t arch_flags = PageTable::arch_flags(flags_, mmu_flags);

    size_t ps = PageTable::page_size();
    uint index = PageTable::vaddr_to_index(new_cursor->vaddr);
    for (; index != NO_OF_PT_ENTRIES && new_cursor->size != 0; ++index) {
        volatile pt_entry_t* e = table + index;
        pt_entry_t pt_val = *e;
        // Skip unmapped pages (we may encounter these due to demand paging)
        if (!IS_PAGE_PRESENT(pt_val)) {
            new_cursor->SkipEntry<PageTable>();
            continue;
        }

        if (IS_LARGE_PAGE(pt_val)) {
            bool vaddr_level_aligned = PageTable::page_aligned(new_cursor->vaddr);
            // If the request covers the entire large page, just change the
            // permissions
            if (vaddr_level_aligned && new_cursor->size >= ps) {
                UpdateEntry<PageTable>(new_cursor->vaddr, e,
                                       PageTable::paddr_from_pte(pt_val),
                                       arch_flags | X86_MMU_PG_PS);

                new_cursor->vaddr += ps;
                new_cursor->size -= ps;
                DEBUG_ASSERT(new_cursor->size <= start_cursor.size);
                continue;
            }
            // Otherwise, we need to split it
            vaddr_t page_vaddr = new_cursor->vaddr & ~(ps - 1);
            ret = SplitLargePage<PageTable>(page_vaddr, e);
            if (ret != MX_OK) {
                // If we failed to split the table, just unmap it.  Subsequent
                // page faults will bring it back in.
                MappingCursor cursor;
                cursor.vaddr = new_cursor->vaddr;
                cursor.size = ps;

                MappingCursor tmp_cursor;
                RemoveMapping<PageTable>(table, cursor, &tmp_cursor);

                new_cursor->SkipEntry<PageTable>();
            }
            pt_val = *e;
        }

        MappingCursor cursor;
        volatile pt_entry_t* next_table = get_next_table_from_entry(pt_val);
        ret = UpdateMapping<typename PageTable::LowerTable>(next_table, mmu_flags,
                                                            *new_cursor, &cursor);
        *new_cursor = cursor;
        if (ret != MX_OK) {
            // Currently this can't happen
            ASSERT(false);
        }
        DEBUG_ASSERT(new_cursor->size <= start_cursor.size);

        DEBUG_ASSERT(new_cursor->size == 0 || PageTable::page_aligned(new_cursor->vaddr));
    }
    return MX_OK;
}

template <>
status_t X86ArchVmAspace::UpdateMapping<PageTable<PT_L>>(
    volatile pt_entry_t* table, uint mmu_flags,
    const MappingCursor& start_cursor, MappingCursor* new_cursor) {
    return UpdateMappingL0<PageTable<PT_L>>(table, mmu_flags,
                                            start_cursor, new_cursor);
}

template <>
status_t X86ArchVmAspace::UpdateMapping<ExtendedPageTable<PT_L>>(
    volatile pt_entry_t* table, uint mmu_flags,
    const MappingCursor& start_cursor, MappingCursor* new_cursor) {
    return UpdateMappingL0<ExtendedPageTable<PT_L>>(table, mmu_flags,
                                                    start_cursor, new_cursor);
}

// Base case of UpdateMapping for smallest page size.
template <typename PageTable>
status_t X86ArchVmAspace::UpdateMappingL0(volatile pt_entry_t* table,
                                          uint mmu_flags,
                                          const MappingCursor& start_cursor,
                                          MappingCursor* new_cursor) {
    static_assert(PageTable::level == PT_L, "UpdateMappingL0 used with wrong level");
    LTRACEF("%016" PRIxPTR " %016zx\n", start_cursor.vaddr, start_cursor.size);
    DEBUG_ASSERT(IS_PAGE_ALIGNED(start_cursor.size));

    *new_cursor = start_cursor;

    arch_flags_t arch_flags = PageTable::arch_flags(flags_, mmu_flags);

    uint index = PageTable::vaddr_to_index(new_cursor->vaddr);
    for (; index != NO_OF_PT_ENTRIES && new_cursor->size != 0; ++index) {
        volatile pt_entry_t* e = table + index;
        pt_entry_t pt_val = *e;
        // Skip unmapped pages (we may encounter these due to demand paging)
        if (IS_PAGE_PRESENT(pt_val)) {
            UpdateEntry<PageTable>(new_cursor->vaddr, e,
                                   PageTable::paddr_from_pte(pt_val),
                                   arch_flags);
        }

        new_cursor->vaddr += PAGE_SIZE;
        new_cursor->size -= PAGE_SIZE;
        DEBUG_ASSERT(new_cursor->size <= start_cursor.size);
    }
    DEBUG_ASSERT(new_cursor->size == 0 || PageTable::page_aligned(new_cursor->vaddr));
    return MX_OK;
}

template <template <int> class PageTable>
status_t X86ArchVmAspace::UnmapPages(vaddr_t vaddr, const size_t count,
                                     size_t* unmapped) {
    LTRACEF("aspace %p, vaddr %#" PRIxPTR ", count %#zx\n", this, vaddr, count);

    canary_.Assert();

    if (!x86_mmu_check_vaddr(vaddr))
        return MX_ERR_INVALID_ARGS;
    if (!IsValidVaddr(vaddr))
        return MX_ERR_INVALID_ARGS;
    if (count == 0)
        return MX_OK;

    DEBUG_ASSERT(pt_virt_);

    MappingCursor start = {
        .paddr = 0, .vaddr = vaddr, .size = count * PAGE_SIZE,
    };

    MappingCursor result;
    RemoveMapping<PageTable<MAX_PAGING_LEVEL>>(pt_virt_, start, &result);
    DEBUG_ASSERT(result.size == 0);

    if (unmapped)
        *unmapped = count;

    return MX_OK;
}

status_t X86ArchVmAspace::Unmap(vaddr_t vaddr, size_t count, size_t* unmapped) {
    fbl::AutoLock a(&lock_);

    if (flags_ & ARCH_ASPACE_FLAG_GUEST_PASPACE) {
        return UnmapPages<ExtendedPageTable>(vaddr, count, unmapped);
    } else {
        return UnmapPages<PageTable>(vaddr, count, unmapped);
    }
}

template <template <int> class PageTable>
status_t X86ArchVmAspace::MapPages(vaddr_t vaddr, paddr_t paddr,
                                   const size_t count, uint mmu_flags,
                                   size_t* mapped) {
    canary_.Assert();

    LTRACEF("aspace %p, vaddr %#" PRIxPTR " paddr %#" PRIxPTR " count %#zx mmu_flags 0x%x\n",
            this, vaddr, paddr, count, mmu_flags);

    if ((!x86_mmu_check_paddr(paddr)))
        return MX_ERR_INVALID_ARGS;
    if (!x86_mmu_check_vaddr(vaddr))
        return MX_ERR_INVALID_ARGS;
    if (!IsValidVaddr(vaddr))
        return MX_ERR_INVALID_ARGS;
    if (count == 0)
        return MX_OK;

    if (!(mmu_flags & ARCH_MMU_FLAG_PERM_READ))
        return MX_ERR_INVALID_ARGS;

    DEBUG_ASSERT(pt_virt_);

    MappingCursor start = {
        .paddr = paddr, .vaddr = vaddr, .size = count * PAGE_SIZE,
    };
    MappingCursor result;
    status_t status = AddMapping<PageTable<MAX_PAGING_LEVEL>>(pt_virt_, mmu_flags,
                                                              start, &result);
    if (status != MX_OK) {
        dprintf(SPEW, "Add mapping failed with err=%d\n", status);
        return status;
    }
    DEBUG_ASSERT(result.size == 0);

    if (mapped)
        *mapped = count;

    return MX_OK;
}

status_t X86ArchVmAspace::Map(vaddr_t vaddr, paddr_t paddr, size_t count,
                              uint mmu_flags, size_t* mapped) {
    fbl::AutoLock a(&lock_);

    if (flags_ & ARCH_ASPACE_FLAG_GUEST_PASPACE) {
        if (mmu_flags & ~kValidEptFlags)
            return MX_ERR_INVALID_ARGS;
        return MapPages<ExtendedPageTable>(vaddr, paddr, count, mmu_flags, mapped);
    } else {
        return MapPages<PageTable>(vaddr, paddr, count, mmu_flags, mapped);
    }
}

template <template <int> class PageTable>
status_t X86ArchVmAspace::ProtectPages(vaddr_t vaddr, size_t count, uint mmu_flags) {
    canary_.Assert();

    LTRACEF("aspace %p, vaddr %#" PRIxPTR " count %#zx mmu_flags 0x%x\n",
            this, vaddr, count, mmu_flags);

    if (!x86_mmu_check_vaddr(vaddr))
        return MX_ERR_INVALID_ARGS;
    if (!IsValidVaddr(vaddr))
        return MX_ERR_INVALID_ARGS;
    if (count == 0)
        return MX_OK;

    if (!(mmu_flags & ARCH_MMU_FLAG_PERM_READ))
        return MX_ERR_INVALID_ARGS;

    MappingCursor start = {
        .paddr = 0, .vaddr = vaddr, .size = count * PAGE_SIZE,
    };
    MappingCursor result;
    status_t status = UpdateMapping<PageTable<MAX_PAGING_LEVEL>>(
        pt_virt_, mmu_flags, start, &result);
    if (status != MX_OK) {
        return status;
    }
    DEBUG_ASSERT(result.size == 0);
    return MX_OK;
}

status_t X86ArchVmAspace::Protect(vaddr_t vaddr, size_t count, uint mmu_flags) {
    fbl::AutoLock a(&lock_);

    if (flags_ & ARCH_ASPACE_FLAG_GUEST_PASPACE) {
        if (mmu_flags & ~kValidEptFlags)
            return MX_ERR_INVALID_ARGS;
        return ProtectPages<ExtendedPageTable>(vaddr, count, mmu_flags);
    } else {
        return ProtectPages<PageTable>(vaddr, count, mmu_flags);
    }
}

void x86_mmu_early_init() {
    x86_mmu_mem_type_init();
    x86_mmu_percpu_init();

    // Unmap the lower identity mapping.
    X86ArchVmAspace::UnmapEntry<PageTable<PML4_L>>(nullptr, 0, &pml4[0]);

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

X86ArchVmAspace::X86ArchVmAspace() {}

/*
 * Fill in the high level x86 arch aspace structure and allocating a top level page table.
 */
status_t X86ArchVmAspace::Init(vaddr_t base, size_t size, uint mmu_flags) {
    static_assert(sizeof(mp_cpu_mask_t) == sizeof(active_cpus_), "err");
    canary_.Assert();

    fbl::AutoLock a(&lock_);

    LTRACEF("aspace %p, base %#" PRIxPTR ", size 0x%zx, mmu_flags 0x%x\n", this, base, size,
            mmu_flags);

    flags_ = mmu_flags;
    base_ = base;
    size_ = size;
    if (mmu_flags & ARCH_ASPACE_FLAG_KERNEL) {
        pt_phys_ = kernel_pt_phys;
        pt_virt_ = (pt_entry_t*)X86_PHYS_TO_VIRT(pt_phys_);
        LTRACEF("kernel aspace: pt phys %#" PRIxPTR ", virt %p\n", pt_phys_, pt_virt_);
    } else if (mmu_flags & ARCH_ASPACE_FLAG_GUEST_PASPACE) {
        vm_page_t* p = pmm_alloc_page(0, &pt_phys_);
        if (p == nullptr) {
            TRACEF("error allocating top level page directory\n");
            return MX_ERR_NO_MEMORY;
        }
        p->state = VM_PAGE_STATE_MMU;
        pt_virt_ = static_cast<pt_entry_t*>(paddr_to_kvaddr(pt_phys_));
        memset(pt_virt_, 0, sizeof(pt_entry_t) * NO_OF_PT_ENTRIES);
        LTRACEF("guest paspace: pt phys %#" PRIxPTR ", virt %p\n", pt_phys_, pt_virt_);
    } else {
        /* allocate a top level page table for the new address space */
        paddr_t pa;
        vm_page_t* p;
        pt_virt_ = (pt_entry_t*)pmm_alloc_kpage(&pa, &p);
        if (!pt_virt_) {
            TRACEF("error allocating top level page directory\n");
            return MX_ERR_NO_MEMORY;
        }
        pt_phys_ = pa;

        p->state = VM_PAGE_STATE_MMU;

        // Zero out the user space half of it.
        memset(pt_virt_, 0, sizeof(pt_entry_t) * NO_OF_PT_ENTRIES / 2);

        // Copy the kernel portion of it from the master kernel pt.
        memcpy(pt_virt_ + NO_OF_PT_ENTRIES / 2,
               const_cast<pt_entry_t*>(&KERNEL_PT[NO_OF_PT_ENTRIES / 2]),
               sizeof(pt_entry_t) * NO_OF_PT_ENTRIES / 2);

        LTRACEF("user aspace: pt phys %#" PRIxPTR ", virt %p\n", pt_phys_, pt_virt_);
    }
    pt_pages_ = 1;
    active_cpus_ = 0;

    return MX_OK;
}

template <template <int> class PageTable>
status_t X86ArchVmAspace::DestroyAspace() {
    canary_.Assert();
    DEBUG_ASSERT(active_cpus_ == 0);

#if LK_DEBUGLEVEL > 1
    pt_entry_t* table = static_cast<pt_entry_t*>(pt_virt_);
    uint start = PageTable<MAX_PAGING_LEVEL>::vaddr_to_index(base_);
    uint end = PageTable<MAX_PAGING_LEVEL>::vaddr_to_index(base_ + size_ - 1);

    // Don't check start if that table is shared with another aspace.
    if (!PageTable<MAX_PAGING_LEVEL>::page_aligned(base_)) {
        start += 1;
    }
    // Do check the end if it fills out the table entry.
    if (PageTable<MAX_PAGING_LEVEL>::page_aligned(base_ + size_)) {
        end += 1;
    }

    for (uint i = start; i < end; ++i) {
        DEBUG_ASSERT(!IS_PAGE_PRESENT(table[i]));
    }
#endif

    pmm_free_page(paddr_to_vm_page(pt_phys_));
    pt_phys_ = 0;

    return MX_OK;
}

status_t X86ArchVmAspace::Destroy() {
    fbl::AutoLock a(&lock_);

    if (flags_ & ARCH_ASPACE_FLAG_GUEST_PASPACE)
        return DestroyAspace<ExtendedPageTable>();
    else
        return DestroyAspace<PageTable>();
}

void X86ArchVmAspace::ContextSwitch(X86ArchVmAspace* old_aspace, X86ArchVmAspace* aspace) {
    mp_cpu_mask_t cpu_bit = 1U << arch_curr_cpu_num();
    if (aspace != nullptr) {
        aspace->canary_.Assert();
        LTRACEF_LEVEL(3, "switching to aspace %p, pt %#" PRIXPTR "\n", aspace, aspace->pt_phys_);
        x86_set_cr3(aspace->pt_phys_);

        if (old_aspace != nullptr) {
            atomic_and(&old_aspace->active_cpus_, ~cpu_bit);
        }
        atomic_or(&aspace->active_cpus_, cpu_bit);
    } else {
        LTRACEF_LEVEL(3, "switching to kernel aspace, pt %#" PRIxPTR "\n", kernel_pt_phys);
        x86_set_cr3(kernel_pt_phys);
        if (old_aspace != nullptr) {
            atomic_and(&old_aspace->active_cpus_, ~cpu_bit);
        }
    }

    // Cleanup io bitmap entries from previous thread.
    if (old_aspace)
        x86_clear_tss_io_bitmap(old_aspace->io_bitmap());

    // Set the io bitmap for this thread.
    if (aspace)
        x86_set_tss_io_bitmap(aspace->io_bitmap());
}

template <template <int> class PageTable, typename F>
status_t X86ArchVmAspace::QueryVaddr(vaddr_t vaddr, paddr_t* paddr,
                                     uint* mmu_flags, F arch_to_mmu) {
    canary_.Assert();

    page_table_levels ret_level;

    LTRACEF("aspace %p, vaddr %#" PRIxPTR ", paddr %p, mmu_flags %p\n", this, vaddr, paddr,
            mmu_flags);

    if (!IsValidVaddr(vaddr))
        return MX_ERR_INVALID_ARGS;

    volatile pt_entry_t* last_valid_entry;
    status_t status = GetMapping<PageTable<MAX_PAGING_LEVEL>>(
        pt_virt_, vaddr, &ret_level, &last_valid_entry);
    if (status != MX_OK)
        return status;

    DEBUG_ASSERT(last_valid_entry);
    LTRACEF("last_valid_entry (%p) 0x%" PRIxPTE ", level %d\n", last_valid_entry, *last_valid_entry,
            ret_level);

    /* based on the return level, parse the page table entry */
    if (paddr) {
        switch (ret_level) {
        case PDP_L: /* 1GB page */
            *paddr = PageTable<PDP_L>::paddr_from_pte(*last_valid_entry);
            *paddr |= vaddr & PAGE_OFFSET_MASK_HUGE;
            break;
        case PD_L: /* 2MB page */
            *paddr = PageTable<PD_L>::paddr_from_pte(*last_valid_entry);
            *paddr |= vaddr & PAGE_OFFSET_MASK_LARGE;
            break;
        case PT_L: /* 4K page */
            *paddr = PageTable<PT_L>::paddr_from_pte(*last_valid_entry);
            *paddr |= vaddr & PAGE_OFFSET_MASK_4KB;
            break;
        default:
            panic("arch_mmu_query: unhandled frame level\n");
        }

        LTRACEF("paddr %#" PRIxPTR "\n", *paddr);
    }

    /* converting arch-specific flags to mmu flags */
    if (mmu_flags) {
        *mmu_flags = arch_to_mmu(*last_valid_entry, ret_level);
    }

    return MX_OK;
}

status_t X86ArchVmAspace::Query(vaddr_t vaddr, paddr_t* paddr, uint* mmu_flags) {
    fbl::AutoLock a(&lock_);

    if (flags_ & ARCH_ASPACE_FLAG_GUEST_PASPACE) {
        return QueryVaddr<ExtendedPageTable>(vaddr, paddr, mmu_flags,
                                             ept_mmu_flags);
    } else {
        return QueryVaddr<PageTable>(vaddr, paddr, mmu_flags,
                                     x86_mmu_flags);
    }
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
    // TODO(MG-980): check that we've destroyed the aspace.
}

vaddr_t X86ArchVmAspace::PickSpot(vaddr_t base, uint prev_region_mmu_flags,
                                  vaddr_t end, uint next_region_mmu_flags,
                                  vaddr_t align, size_t size, uint mmu_flags) {
    canary_.Assert();
    return PAGE_ALIGN(base);
}
