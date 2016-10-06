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

#define LOCAL_TRACE 0

/* Default address width including virtual/physical address.
 * newer versions fetched below */
#if ARCH_X86_64
uint8_t g_vaddr_width = 48;
uint8_t g_paddr_width = 32;
#elif ARCH_X86_32
uint8_t g_vaddr_width = 32;
uint8_t g_paddr_width = 32;
#endif

#if ARCH_X86_64
/* top level kernel page tables, initialized in start.S */
pt_entry_t pml4[NO_OF_PT_ENTRIES] __ALIGNED(PAGE_SIZE);
pt_entry_t pdp[NO_OF_PT_ENTRIES] __ALIGNED(PAGE_SIZE); /* temporary */
pt_entry_t pte[NO_OF_PT_ENTRIES] __ALIGNED(PAGE_SIZE);

/* top level pdp needed to map the -512GB..0 space */
pt_entry_t pdp_high[NO_OF_PT_ENTRIES] __ALIGNED(PAGE_SIZE);

/* a big pile of page tables needed to map 64GB of memory into kernel space using 2MB pages */
pt_entry_t linear_map_pdp[(64ULL * GB) / (2 * MB)] __ALIGNED(PAGE_SIZE);

/* which of the above variables is the top level page table */
#define KERNEL_PT pml4
#else
/* 32bit code */
pt_entry_t pd[NO_OF_PT_ENTRIES] __ALIGNED(PAGE_SIZE);

#if PAE_MODE_ENABLED
/* PAE has one more level, with the pdp at the top */
pt_entry_t pdp[NO_OF_PT_ENTRIES] __ALIGNED(PAGE_SIZE);
#define KERNEL_PT pdp
#else
#define KERNEL_PT pd
#endif
#endif /* !ARCH_X86_64 */

/* kernel base top level page table in physical space */
static const paddr_t kernel_pt_phys = (vaddr_t)KERNEL_PT - KERNEL_BASE;

/* test the vaddr against the address space's range */
static bool is_valid_vaddr(arch_aspace_t* aspace, vaddr_t vaddr) {
    return (vaddr >= aspace->base && vaddr <= aspace->base + aspace->size - 1);
}

/**
 * @brief  check if the virtual address is canonical
 */
bool x86_is_vaddr_canonical(vaddr_t vaddr) {
#if ARCH_X86_64
    uint64_t max_vaddr_lohalf, min_vaddr_hihalf;

    /* get max address in lower-half canonical addr space */
    /* e.g. if width is 48, then 0x00007FFF_FFFFFFFF */
    max_vaddr_lohalf = ((uint64_t)1ull << (g_vaddr_width - 1)) - 1;

    /* get min address in higher-half canonical addr space */
    /* e.g. if width is 48, then 0xFFFF8000_00000000*/
    min_vaddr_hihalf = ~max_vaddr_lohalf;

    /* Check to see if the address in a canonical address */
    if ((vaddr > max_vaddr_lohalf) && (vaddr < min_vaddr_hihalf)) return false;
#endif

    return true;
}

/**
 * @brief  check if the virtual address is aligned and canonical
 */
static bool x86_mmu_check_vaddr(vaddr_t vaddr) {
    /* Check to see if the address is PAGE aligned */
    if (!IS_ALIGNED(vaddr, PAGE_SIZE)) return false;

    return x86_is_vaddr_canonical(vaddr);
}

/**
 * @brief  check if the physical address is valid and aligned
 */
static bool x86_mmu_check_paddr(paddr_t paddr) {
    uint64_t max_paddr;

    /* Check to see if the address is PAGE aligned */
    if (!IS_ALIGNED(paddr, PAGE_SIZE)) return false;

    max_paddr = ((uint64_t)1ull << g_paddr_width) - 1;

    return paddr <= max_paddr;
}

/**
 * @brief Returning the x86 arch flags from generic mmu flags
 *
 * These are used for page mapping entries in the table
 */
template <int Level>
static arch_flags_t x86_arch_flags(uint flags) {
    arch_flags_t arch_flags = 0;

    if (flags & ARCH_MMU_FLAG_PERM_WRITE) arch_flags |= X86_MMU_PG_RW;

    if (flags & ARCH_MMU_FLAG_PERM_USER) {
        arch_flags |= X86_MMU_PG_U;
    } else {
        /* setting global flag for kernel pages */
        arch_flags |= X86_MMU_PG_G;
    }

#if defined(PAE_MODE_ENABLED) || ARCH_X86_64
    if (!(flags & ARCH_MMU_FLAG_PERM_EXECUTE)) arch_flags |= X86_MMU_PG_NX;

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
#else
    switch (flags & ARCH_MMU_FLAG_CACHE_MASK) {
        case ARCH_MMU_FLAG_CACHED:
            break;
        case ARCH_MMU_FLAG_WRITE_COMBINING:
        case ARCH_MMU_FLAG_UNCACHED_DEVICE:
        case ARCH_MMU_FLAG_UNCACHED:
            arch_flags |= X86_MMU_PG_CD | X86_MMU_PG_WT;
            break;
        default:
            PANIC_UNIMPLEMENTED;
    }
#endif
    return arch_flags;
}

/**
 * @brief Returning the x86 arch flags for intermediate tables from generic
 *        mmu flags
 */
static arch_flags_t get_x86_intermediate_arch_flags() {
    return X86_MMU_PG_RW | X86_MMU_PG_U;
}

