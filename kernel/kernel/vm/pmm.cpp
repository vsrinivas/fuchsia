// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "vm_priv.h"
#include <assert.h>
#include <err.h>
#include <inttypes.h>
#include <kernel/auto_lock.h>
#include <kernel/mutex.h>
#include <kernel/vm.h>
#include <lib/console.h>
#include <list.h>
#include <pow2.h>
#include <stdlib.h>
#include <string.h>
#include <trace.h>

#define LOCAL_TRACE MAX(VM_GLOBAL_TRACE, 0)

static struct list_node arena_list = LIST_INITIAL_VALUE(arena_list);
static mutex_t lock = MUTEX_INITIAL_VALUE(lock);

#define PAGE_BELONGS_TO_ARENA(page, arena)                    \
    (((uintptr_t)(page) >= (uintptr_t)(arena)->page_array) && \
     ((uintptr_t)(page) <                                     \
      ((uintptr_t)(arena)->page_array + (arena)->size / PAGE_SIZE * sizeof(vm_page_t))))

#define PAGE_ADDRESS_FROM_ARENA(page, arena)                                                           \
    ((paddr_t)(((uintptr_t)(page) - (uintptr_t)(arena)->page_array) / sizeof(vm_page_t)) * PAGE_SIZE + \
     (arena)->base)

#define ADDRESS_IN_ARENA(address, arena) \
    ((address) >= (arena)->base && (address) <= (arena)->base + (arena)->size - 1)

static inline bool page_is_free(const vm_page_t* page) {
    return page->state == VM_PAGE_STATE_FREE;
}

paddr_t vm_page_to_paddr(const vm_page_t* page) {
    pmm_arena_t* a;
    list_for_every_entry (&arena_list, a, pmm_arena_t, node) {
        if (PAGE_BELONGS_TO_ARENA(page, a)) {
            return PAGE_ADDRESS_FROM_ARENA(page, a);
        }
    }
    return -1;
}

vm_page_t* paddr_to_vm_page(paddr_t addr) {
    pmm_arena_t* a;
    list_for_every_entry (&arena_list, a, pmm_arena_t, node) {
        if (addr >= a->base && addr <= a->base + a->size - 1) {
            size_t index = (addr - a->base) / PAGE_SIZE;
            return &a->page_array[index];
        }
    }
    return NULL;
}

status_t pmm_add_arena(pmm_arena_t* arena) {
    LTRACEF("arena %p name '%s' base %#" PRIxPTR " size %#zx\n",
            arena, arena->name, arena->base, arena->size);

    DEBUG_ASSERT(IS_PAGE_ALIGNED(arena->base));
    DEBUG_ASSERT(IS_PAGE_ALIGNED(arena->size));
    DEBUG_ASSERT(arena->size > 0);

    /* walk the arena list and add arena based on priority order */
    pmm_arena_t* a;
    list_for_every_entry (&arena_list, a, pmm_arena_t, node) {
        if (a->priority > arena->priority) {
            list_add_before(&a->node, &arena->node);
            goto done_add;
        }
    }

    /* walked off the end, add it to the end of the list */
    list_add_tail(&arena_list, &arena->node);

done_add:

    /* zero out some of the structure */
    arena->free_count = 0;
    list_initialize(&arena->free_list);

    /* allocate an array of pages to back this one */
    size_t page_count = arena->size / PAGE_SIZE;
    arena->page_array = (vm_page_t*)boot_alloc_mem(page_count * sizeof(vm_page_t));

    /* initialize all of the pages */
    memset(arena->page_array, 0, page_count * sizeof(vm_page_t));

    /* add them to the free list */
    for (size_t i = 0; i < page_count; i++) {
        vm_page_t* p = &arena->page_array[i];

        list_add_tail(&arena->free_list, &p->node);

        arena->free_count++;
    }

    return NO_ERROR;
}

