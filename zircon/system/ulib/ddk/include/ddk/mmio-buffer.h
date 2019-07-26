// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DDK_MMIO_BUFFER_H_
#define DDK_MMIO_BUFFER_H_

#include <zircon/types.h>

__BEGIN_CDECLS

typedef struct {
  // |vaddr| points to the content starting at |offset| in |vmo|.
  void* vaddr;
  zx_off_t offset;
  size_t size;
  zx_handle_t vmo;
} mmio_buffer_t;

typedef struct {
  const mmio_buffer_t* mmio;
  zx_handle_t pmt;
  // |paddr| points to the content starting at |mmio->offset| in |mmio->vmo|.
  zx_paddr_t paddr;
} mmio_pinned_buffer_t;

// Takes raw mmio resources, and maps it into address space. |offset| is the
// offset from the beginning of |vmo| where the mmio region begins. |size|
// specifies the size of the mmio region. |offset| + |size| must be less than
// or equal to the size of |vmo|.
// Always consumes |vmo|, including in error cases.
zx_status_t mmio_buffer_init(mmio_buffer_t* buffer, zx_off_t offset, size_t size, zx_handle_t vmo,
                             uint32_t cache_policy);

// Takes a physical region, and maps it into address space. |base| and |size|
// must be page aligned.
// Callee retains ownership of |resource|.
zx_status_t mmio_buffer_init_physical(mmio_buffer_t* buffer, zx_paddr_t base, size_t size,
                                      zx_handle_t resource, uint32_t cache_policy);

// Unmaps the mmio region.
void mmio_buffer_release(mmio_buffer_t* buffer);

// Returns a pinned buffer if successful. |buffer| must outlive |out|.
//
// Example usage: A device needs access to another device's MMIO space.
zx_status_t mmio_buffer_pin(mmio_buffer_t* buffer, zx_handle_t bti, mmio_pinned_buffer_t* out);

// Unpins the buffer.
void mmio_buffer_unpin(mmio_pinned_buffer_t* buffer);

__END_CDECLS

#endif  // DDK_MMIO_BUFFER_H_
