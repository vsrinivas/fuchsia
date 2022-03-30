// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/mmio/mmio-buffer.h>
#include <zircon/errors.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/object.h>
#include <zircon/types.h>

#define MMIO_ROUNDUP(a, b)      \
  ({                            \
    const __typeof(a) _a = (a); \
    const __typeof(b) _b = (b); \
    ((_a + _b - 1) / _b * _b);  \
  })
#define MMIO_ROUNDDOWN(a, b)    \
  ({                            \
    const __typeof(a) _a = (a); \
    const __typeof(b) _b = (b); \
    _a - (_a % _b);             \
  })

zx_status_t mmio_buffer_init(mmio_buffer_t* buffer, zx_off_t offset, size_t size, zx_handle_t vmo,
                             uint32_t cache_policy) {
  if (!buffer) {
    zx_handle_close(vmo);
    return ZX_ERR_INVALID_ARGS;
  }
  // |zx_vmo_set_cache_policy| will always return an error if it encounters a
  // VMO that has already been mapped. To enable tests where a VMO may be mapped
  // and modified already by a test fixture we only set the cache policy of a
  // provided VMO if the requested cache policy does not match the VMO's current
  // cache policy.
  zx_info_vmo_t info = {};
  zx_status_t status = zx_object_get_info(vmo, ZX_INFO_VMO, &info, sizeof(info), NULL, NULL);
  if (status != ZX_OK) {
    zx_handle_close(vmo);
    return status;
  }

  if (info.cache_policy != cache_policy) {
    status = zx_vmo_set_cache_policy(vmo, cache_policy);
    if (status != ZX_OK) {
      zx_handle_close(vmo);
      return status;
    }
  }

  uint64_t result = 0;
  if (add_overflow(offset, size, &result) || result > info.size_bytes) {
    zx_handle_close(vmo);
    return ZX_ERR_OUT_OF_RANGE;
  }

  uintptr_t vaddr;
  const size_t vmo_offset = MMIO_ROUNDDOWN(offset, zx_system_get_page_size());
  const size_t page_offset = offset - vmo_offset;
  const size_t vmo_size = MMIO_ROUNDUP(size + page_offset, zx_system_get_page_size());

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
  const size_t vmo_offset = MMIO_ROUNDDOWN(buffer->offset, zx_system_get_page_size());
  const size_t page_offset = buffer->offset - vmo_offset;
  const size_t vmo_size = MMIO_ROUNDUP(buffer->size + page_offset, zx_system_get_page_size());

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
