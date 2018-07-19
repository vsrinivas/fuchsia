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
#include <lib/console.h>
#include <lk/init.h>
#include <platform.h>
#include <pow2.h>
#include <stdlib.h>
#include <string.h>
#include <trace.h>
#include <vm/bootalloc.h>
#include <vm/physmap.h>
#include <vm/vm.h>

#include "pmm_arena.h"
#include "pmm_node.h"
#include "vm_priv.h"

#include <fbl/auto_lock.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/mutex.h>
#include <zircon/thread_annotations.h>
#include <zircon/types.h>
#include <zxcpp/new.h>

#define LOCAL_TRACE MAX(VM_GLOBAL_TRACE, 0)

// The (currently) one and only pmm node
static PmmNode pmm_node;

#if PMM_ENABLE_FREE_FILL
static void pmm_enforce_fill(uint level) {
    pmm_node.EnforceFill();
}
LK_INIT_HOOK(pmm_fill, &pmm_enforce_fill, LK_INIT_LEVEL_VM);
#endif

vm_page_t* paddr_to_vm_page(paddr_t addr) {
    return pmm_node.PaddrToPage(addr);
}

zx_status_t pmm_add_arena(const pmm_arena_info_t* info) {
    return pmm_node.AddArena(info);
}

vm_page_t* pmm_alloc_page(uint alloc_flags, paddr_t* pa) {
    return pmm_node.AllocPage(alloc_flags, pa);
}

size_t pmm_alloc_pages(size_t count, uint alloc_flags, list_node* list) {
    return pmm_node.AllocPages(count, alloc_flags, list);
}

size_t pmm_alloc_range(paddr_t address, size_t count, list_node* list) {
    return pmm_node.AllocRange(address, count, list);
}

size_t pmm_alloc_contiguous(size_t count, uint alloc_flags, uint8_t alignment_log2, paddr_t* pa,
                            list_node* list) {
    // if we're called with a single page, just fall through to the regular allocation routine
    if (unlikely(count == 1 && alignment_log2 == PAGE_SIZE_SHIFT)) {
        vm_page_t* page = pmm_node.AllocPage(alloc_flags, pa);
        if (page == nullptr) {
            return 0;
        }
        if (list != nullptr) {
            list_add_tail(list, &page->queue_node);
        }
        return 1;
    }

    return pmm_node.AllocContiguous(count, alloc_flags, alignment_log2, pa, list);
}

size_t pmm_free(list_node* list) {
    return pmm_node.Free(list);
}

size_t pmm_free_page(vm_page* page) {
    pmm_node.Free(page);
    return 1;
}

uint64_t pmm_count_free_pages() {
    return pmm_node.CountFreePages();
}

uint64_t pmm_count_total_bytes() {
    return pmm_node.CountTotalBytes();
}

void pmm_count_total_states(size_t state_count[VM_PAGE_STATE_COUNT_]) {
    pmm_node.CountTotalStates(state_count);
}

static void pmm_dump_timer(struct timer* t, zx_time_t now, void*) {
    timer_set_oneshot(t, now + ZX_SEC(1), &pmm_dump_timer, nullptr);
    pmm_node.DumpFree();
}

static int cmd_pmm(int argc, const cmd_args* argv, uint32_t flags) {
    bool is_panic = flags & CMD_FLAG_PANIC;

    if (argc < 2) {
        printf("not enough arguments\n");
    usage:
        printf("usage:\n");
        printf("%s dump\n", argv[0].str);
        if (!is_panic) {
            printf("%s free\n", argv[0].str);
        }
        return ZX_ERR_INTERNAL;
    }

    if (!strcmp(argv[1].str, "dump")) {
        pmm_node.Dump(is_panic);
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
            timer_set(&timer, current_time() + ZX_SEC(1), TIMER_SLACK_CENTER, ZX_MSEC(20),
                      &pmm_dump_timer, nullptr);
            show_mem = true;
        } else {
            timer_cancel(&timer);
            show_mem = false;
        }
    } else {
        printf("unknown command\n");
        goto usage;
    }

    return ZX_OK;
}

STATIC_COMMAND_START
#if LK_DEBUGLEVEL > 0
STATIC_COMMAND_MASKED("pmm", "physical memory manager", &cmd_pmm, CMD_AVAIL_ALWAYS)
#endif
STATIC_COMMAND_END(pmm);