/**
 * @brief Returning the generic mmu flags from x86 arch flags
 */
static uint arch_mmu_flags(arch_flags_t flags, enum page_table_levels level) {
    uint mmu_flags = ARCH_MMU_FLAG_PERM_READ;

    if (flags & X86_MMU_PG_RW) mmu_flags |= ARCH_MMU_FLAG_PERM_WRITE;

    if (flags & X86_MMU_PG_U) mmu_flags |= ARCH_MMU_FLAG_PERM_USER;

#if defined(PAE_MODE_ENABLED) || ARCH_X86_64
    if (!(flags & X86_MMU_PG_NX)) mmu_flags |= ARCH_MMU_FLAG_PERM_EXECUTE;

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
#else
    if (flags & X86_MMU_PG_CD) {
        mmu_flags |= ARCH_MMU_FLAG_UNCACHED;
    } else {
        mmu_flags |= ARCH_MMU_FLAG_CACHED;
    }
#endif
    return mmu_flags;
}

template <int Level>
static inline uint vaddr_to_index(vaddr_t vaddr) {
    static_assert(Level >= 0, "level too low");
    static_assert(Level < X86_PAGING_LEVELS, "level too high");

    switch (Level) {
#if X86_PAGING_LEVELS > 3
        case PML4_L:
            return VADDR_TO_PML4_INDEX(vaddr);
#endif
#if X86_PAGING_LEVELS > 2
        case PDP_L:
            return VADDR_TO_PDP_INDEX(vaddr);
#endif
        case PD_L:
            return VADDR_TO_PD_INDEX(vaddr);
        case PT_L:
            return VADDR_TO_PT_INDEX(vaddr);
        default:
            panic("vaddr_to_index: invalid level\n");
    }
}

/*
 * @brief convert a pte to a physical address
 */
