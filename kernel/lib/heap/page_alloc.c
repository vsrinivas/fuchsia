// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2015 Google, Inc. All rights reserved
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT


#include <lib/page_alloc.h>

#include <debug.h>
#include <assert.h>
#include <string.h>
#include <trace.h>
#include <kernel/vm.h>

/* A simple page-aligned wrapper around the pmm or novm implementation of
 * the underlying physical page allocator. Used by system heaps or any
 * other user that wants pages of memory but doesn't want to use LK
 * specific apis.
 */
#define LOCAL_TRACE 0

#if WITH_STATIC_HEAP

#error "fix static heap post page allocator and novm stuff"

#if !defined(HEAP_START) || !defined(HEAP_LEN)
#error WITH_STATIC_HEAP set but no HEAP_START or HEAP_LEN defined
#endif

#endif

void *page_alloc(size_t pages)
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

void page_free(void *ptr, size_t pages)
{
    DEBUG_ASSERT(IS_PAGE_ALIGNED((uintptr_t)ptr));
    DEBUG_ASSERT(pages > 0);

    pmm_free_kpages(ptr, pages);
}

void *page_first_alloc(size_t *size_return)
{
    *size_return = PAGE_SIZE;
    return page_alloc(1);
}
