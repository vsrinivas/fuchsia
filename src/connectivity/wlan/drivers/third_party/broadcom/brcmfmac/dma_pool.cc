// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/dma_pool.h"

#include <zircon/errors.h>
#include <zircon/syscalls.h>

#include <algorithm>
#include <cstddef>
#include <limits>
#include <utility>

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/debug.h"

namespace wlan {
namespace brcmfmac {

DmaPool::Buffer::Buffer() = default;

DmaPool::Buffer::Buffer(DmaPool* parent, int index, size_t read_size, size_t write_size)
    : parent_(parent), index_(index), read_size_(read_size), write_size_(write_size) {}

DmaPool::Buffer::Buffer(Buffer&& other) { swap(*this, other); }

DmaPool::Buffer& DmaPool::Buffer::operator=(Buffer other) {
  swap(*this, other);
  return *this;
}

void swap(DmaPool::Buffer& lhs, DmaPool::Buffer& rhs) {
  using std::swap;
  swap(lhs.parent_, rhs.parent_);
  swap(lhs.index_, rhs.index_);
  swap(lhs.read_size_, rhs.read_size_);
  swap(lhs.write_size_, rhs.write_size_);
}

DmaPool::Buffer::~Buffer() { Reset(); }

bool DmaPool::Buffer::is_valid() const { return parent_ != nullptr; }

int DmaPool::Buffer::index() const { return index_; }

size_t DmaPool::Buffer::size() const { return parent_->buffer_size(); }

zx_status_t DmaPool::Buffer::MapRead(size_t read_size, const void** out_data) {
  zx_status_t status = ZX_OK;

  if (parent_ == nullptr) {
    return ZX_ERR_BAD_STATE;
  }
  if (read_size > parent_->buffer_size()) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  const void* const address = parent_->GetAddress(index_);

  // If this Buffer was returned from the device recently, its data may not yet be coherent in the
  // CPU cache.  We handle this by performing an explicit cache invalidation on the buffer;
  // `read_size_` is the high-water mark for these invalidations so that we do not redundantly
  // perform it for portions of the buffers that have already been processed.
  if (read_size > read_size_) {
    if ((status = zx_cache_flush(
             reinterpret_cast<const void*>(reinterpret_cast<uintptr_t>(address) + read_size_),
             read_size - read_size_, ZX_CACHE_FLUSH_DATA | ZX_CACHE_FLUSH_INVALIDATE)) != ZX_OK) {
      return status;
    }
    std::atomic_thread_fence(std::memory_order::memory_order_acquire);
    read_size_ = read_size;
  }

  *out_data = address;
  return ZX_OK;
}

zx_status_t DmaPool::Buffer::MapWrite(size_t write_size, void** out_data) {
  if (parent_ == nullptr) {
    return ZX_ERR_BAD_STATE;
  }
  if (write_size > parent_->buffer_size()) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  // Before we make items available for the DMA device, we have to flush any new dirty writes from
  // the CPU cache.  To avoid redundant flushes, we only update `write_size_` here as the high-water
  // mark of our CPU writes; we flush it once we are about to pin it for the DMA device.
  write_size_ = std::max(write_size_, write_size);

  *out_data = parent_->GetAddress(index_);
  return ZX_OK;
}

zx_status_t DmaPool::Buffer::Pin(zx_paddr_t* out_dma_address) {
  zx_status_t status = ZX_OK;

  if (parent_ == nullptr) {
    return ZX_ERR_BAD_STATE;
  }

  const void* const address = parent_->GetAddress(index_);

  // Once we pin this buffer for device access, the buffer will need to be invalidated for read, to
  // pick up any new data written by the device.
  read_size_ = 0;

  // New data written by the CPU for the device now needs to be flushed from the cache.
  if (write_size_ > 0) {
    std::atomic_thread_fence(std::memory_order::memory_order_release);
    if ((status = zx_cache_flush(address, write_size_, ZX_CACHE_FLUSH_DATA)) != ZX_OK) {
      return status;
    }
    write_size_ = 0;
  }

  *out_dma_address = parent_->GetDmaAddress(index_);
  return ZX_OK;
}

void DmaPool::Buffer::Release() {
  parent_ = nullptr;
  index_ = kInvalidIndex;
  read_size_ = 0;
  write_size_ = 0;
}

void DmaPool::Buffer::Reset() {
  if (parent_ != nullptr) {
    parent_->Return(index_);
  }
  parent_ = nullptr;
  index_ = kInvalidIndex;
  read_size_ = 0;
  write_size_ = 0;
}

DmaPool::DmaPool() = default;

DmaPool::DmaPool(DmaPool&& other) { swap(*this, other); };

DmaPool& DmaPool::operator=(DmaPool other) {
  swap(*this, other);
  return *this;
}

void swap(DmaPool& lhs, DmaPool& rhs) {
  using std::swap;
  swap(lhs.buffer_size_, rhs.buffer_size_);
  swap(lhs.buffer_count_, rhs.buffer_count_);
  swap(lhs.dma_allocation_, rhs.dma_allocation_);
  swap(lhs.records_, rhs.records_);
  const DmaPool::ListHead lhs_next_free_record =
      lhs.next_free_record_.load(std::memory_order::memory_order_relaxed);
  const DmaPool::ListHead rhs_next_free_record =
      rhs.next_free_record_.load(std::memory_order::memory_order_relaxed);
  lhs.next_free_record_.store(rhs_next_free_record, std::memory_order::memory_order_relaxed);
  rhs.next_free_record_.store(lhs_next_free_record, std::memory_order::memory_order_relaxed);
}

DmaPool::~DmaPool() = default;

// static
zx_status_t DmaPool::Create(size_t buffer_size, int buffer_count,
                            std::unique_ptr<DmaBuffer> dma_buffer,
                            std::unique_ptr<DmaPool>* out_dma_pool) {
  zx_status_t status = ZX_OK;
  static_assert(std::atomic<ListHead>::is_always_lock_free, "ListHead is not always lock-free");

  if (buffer_size == 0 || buffer_count == 0) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (dma_buffer->size() < buffer_size * buffer_count) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }
  if (dma_buffer->cache_policy() != ZX_CACHE_POLICY_CACHED) {
    // We handle cache invalidations internally, so there is no need to use an uncached buffer.
    return ZX_ERR_INVALID_ARGS;
  }
  if ((status = dma_buffer->Map(ZX_VM_PERM_READ | ZX_VM_PERM_WRITE) != ZX_OK)) {
    return status;
  }