template <int Level>
static paddr_t paddr_from_pte(pt_entry_t pte) {
    static_assert(Level >= 0, "level too low");
    static_assert(Level < X86_PAGING_LEVELS, "level too high");

    DEBUG_ASSERT(IS_PAGE_PRESENT(pte));

    paddr_t pa;
    switch (Level) {
#if X86_PAGING_LEVELS > 2
        case PDP_L:
            pa = (pte & X86_HUGE_PAGE_FRAME);
            break;
#endif
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

template <int Level>
static inline size_t page_size() {
    static_assert(Level >= 0, "level too low");
    static_assert(Level < X86_PAGING_LEVELS, "level too high");
    switch (Level) {
        case PT_L:
            return 1ULL << PT_SHIFT;
        case PD_L:
            return 1ULL << PD_SHIFT;
#if X86_PAGING_LEVELS > 2
        case PDP_L:
            return 1ULL << PDP_SHIFT;
#if X86_PAGING_LEVELS > 3
        case PML4_L:
            return 1ULL << PML4_SHIFT;
#endif
#endif
        default:
            panic("page_size: invalid level\n");
    }
}

template <int Level>
static inline bool page_aligned(vaddr_t vaddr) {
    return (vaddr & (page_size<Level>() - 1)) == 0;
}

static void tlb_global_invalidate() {
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
#if X86_PAGING_LEVELS > 3
        case PML4_L:
            tlb_global_invalidate();
            break;
#endif
#if X86_PAGING_LEVELS > 2
        case PDP_L:
#endif
        case PD_L:
        case PT_L:
            __asm__ volatile("invlpg %0" ::"m"(*(uint8_t*)context->vaddr));
            break;
    }
}

/**
 * @brief Invalidate a single page at a given page table level
 *
 * @param cr3 The top-level page table physical address we are performing an invalidation on
 * @param vaddr The virtual address we are invalidating the TLB entry for
 * @param level The page table level that maps this vaddr
 * @param global_page True if we are invalidating a global mapping
 *
 * TODO(teisenbe): Optimize this.  This is horrible inefficient on several
 * levels.  We can easily check what CPUs are currently using this page table,
 * and so cut out the spurious IPIs.  We should also change this to pool
 * invalidations from a single "transaction" and then only execute a single
 * mp_sync_exec for that transaction, rather than one per page.
 */
void x86_tlb_invalidate_page(ulong cr3, vaddr_t vaddr, enum page_table_levels level,
                             bool global_page) {
    struct tlb_invalidate_page_context task_context = {
        .target_cr3 = cr3, .vaddr = vaddr, .level = level, .global_page = global_page,
    };
    mp_sync_exec(MP_CPU_ALL, tlb_invalidate_page_task, &task_context);
}

struct MappingCursor {
    paddr_t paddr;
    vaddr_t vaddr;
    size_t size;
};

template <int Level>
static void update_entry(ulong cr3, vaddr_t vaddr, pt_entry_t* pte, paddr_t paddr,
                         arch_flags_t flags) {

    DEBUG_ASSERT(pte);
    DEBUG_ASSERT(IS_PAGE_ALIGNED(paddr));

    pt_entry_t olde = *pte;

    /* set the new entry */
    *pte = paddr;
    *pte |= flags | X86_MMU_PG_P;

    /* attempt to invalidate the page */
    if (IS_PAGE_PRESENT(olde)) {
        x86_tlb_invalidate_page(cr3, vaddr, (page_table_levels)Level, is_kernel_address(vaddr));
    }
}

template <int Level>
static void unmap_entry(ulong cr3, vaddr_t vaddr, pt_entry_t* pte, bool flush) {
    DEBUG_ASSERT(pte);

    pt_entry_t olde = *pte;

    *pte = 0;

    /* attempt to invalidate the page */
    if (flush && IS_PAGE_PRESENT(olde)) {
        x86_tlb_invalidate_page(cr3, vaddr, (page_table_levels)Level, is_kernel_address(vaddr));
    }
}

/**
 * @brief Allocating a new page table
 */
static pt_entry_t* _map_alloc_page(void) {
    pt_entry_t* page_ptr = static_cast<pt_entry_t*>(pmm_alloc_kpage(nullptr));
    DEBUG_ASSERT(page_ptr);

    if (page_ptr) arch_zero_page(page_ptr);

    return page_ptr;
}

/*
 * @brief Split the given large page into smaller pages
 */
template <int Level>
static status_t x86_mmu_split(ulong cr3, vaddr_t vaddr, pt_entry_t* pte) {
    static_assert(Level != PT_L, "tried splitting PT_L");
#if X86_PAGING_LEVELS > 3
    // This can't easily be a static assert without duplicating
    // a bunch of code in the callers
    DEBUG_ASSERT(Level != PML4_L);
#endif
    LTRACEF_LEVEL(2, "splitting table %p at level %d\n", pte, Level);

    DEBUG_ASSERT(IS_PAGE_PRESENT(*pte) && IS_LARGE_PAGE(*pte));
    pt_entry_t* m = _map_alloc_page();
    if (m == NULL) {
        return ERR_NO_MEMORY;
    }

    paddr_t paddr_base = paddr_from_pte<Level>(*pte);
    arch_flags_t flags = *pte & X86_LARGE_FLAGS_MASK;
    DEBUG_ASSERT(flags & X86_MMU_PG_PS);
    if (Level == PD_L) {
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

    DEBUG_ASSERT(page_aligned<Level>(vaddr));
    vaddr_t new_vaddr = vaddr;
    paddr_t new_paddr = paddr_base;
    size_t ps = page_size<Level - 1>();
    for (int i = 0; i < NO_OF_PT_ENTRIES; i++) {
        pt_entry_t* e = m + i;
        // If this is a PDP_L (i.e. huge page), flags will include the
        // PS bit still, so the new PD entries will be large pages.
        update_entry<Level - 1>(cr3, new_vaddr, e, new_paddr, flags);
        new_vaddr += ps;
        new_paddr += ps;
    }
    DEBUG_ASSERT(new_vaddr == vaddr + page_size<Level>());

    flags = get_x86_intermediate_arch_flags();
    update_entry<Level>(cr3, vaddr, pte, X86_VIRT_TO_PHYS(m), flags);
    return NO_ERROR;
}

/*
 * @brief given a page table entry, return a pointer to the next page table one level down
 */
static inline pt_entry_t* get_next_table_from_entry(pt_entry_t entry) {
    if (!IS_PAGE_PRESENT(entry) || IS_LARGE_PAGE(entry)) return NULL;

    return (pt_entry_t*)X86_PHYS_TO_VIRT(entry & X86_PG_FRAME);
}

static bool level_supports_ps(page_table_levels level) {
    DEBUG_ASSERT(level != 0);
    switch (level) {
        case PD_L:
            return true;
#if X86_PAGING_LEVELS > 2
        // TODO(teisenbe): Technically need to feature detect this
        case PDP_L:
            return true;
#if X86_PAGING_LEVELS > 3
        case PML4_L:
            return false;
#endif
#endif
        default:
            panic("Unreachable case in level_supports_ps");
    }
}

/**
 * @brief  Walk the page table structures returning the entry and level that maps the address.
 *
 * @param table The top-level paging structure's virtual address
 * @param vaddr The virtual address to retrieve the mapping for
 * @param ret_level The level of the table that defines the found mapping
 * @param mapping The mapping that was found
 *
 * @return NO_ERROR if mapping is found
 * @return ERR_NOT_FOUND if mapping is not found
 */
template <int Level>
static status_t x86_mmu_get_mapping(pt_entry_t* table, vaddr_t vaddr,
                                    enum page_table_levels* ret_level, pt_entry_t** mapping) {

    DEBUG_ASSERT(table);
    DEBUG_ASSERT(ret_level);
    DEBUG_ASSERT(mapping);

    LTRACEF_LEVEL(2, "table %p\n", table);

    uint index = vaddr_to_index<Level>(vaddr);
    pt_entry_t* e = table + index;
    if (!IS_PAGE_PRESENT(*e)) return ERR_NOT_FOUND;

    /* if this is a large page, stop here */
    if (IS_LARGE_PAGE(*e)) {
        *mapping = e;
        *ret_level = static_cast<page_table_levels>(Level);
        return NO_ERROR;
    }

    pt_entry_t* next_table = get_next_table_from_entry(*e);
    return x86_mmu_get_mapping<Level - 1>(next_table, vaddr, ret_level, mapping);
}

template <>
status_t x86_mmu_get_mapping<PT_L>(pt_entry_t* table, vaddr_t vaddr,
                                   enum page_table_levels* ret_level, pt_entry_t** mapping) {

    /* do the final page table lookup */
    uint index = vaddr_to_index<PT_L>(vaddr);
    pt_entry_t* e = table + index;
    if (!IS_PAGE_PRESENT(*e)) return ERR_NOT_FOUND;

    *mapping = e;
    *ret_level = PT_L;
    return NO_ERROR;
}

/**
 * @brief Unmaps the range specified by start_cursor
 *
 * Level must be MAX_PAGING_LEVEL when invoked.
 *
 * @param cr3 The top-level paging structure's physical address
 * @param table The top-level paging structure's virtual address
 * @param start_cursor A cursor describing the range of address space to
 * unmap within table
 * @param new_cursor A returned cursor describing how much work was not
 * completed.  Must be non-null.
 *
 * @return true if at least one page was unmapped at this level
 */
template <int Level>
static bool x86_mmu_remove_mapping(ulong cr3, pt_entry_t* table, const MappingCursor& start_cursor,
                                   MappingCursor* new_cursor) {
    static_assert(Level >= 0, "level too low");
    static_assert(Level < X86_PAGING_LEVELS, "level too high");

    DEBUG_ASSERT(table);
    LTRACEF("L: %d, %016" PRIxPTR " %016zx\n", Level, start_cursor.vaddr, start_cursor.size);
    DEBUG_ASSERT(x86_mmu_check_vaddr(start_cursor.vaddr));

    *new_cursor = start_cursor;

    bool unmapped = false;
    size_t ps = page_size<Level>();
    uint index = vaddr_to_index<Level>(new_cursor->vaddr);
    for (; index != NO_OF_PT_ENTRIES && new_cursor->size != 0; ++index) {
        pt_entry_t* e = table + index;
        // If the page isn't even mapped, just skip it
        if (!IS_PAGE_PRESENT(*e)) {
            size_t next_entry_vaddr = ROUNDDOWN(new_cursor->vaddr, ps) + ps;

            // If our endpoint was in the middle of this range, clamp the
            // amount we remove from the cursor
            if (new_cursor->size < next_entry_vaddr - new_cursor->vaddr) {
                new_cursor->vaddr += new_cursor->size;
                new_cursor->size = 0;
                continue;
            }
            new_cursor->size -= next_entry_vaddr - new_cursor->vaddr;
            new_cursor->vaddr = next_entry_vaddr;
            DEBUG_ASSERT(new_cursor->size <= start_cursor.size);
            continue;
        }

        if (IS_LARGE_PAGE(*e)) {
            bool vaddr_level_aligned = page_aligned<Level>(new_cursor->vaddr);
            // If the request covers the entire large page, just unmap it
            if (vaddr_level_aligned && new_cursor->size >= ps) {
                unmap_entry<Level>(cr3, new_cursor->vaddr, e, true);
                unmapped = true;

                new_cursor->vaddr += ps;
                new_cursor->size -= ps;
                DEBUG_ASSERT(new_cursor->size <= start_cursor.size);
                continue;
            }
            // Otherwise, we need to split it
            vaddr_t page_vaddr = new_cursor->vaddr & ~(ps - 1);
            status_t status = x86_mmu_split<Level>(cr3, page_vaddr, e);
            if (status != NO_ERROR) {
                panic("Need to implement recovery from split failure");
            }
        }

        MappingCursor cursor;
        pt_entry_t* next_table = get_next_table_from_entry(*e);
        bool lower_unmapped = x86_mmu_remove_mapping<Level - 1>(
                cr3, next_table, *new_cursor, &cursor);

        // If we were requesting to unmap everything in the lower page table,
        // we know we can unmap the lower level page table.  Otherwise, if
        // we unmapped anything in the lower level, check to see if that
        // level is now empty.
        bool unmap_page_table =
                page_aligned<Level>(new_cursor->vaddr) && new_cursor->size >= ps;
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
            unmap_entry<Level>(cr3, new_cursor->vaddr, e, false);
            pmm_free_page(paddr_to_vm_page(X86_VIRT_TO_PHYS(next_table)));
            unmapped = true;
        }
        *new_cursor = cursor;
        DEBUG_ASSERT(new_cursor->size <= start_cursor.size);

        DEBUG_ASSERT(new_cursor->size == 0 || page_aligned<Level>(new_cursor->vaddr));
    }

    return unmapped;
}

// Base case of x86_remove_mapping for smallest page size
template <>
bool x86_mmu_remove_mapping<PT_L>(ulong cr3, pt_entry_t* table, const MappingCursor& start_cursor,
                                  MappingCursor* new_cursor) {

    LTRACEF("%016" PRIxPTR " %016zx\n", start_cursor.vaddr, start_cursor.size);
    DEBUG_ASSERT(IS_PAGE_ALIGNED(start_cursor.size));

    *new_cursor = start_cursor;

    bool unmapped = false;
    uint index = vaddr_to_index<PT_L>(new_cursor->vaddr);
    for (; index != NO_OF_PT_ENTRIES && new_cursor->size != 0; ++index) {
        pt_entry_t* e = table + index;
        if (IS_PAGE_PRESENT(*e)) {
            unmap_entry<PT_L>(cr3, new_cursor->vaddr, e, true);
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
 * @param cr3 The top-level paging structure's physical address
 * @param table The top-level paging structure's virtual address
 * @param start_cursor A cursor describing the range of address space to
 * act on within table
 * @param new_cursor A returned cursor describing how much work was not
 * completed.  Must be non-null.
 *
 * @return NO_ERROR if successful
 * @return ERR_ALREADY_EXISTS if the range overlaps an existing mapping
 * @return ERR_NO_MEMORY if intermediate page tables could not be allocated
 */
template <int Level>
static status_t x86_mmu_add_mapping(ulong cr3, pt_entry_t* table, uint mmu_flags,
                                    const MappingCursor& start_cursor, MappingCursor* new_cursor) {
    static_assert(Level >= 0, "level too low");
    static_assert(Level < X86_PAGING_LEVELS, "level too high");

    DEBUG_ASSERT(table);
    DEBUG_ASSERT(x86_mmu_check_vaddr(start_cursor.vaddr));
    DEBUG_ASSERT(x86_mmu_check_paddr(start_cursor.paddr));

    status_t ret = NO_ERROR;
    *new_cursor = start_cursor;

    arch_flags_t interm_arch_flags = get_x86_intermediate_arch_flags();
    arch_flags_t arch_flags = x86_arch_flags<Level>(mmu_flags);

    size_t ps = page_size<Level>();
    bool level_supports_large_pages = level_supports_ps(static_cast<page_table_levels>(Level));
    uint index = vaddr_to_index<Level>(new_cursor->vaddr);
    for (; index != NO_OF_PT_ENTRIES && new_cursor->size != 0; ++index) {
        pt_entry_t* e = table + index;
        // See if there's a large page in our way
        if (IS_PAGE_PRESENT(*e) && IS_LARGE_PAGE(*e)) {
            ret = ERR_ALREADY_EXISTS;
            goto err;
        }

        // Check if this is a candidate for a new large page
        bool level_valigned = page_aligned<Level>(new_cursor->vaddr);
        bool level_paligned = page_aligned<Level>(new_cursor->paddr);
        if (level_supports_large_pages && !IS_PAGE_PRESENT(*e) && level_valigned &&
            level_paligned && new_cursor->size >= ps) {

            update_entry<Level>(cr3, new_cursor->vaddr, table + index, new_cursor->paddr,
                                arch_flags | X86_MMU_PG_PS);

            new_cursor->paddr += ps;
            new_cursor->vaddr += ps;
            new_cursor->size -= ps;
            DEBUG_ASSERT(new_cursor->size <= start_cursor.size);
        } else {
            // See if we need to create a new table
            if (!IS_PAGE_PRESENT(*e)) {
                pt_entry_t* m = _map_alloc_page();
                if (m == NULL) {
                    ret = ERR_NO_MEMORY;
                    goto err;
                }

                LTRACEF_LEVEL(2, "new table %p at level %d\n", m, Level);

                update_entry<Level>(cr3, new_cursor->vaddr, e, X86_VIRT_TO_PHYS(m),
                                    interm_arch_flags);
            }

            MappingCursor cursor;
            ret = x86_mmu_add_mapping<Level - 1>(cr3, get_next_table_from_entry(*e), mmu_flags,
                                                 *new_cursor, &cursor);
            *new_cursor = cursor;
            DEBUG_ASSERT(new_cursor->size <= start_cursor.size);
            if (ret != NO_ERROR) {
                goto err;
            }
        }
    }
    return NO_ERROR;
err:
    if (Level == MAX_PAGING_LEVEL) {
        MappingCursor cursor = start_cursor;
        MappingCursor result;
        // new_cursor->size should be how much is left to be mapped still
        cursor.size -= new_cursor->size;
        if (cursor.size > 0) {
            x86_mmu_remove_mapping<MAX_PAGING_LEVEL>(cr3, table, cursor, &result);
            DEBUG_ASSERT(result.size == 0);
        }
    }
    return ret;
}

// Base case of x86_mmu_add_mapping for smallest page size
template <>
status_t x86_mmu_add_mapping<PT_L>(ulong cr3, pt_entry_t* table, uint mmu_flags,
                                   const MappingCursor& start_cursor, MappingCursor* new_cursor) {

    DEBUG_ASSERT(IS_PAGE_ALIGNED(start_cursor.size));

    *new_cursor = start_cursor;

    arch_flags_t arch_flags = x86_arch_flags<PT_L>(mmu_flags);

    uint index = vaddr_to_index<PT_L>(new_cursor->vaddr);
    for (; index != NO_OF_PT_ENTRIES && new_cursor->size != 0; ++index) {
        pt_entry_t* e = table + index;
        if (IS_PAGE_PRESENT(*e)) {
            return ERR_ALREADY_EXISTS;
        }

        update_entry<PT_L>(cr3, new_cursor->vaddr, table + index, new_cursor->paddr, arch_flags);

        new_cursor->paddr += PAGE_SIZE;
        new_cursor->vaddr += PAGE_SIZE;
        new_cursor->size -= PAGE_SIZE;
        DEBUG_ASSERT(new_cursor->size <= start_cursor.size);
    }

    return NO_ERROR;
}

/**
 * @brief Changes the permissions/caching of the range specified by start_cursor
 *
 * Level must be MAX_PAGING_LEVEL when invoked.
 *
 * @param cr3 The top-level paging structure's physical address
 * @param table The top-level paging structure's virtual address
 * @param start_cursor A cursor describing the range of address space to
 * act on within table
 * @param new_cursor A returned cursor describing how much work was not
 * completed.  Must be non-null.
 */
template <int Level>
static status_t x86_mmu_update_mapping(ulong cr3, pt_entry_t* table, uint mmu_flags,
                                       const MappingCursor& start_cursor,
                                       MappingCursor* new_cursor) {
    static_assert(Level >= 0, "level too low");
    static_assert(Level < X86_PAGING_LEVELS, "level too high");

    DEBUG_ASSERT(table);
    LTRACEF("L: %d, %016" PRIxPTR " %016zx\n", Level, start_cursor.vaddr, start_cursor.size);
    DEBUG_ASSERT(x86_mmu_check_vaddr(start_cursor.vaddr));

    status_t ret = NO_ERROR;
    *new_cursor = start_cursor;

    arch_flags_t arch_flags = x86_arch_flags<Level>(mmu_flags);

    size_t ps = page_size<Level>();
    uint index = vaddr_to_index<Level>(new_cursor->vaddr);
    for (; index != NO_OF_PT_ENTRIES && new_cursor->size != 0; ++index) {
        pt_entry_t* e = table + index;
        if (!IS_PAGE_PRESENT(*e)) {
            ret = ERR_NOT_FOUND;
            goto err;
        }

        if (IS_LARGE_PAGE(*e)) {
            bool vaddr_level_aligned = page_aligned<Level>(new_cursor->vaddr);
            // If the request covers the entire large page, just change the
            // permissions
            if (vaddr_level_aligned && new_cursor->size >= ps) {
                update_entry<Level>(cr3, new_cursor->vaddr, e, paddr_from_pte<Level>(*e),
                                    arch_flags | X86_MMU_PG_PS);

                new_cursor->vaddr += ps;
                new_cursor->size -= ps;
                DEBUG_ASSERT(new_cursor->size <= start_cursor.size);
                continue;
            }
            // Otherwise, we need to split it
            vaddr_t page_vaddr = new_cursor->vaddr & ~(ps - 1);
            ret = x86_mmu_split<Level>(cr3, page_vaddr, e);
            if (ret != NO_ERROR) {
                goto err;
            }
        }

        MappingCursor cursor;
        pt_entry_t* next_table = get_next_table_from_entry(*e);
        ret = x86_mmu_update_mapping<Level - 1>(cr3, next_table, mmu_flags, *new_cursor, &cursor);
        *new_cursor = cursor;
        if (ret != NO_ERROR) {
            goto err;
        }
        DEBUG_ASSERT(new_cursor->size <= start_cursor.size);

        DEBUG_ASSERT(new_cursor->size == 0 || page_aligned<Level>(new_cursor->vaddr));
    }
    return NO_ERROR;
err:
    // TODO: Cleanup
    return ret;
}

// Base case of x86_update_mapping for smallest page size
template <>
status_t x86_mmu_update_mapping<PT_L>(ulong cr3, pt_entry_t* table, uint mmu_flags,
                                      const MappingCursor& start_cursor,
                                      MappingCursor* new_cursor) {

    LTRACEF("%016" PRIxPTR " %016zx\n", start_cursor.vaddr, start_cursor.size);
    DEBUG_ASSERT(IS_PAGE_ALIGNED(start_cursor.size));

    *new_cursor = start_cursor;

    arch_flags_t arch_flags = x86_arch_flags<PT_L>(mmu_flags);

    uint index = vaddr_to_index<PT_L>(new_cursor->vaddr);
    for (; index != NO_OF_PT_ENTRIES && new_cursor->size != 0; ++index) {
        pt_entry_t* e = table + index;
        if (!IS_PAGE_PRESENT(*e)) {
            // TODO: Cleanup
            return ERR_NOT_FOUND;
        }
        update_entry<PT_L>(cr3, new_cursor->vaddr, e, paddr_from_pte<PT_L>(*e), arch_flags);

        new_cursor->vaddr += PAGE_SIZE;
        new_cursor->size -= PAGE_SIZE;
        DEBUG_ASSERT(new_cursor->size <= start_cursor.size);
    }
    return NO_ERROR;
}

int arch_mmu_unmap(arch_aspace_t* aspace, vaddr_t vaddr, size_t count) {
    LTRACEF("aspace %p, vaddr %#" PRIxPTR ", count %#zx\n", aspace, vaddr, count);

    DEBUG_ASSERT(aspace);
    DEBUG_ASSERT(aspace->magic == ARCH_ASPACE_MAGIC);

    if (!x86_mmu_check_vaddr(vaddr)) return ERR_INVALID_ARGS;
    if (!is_valid_vaddr(aspace, vaddr)) return ERR_INVALID_ARGS;
    if (count == 0) return NO_ERROR;

    DEBUG_ASSERT(aspace->pt_virt);

    MappingCursor start = {
        .paddr = 0, .vaddr = vaddr, .size = count * PAGE_SIZE,
    };

    MappingCursor result;
    x86_mmu_remove_mapping<MAX_PAGING_LEVEL>(aspace->pt_phys, aspace->pt_virt, start, &result);
    DEBUG_ASSERT(result.size == 0);
    return NO_ERROR;
}

int arch_mmu_map(arch_aspace_t* aspace, vaddr_t vaddr, paddr_t paddr, size_t count, uint flags) {
    DEBUG_ASSERT(aspace);
    DEBUG_ASSERT(aspace->magic == ARCH_ASPACE_MAGIC);

    LTRACEF("aspace %p, vaddr %#" PRIxPTR " paddr %#" PRIxPTR " count %#zx flags 0x%x\n", aspace, vaddr, paddr,
            count, flags);

    if ((!x86_mmu_check_paddr(paddr))) return ERR_INVALID_ARGS;
    if (!x86_mmu_check_vaddr(vaddr)) return ERR_INVALID_ARGS;
    if (!is_valid_vaddr(aspace, vaddr)) return ERR_INVALID_ARGS;
    if (count == 0) return NO_ERROR;

    if (!(flags & ARCH_MMU_FLAG_PERM_READ))
        return ERR_INVALID_ARGS;

    DEBUG_ASSERT(aspace->pt_virt);

    MappingCursor start = {
        .paddr = paddr, .vaddr = vaddr, .size = count * PAGE_SIZE,
    };
    MappingCursor result;
    status_t status = x86_mmu_add_mapping<MAX_PAGING_LEVEL>(aspace->pt_phys, aspace->pt_virt, flags,
                                                            start, &result);
    if (status != NO_ERROR) {
        dprintf(SPEW, "Add mapping failed with err=%d\n", status);
        return status;
    }
    DEBUG_ASSERT(result.size == 0);
    return NO_ERROR;
}

int arch_mmu_protect(arch_aspace_t* aspace, vaddr_t vaddr, size_t count, uint flags) {
    DEBUG_ASSERT(aspace);
    DEBUG_ASSERT(aspace->magic == ARCH_ASPACE_MAGIC);

    LTRACEF("aspace %p, vaddr %#" PRIxPTR " count %#zx flags 0x%x\n", aspace, vaddr, count, flags);

    if (!x86_mmu_check_vaddr(vaddr)) return ERR_INVALID_ARGS;
    if (!is_valid_vaddr(aspace, vaddr)) return ERR_INVALID_ARGS;
    if (count == 0) return NO_ERROR;

    if (!(flags & ARCH_MMU_FLAG_PERM_READ))
        return ERR_INVALID_ARGS;

    MappingCursor start = {
        .paddr = 0, .vaddr = vaddr, .size = count * PAGE_SIZE,
    };
    MappingCursor result;
    status_t status = x86_mmu_update_mapping<MAX_PAGING_LEVEL>(aspace->pt_phys, aspace->pt_virt,
                                                               flags, start, &result);
    if (status != NO_ERROR) {
        return status;
    }
    DEBUG_ASSERT(result.size == 0);
    return NO_ERROR;
}

void x86_mmu_early_init() {
    x86_mmu_mem_type_init();
    x86_mmu_percpu_init();

#if ARCH_X86_64
    /* unmap the lower identity mapping */
    unmap_entry<PML4_L>(x86_get_cr3(), 0, &pml4[0], true);
#else
    /* unmap the lower identity mapping */
    for (uint i = 0; i < (1 * GB) / (4 * MB); i++) {
        pd[i] = 0;
    }

    /* tlb flush */
    tlb_global_invalidate();
#endif

    /* get the address width from the CPU */
    uint8_t vaddr_width = x86_linear_address_width();
    uint8_t paddr_width = x86_physical_address_width();

    /* if we got something meaningful, override the defaults.
     * some combinations of cpu on certain emulators seems to return
     * nonsense paddr widths (1), so trim it. */
    if (paddr_width > g_paddr_width) g_paddr_width = paddr_width;
    if (vaddr_width > g_vaddr_width) g_vaddr_width = vaddr_width;

    LTRACEF("paddr_width %u vaddr_width %u\n", g_paddr_width, g_vaddr_width);
}

void x86_mmu_init(void) {}

/*
 * Fill in the high level x86 arch aspace structure and allocating a top level page table.
 */
status_t arch_mmu_init_aspace(arch_aspace_t* aspace, vaddr_t base, size_t size, uint flags) {
    DEBUG_ASSERT(aspace);
    DEBUG_ASSERT(aspace->magic != ARCH_ASPACE_MAGIC);

    LTRACEF("aspace %p, base %#" PRIxPTR ", size 0x%zx, flags 0x%x\n", aspace, base, size, flags);

    aspace->magic = ARCH_ASPACE_MAGIC;
    aspace->flags = flags;
    aspace->base = base;
    aspace->size = size;
    if (flags & ARCH_ASPACE_FLAG_KERNEL) {
        aspace->pt_phys = kernel_pt_phys;
        aspace->pt_virt = (pt_entry_t*)X86_PHYS_TO_VIRT(aspace->pt_phys);
        LTRACEF("kernel aspace: pt phys %#" PRIxPTR ", virt %p\n", aspace->pt_phys, aspace->pt_virt);
    } else {
#if ARCH_X86_32
        /* not fully functional on 32bit x86 */
        return ERR_NOT_SUPPORTED;
#else
        /* allocate a top level page table for the new address space */
        paddr_t pa;
        aspace->pt_virt = (pt_entry_t*)pmm_alloc_kpage(&pa);
        if (!aspace->pt_virt) {
            TRACEF("error allocating top level page directory\n");
            return ERR_NO_MEMORY;
        }
        aspace->pt_phys = pa;

        /* zero out the user space half of it */
        memset(aspace->pt_virt, 0, sizeof(pt_entry_t) * NO_OF_PT_ENTRIES / 2);

        /* copy the kernel portion of it from the master kernel pt */
        memcpy(aspace->pt_virt + NO_OF_PT_ENTRIES / 2, &KERNEL_PT[NO_OF_PT_ENTRIES / 2],
               sizeof(pt_entry_t) * NO_OF_PT_ENTRIES / 2);

        LTRACEF("user aspace: pt phys %#" PRIxPTR ", virt %p\n", aspace->pt_phys, aspace->pt_virt);
#endif
    }
    aspace->io_bitmap_ptr = NULL;
    spin_lock_init(&aspace->io_bitmap_lock);

    return NO_ERROR;
}

status_t arch_mmu_destroy_aspace(arch_aspace_t* aspace) {
    DEBUG_ASSERT(aspace->magic == ARCH_ASPACE_MAGIC);

#if LK_DEBUGLEVEL > 1
    pt_entry_t *table = static_cast<pt_entry_t *>(aspace->pt_virt);
    uint start = vaddr_to_index<MAX_PAGING_LEVEL>(aspace->base);
    uint end = vaddr_to_index<MAX_PAGING_LEVEL>(aspace->base + aspace->size - 1);

    // Don't check start if that table is shared with another aspace
    if (!page_aligned<MAX_PAGING_LEVEL>(aspace->base)) {
        start += 1;
    }
    // Do check the end if it fills out the table entry
    if (page_aligned<MAX_PAGING_LEVEL>(aspace->base + aspace->size)) {
        end += 1;
    }

    for (uint i = start; i < end; ++i) {
        DEBUG_ASSERT(!IS_PAGE_PRESENT(table[i]));
    }
#endif

    if (aspace->io_bitmap_ptr) {
        free(aspace->io_bitmap_ptr);
    }

    pmm_free_page(paddr_to_vm_page(aspace->pt_phys));

    aspace->magic = 0;

    return NO_ERROR;
}

void arch_mmu_context_switch(arch_aspace_t *old_aspace, arch_aspace_t *aspace) {
    if (aspace != NULL) {
        DEBUG_ASSERT(aspace->magic == ARCH_ASPACE_MAGIC);
        LTRACEF_LEVEL(3, "switching to aspace %p, pt %#" PRIXPTR "\n", aspace, aspace->pt_phys);
        x86_set_cr3(aspace->pt_phys);
    } else {
        LTRACEF_LEVEL(3, "switching to kernel aspace, pt %#" PRIxPTR "\n", kernel_pt_phys);
        x86_set_cr3(kernel_pt_phys);
    }

    /* set the io bitmap for this thread */
    bool set_bitmap = false;
    if (aspace) {
        spin_lock(&aspace->io_bitmap_lock);
        if (aspace->io_bitmap_ptr) {
            x86_set_tss_io_bitmap(static_cast<uint8_t *>(aspace->io_bitmap_ptr));
            set_bitmap = true;
        }
        spin_unlock(&aspace->io_bitmap_lock);
    }
    if (!set_bitmap && old_aspace && old_aspace->io_bitmap_ptr) {
        x86_clear_tss_io_bitmap();
    }
}

status_t arch_mmu_query(arch_aspace_t* aspace, vaddr_t vaddr, paddr_t* paddr, uint* flags) {
    DEBUG_ASSERT(aspace->magic == ARCH_ASPACE_MAGIC);

    page_table_levels ret_level;

    LTRACEF("aspace %p, vaddr %#" PRIxPTR ", paddr %p, flags %p\n", aspace, vaddr, paddr, flags);

    DEBUG_ASSERT(aspace);

    if (!paddr) return ERR_INVALID_ARGS;

    if (!is_valid_vaddr(aspace, vaddr)) return ERR_INVALID_ARGS;

    pt_entry_t* last_valid_entry;
    status_t stat = x86_mmu_get_mapping<MAX_PAGING_LEVEL>(aspace->pt_virt, vaddr, &ret_level,
                                                          &last_valid_entry);
    if (stat != NO_ERROR) return stat;

    DEBUG_ASSERT(last_valid_entry);
    LTRACEF("last_valid_entry (%p) 0x%" PRIxPTE ", level %d\n", last_valid_entry, *last_valid_entry,
            ret_level);

    /* based on the return level, parse the page table entry */
    switch (ret_level) {
#if X86_PAGING_LEVELS > 2
        case PDP_L: /* 1GB page */
            *paddr = paddr_from_pte<PDP_L>(*last_valid_entry);
            *paddr |= vaddr & PAGE_OFFSET_MASK_HUGE;
            break;
#endif
        case PD_L: /* 2MB page */
            *paddr = paddr_from_pte<PD_L>(*last_valid_entry);
            *paddr |= vaddr & PAGE_OFFSET_MASK_LARGE;
            break;
        case PT_L: /* 4K page */
            *paddr = paddr_from_pte<PT_L>(*last_valid_entry);
            *paddr |= vaddr & PAGE_OFFSET_MASK_4KB;
            break;
        default:
            panic("arch_mmu_query: unhandled frame level\n");
    }

    LTRACEF("paddr %#" PRIxPTR "\n", *paddr);

    /* converting x86 arch specific flags to arch mmu flags */
    if (flags) {
        *flags = arch_mmu_flags(*last_valid_entry, ret_level);
    }

    return NO_ERROR;
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
    if (x86_feature_test(X86_FEATURE_SMEP)) cr4 |= X86_CR4_SMEP;
    if (x86_feature_test(X86_FEATURE_SMAP)) cr4 |= X86_CR4_SMAP;
    x86_set_cr4(cr4);

    /* Set NXE bit in MSR_EFER*/
    uint64_t efer_msr = read_msr(X86_MSR_EFER);
    efer_msr |= X86_EFER_NXE;
    write_msr(X86_MSR_EFER, efer_msr);
}
