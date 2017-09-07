// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <vm/pmm.h>

#include <assert.h>
#include <err.h>
#include <inttypes.h>
#include <kernel/mp.h>
#include <kernel/timer.h>
#include <kernel/vm.h>
#include <lib/console.h>
#include <list.h>
#include <lk/init.h>
#include <platform.h>
#include <pow2.h>
#include <stdlib.h>
#include <string.h>
#include <trace.h>

#include "pmm_arena.h"
#include "vm_priv.h"

#include <magenta/thread_annotations.h>
#include <mxcpp/new.h>
#include <fbl/auto_lock.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/mutex.h>

using fbl::AutoLock;

#define LOCAL_TRACE MAX(VM_GLOBAL_TRACE, 0)

// the main arena list
static fbl::Mutex arena_lock;
static fbl::DoublyLinkedList<PmmArena*> arena_list TA_GUARDED(arena_lock);
static size_t arena_cumulative_size TA_GUARDED(arena_lock);

#if PMM_ENABLE_FREE_FILL
static void pmm_enforce_fill(uint level) {
    for (auto& a : arena_list) {
        a.EnforceFill();
    }
}
LK_INIT_HOOK(pmm_fill, &pmm_enforce_fill, LK_INIT_LEVEL_VM);
#endif

// We don't need to hold the arena lock while executing this, since it is
// only accesses values that are set once during system initialization.
paddr_t vm_page_to_paddr(const vm_page_t* page) TA_NO_THREAD_SAFETY_ANALYSIS {
    for (const auto& a : arena_list) {
        // LTRACEF("testing page %p against arena %p\n", page, &a);
        if (a.page_belongs_to_arena(page)) {
            return a.page_address_from_arena(page);
        }
    }
    return -1;
}

// We don't need to hold the arena lock while executing this, since it is
// only accesses values that are set once during system initialization.
vm_page_t* paddr_to_vm_page(paddr_t addr) TA_NO_THREAD_SAFETY_ANALYSIS {
    for (auto& a : arena_list) {
        if (a.address_in_arena(addr)) {
            size_t index = (addr - a.base()) / PAGE_SIZE;
            return a.get_page(index);
        }
    }
    return nullptr;
}

// We disable thread safety analysis here, since this function is only called
// during early boot before threading exists.
status_t pmm_add_arena(const pmm_arena_info_t* info) TA_NO_THREAD_SAFETY_ANALYSIS {
    LTRACEF("arena %p name '%s' base %#" PRIxPTR " size %#zx\n", info, info->name, info->base, info->size);

    // Make sure we're in early boot (ints disabled and no active CPUs according
    // to the scheduler).
    DEBUG_ASSERT(mp_get_active_mask() == 0);
    DEBUG_ASSERT(arch_ints_disabled());

    DEBUG_ASSERT(IS_PAGE_ALIGNED(info->base));
    DEBUG_ASSERT(IS_PAGE_ALIGNED(info->size));
    DEBUG_ASSERT(info->size > 0);

    // allocate a c++ arena object
    PmmArena* arena = new (boot_alloc_mem(sizeof(PmmArena))) PmmArena(info);

    // walk the arena list and add arena based on priority order
    for (auto& a : arena_list) {
        if (a.priority() > arena->priority()) {
            arena_list.insert(a, arena);
            goto done_add;
        }
    }

    // walked off the end, add it to the end of the list
    arena_list.push_back(arena);

done_add:
    // tell the arena to allocate a page array
    arena->BootAllocArray();

    arena_cumulative_size += info->size;

    return MX_OK;
}

vm_page_t* pmm_alloc_page(uint alloc_flags, paddr_t* pa) {
    AutoLock al(&arena_lock);

    /* walk the arenas in order until we find one with a free page */
    for (auto& a : arena_list) {
        /* skip the arena if it's not KMAP and the KMAP only allocation flag was passed */
        if (alloc_flags & PMM_ALLOC_FLAG_KMAP) {
            if ((a.flags() & PMM_ARENA_FLAG_KMAP) == 0)
                continue;
        }

        // try to allocate the page out of the arena
        vm_page_t* page = a.AllocPage(pa);
        if (page)
            return page;
    }

    LTRACEF("failed to allocate page\n");
    return nullptr;
}

size_t pmm_alloc_pages(size_t count, uint alloc_flags, struct list_node* list) {
    LTRACEF("count %zu\n", count);

    /* list must be initialized prior to calling this */
    DEBUG_ASSERT(list);

    if (count == 0)
        return 0;

    AutoLock al(&arena_lock);

    /* walk the arenas in order, allocating as many pages as we can from each */
    size_t allocated = 0;
    for (auto& a : arena_list) {
        DEBUG_ASSERT(count > allocated);

        /* skip the arena if it's not KMAP and the KMAP only allocation flag was passed */
        if (alloc_flags & PMM_ALLOC_FLAG_KMAP) {
            if ((a.flags() & PMM_ARENA_FLAG_KMAP) == 0)
                continue;
        }

        // ask the arena to allocate some pages
        allocated += a.AllocPages(count - allocated, list);
        DEBUG_ASSERT(allocated <= count);
        if (allocated == count)
            break;
    }

    return allocated;
}

