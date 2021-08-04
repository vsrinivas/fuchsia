// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008-2015 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_HEAP_INCLUDE_LIB_HEAP_H_
#define ZIRCON_KERNEL_LIB_HEAP_INCLUDE_LIB_HEAP_H_

#include <stddef.h>
#include <sys/types.h>
#include <zircon/compiler.h>

__BEGIN_CDECLS

// define this to enable collection of all unique call sites with unique sizes
#define HEAP_COLLECT_STATS 0

// The underlying cmpctmalloc allocator defaults to 8 byte alignment.
#define HEAP_DEFAULT_ALIGNMENT 8

// standard heap definitions
void* malloc(size_t size) __MALLOC;
void* memalign(size_t alignment, size_t size) __MALLOC;
void* calloc(size_t count, size_t size) __MALLOC;
void free(void* ptr);
void sized_free(void* ptr, size_t s);

// Gets stats about the heap.
// |total_bytes| is the total size of the heap (the sum of all pages allocated
// from the PMM), |free_bytes| is the free portion.
void heap_get_info(size_t* total_bytes, size_t* free_bytes);

// called once at kernel initialization
void heap_init(void);

__END_CDECLS

#endif  // ZIRCON_KERNEL_LIB_HEAP_INCLUDE_LIB_HEAP_H_
