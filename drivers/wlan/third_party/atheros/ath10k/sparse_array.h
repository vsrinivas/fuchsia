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
#ifndef _SPARSE_ARRAY_H_
#define _SPARSE_ARRAY_H_

#include <stddef.h>
#include <sys/types.h>

// A sparse array provides an association between index values and payloads.
// The operations available and their time complexity are:
//   add: add a new element, returning the index into which it was stored, O(1)
//   get: return the value associated with an index, O(1)
//   remove: remove the value associated with an index, O(1)
//   for_each: call a function for each value in the used list, O(n), where n
//             is the number of indices in use

// Note that concurrent accesses are unsupported, so the caller must provide
// their own mutex if it's needed.

struct sparse_array;
typedef struct sparse_array* sparse_array_t;

// Allocate a new sparse array into *psa. Sets to NULL if memory allocation fails.
void sa_init(sparse_array_t* psa, size_t size);

// Deallocate a sparse array.
void sa_free(sparse_array_t sa);

// Add an element to a sparse array, returns the index or -1 if no entries are available.
ssize_t sa_add(sparse_array_t sa, void* payload);

// Get the element at the specified index, which must be in the used list.
void* sa_get(sparse_array_t sa, ssize_t ndx);

// Remove an element from a sparse array, which must be in the used list.
void sa_remove(sparse_array_t sa, ssize_t ndx);

// Call a function on each element in a sparse array.
void sa_for_each(sparse_array_t sa, void (*fn)(ssize_t ndx, void* ptr, void* ctx), void* ctx);

#endif
