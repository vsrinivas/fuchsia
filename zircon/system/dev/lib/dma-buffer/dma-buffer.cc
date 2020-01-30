// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/vmar.h>

#include "include/lib/dma-buffer/buffer.h"

namespace dma_buffer {

__EXPORT zx_status_t ContiguousBuffer::Create(const zx::bti& bti, size_t size,
                                              uint32_t alignment_log2,
                                              std::optional<ContiguousBuffer>* out) {
  zx::vmo vmo;
  zx_status_t status;
  status = zx::vmo::create_contiguous(bti, size, alignment_log2, &vmo);
  if (status != ZX_OK) {
    return status;
  }
  void* virt;
  zx_paddr_t phys;
  status = zx::vmar::root_self()->map(0, vmo, 0, size, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE,
                                      reinterpret_cast<zx_vaddr_t*>(&virt));
  if (status != ZX_OK) {
    return status;
  }
  zx::pmt pmt;
  status = bti.pin(ZX_BTI_PERM_READ | ZX_BTI_PERM_WRITE, vmo, 0, size, &phys, 1, &pmt);
  if (status != ZX_OK) {
    return status;
  }
  ContiguousBuffer buffer(size, std::move(vmo), virt, phys, std::move(pmt));
  *out = std::move(buffer);
  return ZX_OK;
}

__EXPORT ContiguousBuffer::~ContiguousBuffer() {}

__EXPORT zx_status_t PagedBuffer::Create(const zx::bti& bti, size_t size, bool is_cached,
                                         std::optional<PagedBuffer>* out) {
  zx::vmo vmo;
  zx_status_t status;
  status = zx::vmo::create(size, 0, &vmo);
  if (status != ZX_OK) {
    return status;
  }
  if (!is_cached) {
    status = vmo.set_cache_policy(ZX_CACHE_POLICY_UNCACHED);
  }
  if (status != ZX_OK) {
    return status;
  }
  if (size % ZX_PAGE_SIZE) {
    size = ((size / ZX_PAGE_SIZE) + 1) * ZX_PAGE_SIZE;
  }
  void* virt;
  std::vector<zx_paddr_t> phys;
  phys.resize(size / ZX_PAGE_SIZE);
  status = zx::vmar::root_self()->map(0, vmo, 0, size, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE,
                                      reinterpret_cast<zx_vaddr_t*>(&virt));
  if (status != ZX_OK) {
    return status;
  }
  zx::pmt pmt;
  status =
      bti.pin(ZX_BTI_PERM_READ | ZX_BTI_PERM_WRITE, vmo, 0, size, phys.data(), phys.size(), &pmt);
  if (status != ZX_OK) {
    return status;
  }
  PagedBuffer buffer(size, std::move(vmo), virt, std::move(phys), std::move(pmt));
  *out = std::move(buffer);
  return ZX_OK;
}

__EXPORT PagedBuffer::~PagedBuffer() {}

}  // namespace dma_buffer
