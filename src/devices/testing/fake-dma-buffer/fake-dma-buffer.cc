// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/dma-buffer/buffer.h>
#include <lib/zx/vmar.h>

#include <fake-dma-buffer/fake-dma-buffer.h>

class ContiguousBufferImpl : public dma_buffer::ContiguousBuffer {
 public:
  ContiguousBufferImpl(size_t size, zx::vmo vmo, void* virt, zx_paddr_t phys, zx::pmt pmt)
      : size_(size), virt_(virt), phys_(phys), vmo_(std::move(vmo)), pmt_(std::move(pmt)) {}
  size_t size() const { return size_; }
  void* virt() const { return virt_; }

  zx_paddr_t phys() const { return phys_; }
  ~ContiguousBufferImpl() {
    if (vmo_.is_valid()) {
      delete reinterpret_cast<ddk_fake::FakePage*>(phys_);
    }
  }

 private:
  size_t size_;
  void* virt_;
  zx_paddr_t phys_;
  zx::vmo vmo_;
  zx::pmt pmt_;
};

class PagedBufferImpl : public dma_buffer::PagedBuffer {
 public:
  PagedBufferImpl(size_t size, zx::vmo vmo, void* virt, std::vector<zx_paddr_t> phys, zx::pmt pmt)
      : size_(size), virt_(virt), phys_(phys), vmo_(std::move(vmo)), pmt_(std::move(pmt)) {}

  size_t size() const override { return size_; }
  void* virt() const override { return virt_; }

  const zx_paddr_t* phys() const override { return phys_.data(); }

  ~PagedBufferImpl() override {
    if (vmo_.is_valid()) {
      static_assert(sizeof(phys_[0]) == sizeof(ddk_fake::FakePage*));
      for (auto paddr : phys_) {
        delete reinterpret_cast<ddk_fake::FakePage*>(paddr);
      }
    }
  }

 private:
  size_t size_;
  void* virt_;
  std::vector<zx_paddr_t> phys_;
  zx::vmo vmo_;
  zx::pmt pmt_;
};

class BufferFactoryImpl : public dma_buffer::BufferFactory {
  zx_status_t CreateContiguous(const zx::bti& bti, size_t size, uint32_t alignment_log2,
                               std::unique_ptr<dma_buffer::ContiguousBuffer>* out) const override {
    if (size > ZX_PAGE_SIZE) {
      // TODO(fxb/45011): We don't currently support contiguous buffers > 1 page.
      return ZX_ERR_NOT_SUPPORTED;
    }
    zx::vmo real_vmo;
    zx_status_t status = zx::vmo::create(size, 0, &real_vmo);
    if (status != ZX_OK) {
      return status;
    }
    void* virt;
    status = zx::vmar::root_self()->map(0, real_vmo, 0, size, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE,
                                        reinterpret_cast<zx_vaddr_t*>(&virt));
    if (status != ZX_OK) {
      return status;
    }
    auto fake = new ddk_fake::FakePage();
    fake->alignment_log2 = alignment_log2;
    fake->enable_cache = true;
    fake->size = size;
    status = real_vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &fake->backing_storage);
    if (status != ZX_OK) {
      return status;
    }
    fake->virt = virt;
    fake->contiguous = true;
    fake->bti = bti.get();

    auto buffer = std::make_unique<ContiguousBufferImpl>(size, std::move(real_vmo), virt,
                                                         reinterpret_cast<zx_paddr_t>(fake),
                                                         zx::pmt(ZX_HANDLE_INVALID));
    *out = std::move(buffer);
    return ZX_OK;
  }
  zx_status_t CreatePaged(const zx::bti& bti, size_t size, bool enable_cache,
                          std::unique_ptr<dma_buffer::PagedBuffer>* out) const override {
    zx::vmo real_vmo;
    zx_status_t status = zx::vmo::create(size, 0, &real_vmo);
    if (status != ZX_OK) {
      return status;
    }

    uint8_t* virt;
    status = zx::vmar::root_self()->map(0, real_vmo, 0, size, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE,
                                        reinterpret_cast<zx_vaddr_t*>(&virt));
    if (status != ZX_OK) {
      return status;
    }
    size_t pages = fbl::round_up(size, ZX_PAGE_SIZE) / ZX_PAGE_SIZE;
    std::vector<zx_paddr_t> physvec;
    physvec.resize(pages);
    for (size_t i = 0; i < pages; i++) {
      auto phys = new ddk_fake::FakePage();
      phys->enable_cache = enable_cache;
      phys->size = size;
      status = real_vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &phys->backing_storage);
      if (status != ZX_OK) {
        return status;
      }
      phys->virt = virt + (ZX_PAGE_SIZE * i);
      phys->bti = bti.get();
      phys->contiguous = false;
      physvec[i] = reinterpret_cast<zx_paddr_t>(phys);
    }
    auto buffer = std::make_unique<PagedBufferImpl>(size, std::move(real_vmo), virt,
                                                    std::move(physvec), zx::pmt(ZX_HANDLE_INVALID));
    *out = std::move(buffer);
    return ZX_OK;
  }
};

namespace ddk_fake {

void* PhysToVirt(zx_paddr_t phys) { return PhysToVirt<void*>(phys); }

const FakePage& GetPage(zx_paddr_t phys) {
  size_t start = fbl::round_down(phys, ZX_PAGE_SIZE);
  return *reinterpret_cast<FakePage*>(start);
}

std::unique_ptr<dma_buffer::BufferFactory> CreateBufferFactory() {
  return std::make_unique<BufferFactoryImpl>();
}

}  // namespace ddk_fake
