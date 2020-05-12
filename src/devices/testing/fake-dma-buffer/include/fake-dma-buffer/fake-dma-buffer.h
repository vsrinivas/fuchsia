// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_TESTING_FAKE_DMA_BUFFER_INCLUDE_FAKE_DMA_BUFFER_FAKE_DMA_BUFFER_H_
#define SRC_DEVICES_TESTING_FAKE_DMA_BUFFER_INCLUDE_FAKE_DMA_BUFFER_FAKE_DMA_BUFFER_H_

#include <lib/dma-buffer/buffer.h>
#include <lib/fit/function.h>
#include <lib/zx/vmo.h>

#include <fbl/algorithm.h>
#include <fbl/vector.h>

namespace ddk_fake {

// This library provides a fake implementation of DMA buffers and virtual address translation (for
// those buffers) to drivers for testing.

// Fake VMO page containing information passed to ContiguousBuffer::Create or PagedBuffer::Create.
struct alignas(4096) FakePage {
  // Size of the VMO that was passed in by the constructor
  // (this is the raw value provided by the user -- not necessarily the actual size of the VMO)
  size_t size;

  // alignment_log2 value passed in by the user
  uint32_t alignment_log2;

  // True if cache is enabled, false otherwise
  bool enable_cache;

  // Actual VMO backing this page
  zx::vmo backing_storage;

  // Starting virtual address for this VMO
  void* virt;

  // BTI handle value for this VMO
  uint32_t bti;

  // True if this VMO is contiguous; false otherwise.
  bool contiguous;
};

// Converts a physical address to a page
// The pointer returned is owned by the DMA buffer,
// and is freed when the DMA buffer is released.
const FakePage& GetPage(zx_paddr_t phys);

template <typename T>
static T PhysToVirt(zx_paddr_t phys) {
  size_t start = fbl::round_down(phys, ZX_PAGE_SIZE);
  size_t offset = phys - start;
  return reinterpret_cast<T>(reinterpret_cast<uintptr_t>(GetPage(start).virt) + offset);
}

// Converts a physical address to a virtual address
// The pointer returned is owned by the DMA buffer,
// and is freed when the DMA buffer is released.
void* PhysToVirt(zx_paddr_t phys);

static_assert(sizeof(uint32_t) == sizeof(zx_handle_t));

std::unique_ptr<dma_buffer::BufferFactory> CreateBufferFactory();

}  // namespace ddk_fake

#endif  // SRC_DEVICES_TESTING_FAKE_DMA_BUFFER_INCLUDE_FAKE_DMA_BUFFER_FAKE_DMA_BUFFER_H_
