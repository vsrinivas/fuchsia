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

#define LOCAL_TRACE MAX(VM_GLOBAL_TRACE, 0)

PmmArena::PmmArena(const pmm_arena_info_t* info)
    : info_(*info) {}

PmmArena::~PmmArena() {}

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
    void* kvaddr = paddr_to_kvaddr(paddr);
    memset(kvaddr, PMM_FREE_FILL_BYTE, PAGE_SIZE);
}

void PmmArena::CheckFreeFill(vm_page_t* page) {
    paddr_t paddr = page_address_from_arena(page);
    uint8_t* kvaddr = static_cast<uint8_t*>(paddr_to_kvaddr(paddr));
    for (size_t j = 0; j < PAGE_SIZE; ++j) {
        ASSERT(!enforce_fill_ || *(kvaddr + j) == PMM_FREE_FILL_BYTE);
    }
}
#endif // PMM_ENABLE_FREE_FILL

void PmmArena::BootAllocArray() {
    /* allocate an array of pages to back this one */
    size_t page_count = size() / PAGE_SIZE;
    size_t size = page_count * VM_PAGE_STRUCT_SIZE;
    void* raw_page_array = boot_alloc_mem(size);

    LTRACEF("arena for base 0%#" PRIxPTR " size %#zx page array at %p size %zu\n", info_.base, info_.size,
            raw_page_array, size);

    memset(raw_page_array, 0, size);

    page_array_ = (vm_page_t*)raw_page_array;

    /* add them to the free list */
    for (size_t i = 0; i < page_count; i++) {
        auto& p = page_array_[i];

        list_add_tail(&free_list_, &p.free.node);
    }

    free_count_ += page_count;
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

status_t PmmArena::FreePage(vm_page_t* page) {
    LTRACEF("page %p\n", page);
    if (!page_belongs_to_arena(page))
        return MX_ERR_NOT_FOUND;

    DEBUG_ASSERT(page->state != VM_PAGE_STATE_OBJECT || page->object.pin_count == 0);

#if PMM_ENABLE_FREE_FILL
    FreeFill(page);
#endif

    page->state = VM_PAGE_STATE_FREE;

    list_add_head(&free_list_, &page->free.node);
    free_count_++;
    return MX_OK;
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
