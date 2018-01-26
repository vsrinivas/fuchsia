// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
#include "pmm_arena.h"

#include "vm_priv.h"

#include <err.h>
#include <inttypes.h>
#include <pretty/sizes.h>
#include <string.h>
#include <trace.h>
#include <vm/bootalloc.h>
#include <vm/bootreserve.h>
#include <vm/physmap.h>
#include <zircon/types.h>

#define LOCAL_TRACE MAX(VM_GLOBAL_TRACE, 0)

#if PMM_ENABLE_FREE_FILL
void PmmArena::EnforceFill() {
    DEBUG_ASSERT(!enforce_fill_);

    vm_page_t* page;
    list_for_every_entry (&free_list_, page, vm_page_t, free.node) {
        FreeFill(page);
    }

    enforce_fill_ = true;
}

void PmmArena::FreeFill(vm_page_t* page) {
    paddr_t paddr = page_address_from_arena(page);
    void* kvaddr = paddr_to_physmap(paddr);
    memset(kvaddr, PMM_FREE_FILL_BYTE, PAGE_SIZE);
}

void PmmArena::CheckFreeFill(vm_page_t* page) {
    paddr_t paddr = page_address_from_arena(page);
    uint8_t* kvaddr = static_cast<uint8_t*>(paddr_to_physmap(paddr));
    for (size_t j = 0; j < PAGE_SIZE; ++j) {
        ASSERT(!enforce_fill_ || *(kvaddr + j) == PMM_FREE_FILL_BYTE);
    }
}
#endif // PMM_ENABLE_FREE_FILL

zx_status_t PmmArena::Init(const pmm_arena_info_t* info) {
    // TODO: validate that info is sane (page aligned, etc)
    info_ = *info;

    /* allocate an array of pages to back this one */
    size_t page_count = size() / PAGE_SIZE;
    size_t page_array_size = ROUNDUP_PAGE_SIZE(page_count * VM_PAGE_STRUCT_SIZE);

    // if the arena is too small to be useful, bail
    if (page_array_size >= size()) {
        printf("PMM: arena too small to be useful (size %zu)\n", size());
        return ZX_ERR_BUFFER_TOO_SMALL;
    }

    /* allocate a chunk to back the page array out of the arena itself, near the top of memory */
    reserve_range_t range;
    auto status = boot_reserve_range_search(base(), size(), page_array_size, &range);
    if (status != ZX_OK) {
        printf("PMM: arena intersects with reserved memory in unresovable way\n");
        return ZX_ERR_NO_MEMORY;
    }

    DEBUG_ASSERT(range.pa >= base() && range.len <= page_array_size);

    /* get the kernel pointer */
    void* raw_page_array = paddr_to_physmap(range.pa);
    LTRACEF("arena for base 0%#" PRIxPTR " size %#zx page array at %p size %#zx\n", base(), size(),
            raw_page_array, page_array_size);

    memset(raw_page_array, 0, page_array_size);

    page_array_ = (vm_page_t*)raw_page_array;

    /* compute the range of the array that backs the array itself */
    size_t array_start_index = (PAGE_ALIGN(range.pa) - info_.base) / PAGE_SIZE;
    size_t array_end_index = array_start_index + page_array_size / PAGE_SIZE;
    LTRACEF("array_start_index %zu, array_end_index %zu, page_count %zu\n",
            array_start_index, array_end_index, page_count);

    DEBUG_ASSERT(array_start_index < page_count && array_end_index <= page_count);

    /* add all pages that aren't part of the page array to the free list */
    /* pages part of the free array go to the WIRED state */
    for (size_t i = 0; i < page_count; i++) {
        auto& p = page_array_[i];

        if (i >= array_start_index && i < array_end_index) {
            p.state = VM_PAGE_STATE_WIRED;
        } else {
            p.state = VM_PAGE_STATE_FREE;
            list_add_tail(&free_list_, &p.free.node);
            free_count_++;
        }
    }

    return ZX_OK;
}

vm_page_t* PmmArena::AllocPage(paddr_t* pa) {
    vm_page_t* page = list_remove_head_type(&free_list_, vm_page_t, free.node);
    if (!page)
        return nullptr;

    DEBUG_ASSERT(free_count_ > 0);

    free_count_--;

    DEBUG_ASSERT(page_is_free(page));

    page->state = VM_PAGE_STATE_ALLOC;
#if PMM_ENABLE_FREE_FILL
    CheckFreeFill(page);
#endif

    if (pa) {
        /* compute the physical address of the page based on its offset into the arena */
        *pa = page_address_from_arena(page);
        LTRACEF("pa %#" PRIxPTR ", page %p\n", *pa, page);
    }

    LTRACEF("allocating page %p, pa %#" PRIxPTR "\n", page, page_address_from_arena(page));

    return page;
}

vm_page_t* PmmArena::AllocSpecific(paddr_t pa) {
    if (!address_in_arena(pa))
        return nullptr;

    size_t index = (pa - base()) / PAGE_SIZE;

    DEBUG_ASSERT(index < size() / PAGE_SIZE);

    vm_page_t* page = get_page(index);
    if (!page_is_free(page)) {
        /* we hit an allocated page */
        return nullptr;
    }

    list_delete(&page->free.node);

    page->state = VM_PAGE_STATE_ALLOC;

    DEBUG_ASSERT(free_count_ > 0);

    free_count_--;

    return page;
}

