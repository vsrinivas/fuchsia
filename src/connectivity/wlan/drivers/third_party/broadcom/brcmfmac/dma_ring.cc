// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/dma_ring.h"

#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>
#include <zircon/errors.h>
#include <zircon/syscalls.h>

#include <algorithm>
#include <limits>

namespace wlan {
namespace brcmfmac {

BaseDmaRing::BaseDmaRing(std::unique_ptr<DmaBuffer> dma_buffer, size_t item_size,
                         uint16_t item_capacity, volatile std::atomic<uint16_t>* read_index,
                         volatile std::atomic<uint16_t>* write_index)
    : dma_buffer_(std::move(dma_buffer)),
      item_size_(item_size),
      item_capacity_(item_capacity),
      read_index_(read_index),
      write_index_(write_index) {
  read_index->store(0, std::memory_order::memory_order_release);
  write_index->store(0, std::memory_order::memory_order_release);
}

BaseDmaRing::~BaseDmaRing() = default;

size_t BaseDmaRing::item_size() const { return item_size_; }

uint16_t BaseDmaRing::capacity() const { return item_capacity_; }

zx_paddr_t BaseDmaRing::dma_address() const { return dma_buffer_->dma_address(); }

ReadDmaRing::ReadDmaRing(std::unique_ptr<DmaBuffer> dma_buffer, size_t item_size,
                         uint16_t item_capacity, volatile std::atomic<uint16_t>* read_index,
                         volatile std::atomic<uint16_t>* write_index)
    : BaseDmaRing(std::move(dma_buffer), item_size, item_capacity, read_index, write_index) {}

ReadDmaRing::~ReadDmaRing() = default;

// static
zx_status_t ReadDmaRing::Create(std::unique_ptr<DmaBuffer> dma_buffer, size_t item_size,
                                uint16_t item_capacity, volatile std::atomic<uint16_t>* read_index,
                                volatile std::atomic<uint16_t>* write_index,
                                std::unique_ptr<ReadDmaRing>* out_read_dma_ring) {
  zx_status_t status = ZX_OK;

  if (dma_buffer->size() < item_size * item_capacity) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }
  if (dma_buffer->cache_policy() != ZX_CACHE_POLICY_CACHED) {
    // We handle cache invalidations internally, so there is no need to use an uncached buffer.
    return ZX_ERR_INVALID_ARGS;
  }
  if ((status = dma_buffer->Map(ZX_VM_PERM_READ)) != ZX_OK) {
    return status;
  }

  out_read_dma_ring->reset(
      new ReadDmaRing(std::move(dma_buffer), item_size, item_capacity, read_index, write_index));
  return ZX_OK;
}

uint16_t ReadDmaRing::GetAvailableReads() {
  uint16_t read_index = 0;
  return GetAvailableReads(&read_index);
}

zx_status_t ReadDmaRing::MapRead(uint16_t item_count, const void** out_buffer) {
  zx_status_t status = ZX_OK;
  uint16_t read_index = 0;
  if (item_count > GetAvailableReads(&read_index)) {
    return ZX_ERR_UNAVAILABLE;
  }

  // There is some set of items ahead of `read_index` which have arrived from the DMA device
  // recently, and thus may not yet be coherent in the CPU cache.  We handle this by performing an
  // explicit cache invalidation on these new entries; `cache_invalidate_index_` is the high-water
  // mark for these invalidations so that we do not redundantly perform it for items that have
  // already been processed.
  const uint16_t end_index = read_index + item_count;
  if (cache_invalidate_index_ < end_index) {
    if ((status = zx_cache_flush(reinterpret_cast<const void*>(
                                     dma_buffer_->address() + cache_invalidate_index_ * item_size_),
                                 (end_index - cache_invalidate_index_) * item_size_,
                                 ZX_CACHE_FLUSH_DATA | ZX_CACHE_FLUSH_INVALIDATE)) != ZX_OK) {
      return status;
    }
    cache_invalidate_index_ = end_index;
  }
  std::atomic_thread_fence(std::memory_order::memory_order_acquire);

  *out_buffer = reinterpret_cast<const void*>(dma_buffer_->address() + read_index * item_size_);
  return ZX_OK;
}

zx_status_t ReadDmaRing::CommitRead(uint16_t item_count) {
  uint16_t read_index = 0;
  if (item_count > GetAvailableReads(&read_index)) {
    return ZX_ERR_UNAVAILABLE;
  }
  uint16_t new_read_index = read_index + item_count;

  // If we commit past the existing `cache_invalidate_index_`, we can skip it ahead to the new
  // `read_index_`.
  cache_invalidate_index_ = std::max(cache_invalidate_index_, new_read_index);

  // Handle wrapping.
  if (new_read_index == item_capacity_) {
    new_read_index = 0;
    cache_invalidate_index_ = 0;
  }

  read_index_->store(new_read_index, std::memory_order::memory_order_release);
  return ZX_OK;
}

uint16_t ReadDmaRing::GetAvailableReads(uint16_t* out_read_index) {
  const uint16_t read_index = read_index_->load(std::memory_order::memory_order_acquire);
  const uint16_t write_index = write_index_->load(std::memory_order::memory_order_acquire);
  *out_read_index = read_index;

  if (read_index <= write_index) {
    return write_index - read_index;
  } else {
    // The ringbuffer does not wrap at the linear end, so we read only until then.
    return item_capacity_ - read_index;
  }
}

WriteDmaRing::WriteDmaRing(std::unique_ptr<DmaBuffer> dma_buffer, size_t item_size,
                           uint16_t item_capacity, volatile std::atomic<uint16_t>* read_index,
                           volatile std::atomic<uint16_t>* write_index,
                           volatile std::atomic<uint32_t>* write_signal)
    : BaseDmaRing(std::move(dma_buffer), item_size, item_capacity, read_index, write_index),
      write_signal_(write_signal) {}

WriteDmaRing::~WriteDmaRing() = default;

// static
zx_status_t WriteDmaRing::Create(std::unique_ptr<DmaBuffer> dma_buffer, size_t item_size,
                                 uint16_t item_capacity, volatile std::atomic<uint16_t>* read_index,
                                 volatile std::atomic<uint16_t>* write_index,
                                 volatile std::atomic<uint32_t>* write_signal,
                                 std::unique_ptr<WriteDmaRing>* out_write_dma_ring) {
  zx_status_t status = ZX_OK;

  if (dma_buffer->size() < item_size * item_capacity) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }
  if (dma_buffer->cache_policy() != ZX_CACHE_POLICY_CACHED) {
    // We handle cache flushes internally, so there is no need to use an uncached buffer.
    return ZX_ERR_INVALID_ARGS;
  }
  if ((status = dma_buffer->Map(ZX_VM_PERM_READ | ZX_VM_PERM_WRITE)) != ZX_OK) {
    return status;
  }

