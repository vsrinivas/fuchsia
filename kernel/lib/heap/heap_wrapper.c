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
#include <lib/console.h>
#include <lib/page_alloc.h>

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

#if WITH_LIB_HEAP_MINIHEAP
/* miniheap implementation */
#include <lib/miniheap.h>

static inline void *HEAP_MALLOC(size_t s) { return miniheap_alloc(s, 0); }
static inline void *HEAP_REALLOC(void *ptr, size_t s) { return miniheap_realloc(ptr, s); }
static inline void *HEAP_MEMALIGN(size_t boundary, size_t s) { return miniheap_alloc(s, boundary); }
#define HEAP_FREE miniheap_free
static inline void *HEAP_CALLOC(size_t n, size_t s)
{
    size_t realsize = n * s;

    void *ptr = miniheap_alloc(realsize, 0);
    if (likely(ptr))
        memset(ptr, 0, realsize);
    return ptr;
}
static inline void HEAP_INIT(void)
{
    /* start the heap off with some spare memory in the page allocator */
    size_t len;
    void *ptr = page_first_alloc(&len);
    miniheap_init(ptr, len);
}
#define HEAP_DUMP miniheap_dump
#define HEAP_TRIM miniheap_trim

/* end miniheap implementation */
#elif WITH_LIB_HEAP_CMPCTMALLOC
/* cmpctmalloc implementation */
#include <lib/cmpctmalloc.h>

#define HEAP_MEMALIGN(boundary, s) cmpct_memalign(s, boundary)
#define HEAP_MALLOC cmpct_alloc
#define HEAP_REALLOC cmpct_realloc
#define HEAP_FREE cmpct_free
#define HEAP_INIT cmpct_init
#define HEAP_DUMP cmpct_dump
#define HEAP_TRIM cmpct_trim
static inline void *HEAP_CALLOC(size_t n, size_t s)
{
    size_t realsize = n * s;

    void *ptr = cmpct_alloc(realsize);
    if (likely(ptr))
        memset(ptr, 0, realsize);
    return ptr;
}

/* end cmpctmalloc implementation */
#else
#error need to select valid heap implementation or provide wrapper
#endif

void heap_init(void)
{
    HEAP_INIT();
}

void heap_trim(void)
{
    HEAP_TRIM();
}

void *malloc(size_t size)
{
    DEBUG_ASSERT(!arch_in_int_handler());

    LTRACEF("size %zu\n", size);

    void *ptr = HEAP_MALLOC(size);
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

    void *ptr = HEAP_MEMALIGN(boundary, size);
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

    void *ptr = HEAP_CALLOC(count, size);
    if (unlikely(heap_trace))
        printf("caller %p calloc %zu, %zu -> %p\n", __GET_CALLER(), count, size, ptr);
    return ptr;
}

void *realloc(void *ptr, size_t size)
{
    DEBUG_ASSERT(!arch_in_int_handler());

    LTRACEF("ptr %p, size %zu\n", ptr, size);

    void *ptr2 = HEAP_REALLOC(ptr, size);
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

    HEAP_FREE(ptr);
}

static void heap_dump(bool panic_time)
{
    HEAP_DUMP(panic_time);
}

static void heap_test(void)
{
#if WITH_LIB_HEAP_CMPCTMALLOC
    cmpct_test();
#else
    void *ptr[16];

    ptr[0] = HEAP_MALLOC(8);
    ptr[1] = HEAP_MALLOC(32);
    ptr[2] = HEAP_MALLOC(7);
    ptr[3] = HEAP_MALLOC(0);
    ptr[4] = HEAP_MALLOC(98713);
    ptr[5] = HEAP_MALLOC(16);

    HEAP_FREE(ptr[5]);
    HEAP_FREE(ptr[1]);
    HEAP_FREE(ptr[3]);
    HEAP_FREE(ptr[0]);
    HEAP_FREE(ptr[4]);
    HEAP_FREE(ptr[2]);

    HEAP_DUMP(false);

    int i;
    for (i=0; i < 16; i++)
        ptr[i] = 0;

    for (i=0; i < 32768; i++) {
        unsigned int index = (unsigned int)rand() % 16;

        if ((i % (16*1024)) == 0)
            printf("pass %d\n", i);

//      printf("index 0x%x\n", index);
        if (ptr[index]) {
//          printf("freeing ptr[0x%x] = %p\n", index, ptr[index]);
            HEAP_FREE(ptr[index]);
            ptr[index] = 0;
        }
        unsigned int align = 1 << ((unsigned int)rand() % 8);
        ptr[index] = HEAP_MEMALIGN(align, (unsigned int)rand() % 32768);
//      printf("ptr[0x%x] = %p, align 0x%x\n", index, ptr[index], align);

        DEBUG_ASSERT(((addr_t)ptr[index] % align) == 0);
//      heap_dump();
    }

    for (i=0; i < 16; i++) {
        if (ptr[i])
            HEAP_FREE(ptr[i]);
    }

    HEAP_DUMP(false);
#endif
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


