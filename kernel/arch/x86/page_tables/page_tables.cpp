// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/x86/page_tables/page_tables.h>

#include <arch/x86/feature.h>
#include <arch/x86/page_tables/constants.h>
#include <assert.h>
#include <fbl/algorithm.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <trace.h>
#include <vm/physmap.h>
#include <vm/pmm.h>

#define LOCAL_TRACE 0

namespace {

// Return the page size for this level
size_t page_size(PageTableLevel level) {
    switch (level) {
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

// Whether an address is aligned to the page size of this level
bool page_aligned(PageTableLevel level, vaddr_t vaddr) {
    return (vaddr & (page_size(level) - 1)) == 0;
}

// Extract the index needed for finding |vaddr| for the given level
uint vaddr_to_index(PageTableLevel level, vaddr_t vaddr) {
    switch (level) {
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

// Convert a PTE to a physical address
paddr_t paddr_from_pte(PageTableLevel level, pt_entry_t pte) {
    DEBUG_ASSERT(IS_PAGE_PRESENT(pte));

    paddr_t pa;
    switch (level) {
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
        panic("paddr_from_pte at unhandled level %d\n", level);
    }

    return pa;
}

PageTableLevel lower_level(PageTableLevel level) {
    DEBUG_ASSERT(level != 0);
    return (PageTableLevel)(level - 1);
}

} // namespace

void PendingTlbInvalidation::enqueue(vaddr_t v, PageTableLevel level, bool is_global_page,
                                     bool is_terminal) {
    if (is_global_page) {
        contains_global = true;
    }

    // We mark PML4_L entries as full shootdowns, since it's going to be
    // expensive one way or another.
    if (count >= fbl::count_of(item) || level == PML4_L) {
        full_shootdown = true;
        return;
    }
    item[count].set_page_level(static_cast<uint64_t>(level));
    item[count].set_is_global(is_global_page);
    item[count].set_is_terminal(is_terminal);
    item[count].set_encoded_addr(v >> PAGE_SIZE_SHIFT);
    count++;
}

void PendingTlbInvalidation::clear() {
    count = 0;
    full_shootdown = false;
    contains_global = false;
}

PendingTlbInvalidation::~PendingTlbInvalidation() {
    DEBUG_ASSERT(count == 0);
}

// Utility for coalescing cache line flushes when modifying page tables.  This
// allows us to mutate adjacent page table entries without having to flush for
// each cache line multiple times.
class X86PageTableBase::CacheLineFlusher {
public:
    // If |perform_invalidations| is false, this class acts as a no-op.
    explicit CacheLineFlusher(bool perform_invalidations);
    ~CacheLineFlusher();
    void FlushPtEntry(const volatile pt_entry_t* entry);

    void ForceFlush();
private:
    DISALLOW_COPY_ASSIGN_AND_MOVE(CacheLineFlusher);

    // The cache-aligned address that currently dirty.  If 0, no dirty line.
    uintptr_t dirty_line_;

    const uintptr_t cl_mask_;
    const bool perform_invalidations_;
};

X86PageTableBase::CacheLineFlusher::CacheLineFlusher(bool perform_invalidations)
    : dirty_line_(0), cl_mask_(~(x86_get_clflush_line_size() - 1ull)),
      perform_invalidations_(perform_invalidations) {
}

X86PageTableBase::CacheLineFlusher::~CacheLineFlusher() {
    ForceFlush();
}

void X86PageTableBase::CacheLineFlusher::ForceFlush() {
    if (dirty_line_ && perform_invalidations_) {
        __asm__ volatile("clflush %0\n"
                         :
                         : "m"(*reinterpret_cast<char*>(dirty_line_))
                         : "memory");
        dirty_line_ = 0;
    }
}

void X86PageTableBase::CacheLineFlusher::FlushPtEntry(const volatile pt_entry_t* entry) {
    uintptr_t entry_line = reinterpret_cast<uintptr_t>(entry) & cl_mask_;
    if (entry_line != dirty_line_) {
        ForceFlush();
        dirty_line_ = entry_line;
    }
}

// Utility for managing consistency of the page tables from a cache and TLB
// point-of-view.  It ensures that memory is not freed while a TLB entry may
// refer to it, and that changes to the page tables have appropriate visiblity
// to the hardware interpreting them.  Finish MUST be called on this
// class, even if the page table change failed.
class X86PageTableBase::ConsistencyManager {
public:
    explicit ConsistencyManager(X86PageTableBase* pt);
    ~ConsistencyManager();

    // Disable thread safety analysis here because it has trouble identifying
    // that |pt_->lock_| is held here.
    void queue_free(vm_page_t* page) TA_NO_THREAD_SAFETY_ANALYSIS {
        DEBUG_ASSERT(pt_->lock_.IsHeld());

        list_add_tail(&to_free_, &page->queue_node);
        pt_->pages_--;
    }

    CacheLineFlusher* cache_line_flusher() { return &clf_; }
    PendingTlbInvalidation* pending_tlb() { return &tlb_; }

    // This function must be called while holding pt_->lock_.
    void Finish();
private:
    X86PageTableBase* pt_;

    // Cache line to flush prior to TLB invalidations
    X86PageTableBase::CacheLineFlusher clf_;

    // TLB invalidations that need to occur
    PendingTlbInvalidation tlb_;

    // vm_page_t's to relese to the PMM after the TLB invalidation occurs
    list_node to_free_;
};

X86PageTableBase::ConsistencyManager::ConsistencyManager(X86PageTableBase* pt)
    : pt_(pt), clf_(pt->needs_cache_flushes()) {

    to_free_ = LIST_INITIAL_VALUE(to_free_);
}

X86PageTableBase::ConsistencyManager::~ConsistencyManager() {
    DEBUG_ASSERT(pt_ == nullptr);

    // We free the paging structures here rather than in Finish(), to allow
    // support deferring invoking pmm_free() until after we've left the page
    // table lock.
    if (!list_is_empty(&to_free_)) {
        pmm_free(&to_free_);
    }
}

void X86PageTableBase::ConsistencyManager::Finish() {
    DEBUG_ASSERT(pt_->lock_.IsHeld());

    clf_.ForceFlush();
    if (pt_->needs_cache_flushes()) {
        // If the hardware needs cache flushes for the tables to be visible,
        // make sure we serialize the flushes before issuing the TLB
        // invalidations.
        mb();
    }
    pt_->TlbInvalidate(&tlb_);
    pt_ = nullptr;
}

struct X86PageTableBase::MappingCursor {
public:
    /**
   * @brief Update the cursor to skip over a not-present page table entry.
   */
    void SkipEntry(PageTableLevel level) {
        const size_t ps = page_size(level);
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

void X86PageTableBase::UpdateEntry(ConsistencyManager* cm, PageTableLevel level, vaddr_t vaddr,
                                   volatile pt_entry_t* pte, paddr_t paddr, PtFlags flags,
                                   bool was_terminal) {
    DEBUG_ASSERT(pte);
    DEBUG_ASSERT(IS_PAGE_ALIGNED(paddr));

    pt_entry_t olde = *pte;

    /* set the new entry */
    *pte = paddr | flags | X86_MMU_PG_P;
    cm->cache_line_flusher()->FlushPtEntry(pte);

    /* attempt to invalidate the page */
    if (IS_PAGE_PRESENT(olde)) {
        // TODO(teisenbe): the is_kernel_address should be a check for the
        // global bit
        cm->pending_tlb()->enqueue(vaddr, level, is_kernel_address(vaddr), was_terminal);
    }
}

void X86PageTableBase::UnmapEntry(ConsistencyManager* cm, PageTableLevel level, vaddr_t vaddr,
                                  volatile pt_entry_t* pte, bool was_terminal) {
    DEBUG_ASSERT(pte);

    pt_entry_t olde = *pte;

    *pte = 0;
    cm->cache_line_flusher()->FlushPtEntry(pte);

    /* attempt to invalidate the page */
    if (IS_PAGE_PRESENT(olde)) {
        // TODO(teisenbe): the is_kernel_address should be a check for the
        // global bit
        cm->pending_tlb()->enqueue(vaddr, level, is_kernel_address(vaddr), was_terminal);
    }
}

/**
 * @brief Allocating a new page table
 */
static volatile pt_entry_t* _map_alloc_page(void) {
    paddr_t pa;
    vm_page_t* p = pmm_alloc_page(0, &pa);
    if (!p) {
        return nullptr;
    }
    p->state = VM_PAGE_STATE_MMU;

    pt_entry_t* page_ptr = static_cast<pt_entry_t*>(paddr_to_physmap(pa));
    DEBUG_ASSERT(page_ptr);

    arch_zero_page(page_ptr);

    return page_ptr;
}

/*
 * @brief Split the given large page into smaller pages
 */
zx_status_t X86PageTableBase::SplitLargePage(PageTableLevel level, vaddr_t vaddr,
                                             volatile pt_entry_t* pte, ConsistencyManager* cm) {
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
        UpdateEntry(cm, lower_level(level), new_vaddr, e, new_paddr, flags,
                    false /* was_terminal */);
        new_vaddr += ps;
        new_paddr += ps;
    }
    DEBUG_ASSERT(new_vaddr == vaddr + page_size(level));

    flags = intermediate_flags();
    UpdateEntry(cm, level, vaddr, pte, X86_VIRT_TO_PHYS(m), flags, true /* was_terminal */);
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
                                         PageTableLevel level,
                                         PageTableLevel* ret_level,
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
                                           PageTableLevel* ret_level,
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
 * Level must be top_level() when invoked.  The caller must, even on failure,
 * free all pages in the |to_free| list and adjust the |pages_| count.
 *
 * @param table The top-level paging structure's virtual address.
 * @param start_cursor A cursor describing the range of address space to
 * unmap within table
 * @param new_cursor A returned cursor describing how much work was not
 * completed.  Must be non-null.
 *
 * @return true if at least one page was unmapped at this level
 */
bool X86PageTableBase::RemoveMapping(volatile pt_entry_t* table, PageTableLevel level,
                                     const MappingCursor& start_cursor, MappingCursor* new_cursor,
                                     ConsistencyManager* cm) {
    DEBUG_ASSERT(table);
    LTRACEF("L: %d, %016" PRIxPTR " %016zx\n", level, start_cursor.vaddr,
            start_cursor.size);
    DEBUG_ASSERT(check_vaddr(start_cursor.vaddr));

    if (level == PT_L) {
        return RemoveMappingL0(table, start_cursor, new_cursor, cm);
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
                UnmapEntry(cm, level, new_cursor->vaddr, e, true /* was_terminal */);
                unmapped = true;

                new_cursor->vaddr += ps;
                new_cursor->size -= ps;
                DEBUG_ASSERT(new_cursor->size <= start_cursor.size);
                continue;
            }
            // Otherwise, we need to split it
            vaddr_t page_vaddr = new_cursor->vaddr & ~(ps - 1);
            zx_status_t status = SplitLargePage(level, page_vaddr, e, cm);
            if (status != ZX_OK) {
                // If split fails, just unmap the whole thing, and let a
                // subsequent page fault clean it up.
                UnmapEntry(cm, level, new_cursor->vaddr, e, true /* was_terminal */);
                unmapped = true;

                new_cursor->SkipEntry(level);
                DEBUG_ASSERT(new_cursor->size <= start_cursor.size);
            }
            pt_val = *e;
        }

        MappingCursor cursor;
        volatile pt_entry_t* next_table = get_next_table_from_entry(pt_val);
        bool lower_unmapped = RemoveMapping(next_table, lower_level(level),
                                            *new_cursor, &cursor, cm);

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

            UnmapEntry(cm, level, new_cursor->vaddr, e, false /* was_terminal */);
            vm_page_t* page = paddr_to_vm_page(ptable_phys);

            DEBUG_ASSERT(page);
            DEBUG_ASSERT_MSG(page->state == VM_PAGE_STATE_MMU,
                             "page %p state %u, paddr %#" PRIxPTR "\n", page, page->state,
                             X86_VIRT_TO_PHYS(next_table));
            DEBUG_ASSERT(!list_in_list(&page->queue_node));

            cm->queue_free(page);
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
                                       const MappingCursor& start_cursor, MappingCursor* new_cursor,
                                       ConsistencyManager* cm) {
    LTRACEF("%016" PRIxPTR " %016zx\n", start_cursor.vaddr, start_cursor.size);
    DEBUG_ASSERT(IS_PAGE_ALIGNED(start_cursor.size));

    *new_cursor = start_cursor;

    bool unmapped = false;
    uint index = vaddr_to_index(PT_L, new_cursor->vaddr);
    for (; index != NO_OF_PT_ENTRIES && new_cursor->size != 0; ++index) {
        volatile pt_entry_t* e = table + index;
        if (IS_PAGE_PRESENT(*e)) {
            UnmapEntry(cm, PT_L, new_cursor->vaddr, e, true /* was_terminal */);
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
 * Level must be top_level() when invoked.
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
                                         PageTableLevel level, const MappingCursor& start_cursor,
                                         MappingCursor* new_cursor,
                                         ConsistencyManager* cm) {
    DEBUG_ASSERT(table);
    DEBUG_ASSERT(check_vaddr(start_cursor.vaddr));
    DEBUG_ASSERT(check_paddr(start_cursor.paddr));

    zx_status_t ret = ZX_OK;
    *new_cursor = start_cursor;

    if (level == PT_L) {
        return AddMappingL0(table, mmu_flags, start_cursor, new_cursor, cm);
    }

    // Disable thread safety analysis, since Clang has trouble noticing that
    // lock_ is held when RemoveMapping is called.
    auto abort = fbl::MakeAutoCall([&]() TA_NO_THREAD_SAFETY_ANALYSIS {
        if (level == top_level()) {
            MappingCursor cursor = start_cursor;
            MappingCursor result;
            // new_cursor->size should be how much is left to be mapped still
            cursor.size -= new_cursor->size;
            if (cursor.size > 0) {
                RemoveMapping(table, level, cursor, &result, cm);
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

            UpdateEntry(cm, level, new_cursor->vaddr, table + index,
                        new_cursor->paddr, term_flags | X86_MMU_PG_PS, false /* was_terminal */);
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

                UpdateEntry(cm, level, new_cursor->vaddr, e,
                            X86_VIRT_TO_PHYS(m), interm_flags, false /* was_terminal */);
                pt_val = *e;
                pages_++;
            }

            MappingCursor cursor;
            ret = AddMapping(get_next_table_from_entry(pt_val), mmu_flags,
                             lower_level(level), *new_cursor, &cursor, cm);
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
                                           MappingCursor* new_cursor, ConsistencyManager* cm) {
    DEBUG_ASSERT(IS_PAGE_ALIGNED(start_cursor.size));

    *new_cursor = start_cursor;

    X86PageTableBase::PtFlags term_flags = terminal_flags(PT_L, mmu_flags);

    uint index = vaddr_to_index(PT_L, new_cursor->vaddr);
    for (; index != NO_OF_PT_ENTRIES && new_cursor->size != 0; ++index) {
        volatile pt_entry_t* e = table + index;
        if (IS_PAGE_PRESENT(*e)) {
            return ZX_ERR_ALREADY_EXISTS;
        }

        UpdateEntry(cm, PT_L, new_cursor->vaddr, e, new_cursor->paddr, term_flags,
                    false /* was_terminal */);

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
 * Level must be top_level() when invoked.  The caller must, even on failure,
 * free all pages in the |to_free| list and adjust the |pages_| count.
 *
 * @param table The top-level paging structure's virtual address.
 * @param start_cursor A cursor describing the range of address space to
 * act on within table
 * @param new_cursor A returned cursor describing how much work was not
 * completed.  Must be non-null.
 */
zx_status_t X86PageTableBase::UpdateMapping(volatile pt_entry_t* table, uint mmu_flags,
                                            PageTableLevel level, const MappingCursor& start_cursor,
                                            MappingCursor* new_cursor, ConsistencyManager* cm) {
    DEBUG_ASSERT(table);
    LTRACEF("L: %d, %016" PRIxPTR " %016zx\n", level, start_cursor.vaddr,
            start_cursor.size);
    DEBUG_ASSERT(check_vaddr(start_cursor.vaddr));

    if (level == PT_L) {
        return UpdateMappingL0(table, mmu_flags, start_cursor, new_cursor, cm);
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
                UpdateEntry(cm, level, new_cursor->vaddr, e,
                            paddr_from_pte(level, pt_val),
                            term_flags | X86_MMU_PG_PS, true /* was_terminal */);
                new_cursor->vaddr += ps;
                new_cursor->size -= ps;
                DEBUG_ASSERT(new_cursor->size <= start_cursor.size);
                continue;
            }
            // Otherwise, we need to split it
            vaddr_t page_vaddr = new_cursor->vaddr & ~(ps - 1);
            ret = SplitLargePage(level, page_vaddr, e, cm);
            if (ret != ZX_OK) {
                // If we failed to split the table, just unmap it.  Subsequent
                // page faults will bring it back in.
                MappingCursor cursor;
                cursor.vaddr = new_cursor->vaddr;
                cursor.size = ps;

                MappingCursor tmp_cursor;
                RemoveMapping(table, level, cursor, &tmp_cursor, cm);

                new_cursor->SkipEntry(level);
            }
            pt_val = *e;
        }

        MappingCursor cursor;
        volatile pt_entry_t* next_table = get_next_table_from_entry(pt_val);
        ret = UpdateMapping(next_table, mmu_flags, lower_level(level),
                            *new_cursor, &cursor, cm);
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
zx_status_t X86PageTableBase::UpdateMappingL0(volatile pt_entry_t* table, uint mmu_flags,
                                              const MappingCursor& start_cursor,
                                              MappingCursor* new_cursor,
                                              ConsistencyManager* cm) {
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
            UpdateEntry(cm, PT_L, new_cursor->vaddr, e, paddr_from_pte(PT_L, pt_val),
                        term_flags, true /* was_terminal */);
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

    if (!check_vaddr(vaddr))
        return ZX_ERR_INVALID_ARGS;
    if (count == 0)
        return ZX_OK;

    MappingCursor start = {
        .paddr = 0, .vaddr = vaddr, .size = count * PAGE_SIZE,
    };
    MappingCursor result;

    ConsistencyManager cm(this);
    {
        fbl::AutoLock a(&lock_);
        DEBUG_ASSERT(virt_);
        RemoveMapping(virt_, top_level(), start, &result, &cm);
        cm.Finish();
    }
    DEBUG_ASSERT(result.size == 0);

    if (unmapped)
        *unmapped = count;

    return ZX_OK;
}

zx_status_t X86PageTableBase::MapPages(vaddr_t vaddr, paddr_t* phys, size_t count,
                                       uint mmu_flags, size_t* mapped) {
    canary_.Assert();

    LTRACEF("aspace %p, vaddr %#" PRIxPTR " count %#zx mmu_flags 0x%x\n",
            this, vaddr, count, mmu_flags);

    if (!check_vaddr(vaddr))
        return ZX_ERR_INVALID_ARGS;
    for (size_t i = 0; i < count; ++i) {
        if (!check_paddr(phys[i]))
            return ZX_ERR_INVALID_ARGS;
    }
    if (count == 0)
        return ZX_OK;

    if (!allowed_flags(mmu_flags))
        return ZX_ERR_INVALID_ARGS;

    PageTableLevel top = top_level();
    ConsistencyManager cm(this);
    {
        fbl::AutoLock a(&lock_);
        DEBUG_ASSERT(virt_);

        // TODO(teisenbe): Improve performance of this function by integrating deeper into
        // the algorithm (e.g. make the cursors aware of the page array).
        size_t idx = 0;
        auto undo = fbl::MakeAutoCall([&]() TA_NO_THREAD_SAFETY_ANALYSIS {
            if (idx > 0) {
                MappingCursor start = {
                    .paddr = 0, .vaddr = vaddr, .size = idx * PAGE_SIZE,
                };

                MappingCursor result;
                RemoveMapping(virt_, top, start, &result, &cm);
                DEBUG_ASSERT(result.size == 0);
            }
            cm.Finish();
        });

        vaddr_t v = vaddr;
        for (; idx < count; ++idx) {
            MappingCursor start = {
                .paddr = phys[idx], .vaddr = v, .size = PAGE_SIZE,
            };
            MappingCursor result;
            zx_status_t status = AddMapping(virt_, mmu_flags, top, start, &result, &cm);
            if (status != ZX_OK) {
                dprintf(SPEW, "Add mapping failed with err=%d\n", status);
                return status;
            }
            DEBUG_ASSERT(result.size == 0);

            v += PAGE_SIZE;
        }

        undo.cancel();
        cm.Finish();
    }

    if (mapped) {
        *mapped = count;
    }
    return ZX_OK;
}

zx_status_t X86PageTableBase::MapPagesContiguous(vaddr_t vaddr, paddr_t paddr,
                                                 const size_t count, uint mmu_flags,
                                                 size_t* mapped) {
    canary_.Assert();

    LTRACEF("aspace %p, vaddr %#" PRIxPTR " paddr %#" PRIxPTR " count %#zx mmu_flags 0x%x\n",
            this, vaddr, paddr, count, mmu_flags);

    if (!check_paddr(paddr))
        return ZX_ERR_INVALID_ARGS;
    if (!check_vaddr(vaddr))
        return ZX_ERR_INVALID_ARGS;
    if (count == 0)
        return ZX_OK;

    if (!allowed_flags(mmu_flags))
        return ZX_ERR_INVALID_ARGS;

    MappingCursor start = {
        .paddr = paddr, .vaddr = vaddr, .size = count * PAGE_SIZE,
    };
    MappingCursor result;
    ConsistencyManager cm(this);
    {
        fbl::AutoLock a(&lock_);
        DEBUG_ASSERT(virt_);
        zx_status_t status = AddMapping(virt_, mmu_flags, top_level(), start, &result, &cm);
        cm.Finish();
        if (status != ZX_OK) {
            dprintf(SPEW, "Add mapping failed with err=%d\n", status);
            return status;
        }
    }
    DEBUG_ASSERT(result.size == 0);

    if (mapped)
        *mapped = count;

    return ZX_OK;
}

zx_status_t X86PageTableBase::ProtectPages(vaddr_t vaddr, size_t count, uint mmu_flags) {
    canary_.Assert();

    LTRACEF("aspace %p, vaddr %#" PRIxPTR " count %#zx mmu_flags 0x%x\n",
            this, vaddr, count, mmu_flags);

    if (!check_vaddr(vaddr))
        return ZX_ERR_INVALID_ARGS;
    if (count == 0)
        return ZX_OK;

    if (!allowed_flags(mmu_flags))
        return ZX_ERR_INVALID_ARGS;

    MappingCursor start = {
        .paddr = 0, .vaddr = vaddr, .size = count * PAGE_SIZE,
    };
    MappingCursor result;
    ConsistencyManager cm(this);
    {
        fbl::AutoLock a(&lock_);
        zx_status_t status = UpdateMapping(virt_, mmu_flags, top_level(), start, &result, &cm);
        cm.Finish();
        if (status != ZX_OK) {
            return status;
        }
    }
    DEBUG_ASSERT(result.size == 0);
    return ZX_OK;
}

zx_status_t X86PageTableBase::QueryVaddr(vaddr_t vaddr, paddr_t* paddr, uint* mmu_flags) {
    canary_.Assert();

    PageTableLevel ret_level;

    LTRACEF("aspace %p, vaddr %#" PRIxPTR ", paddr %p, mmu_flags %p\n", this, vaddr, paddr,
            mmu_flags);

    fbl::AutoLock a(&lock_);

    volatile pt_entry_t* last_valid_entry;
    zx_status_t status = GetMapping(virt_, vaddr, top_level(), &ret_level, &last_valid_entry);
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

void X86PageTableBase::Destroy(vaddr_t base, size_t size) {
    canary_.Assert();

#if LK_DEBUGLEVEL > 1
    PageTableLevel top = top_level();
    if (virt_) {
        pt_entry_t* table = static_cast<pt_entry_t*>(virt_);
        uint start = vaddr_to_index(top, base);
        uint end = vaddr_to_index(top, base + size - 1);

        // Don't check start if that table is shared with another aspace.
        if (!page_aligned(top, base)) {
            start += 1;
        }
        // Do check the end if it fills out the table entry.
        if (page_aligned(top, base + size)) {
            end += 1;
        }

        for (uint i = start; i < end; ++i) {
            DEBUG_ASSERT(!IS_PAGE_PRESENT(table[i]));
        }
    }
#endif

    if (phys_) {
        pmm_free_page(paddr_to_vm_page(phys_));
        phys_ = 0;
    }
}