vm_page_t* pmm_alloc_page(uint alloc_flags, paddr_t* pa) {
    AutoLock al(lock);

    /* walk the arenas in order until we find one with a free page */
    pmm_arena_t* a;
    list_for_every_entry (&arena_list, a, pmm_arena_t, node) {
        /* skip the arena if it's not KMAP and the KMAP only allocation flag was passed */
        if (alloc_flags & PMM_ALLOC_FLAG_KMAP) {
            if ((a->flags & PMM_ARENA_FLAG_KMAP) == 0)
                continue;
        }
        vm_page_t* page = list_remove_head_type(&a->free_list, vm_page_t, node);
        if (!page)
            continue;

        a->free_count--;

        DEBUG_ASSERT(page_is_free(page));

        page->state = VM_PAGE_STATE_ALLOC;

        if (pa) {
            /* compute the physical address of the page based on its offset into the arena */
            *pa = PAGE_ADDRESS_FROM_ARENA(page, a);
        }

        LTRACEF("allocating page %p, pa %#" PRIxPTR "\n", page, PAGE_ADDRESS_FROM_ARENA(page, a));

        return page;
    }

    LTRACEF("failed to allocate page\n");
    return nullptr;
}

size_t pmm_alloc_pages(size_t count, uint alloc_flags, struct list_node* list) {
    LTRACEF("count %zu\n", count);

    /* list must be initialized prior to calling this */
    DEBUG_ASSERT(list);

    uint allocated = 0;
    if (count == 0)
        return 0;

    AutoLock al(lock);

    /* walk the arenas in order, allocating as many pages as we can from each */
    pmm_arena_t* a;
    list_for_every_entry (&arena_list, a, pmm_arena_t, node) {
        /* skip the arena if it's not KMAP and the KMAP only allocation flag was passed */
        if (alloc_flags & PMM_ALLOC_FLAG_KMAP) {
            if ((a->flags & PMM_ARENA_FLAG_KMAP) == 0)
                continue;
        }
        while (allocated < count) {
            /* Go to the next arena if this one's free list is exhausted. */
            if (!a->free_count)
                break;

            vm_page_t* page = list_remove_head_type(&a->free_list, vm_page_t, node);
            if (!page)
                return allocated;

            a->free_count--;

            DEBUG_ASSERT(page_is_free(page));

            page->state = VM_PAGE_STATE_ALLOC;
            list_add_tail(list, &page->node);

            allocated++;
        }
    }

    return allocated;
}

size_t pmm_alloc_range(paddr_t address, size_t count, struct list_node* list) {
    LTRACEF("address %#" PRIxPTR ", count %zu\n", address, count);

    uint allocated = 0;
    if (count == 0)
        return 0;

    address = ROUNDDOWN(address, PAGE_SIZE);

    AutoLock al(lock);

    /* walk through the arenas, looking to see if the physical page belongs to it */
    pmm_arena_t* a;
    list_for_every_entry (&arena_list, a, pmm_arena_t, node) {
        while (allocated < count && ADDRESS_IN_ARENA(address, a)) {
            size_t index = (address - a->base) / PAGE_SIZE;

            DEBUG_ASSERT(index < a->size / PAGE_SIZE);

            vm_page_t* page = &a->page_array[index];
            if (!page_is_free(page)) {
                /* we hit an allocated page */
                break;
            }

            DEBUG_ASSERT(list_in_list(&page->node));

            list_delete(&page->node);
            page->state = VM_PAGE_STATE_ALLOC;

            if (list)
                list_add_tail(list, &page->node);

            a->free_count--;
            allocated++;
            address += PAGE_SIZE;
        }

        if (allocated == count)
            break;
    }

    return allocated;
}