  out_write_dma_ring->reset(new WriteDmaRing(std::move(dma_buffer), item_size, item_capacity,
                                             read_index, write_index, write_signal));
  return ZX_OK;
}

uint16_t WriteDmaRing::GetAvailableWrites() {
  uint16_t write_index = 0;
  return GetAvailableWrites(&write_index);
}

zx_status_t WriteDmaRing::MapWrite(uint16_t item_count, void** out_buffer) {
  uint16_t write_index = 0;
  if (item_count > GetAvailableWrites(&write_index)) {
    return ZX_ERR_UNAVAILABLE;
  }

  // Before we make items available for the DMA device, we have to flush any new dirty writes from
  // the CPU cache.  To avoid redundant flushes, we only update `cache_clean_index_` here as the
  // high-water mark of our CPU writes; we flush it once when we are about to commit.
  const uint16_t end_index = write_index + item_count;
  cache_clean_index_ = std::max(cache_clean_index_, end_index);

  *out_buffer = reinterpret_cast<void*>(dma_buffer_->address() + write_index * item_size_);
  return ZX_OK;
}

zx_status_t WriteDmaRing::CommitWrite(uint16_t item_count) {
  zx_status_t status = ZX_OK;
  uint16_t write_index = 0;
  if (item_count > GetAvailableWrites(&write_index)) {
    return ZX_ERR_UNAVAILABLE;
  }
  uint16_t new_write_index = write_index + item_count;

  // Flush our new CPU-written entires from the cache, for DMA device access.
  std::atomic_thread_fence(std::memory_order::memory_order_release);
  const uint16_t cache_clean_end = std::min(cache_clean_index_, new_write_index);
  cache_clean_index_ = std::max(cache_clean_index_, new_write_index);
  if (cache_clean_end > write_index) {
    if ((status = zx_cache_flush(
             reinterpret_cast<const void*>(dma_buffer_->address() + write_index * item_size_),
             (cache_clean_end - write_index) * item_size_, ZX_CACHE_FLUSH_DATA)) != ZX_OK) {
      return status;
    }
  }

  // Handle wrapping.
  if (new_write_index == item_capacity_) {
    new_write_index = 0;
    cache_clean_index_ = 0;
  }

  write_index_->store(new_write_index, std::memory_order::memory_order_release);
  write_signal_->store(1, std::memory_order::memory_order_release);
  return ZX_OK;
}

uint16_t WriteDmaRing::GetAvailableWrites(uint16_t* out_write_index) {
  const uint16_t read_index = read_index_->load(std::memory_order::memory_order_acquire);
  const uint16_t write_index = write_index_->load(std::memory_order::memory_order_acquire);
  *out_write_index = write_index;

  if (write_index < read_index) {
    // We do not allow the ringbuffer to write completely, as that is indistinguishable from empty.
    return read_index - write_index - 1;
  } else {
    // The ringbuffer does not wrap at the linear end, so we write only until then.  Unless writing
    // until the linear end would cause the write pointer to wrap on top of the read pointer, in
    // which case we write one less.
    return std::min<uint16_t>(item_capacity_, read_index + item_capacity_ - 1) - write_index;
  }
}

}  // namespace brcmfmac
}  // namespace wlan
