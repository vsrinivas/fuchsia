// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/vmar.h>

#include "include/lib/dma-buffer/buffer.h"

namespace dma_buffer {

__EXPORT zx_status_t Buffer::Create(const zx::bti& bti, size_t size, uint32_t alignment_log2,
                                    bool enable_cache, std::optional<Buffer>* out) {
  zx::vmo vmo;
  zx_status_t status = zx::vmo::create_contiguous(bti, size, alignment_log2, &vmo);
  if (status != ZX_OK) {
    return status;
  }
  if (!enable_cache) {
    status = vmo.set_cache_policy(ZX_CACHE_POLICY_UNCACHED);
    if (status != ZX_OK) {
      return status;
    }
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
  Buffer buffer(size, std::move(vmo), virt, phys, std::move(pmt));
  *out = std::move(buffer);
  return ZX_OK;
}

}  // namespace dma_buffer
