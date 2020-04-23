// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_DMA_BUFFER_INCLUDE_LIB_DMA_BUFFER_BUFFER_H_
#define SRC_DEVICES_LIB_DMA_BUFFER_INCLUDE_LIB_DMA_BUFFER_BUFFER_H_

#include <lib/zx/bti.h>
#include <lib/zx/pmt.h>
#include <lib/zx/vmo.h>
#include <zircon/process.h>

#include <optional>
#include <vector>

#include <fbl/intrusive_double_list.h>

namespace dma_buffer {

// I/O buffer for managing physical memory associated with DMA buffers
// DMA buffers are contiguous in physical memory. Contiguous buffers
// are always cached in the current API. A future reivision to this patch
// will allow users to create uncached contiguous buffers.
class ContiguousBuffer : public fbl::DoublyLinkedListable<std::unique_ptr<ContiguousBuffer>> {
 public:
  ContiguousBuffer(ContiguousBuffer&& other) {
    size_ = other.size_;
    virt_ = other.virt_;
    phys_ = other.phys_;
    vmo_ = std::move(other.vmo_);
    pmt_ = std::move(other.pmt_);
  }
  ContiguousBuffer& operator=(ContiguousBuffer&& other) {
    size_ = other.size_;
    virt_ = other.virt_;
    phys_ = other.phys_;
    vmo_ = std::move(other.vmo_);
    pmt_ = std::move(other.pmt_);
    return *this;
  }
  ContiguousBuffer(const ContiguousBuffer&) = delete;
  ContiguousBuffer& operator=(const ContiguousBuffer&) = delete;
  static zx_status_t Create(const zx::bti& bti, size_t size, uint32_t alignment_log2,
                            std::optional<ContiguousBuffer>* out);

  size_t size() { return size_; }
  void* virt() { return virt_; }

  zx_paddr_t phys() { return phys_; }
  ~ContiguousBuffer();

 private:
  ContiguousBuffer(size_t size, zx::vmo vmo, void* virt, zx_paddr_t phys, zx::pmt pmt)
      : size_(size), virt_(virt), phys_(phys), vmo_(std::move(vmo)), pmt_(std::move(pmt)) {}
  size_t size_;
  void* virt_;
  zx_paddr_t phys_;
  zx::vmo vmo_;
  zx::pmt pmt_;
};

// A paged buffer consisting of 1 or more pages pinned in memory
// which are contiguous in virtual memory, but may be discontiguous
// in physical memory.
class PagedBuffer : public fbl::DoublyLinkedListable<std::unique_ptr<PagedBuffer>> {
 public:
  PagedBuffer(PagedBuffer&& other) {
    size_ = other.size_;
    virt_ = other.virt_;
    phys_ = other.phys_;
    vmo_ = std::move(other.vmo_);
    pmt_ = std::move(other.pmt_);
  }
  PagedBuffer& operator=(PagedBuffer&& other) {
    size_ = other.size_;
    virt_ = other.virt_;
    phys_ = other.phys_;
    vmo_ = std::move(other.vmo_);
    pmt_ = std::move(other.pmt_);
    return *this;
  }
  PagedBuffer(const PagedBuffer&) = delete;
  PagedBuffer& operator=(const PagedBuffer&) = delete;
  static zx_status_t Create(const zx::bti& bti, size_t size, bool enable_cache,
                            std::optional<PagedBuffer>* out);

  size_t size() { return size_; }
  void* virt() { return virt_; }

  zx_paddr_t* phys() { return phys_.data(); }
  ~PagedBuffer();

 private:
  PagedBuffer(size_t size, zx::vmo vmo, void* virt, std::vector<zx_paddr_t> phys, zx::pmt pmt)
      : size_(size), virt_(virt), phys_(phys), vmo_(std::move(vmo)), pmt_(std::move(pmt)) {}
  size_t size_;
  void* virt_;
  std::vector<zx_paddr_t> phys_;
  zx::vmo vmo_;
  zx::pmt pmt_;
};

}  // namespace dma_buffer

#endif  // SRC_DEVICES_LIB_DMA_BUFFER_INCLUDE_LIB_DMA_BUFFER_BUFFER_H_
