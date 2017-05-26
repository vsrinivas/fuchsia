// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008-2015 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/heap.h>

#include <trace.h>
#include <debug.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <err.h>
#include <list.h>
#include <arch/ops.h>
#include <kernel/spinlock.h>
#include <kernel/vm.h>
#include <lib/cmpctmalloc.h>
#include <lib/console.h>

#define LOCAL_TRACE 0

#ifndef HEAP_PANIC_ON_ALLOC_FAIL
#if LK_DEBUGLEVEL > 2
#define HEAP_PANIC_ON_ALLOC_FAIL 1
#else
#define HEAP_PANIC_ON_ALLOC_FAIL 0
#endif
#endif

/* heap tracing */
#if LK_DEBUGLEVEL > 1
static bool heap_trace = false;
#else
#define heap_trace (false)
#endif

void heap_init(void)
{
    cmpct_init();
}

void heap_trim(void)
{
    cmpct_trim();
}

void *malloc(size_t size)
{
    DEBUG_ASSERT(!arch_in_int_handler());

    LTRACEF("size %zu\n", size);

    void *ptr = cmpct_alloc(size);
    if (unlikely(heap_trace))
        printf("caller %p malloc %zu -> %p\n", __GET_CALLER(), size, ptr);

    if (HEAP_PANIC_ON_ALLOC_FAIL && unlikely(!ptr)) {
        panic("malloc of size %zu failed\n", size);
    }

    return ptr;
}

void *memalign(size_t boundary, size_t size)
{
    DEBUG_ASSERT(!arch_in_int_handler());

    LTRACEF("boundary %zu, size %zu\n", boundary, size);

    void *ptr = cmpct_memalign(size, boundary);
    if (unlikely(heap_trace))
        printf("caller %p memalign %zu, %zu -> %p\n", __GET_CALLER(), boundary, size, ptr);

    if (HEAP_PANIC_ON_ALLOC_FAIL && unlikely(!ptr)) {
        panic("memalign of size %zu align %zu failed\n", size, boundary);
    }

    return ptr;
}

void *calloc(size_t count, size_t size)
{
    DEBUG_ASSERT(!arch_in_int_handler());

    LTRACEF("count %zu, size %zu\n", count, size);

    size_t realsize = count * size;

    void *ptr = cmpct_alloc(realsize);
    if (likely(ptr))
        memset(ptr, 0, realsize);
    if (unlikely(heap_trace))
        printf("caller %p calloc %zu, %zu -> %p\n", __GET_CALLER(), count, size, ptr);
    return ptr;
}

void *realloc(void *ptr, size_t size)
{
    DEBUG_ASSERT(!arch_in_int_handler());

    LTRACEF("ptr %p, size %zu\n", ptr, size);

    void *ptr2 = cmpct_realloc(ptr, size);
    if (unlikely(heap_trace))
        printf("caller %p realloc %p, %zu -> %p\n", __GET_CALLER(), ptr, size, ptr2);

    if (HEAP_PANIC_ON_ALLOC_FAIL && unlikely(!ptr2)) {
        panic("realloc of size %zu old ptr %p failed\n", size, ptr);
    }

    return ptr2;
}

void free(void *ptr)
{
    DEBUG_ASSERT(!arch_in_int_handler());

    LTRACEF("ptr %p\n", ptr);
    if (unlikely(heap_trace))
        printf("caller %p free %p\n", __GET_CALLER(), ptr);

    cmpct_free(ptr);
}

static void heap_dump(bool panic_time)
{
    cmpct_dump(panic_time);
}

static void heap_test(void)
{
    cmpct_test();
}

void *heap_page_alloc(size_t pages)
{
    DEBUG_ASSERT(pages > 0);

    struct list_node list = LIST_INITIAL_VALUE(list);

    void *result = pmm_alloc_kpages(pages, &list, NULL);

    if (likely(result)) {
        // mark all of the allocated page as HEAP
        vm_page_t *p;
        list_for_every_entry(&list, p, vm_page_t, free.node) {
            p->state = VM_PAGE_STATE_HEAP;
        }
    }

    return result;
}

void heap_page_free(void *ptr, size_t pages)
{
    DEBUG_ASSERT(IS_PAGE_ALIGNED((uintptr_t)ptr));
    DEBUG_ASSERT(pages > 0);

    pmm_free_kpages(ptr, pages);
}

#if LK_DEBUGLEVEL > 1
#if WITH_LIB_CONSOLE

#include <lib/console.h>

static int cmd_heap(int argc, const cmd_args *argv, uint32_t flags);

STATIC_COMMAND_START
STATIC_COMMAND_MASKED("heap", "heap debug commands", &cmd_heap, CMD_AVAIL_ALWAYS)
STATIC_COMMAND_END(heap);

static int cmd_heap(int argc, const cmd_args *argv, uint32_t flags)
{
    if (argc < 2) {
notenoughargs:
        printf("not enough arguments\n");
usage:
        printf("usage:\n");
        printf("\t%s info\n", argv[0].str);
        if (!(flags & CMD_FLAG_PANIC)) {
            printf("\t%s trace\n", argv[0].str);
            printf("\t%s trim\n", argv[0].str);
            printf("\t%s alloc <size> [alignment]\n", argv[0].str);
            printf("\t%s realloc <ptr> <size>\n", argv[0].str);
            printf("\t%s free <address>\n", argv[0].str);
        }
        return -1;
    }

    if (strcmp(argv[1].str, "info") == 0) {
        heap_dump(flags & CMD_FLAG_PANIC);
    } else if (!(flags & CMD_FLAG_PANIC) && strcmp(argv[1].str, "test") == 0) {
        heap_test();
    } else if (!(flags & CMD_FLAG_PANIC) && strcmp(argv[1].str, "trace") == 0) {
        heap_trace = !heap_trace;
        printf("heap trace is now %s\n", heap_trace ? "on" : "off");
    } else if (!(flags & CMD_FLAG_PANIC) && strcmp(argv[1].str, "trim") == 0) {
        heap_trim();
    } else if (!(flags & CMD_FLAG_PANIC) && strcmp(argv[1].str, "alloc") == 0) {
        if (argc < 3) goto notenoughargs;

        void *ptr = memalign((argc >= 4) ? argv[3].u : 0, argv[2].u);
        printf("memalign returns %p\n", ptr);
    } else if (!(flags & CMD_FLAG_PANIC) && strcmp(argv[1].str, "realloc") == 0) {
        if (argc < 4) goto notenoughargs;

        void *ptr = realloc(argv[2].p, argv[3].u);
        printf("realloc returns %p\n", ptr);
    } else if (!(flags & CMD_FLAG_PANIC) && strcmp(argv[1].str, "free") == 0) {
        if (argc < 2) goto notenoughargs;

        free(argv[2].p);
    } else {
        printf("unrecognized command\n");
        goto usage;
    }

    return 0;
}

#endif
#endif