size_t PmmArena::AllocPages(size_t count, list_node* list) {
    size_t allocated = 0;

    while (allocated < count) {
        vm_page_t* page = list_remove_head_type(&free_list_, vm_page_t, free.node);
        if (!page)
            return allocated;

        LTRACEF("allocating page %p, pa %#" PRIxPTR "\n", page, page_address_from_arena(page));

        DEBUG_ASSERT(free_count_ > 0);

        free_count_--;

        DEBUG_ASSERT(page_is_free(page));
#if PMM_ENABLE_FREE_FILL
        CheckFreeFill(page);
#endif

        page->state = VM_PAGE_STATE_ALLOC;
        list_add_tail(list, &page->free.node);

        allocated++;
    }

    return allocated;
}

size_t PmmArena::AllocContiguous(size_t count, uint8_t alignment_log2, paddr_t* pa, struct list_node* list) {
    /* walk the list starting at alignment boundaries.
     * calculate the starting offset into this arena, based on the
     * base address of the arena to handle the case where the arena
     * is not aligned on the same boundary requested.
     */
    paddr_t rounded_base = ROUNDUP(base(), 1UL << alignment_log2);
    if (rounded_base < base() || rounded_base > base() + size() - 1)
        return 0;

    paddr_t aligned_offset = (rounded_base - base()) / PAGE_SIZE;
    paddr_t start = aligned_offset;
    LTRACEF("starting search at aligned offset %#" PRIxPTR "\n", start);
    LTRACEF("arena base %#" PRIxPTR " size %zu\n", base(), size());

retry:
    /* search while we're still within the arena and have a chance of finding a slot
       (start + count < end of arena) */
    while ((start < size() / PAGE_SIZE) && ((start + count) <= size() / PAGE_SIZE)) {
        vm_page_t* p = &page_array_[start];
        for (uint i = 0; i < count; i++) {
            if (!page_is_free(p)) {
                /* this run is broken, break out of the inner loop.
                 * start over at the next alignment boundary
                 */
                start = ROUNDUP(start - aligned_offset + i + 1, 1UL << (alignment_log2 - PAGE_SIZE_SHIFT)) +
                        aligned_offset;
                goto retry;
            }
            p++;
        }

        /* we found a run */
        LTRACEF("found run from pn %" PRIuPTR " to %" PRIuPTR "\n", start, start + count);

        /* remove the pages from the run out of the free list */
        for (paddr_t i = start; i < start + count; i++) {
            p = &page_array_[i];
            DEBUG_ASSERT(page_is_free(p));
            DEBUG_ASSERT(list_in_list(&p->free.node));

            list_delete(&p->free.node);
            p->state = VM_PAGE_STATE_ALLOC;

            DEBUG_ASSERT(free_count_ > 0);

            free_count_--;

#if PMM_ENABLE_FREE_FILL
            CheckFreeFill(p);
#endif

            if (list)
                list_add_tail(list, &p->free.node);
        }

        if (pa)
            *pa = base() + start * PAGE_SIZE;

        return count;
    }

    return 0;
}

zx_status_t PmmArena::FreePage(vm_page_t* page) {
    LTRACEF("page %p\n", page);
    if (!page_belongs_to_arena(page))
        return ZX_ERR_NOT_FOUND;

    DEBUG_ASSERT(page->state != VM_PAGE_STATE_OBJECT || page->object.pin_count == 0);

#if PMM_ENABLE_FREE_FILL
    FreeFill(page);
#endif

    page->state = VM_PAGE_STATE_FREE;

    list_add_head(&free_list_, &page->free.node);
    free_count_++;
    return ZX_OK;
}

void PmmArena::CountStates(size_t state_count[_VM_PAGE_STATE_COUNT]) const {
    for (size_t i = 0; i < size() / PAGE_SIZE; i++) {
        state_count[page_array_[i].state]++;
    }
}

void PmmArena::Dump(bool dump_pages, bool dump_free_ranges) {
    char pbuf[16];
    printf("arena %p: name '%s' base %#" PRIxPTR " size %s (0x%zx) priority %u flags 0x%x\n", this, name(), base(),
           format_size(pbuf, sizeof(pbuf), size()), size(), priority(), flags());
    printf("\tpage_array %p, free_count %zu\n", page_array_, free_count_);

    /* dump all of the pages */
    if (dump_pages) {
        for (size_t i = 0; i < size() / PAGE_SIZE; i++) {
            dump_page(&page_array_[i]);
        }
    }

    /* count the number of pages in every state */
    size_t state_count[_VM_PAGE_STATE_COUNT] = {};
    CountStates(state_count);

    printf("\tpage states:\n");
    for (unsigned int i = 0; i < _VM_PAGE_STATE_COUNT; i++) {
        printf("\t\t%-12s %-16zu (%zu bytes)\n", page_state_to_string(i), state_count[i],
               state_count[i] * PAGE_SIZE);
    }

    /* dump the free pages */
    if (dump_free_ranges) {
        printf("\tfree ranges:\n");
        ssize_t last = -1;
        for (size_t i = 0; i < size() / PAGE_SIZE; i++) {
            if (page_is_free(&page_array_[i])) {
                if (last == -1) {
                    last = i;
                }
            } else {
                if (last != -1) {
                    printf("\t\t%#" PRIxPTR " - %#" PRIxPTR "\n", base() + last * PAGE_SIZE,
                           base() + i * PAGE_SIZE);
                }
                last = -1;
            }
        }

        if (last != -1) {
            printf("\t\t%#" PRIxPTR " - %#" PRIxPTR "\n", base() + last * PAGE_SIZE, base() + size());
        }
    }
}
