// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_MAGMA_SRC_MAGMA_UTIL_RINGBUFFER_H_
#define SRC_GRAPHICS_LIB_MAGMA_SRC_MAGMA_UTIL_RINGBUFFER_H_

#include "accessor.h"
#include "address_space.h"
#include "dlog.h"
#include "instruction_writer.h"
#include "macros.h"

class TestRingbuffer;

namespace magma {

// Template class containing a ringbuffer of instructions, which can be mapped
// onto both the CPU and GPU.
template <typename GpuMapping>
class Ringbuffer : public InstructionWriter {
 public:
  Ringbuffer(std::unique_ptr<typename GpuMapping::BufferType>&& buffer, uint32_t start_offset);

  uint64_t size() { return size_; }

  uint32_t tail() { return tail_; }

  uint32_t head() { return head_; }

  void update_head(uint32_t head) {
    DASSERT((head & (sizeof(*vaddr_) - 1)) == 0);
    DASSERT(head < size_);
    DLOG("updating head 0x%x", head);
    head_ = head;
  }

  void Write32(uint32_t value) override;

  bool HasSpace(uint32_t bytes);

  // Maps to both CPU and GPU.
  bool Map(std::shared_ptr<AddressSpace<GpuMapping>> address_space, uint64_t* gpu_addr_out);
  bool MultiMap(std::shared_ptr<AddressSpace<GpuMapping>> address_space, uint64_t gpu_addr);
  bool Unmap();

 protected:
  uint32_t* vaddr() { return vaddr_; }

  void update_tail(uint32_t tail) {
    DASSERT((tail & (sizeof(*vaddr_) - 1)) == 0);
    DASSERT(tail < size_);
    DLOG("updating tail 0x%x", tail);
    tail_ = tail;
  }

 private:
  std::shared_ptr<typename GpuMapping::BufferType> buffer_;
  std::vector<std::shared_ptr<GpuMapping>> gpu_mappings_;
  uint64_t size_;
  uint32_t head_;
  uint32_t tail_;
  uint32_t* vaddr_{};  // mapped virtual address

  friend class ::TestRingbuffer;
};

template <typename GpuMapping>
Ringbuffer<GpuMapping>::Ringbuffer(std::unique_ptr<typename GpuMapping::BufferType>&& buffer,
                                   uint32_t start_offset)
    : buffer_(std::move(buffer)) {
  size_ = BufferAccessor<typename GpuMapping::BufferType>::platform_buffer(buffer_.get())->size();
  DASSERT(magma::is_page_aligned(size_));

  DASSERT(size_ > start_offset);
  tail_ = start_offset;
  head_ = tail_;
}

template <typename GpuMapping>
void Ringbuffer<GpuMapping>::Write32(uint32_t value) {
  DASSERT(vaddr_);
  // Note vaddr_ is an array of 32-bit=4 byte values
  vaddr_[tail_ >> 2] = value;
  tail_ += sizeof(value);
  if (tail_ >= size_) {
    DLOG("ringbuffer tail wrapped");
    tail_ = 0;
  }
  DASSERT(tail_ != head_);
}

template <typename GpuMapping>
bool Ringbuffer<GpuMapping>::HasSpace(uint32_t bytes) {
  // Can't fill completely such that tail_ == head_
  int32_t space = head_ - tail_ - sizeof(uint32_t);
  if (space <= 0)
    space += size_;
  bool ret = static_cast<uint32_t>(space) >= bytes;
  return DRETF(ret, "Insufficient space: bytes 0x%x space 0x%x", bytes, space);
}

template <typename GpuMapping>
bool Ringbuffer<GpuMapping>::Map(std::shared_ptr<AddressSpace<GpuMapping>> address_space,
                                 uint64_t* gpu_addr_out) {
  DASSERT(!vaddr_);

  auto gpu_mapping = AddressSpace<GpuMapping>::MapBufferGpu(address_space, buffer_);
  if (!gpu_mapping)
    return DRETF(false, "MapBufferGpu failed");

  void* addr;
  if (!BufferAccessor<typename GpuMapping::BufferType>::platform_buffer(buffer_.get())
           ->MapCpu(&addr)) {
    return DRETF(false, "MapCpu failed");
  }

  vaddr_ = reinterpret_cast<uint32_t*>(addr);

  *gpu_addr_out = gpu_mapping->gpu_addr();
  gpu_mappings_.push_back(std::move(gpu_mapping));

  return true;
}

template <typename GpuMapping>
bool Ringbuffer<GpuMapping>::MultiMap(std::shared_ptr<AddressSpace<GpuMapping>> address_space,
                                      uint64_t gpu_addr) {
  // Check if the address space is already mapped.
  // If an address space is already dead, we can also drop the mapping to avoid forever
  // accumulating more gpu mappings.
  bool found_mapping = false;
  unsigned int i = 0;
  while (i < gpu_mappings_.size()) {
    auto mapped_address_space = gpu_mappings_[i]->address_space().lock();
    if (!mapped_address_space) {
      gpu_mappings_.erase(gpu_mappings_.begin() + i);
      continue;
    }
    if (mapped_address_space.get() == address_space.get()) {
      found_mapping = true;
    }
    i++;
  }
  if (found_mapping) {
    return DRETF(false, "Ringbuffer was already mapped in address space %p\n", address_space.get());
  }

  DASSERT(magma::is_page_aligned(size_));
  uint64_t page_count = size_ / magma::page_size();

  std::shared_ptr<GpuMapping> gpu_mapping;
  magma::Status status = AddressSpace<GpuMapping>::MapBufferGpu(
      address_space, buffer_, gpu_addr, 0 /* page_offset */, page_count, &gpu_mapping);
  if (!status.ok()) {
    return DRET_MSG(status.get(), "MapBufferGpu failed");
  }
  DASSERT(gpu_mapping);

  if (!vaddr_) {
    void* addr;
    if (!BufferAccessor<typename GpuMapping::BufferType>::platform_buffer(buffer_.get())
             ->MapCpu(&addr)) {
      return DRETF(false, "MapCpu failed");
    }

    vaddr_ = reinterpret_cast<uint32_t*>(addr);
  }

  gpu_mappings_.push_back(std::move(gpu_mapping));

  return true;
}

template <typename GpuMapping>
bool Ringbuffer<GpuMapping>::Unmap() {
  DASSERT(vaddr_);

  if (!buffer_->platform_buffer()->UnmapCpu())
    return DRETF(false, "UnmapCpu failed");

  gpu_mappings_.clear();

  return true;
}

}  // namespace magma

#endif  // SRC_GRAPHICS_LIB_MAGMA_SRC_MAGMA_UTIL_RINGBUFFER_H_
