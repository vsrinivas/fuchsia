/*
 * Copyright (c) 2018 The Fuchsia Authors.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "sparse_array.h"

#include <stdlib.h>

#include <zircon/assert.h>

// An individual element is either a part of the used list or the free list at
// any given time, each of which is a non-circular doubly-linked list terminated
// at the head and tail by an index value of -1.
struct sa_elem {
    ssize_t prev_ndx;
    ssize_t next_ndx;
    void* ptr;
};

// We store a sparse array as a set of elements with two lists -- one for available
// elements and one for in use elements. "free" and "used" provide the index of the
// head of the list of unused and used indices, respectively.
struct sparse_array {
    size_t size;
    ssize_t free;
    ssize_t used;
    struct sa_elem elems[0];
};

#define RANGE_CHECK(sa, ndx) \
    ZX_DEBUG_ASSERT(((ndx) >= 0) && (((size_t)(ndx)) < sa->size))

void sa_init(sparse_array_t* psa, size_t size) {
    ZX_DEBUG_ASSERT(size > 0);
    size_t total_size = sizeof(struct sparse_array) + (sizeof(struct sa_elem) * size);
    *psa = calloc(1, total_size);
    if (*psa == NULL) {
        return;
    }

    sparse_array_t sa = *psa;
    sa->size = size;

    // Initialize used list as empty
    sa->used = -1;

    // Add all elements to the free list
    sa->elems[0].prev_ndx = -1;
    for (ssize_t ndx = 0; ndx < ((ssize_t)size - 1); ndx++) {
        sa->elems[ndx + 1].prev_ndx = ndx;
        sa->elems[ndx].next_ndx = ndx + 1;
    }
    sa->elems[size - 1].next_ndx = -1;

    // Initialize free list to point to a list of all elements
    sa->free = 0;
}

void sa_free(sparse_array_t sa) {
    free(sa);
}

ssize_t sa_add(sparse_array_t sa, void* payload) {
    ssize_t elem_ndx = sa->free;
    if (elem_ndx == -1) {
        return -1;
    }
    RANGE_CHECK(sa, elem_ndx);
    struct sa_elem* elem = &sa->elems[elem_ndx];

    // Remove from free list
    sa->free = elem->next_ndx;
    if (sa->free != -1) {
        RANGE_CHECK(sa, sa->free);
        sa->elems[sa->free].prev_ndx = -1;
    }

    // Add to used list
    elem->next_ndx = sa->used;
    if (sa->used != -1) {
        RANGE_CHECK(sa, sa->used);
        sa->elems[sa->used].prev_ndx = elem_ndx;
    }
    sa->used = elem_ndx;

    ZX_DEBUG_ASSERT(elem->ptr == NULL);
    elem->ptr = payload;

    return elem_ndx;
}

void* sa_get(sparse_array_t sa, ssize_t ndx) {
    RANGE_CHECK(sa, ndx);
    return sa->elems[ndx].ptr;
}

void sa_remove(sparse_array_t sa, ssize_t ndx) {
    RANGE_CHECK(sa, ndx);
    struct sa_elem* elem = &sa->elems[ndx];
    ssize_t prev_ndx = elem->prev_ndx;
    ssize_t next_ndx = elem->next_ndx;

    // Remove from used list
    if (prev_ndx == -1) {
        sa->used = next_ndx;
    } else {
        RANGE_CHECK(sa, prev_ndx);
        struct sa_elem* prev_elem = &sa->elems[prev_ndx];
        prev_elem->next_ndx = next_ndx;
    }
    if (next_ndx != -1) {
        RANGE_CHECK(sa, next_ndx);
        struct sa_elem* next_elem = &sa->elems[next_ndx];
        next_elem->prev_ndx = prev_ndx;
    }

    // Add to free list
    next_ndx = sa->free;
    elem->prev_ndx = -1;
    elem->next_ndx = next_ndx;
    elem->ptr = NULL;
    sa->free = ndx;
    if (next_ndx != -1) {
        RANGE_CHECK(sa, next_ndx);
        struct sa_elem* next_elem = &sa->elems[next_ndx];
        next_elem->prev_ndx = ndx;
    }
}

void sa_for_each(sparse_array_t sa, void (*fn)(ssize_t, void*, void*), void* ctx) {
    ssize_t next_ndx = sa->used;
    while (next_ndx != -1) {
        RANGE_CHECK(sa, next_ndx);
        struct sa_elem* elem = &sa->elems[next_ndx];
        fn(next_ndx, elem->ptr, ctx);
        next_ndx = sa->elems[next_ndx].next_ndx;
    }
}
