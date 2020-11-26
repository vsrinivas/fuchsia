// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <ddk/driver.h>
#include <ddk/mmio-buffer.h>

#include "macros.h"

zx_status_t mmio_buffer_init(mmio_buffer_t* buffer, zx_off_t offset, size_t size, zx_handle_t vmo,
                             uint32_t cache_policy) {
  zx_status_t status = zx_vmo_set_cache_policy(vmo, cache_policy);
  if (status != ZX_OK) {
    zx_handle_close(vmo);
    return status;
  }

  uintptr_t vaddr;
  const size_t vmo_offset = DDK_ROUNDDOWN(offset, ZX_PAGE_SIZE);
  const size_t page_offset = offset - vmo_offset;
  const size_t vmo_size = DDK_ROUNDUP(size + page_offset, ZX_PAGE_SIZE);

  status = zx_vmar_map(zx_vmar_root_self(), ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_MAP_RANGE, 0,
                       vmo, vmo_offset, vmo_size, &vaddr);
  if (status != ZX_OK) {
    zx_handle_close(vmo);
    return status;
  }

  buffer->vmo = vmo;
  buffer->vaddr = (MMIO_PTR void*)(vaddr + page_offset);
  buffer->offset = offset;
  buffer->size = size;

  return ZX_OK;
}

zx_status_t mmio_buffer_init_physical(mmio_buffer_t* buffer, zx_paddr_t base, size_t size,
                                      zx_handle_t resource, uint32_t cache_policy) {
  zx_handle_t vmo;
  zx_status_t status = zx_vmo_create_physical(resource, base, size, &vmo);
  if (status != ZX_OK) {
    return status;
  }

  // |base| is guaranteed to be page aligned.
  return mmio_buffer_init(buffer, 0, size, vmo, cache_policy);
}

void mmio_buffer_release(mmio_buffer_t* buffer) {
  if (buffer->vmo != ZX_HANDLE_INVALID) {
    zx_vmar_unmap(zx_vmar_root_self(), (uintptr_t)buffer->vaddr, buffer->size);
    zx_handle_close(buffer->vmo);
    buffer->vmo = ZX_HANDLE_INVALID;
  }
}

zx_status_t mmio_buffer_pin(mmio_buffer_t* buffer, zx_handle_t bti, mmio_pinned_buffer_t* out) {
  zx_paddr_t paddr;
  zx_handle_t pmt;
  const uint32_t options = ZX_BTI_PERM_WRITE | ZX_BTI_PERM_READ | ZX_BTI_CONTIGUOUS;
  const size_t vmo_offset = DDK_ROUNDDOWN(buffer->offset, ZX_PAGE_SIZE);
  const size_t page_offset = buffer->offset - vmo_offset;
  const size_t vmo_size = DDK_ROUNDUP(buffer->size + page_offset, ZX_PAGE_SIZE);

  zx_status_t status = zx_bti_pin(bti, options, buffer->vmo, vmo_offset, vmo_size, &paddr, 1, &pmt);
  if (status != ZX_OK) {
    return status;
  }

  out->mmio = buffer;
  out->paddr = paddr + page_offset;
  out->pmt = pmt;

  return ZX_OK;
}

void mmio_buffer_unpin(mmio_pinned_buffer_t* buffer) {
  if (buffer->pmt != ZX_HANDLE_INVALID) {
    zx_pmt_unpin(buffer->pmt);
    buffer->pmt = ZX_HANDLE_INVALID;
  }
}
