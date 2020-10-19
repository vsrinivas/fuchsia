// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/vmar.h>

#include "include/lib/dma-buffer/buffer.h"

namespace dma_buffer {

// I/O buffer for managing physical memory associated with DMA buffers
// DMA buffers are contiguous in physical memory. Contiguous buffers
// are always cached in the current API. A future reivision to this patch
// will allow users to create uncached contiguous buffers.
class ContiguousBufferImpl : public ContiguousBuffer {
 public:
  ContiguousBufferImpl(size_t size, zx::vmo vmo, void* virt, zx_paddr_t phys, zx::pmt pmt)
      : size_(size), virt_(virt), phys_(phys), vmo_(std::move(vmo)), pmt_(std::move(pmt)) {}
  size_t size() const override { return size_; }
  void* virt() const override { return virt_; }

  zx_paddr_t phys() const override { return phys_; }
  ~ContiguousBufferImpl() { pmt_.unpin(); }

 private:
  size_t size_;
  void* virt_;
  zx_paddr_t phys_;
  zx::vmo vmo_;
  zx::pmt pmt_;
};

// A paged buffer consisting of 1 or more pages pinned in memory
// which are contiguous in virtual memory, but may be discontiguous
// in physical memory.
class PagedBufferImpl : public PagedBuffer {
 public:
  PagedBufferImpl(size_t size, zx::vmo vmo, void* virt, std::vector<zx_paddr_t> phys, zx::pmt pmt)
      : size_(size), virt_(virt), phys_(phys), vmo_(std::move(vmo)), pmt_(std::move(pmt)) {}

  size_t size() const override { return size_; }
  void* virt() const override { return virt_; }

  const zx_paddr_t* phys() const override { return phys_.data(); }
  ~PagedBufferImpl() { pmt_.unpin(); }

 private:
  size_t size_;
  void* virt_;
  std::vector<zx_paddr_t> phys_;
  zx::vmo vmo_;
  zx::pmt pmt_;
};

class BufferFactoryImpl : public BufferFactory {
  zx_status_t CreateContiguous(const zx::bti& bti, size_t size, uint32_t alignment_log2,
                               std::unique_ptr<ContiguousBuffer>* out) const override {
    zx::vmo vmo;
    zx_status_t status;
    status = zx::vmo::create_contiguous(bti, size, alignment_log2, &vmo);
    if (status != ZX_OK) {
      return status;
    }
    void* virt;
    zx_paddr_t phys;
    status = zx::vmar::root_self()->map(ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0, vmo, 0, size,
                                        reinterpret_cast<zx_vaddr_t*>(&virt));
    if (status != ZX_OK) {
      return status;
    }
    zx::pmt pmt;
    status = bti.pin(ZX_BTI_PERM_READ | ZX_BTI_PERM_WRITE, vmo, 0, size, &phys, 1, &pmt);
    if (status != ZX_OK) {
      return status;
    }
    auto buffer =
        std::make_unique<ContiguousBufferImpl>(size, std::move(vmo), virt, phys, std::move(pmt));
    *out = std::move(buffer);
    return ZX_OK;
  }
  zx_status_t CreatePaged(const zx::bti& bti, size_t size, bool enable_cache,
                          std::unique_ptr<PagedBuffer>* out) const override {
    zx::vmo vmo;
    zx_status_t status;
    status = zx::vmo::create(size, 0, &vmo);
    if (status != ZX_OK) {
      return status;
    }
    if (!enable_cache) {
      status = vmo.set_cache_policy(ZX_CACHE_POLICY_UNCACHED_DEVICE);
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
    status = zx::vmar::root_self()->map(ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0, vmo, 0, size,
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
    auto buffer = std::make_unique<PagedBufferImpl>(size, std::move(vmo), virt, std::move(phys),
                                                    std::move(pmt));
    *out = std::move(buffer);
    return ZX_OK;
  }
};

std::unique_ptr<BufferFactory> CreateBufferFactory() {
  return std::make_unique<BufferFactoryImpl>();
}

}  // namespace dma_buffer
