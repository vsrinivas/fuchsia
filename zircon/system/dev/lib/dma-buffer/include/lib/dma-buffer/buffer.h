// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <lib/zx/bti.h>
#include <lib/zx/pmt.h>
#include <lib/zx/vmo.h>
#include <zircon/process.h>

#include <optional>

#include <fbl/intrusive_double_list.h>

namespace dma_buffer {

// I/O buffer for managing physical memory associated with DMA buffers
// DMA buffers are contiguous in physical memory.
class Buffer : public fbl::DoublyLinkedListable<std::unique_ptr<Buffer>> {
 public:
  Buffer(Buffer&& other) {
    size_ = other.size_;
    virt_ = other.virt_;
    phys_ = other.phys_;
    vmo_ = std::move(other.vmo_);
    pmt_ = std::move(other.pmt_);
  }
  Buffer& operator=(Buffer&& other) {
    size_ = other.size_;
    virt_ = other.virt_;
    phys_ = other.phys_;
    vmo_ = std::move(other.vmo_);
    pmt_ = std::move(other.pmt_);
    return *this;
  }
  Buffer(const Buffer&) = delete;
  Buffer& operator=(const Buffer&) = delete;
  static zx_status_t Create(const zx::bti& bti, size_t size, uint32_t alignment_log2,
                            bool enable_cache, std::optional<Buffer>* out);

  size_t size() { return size_; }
  void* virt() { return virt_; }

  zx_paddr_t phys() { return phys_; }

 private:
  Buffer(size_t size, zx::vmo vmo, void* virt, zx_paddr_t phys, zx::pmt pmt)
      : size_(size), virt_(virt), phys_(phys), vmo_(std::move(vmo)), pmt_(std::move(pmt)) {}
  size_t size_;
  void* virt_;
  zx_paddr_t phys_;
  zx::vmo vmo_;
  zx::pmt pmt_;
};

}  // namespace dma_buffer
