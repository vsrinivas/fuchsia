// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008-2015 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <stddef.h>
#include <sys/types.h>
#include <magenta/compiler.h>

__BEGIN_CDECLS

/* standard heap definitions */
void *malloc(size_t size) __MALLOC;
void *memalign(size_t boundary, size_t size) __MALLOC;
void *calloc(size_t count, size_t size) __MALLOC;
void *realloc(void *ptr, size_t size) __MALLOC;
void free(void *ptr);

void heap_init(void);

/* tell the heap to return any free pages it can find */
void heap_trim(void);

/* internal apis used by the heap implementation to get/return pages to the VM */
void *heap_page_alloc(size_t pages);
void heap_page_free(void *ptr, size_t pages);

__END_CDECLS