  std::vector<DmaPool::Record> records(buffer_count);
  for (size_t i = 0; i < records.size() - 1; ++i) {
    records[i].next_free = &records[i + 1];
    records[i].state.store(Record::State::kFree, std::memory_order::memory_order_relaxed);
  }
  records[records.size() - 1].next_free = nullptr;
  records[records.size() - 1].state.store(Record::State::kFree,
                                          std::memory_order::memory_order_relaxed);

  std::unique_ptr<DmaPool> dma_pool(new DmaPool());
  dma_pool->buffer_size_ = buffer_size;
  dma_pool->buffer_count_ = buffer_count;
  dma_pool->dma_allocation_ = std::move(dma_buffer);
  dma_pool->records_ = std::move(records);
  ListHead head = {};
  head.record = &dma_pool->records_[0];
  head.aba_state = 0;
  dma_pool->next_free_record_.store(head, std::memory_order::memory_order_release);
  *out_dma_pool = std::move(dma_pool);
  return ZX_OK;
}

size_t DmaPool::buffer_size() const { return buffer_size_; }

int DmaPool::buffer_count() const { return buffer_count_; }

zx_status_t DmaPool::Allocate(Buffer* out_buffer) {
  // Find a free buffer on the stack, locklessly.  We avoid the ABA problem using ListHead as a
  // tagged pointer.
  ListHead head = next_free_record_.load(std::memory_order::memory_order_relaxed);
  while (true) {
    if (head.record == nullptr) {
      return ZX_ERR_NO_RESOURCES;
    }
    ListHead next = {};
    next.record = head.record->next_free;
    next.aba_state = head.aba_state + 1;
    if (next_free_record_.compare_exchange_weak(head, next,
                                                std::memory_order::memory_order_acq_rel)) {
      // Success!
      break;
    }
  }

  Record* const record = head.record;
  record->next_free = nullptr;
  record->state.store(Record::State::kAllocated, std::memory_order::memory_order_release);
  const auto index = record - &records_[0];
  if (index < 0 || index > std::numeric_limits<int>::max()) {
    BRCMF_ERR("DmaPool::Buffer cannot be created, invalid index (out of range)");
    return ZX_ERR_INTERNAL;
  }
  // This is a new allocation, so we disregard its existing contents.  Setting `read_size` to
  // `buffer_size_` will skip the cache invalidations for CPU access.
  const size_t read_size = buffer_size_;
  const size_t write_size = 0;
  *out_buffer = Buffer(this, static_cast<int>(index), read_size, write_size);
  return ZX_OK;
}

zx_status_t DmaPool::Acquire(int index, Buffer* out_buffer) {
  if (static_cast<size_t>(index) >= records_.size()) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  Record* const record = &records_[index];
  if (record->state.load(std::memory_order::memory_order_acquire) != Record::State::kAllocated) {
    switch (record->state) {
      case Record::State::kFree:
        return ZX_ERR_NOT_FOUND;
      default:
        return ZX_ERR_BAD_STATE;
    };
  }

  // This is an acquired allocation, so its existing device-written contents may be important.
  // Setting `read_size` to 0 ensures that we will cache-invalidate before attempting CPU access.
  const size_t read_size = 0;
  const size_t write_size = 0;
  *out_buffer = Buffer(this, index, read_size, write_size);
  return ZX_OK;
}

void* DmaPool::GetAddress(int index) const {
  return reinterpret_cast<void*>(dma_allocation_->address() + buffer_size_ * index);
}

zx_paddr_t DmaPool::GetDmaAddress(int index) const {
  return static_cast<zx_paddr_t>(dma_allocation_->dma_address() + buffer_size_ * index);
}

void DmaPool::Return(int index) {
  Record* const record = &records_[index];
  record->state.store(Record::State::kFree, std::memory_order::memory_order_release);
  ListHead head = next_free_record_.load(std::memory_order::memory_order_relaxed);
  while (true) {
    ListHead next = {};
    record->next_free = head.record;
    next.record = record;
    next.aba_state = head.aba_state + 1;
    if (next_free_record_.compare_exchange_weak(head, next,
                                                std::memory_order::memory_order_acq_rel)) {
      // Success!
      return;
    }
  }
}

}  // namespace brcmfmac
}  // namespace wlan
