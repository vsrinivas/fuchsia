// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/compiler.h>
#include <magenta/types.h>

#include <stddef.h>

__BEGIN_CDECLS;

typedef struct io_alloc io_alloc_t;

io_alloc_t* io_alloc_init(size_t size);
void io_alloc_free(io_alloc_t* ioa);

void* io_malloc(io_alloc_t* ioa, size_t size);
void* io_calloc(io_alloc_t* ioa, size_t count, size_t size);
void* io_memalign(io_alloc_t* ioa, size_t align, size_t size);
void io_free(io_alloc_t* ioa, void* ptr);
mx_paddr_t io_virt_to_phys(io_alloc_t* ioa, mx_vaddr_t virt_addr);
mx_vaddr_t io_phys_to_virt(io_alloc_t* ioa, mx_paddr_t phys_addr);

__END_CDECLS;
