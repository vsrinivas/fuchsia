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
  virtual size_t size() const = 0;
  virtual void* virt() const = 0;

  virtual zx_paddr_t phys() const = 0;
  virtual ~ContiguousBuffer() = default;
};

// A paged buffer consisting of 1 or more pages pinned in memory
// which are contiguous in virtual memory, but may be discontiguous
// in physical memory.
class PagedBuffer : public fbl::DoublyLinkedListable<std::unique_ptr<PagedBuffer>> {
 public:
  virtual size_t size() const = 0;
  virtual void* virt() const = 0;

  virtual const zx_paddr_t* phys() const = 0;
  virtual ~PagedBuffer() = default;
};

// Buffer factory -- abstract class used to create DMA buffers.
// Use CreateBufferFactory() to create a default implementation of a buffer factory.
// This class exists to allow for tests to override the behavior of DMA buffers.
// Refer to fake-dma-buffer to create a fake DMA buffer.
class BufferFactory {
 public:
  virtual zx_status_t CreateContiguous(const zx::bti& bti, size_t size, uint32_t alignment_log2,
                                       std::unique_ptr<ContiguousBuffer>* out) const = 0;
  virtual zx_status_t CreatePaged(const zx::bti& bti, size_t size, bool enable_cache,
                                  std::unique_ptr<PagedBuffer>* out) const = 0;
  virtual ~BufferFactory() = default;
};

std::unique_ptr<BufferFactory> CreateBufferFactory();

}  // namespace dma_buffer

#endif  // SRC_DEVICES_LIB_DMA_BUFFER_INCLUDE_LIB_DMA_BUFFER_BUFFER_H_
