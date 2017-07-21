// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/compiler.h>
#include <magenta/types.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

__BEGIN_CDECLS;

typedef struct {
    mx_handle_t vmo_handle;
    size_t size;
    mx_off_t offset;
    void* virt;
    mx_paddr_t phys;
} io_buffer_t;

enum {
    IO_BUFFER_RO = MX_VM_FLAG_PERM_READ,
    IO_BUFFER_RW = MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE,
};

// Initializes a new io_buffer
mx_status_t io_buffer_init(io_buffer_t* buffer, size_t size, uint32_t flags);
mx_status_t io_buffer_init_aligned(io_buffer_t* buffer, size_t size, uint32_t alignment_log2, uint32_t flags);

// Initializes an io_buffer base on an existing VMO.
// duplicates the provided vmo_handle - does not take ownership
mx_status_t io_buffer_init_vmo(io_buffer_t* buffer, mx_handle_t vmo_handle,
                               mx_off_t offset, uint32_t flags);

// Initializes an io_buffer that maps a given physical address
mx_status_t io_buffer_init_physical(io_buffer_t* buffer, mx_paddr_t addr, size_t size,
                                    mx_handle_t resource, uint32_t cache_policy);

mx_status_t io_buffer_cache_op(io_buffer_t* buffer, const uint32_t op,
                               const mx_off_t offset, const size_t size);
// Releases an io_buffer
void io_buffer_release(io_buffer_t* buffer);

static inline bool io_buffer_is_valid(io_buffer_t* buffer) {
    return (buffer->vmo_handle != MX_HANDLE_INVALID);
}

static inline void* io_buffer_virt(io_buffer_t* buffer) {
    return (void*)(((uintptr_t)buffer->virt) + buffer->offset);
}

static inline mx_paddr_t io_buffer_phys(io_buffer_t* buffer) {
    return buffer->phys + buffer->offset;
}

__END_CDECLS;