size_t pmm_alloc_contiguous(size_t count, uint alloc_flags, uint8_t alignment_log2, paddr_t* pa,
                            struct list_node* list) {
    LTRACEF("count %zu, align %u\n", count, alignment_log2);

    if (count == 0)
        return 0;
    if (alignment_log2 < PAGE_SIZE_SHIFT)
        alignment_log2 = PAGE_SIZE_SHIFT;

    AutoLock al(lock);

    pmm_arena_t* a;
    list_for_every_entry (&arena_list, a, pmm_arena_t, node) {
        /* skip the arena if it's not KMAP and the KMAP only allocation flag was passed */
        if (alloc_flags & PMM_ALLOC_FLAG_KMAP) {
            if ((a->flags & PMM_ARENA_FLAG_KMAP) == 0)
                continue;
        }
        /* walk the list starting at alignment boundaries.
         * calculate the starting offset into this arena, based on the
         * base address of the arena to handle the case where the arena
         * is not aligned on the same boundary requested.
         */
        paddr_t rounded_base = ROUNDUP(a->base, 1UL << alignment_log2);
        if (rounded_base < a->base || rounded_base > a->base + a->size - 1)
            continue;

        paddr_t aligned_offset = (rounded_base - a->base) / PAGE_SIZE;
        paddr_t start = aligned_offset;
        LTRACEF("starting search at aligned offset %" PRIuPTR "\n", start);
        LTRACEF("arena base %#" PRIxPTR " size %zu\n", a->base, a->size);

    retry:
        /* search while we're still within the arena and have a chance of finding a slot
           (start + count < end of arena) */
        while ((start < a->size / PAGE_SIZE) && ((start + count) <= a->size / PAGE_SIZE)) {
            vm_page_t* p = &a->page_array[start];
            for (uint i = 0; i < count; i++) {
                if (!page_is_free(p)) {
                    /* this run is broken, break out of the inner loop.
                     * start over at the next alignment boundary
                     */
                    start = ROUNDUP(start - aligned_offset + i + 1,
                                    1UL << (alignment_log2 - PAGE_SIZE_SHIFT)) +
                            aligned_offset;
                    goto retry;
                }
                p++;
            }

            /* we found a run */
            LTRACEF("found run from pn %" PRIuPTR " to %" PRIuPTR "\n",
                    start, start + count);

            /* remove the pages from the run out of the free list */
            for (paddr_t i = start; i < start + count; i++) {
                p = &a->page_array[i];
                DEBUG_ASSERT(page_is_free(p));
                DEBUG_ASSERT(list_in_list(&p->node));

                list_delete(&p->node);
                p->state = VM_PAGE_STATE_ALLOC;
                a->free_count--;

                if (list)
                    list_add_tail(list, &p->node);
            }

            if (pa)
                *pa = a->base + start * PAGE_SIZE;

            return count;
        }
    }

    LTRACEF("couldn't find run\n");
    return 0;
}

/* physically allocate a run from arenas marked as KMAP */
void* pmm_alloc_kpages(size_t count, struct list_node* list, paddr_t* _pa) {
    LTRACEF("count %zu\n", count);

    paddr_t pa;
    /* fast path for single count allocations */
    if (count == 1) {
        vm_page_t* p = pmm_alloc_page(PMM_ALLOC_FLAG_KMAP, &pa);
        if (!p)
            return nullptr;

        if (list) {
            list_add_tail(list, &p->node);
        }
    } else {
        size_t alloc_count =
            pmm_alloc_contiguous(count, PMM_ALLOC_FLAG_KMAP, PAGE_SIZE_SHIFT, &pa, list);
        if (alloc_count == 0)
            return nullptr;
    }

    LTRACEF("pa %#" PRIxPTR "\n", pa);
    void* ptr = paddr_to_kvaddr(pa);
    DEBUG_ASSERT(ptr);

    if (_pa)
        *_pa = pa;
    return ptr;
}