size_t pmm_alloc_range(paddr_t address, size_t count, struct list_node* list) {
    LTRACEF("address %#" PRIxPTR ", count %zu\n", address, count);

    uint allocated = 0;
    if (count == 0)
        return 0;

    address = ROUNDDOWN(address, PAGE_SIZE);

    AutoLock al(&arena_lock);

    /* walk through the arenas, looking to see if the physical page belongs to it */
    for (auto& a : arena_list) {
        while (allocated < count && a.address_in_arena(address)) {
            vm_page_t* page = a.AllocSpecific(address);
            if (!page)
                break;

            if (list)
                list_add_tail(list, &page->free.node);

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

    AutoLock al(&arena_lock);

    for (auto& a : arena_list) {
        /* skip the arena if it's not KMAP and the KMAP only allocation flag was passed */
        if (alloc_flags & PMM_ALLOC_FLAG_KMAP) {
            if ((a.flags() & PMM_ARENA_FLAG_KMAP) == 0)
                continue;
        }

        size_t allocated = a.AllocContiguous(count, alignment_log2, pa, list);
        if (allocated > 0) {
            DEBUG_ASSERT(allocated == count);
            return allocated;
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
            list_add_tail(list, &p->free.node);
        }
    } else {
        size_t alloc_count = pmm_alloc_contiguous(count, PMM_ALLOC_FLAG_KMAP, PAGE_SIZE_SHIFT, &pa, list);
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
void* pmm_alloc_kpage(paddr_t* _pa, vm_page_t** _p) {
    LTRACE_ENTRY;

    paddr_t pa;
    vm_page_t* p = pmm_alloc_page(PMM_ALLOC_FLAG_KMAP, &pa);
    if (!p)
        return nullptr;

    void* ptr = paddr_to_kvaddr(pa);
    DEBUG_ASSERT(ptr);

    if (_pa)
        *_pa = pa;
    if (_p)
        *_p = p;
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
            list_add_tail(&list, &p->free.node);
        }

        ptr += PAGE_SIZE;
        count--;
    }

    return pmm_free(&list);
}

size_t pmm_free(struct list_node* list) {
    LTRACEF("list %p\n", list);

    DEBUG_ASSERT(list);

    AutoLock al(&arena_lock);

    uint count = 0;
    while (!list_is_empty(list)) {
        vm_page_t* page = list_remove_head_type(list, vm_page_t, free.node);

        DEBUG_ASSERT_MSG(!page_is_free(page), "page %p state %u\n", page, page->state);

        /* see which arena this page belongs to and add it */
        for (auto& a : arena_list) {
            if (a.FreePage(page) >= 0) {
                count++;
                break;
            }
        }
    }

    LTRACEF("returning count %u\n", count);

    return count;
}

size_t pmm_free_page(vm_page_t* page) {
    struct list_node list;
    list_initialize(&list);

    list_add_head(&list, &page->free.node);

    return pmm_free(&list);
}

static size_t pmm_count_free_pages_locked() TA_REQ(arena_lock) {
    size_t free = 0u;
    for (const auto& a : arena_list) {
        free += a.free_count();
    }
    return free;
}

size_t pmm_count_free_pages() {
    AutoLock al(&arena_lock);
    return pmm_count_free_pages_locked();
}

static void pmm_dump_free() TA_REQ(arena_lock) {
    auto megabytes_free = pmm_count_free_pages_locked() / 256u;
    printf(" %zu free MBs\n", megabytes_free);
}

static size_t pmm_count_total_bytes_locked() TA_REQ(arena_lock) {
    return arena_cumulative_size;
}

size_t pmm_count_total_bytes() {
    AutoLock al(&arena_lock);
    return pmm_count_total_bytes_locked();
}

void pmm_count_total_states(size_t state_count[_VM_PAGE_STATE_COUNT]) {
    // TODO(MG-833): This is extremely expensive, holding a global lock
    // and touching every page/arena. We should keep a running count instead.
    AutoLock al(&arena_lock);
    for (auto& a : arena_list) {
        a.CountStates(state_count);
    }
}

extern "C" enum handler_return pmm_dump_timer(struct timer* t, lk_time_t now, void*) TA_REQ(arena_lock) {
    timer_set(t, now + LK_SEC(1), TIMER_SLACK_CENTER, LK_MSEC(20), &pmm_dump_timer, nullptr);
    pmm_dump_free();
    return INT_NO_RESCHEDULE;
}

// No lock analysis here, as we want to just go for it in the panic case without the lock.
static void arena_dump(bool is_panic) TA_NO_THREAD_SAFETY_ANALYSIS {
    if (!is_panic) {
        arena_lock.Acquire();
    }
    for (auto& a : arena_list) {
        a.Dump(false, false);
    }
    if (!is_panic) {
        arena_lock.Release();
    }
}

static int cmd_pmm(int argc, const cmd_args* argv, uint32_t flags) {
    bool is_panic = flags & CMD_FLAG_PANIC;

    if (argc < 2) {
    notenoughargs:
        printf("not enough arguments\n");
    usage:
        printf("usage:\n");
        printf("%s arenas\n", argv[0].str);
        if (!is_panic) {
            printf("%s alloc <count>\n", argv[0].str);
            printf("%s alloc_range <address> <count>\n", argv[0].str);
            printf("%s alloc_kpages <count>\n", argv[0].str);
            printf("%s alloc_contig <count> <alignment>\n", argv[0].str);
            printf("%s dump_alloced\n", argv[0].str);
            printf("%s free_alloced\n", argv[0].str);
            printf("%s free\n", argv[0].str);
        }
        return MX_ERR_INTERNAL;
    }

    static struct list_node allocated = LIST_INITIAL_VALUE(allocated);

    if (!strcmp(argv[1].str, "arenas")) {
        arena_dump(is_panic);
    } else if (is_panic) {
        // No other operations will work during a panic.
        printf("Only the \"arenas\" command is available during a panic.\n");
        goto usage;
    } else if (!strcmp(argv[1].str, "free")) {
        static bool show_mem = false;
        static timer_t timer;

        if (!show_mem) {
            printf("pmm free: issue the same command to stop.\n");
            timer_init(&timer);
            timer_set(&timer, current_time() + LK_SEC(1), TIMER_SLACK_CENTER, LK_MSEC(20),
                      &pmm_dump_timer, nullptr);
            show_mem = true;
        } else {
            timer_cancel(&timer);
            show_mem = false;
        }
    } else if (!strcmp(argv[1].str, "alloc")) {
        if (argc < 3)
            goto notenoughargs;

        struct list_node list;
        list_initialize(&list);

        size_t count = pmm_alloc_pages((uint)argv[2].u, 0, &list);
        printf("alloc returns %zu\n", count);

        vm_page_t* p;
        list_for_every_entry (&list, p, vm_page_t, free.node) {
            paddr_t paddr;
            {
                DEBUG_ASSERT(!is_panic);
                AutoLock al(&arena_lock);
                paddr = vm_page_to_paddr(p);
            };
            printf("\tpage %p, address %#" PRIxPTR "\n", p, paddr);
        }

        /* add the pages to the local allocated list */
        struct list_node* node;
        while ((node = list_remove_head(&list))) {
            list_add_tail(&allocated, node);
        }
    } else if (!strcmp(argv[1].str, "dump_alloced")) {
        vm_page_t* page;

        list_for_every_entry (&allocated, page, vm_page_t, free.node) { dump_page(page); }
    } else if (!strcmp(argv[1].str, "alloc_range")) {
        if (argc < 4)
            goto notenoughargs;

        struct list_node list;
        list_initialize(&list);

        size_t count = pmm_alloc_range(argv[2].u, (uint)argv[3].u, &list);
        printf("alloc returns %zu\n", count);

        vm_page_t* p;
        list_for_every_entry (&list, p, vm_page_t, free.node) {
            paddr_t paddr;
            {
                DEBUG_ASSERT(!is_panic);
                AutoLock al(&arena_lock);
                paddr = vm_page_to_paddr(p);
            }
            printf("\tpage %p, address %#" PRIxPTR "\n", p, paddr);
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
        void* ptr = pmm_alloc_kpages((uint)argv[2].u, nullptr, &pa);
        printf("pmm_alloc_kpages returns %p pa %#" PRIxPTR "\n", ptr, pa);
    } else if (!strcmp(argv[1].str, "alloc_contig")) {
        if (argc < 4)
            goto notenoughargs;

        struct list_node list;
        list_initialize(&list);

        paddr_t pa;
        size_t ret = pmm_alloc_contiguous((uint)argv[2].u, 0, (uint8_t)argv[3].u, &pa, &list);
        printf("pmm_alloc_contiguous returns %zu, address %#" PRIxPTR "\n", ret, pa);
        printf("address %% align = %#" PRIxPTR "\n", static_cast<uintptr_t>(pa % argv[3].u));

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

    return MX_OK;
}

STATIC_COMMAND_START
#if LK_DEBUGLEVEL > 0
STATIC_COMMAND_MASKED("pmm", "physical memory manager", &cmd_pmm, CMD_AVAIL_ALWAYS)
#endif
STATIC_COMMAND_END(pmm);