/* allocate a single page from a KMAP arena and return its virtual address */
void* pmm_alloc_kpage(paddr_t* _pa) {
    LTRACE_ENTRY;

    paddr_t pa;
    vm_page_t* p = pmm_alloc_page(PMM_ALLOC_FLAG_KMAP, &pa);
    if (!p)
        return nullptr;

    void* ptr = paddr_to_kvaddr(pa);
    DEBUG_ASSERT(ptr);

    if (_pa)
        *_pa = pa;
    return ptr;
}

size_t pmm_free_kpages(void* _ptr, size_t count) {
    LTRACEF("ptr %p, count %zu\n", _ptr, count);

    uint8_t* ptr = (uint8_t*)_ptr;

    struct list_node list;
    list_initialize(&list);

    while (count > 0) {
        vm_page_t* p = paddr_to_vm_page(vaddr_to_paddr(ptr));
        if (p) {
            list_add_tail(&list, &p->node);
        }

        ptr += PAGE_SIZE;
        count--;
    }

    return pmm_free(&list);
}

size_t pmm_free(struct list_node* list) {
    LTRACEF("list %p\n", list);

    DEBUG_ASSERT(list);

    AutoLock al(lock);

    uint count = 0;
    while (!list_is_empty(list)) {
        vm_page_t* page = list_remove_head_type(list, vm_page_t, node);

        DEBUG_ASSERT(!list_in_list(&page->node));
        DEBUG_ASSERT(!page_is_free(page));

        /* see which arena this page belongs to and add it */
        pmm_arena_t* a;
        list_for_every_entry (&arena_list, a, pmm_arena_t, node) {
            if (PAGE_BELONGS_TO_ARENA(page, a)) {
                page->state = VM_PAGE_STATE_FREE;

                list_add_head(&a->free_list, &page->node);
                a->free_count++;
                count++;
                break;
            }
        }
    }

    return count;
}

size_t pmm_free_page(vm_page_t* page) {
    struct list_node list;
    list_initialize(&list);

    list_add_head(&list, &page->node);

    return pmm_free(&list);
}

static const char* page_state_to_str(const vm_page_t* page) {
    switch (page->state) {
    case VM_PAGE_STATE_FREE:
        return "free";
    case VM_PAGE_STATE_ALLOC:
        return "alloc";
    case VM_PAGE_STATE_MMU:
        return "mmu";
    default:
        return "unknown";
    }
}

static void dump_page(const vm_page_t* page) {
    printf("page %p: address %#" PRIxPTR " state %s flags 0x%x\n", page, vm_page_to_paddr(page),
           page_state_to_str(page), page->flags);
}

static void dump_arena(const pmm_arena_t* arena, bool dump_pages) {
    printf("arena %p: name '%s' base %#" PRIxPTR " size 0x%zx priority %u flags 0x%x\n", arena, arena->name,
           arena->base, arena->size, arena->priority, arena->flags);
    printf("\tpage_array %p, free_count %zu\n", arena->page_array, arena->free_count);

    /* dump all of the pages */
    if (dump_pages) {
        for (size_t i = 0; i < arena->size / PAGE_SIZE; i++) {
            dump_page(&arena->page_array[i]);
        }
    }

    /* dump the free pages */
    printf("\tfree ranges:\n");
    ssize_t last = -1;
    for (size_t i = 0; i < arena->size / PAGE_SIZE; i++) {
        if (page_is_free(&arena->page_array[i])) {
            if (last == -1) {
                last = i;
            }
        } else {
            if (last != -1) {
                printf("\t\t%#" PRIxPTR " - %#" PRIxPTR "\n",
                       arena->base + last * PAGE_SIZE,
                       arena->base + i * PAGE_SIZE);
            }
            last = -1;
        }
    }

    if (last != -1) {
        printf("\t\t%#" PRIxPTR " - %#" PRIxPTR "\n",
               arena->base + last * PAGE_SIZE,
               arena->base + arena->size);
    }
}

static int cmd_pmm(int argc, const cmd_args* argv) {
    if (argc < 2) {
    notenoughargs:
        printf("not enough arguments\n");
    usage:
        printf("usage:\n");
        printf("%s arenas\n", argv[0].str);
        printf("%s alloc <count>\n", argv[0].str);
        printf("%s alloc_range <address> <count>\n", argv[0].str);
        printf("%s alloc_kpages <count>\n", argv[0].str);
        printf("%s alloc_contig <count> <alignment>\n", argv[0].str);
        printf("%s dump_alloced\n", argv[0].str);
        printf("%s free_alloced\n", argv[0].str);
        return ERR_INTERNAL;
    }

    static struct list_node allocated = LIST_INITIAL_VALUE(allocated);

    if (!strcmp(argv[1].str, "arenas")) {
        pmm_arena_t* a;
        list_for_every_entry (&arena_list, a, pmm_arena_t, node) { dump_arena(a, false); }
    } else if (!strcmp(argv[1].str, "alloc")) {
        if (argc < 3)
            goto notenoughargs;

        struct list_node list;
        list_initialize(&list);

        size_t count = pmm_alloc_pages((uint)argv[2].u, 0, &list);
        printf("alloc returns %zu\n", count);

        vm_page_t* p;
        list_for_every_entry (&list, p, vm_page_t, node) {
            printf("\tpage %p, address %#" PRIxPTR "\n", p, vm_page_to_paddr(p));
        }

        /* add the pages to the local allocated list */
        struct list_node* node;
        while ((node = list_remove_head(&list))) {
            list_add_tail(&allocated, node);
        }
    } else if (!strcmp(argv[1].str, "dump_alloced")) {
        vm_page_t* page;

        list_for_every_entry (&allocated, page, vm_page_t, node) { dump_page(page); }
    } else if (!strcmp(argv[1].str, "alloc_range")) {
        if (argc < 4)
            goto notenoughargs;

        struct list_node list;
        list_initialize(&list);

        size_t count = pmm_alloc_range(argv[2].u, (uint)argv[3].u, &list);
        printf("alloc returns %zu\n", count);

        vm_page_t* p;
        list_for_every_entry (&list, p, vm_page_t, node) {
            printf("\tpage %p, address %#" PRIxPTR "\n", p, vm_page_to_paddr(p));
        }

        /* add the pages to the local allocated list */
        struct list_node* node;
        while ((node = list_remove_head(&list))) {
            list_add_tail(&allocated, node);
        }
    } else if (!strcmp(argv[1].str, "alloc_kpages")) {
        if (argc < 3)
            goto notenoughargs;

        paddr_t pa;
        void* ptr = pmm_alloc_kpages((uint)argv[2].u, NULL, &pa);
        printf("pmm_alloc_kpages returns %p pa %#" PRIxPTR "\n", ptr, pa);
    } else if (!strcmp(argv[1].str, "alloc_contig")) {
        if (argc < 4)
            goto notenoughargs;

        struct list_node list;
        list_initialize(&list);

        paddr_t pa;
        size_t ret = pmm_alloc_contiguous((uint)argv[2].u, 0, (uint8_t)argv[3].u, &pa, &list);
        printf("pmm_alloc_contiguous returns %zu, address %#" PRIxPTR "\n",
               ret, pa);
        printf("address %% align = %#" PRIxPTR "\n",
               static_cast<uintptr_t>(pa % argv[3].u));

        /* add the pages to the local allocated list */
        struct list_node* node;
        while ((node = list_remove_head(&list))) {
            list_add_tail(&allocated, node);
        }
    } else if (!strcmp(argv[1].str, "free_alloced")) {
        size_t err = pmm_free(&allocated);
        printf("pmm_free returns %zu\n", err);
    } else {
        printf("unknown command\n");
        goto usage;
    }

    return NO_ERROR;
}

STATIC_COMMAND_START
#if LK_DEBUGLEVEL > 0
STATIC_COMMAND("pmm", "physical memory manager", &cmd_pmm)
#endif
STATIC_COMMAND_END(pmm);
