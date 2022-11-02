// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fzl/vmo-pool.h>
#include <lib/zx/vmar.h>
#include <string.h>

#include <utility>

namespace fzl {

VmoPool::Buffer::Buffer(VmoPool::Buffer&& other) noexcept
    : VmoPool::Buffer(other.pool_, other.index_) {
  other.pool_ = nullptr;
}

VmoPool::Buffer& VmoPool::Buffer::operator=(VmoPool::Buffer&& other) noexcept {
  if (valid()) {
    Release();
  }
  pool_ = other.pool_;
  index_ = other.index_;
  other.pool_ = nullptr;
  return *this;
}

size_t VmoPool::Buffer::size() const {
  ZX_ASSERT(valid());
  return pool_->buffers_[index_].buffer_size;
}

void* VmoPool::Buffer::virtual_address() const {
  ZX_ASSERT(valid());
  return pool_->buffers_[index_].virtual_address();
}

void* VmoPool::ListableBuffer::virtual_address() const {
  ZX_ASSERT_MSG(is_mapped, "Querying virtual address of unmapped Buffer.");
  return mapped_buffer.start();
}

zx_paddr_t VmoPool::Buffer::physical_address() const {
  ZX_ASSERT(valid());
  return pool_->buffers_[index_].physical_address();
}

zx_handle_t VmoPool::Buffer::vmo_handle() const {
  ZX_ASSERT(valid());
  return pool_->buffers_[index_].vmo.get();
}

zx_paddr_t VmoPool::ListableBuffer::physical_address() const {
  ZX_ASSERT_MSG(is_pinned, "Querying physical address of unpinned Buffer.");
  return pinned_buffer.region(0).phys_addr;
}

// Release the buffer from its write lock.  Returns the index
// of the buffer for use when signaling.
// This will also be called on destruction.
uint32_t VmoPool::Buffer::ReleaseWriteLockAndGetIndex() {
  ZX_ASSERT(valid());
  pool_ = nullptr;
  return index_;
}

// Releases buffer back to free pool.  This can be done regardless of
// write lock.
zx_status_t VmoPool::Buffer::Release() {
  ZX_ASSERT(valid());
  zx_status_t status = pool_->ReleaseBuffer(index_);
  pool_ = nullptr;
  return status;
}

VmoPool::Buffer::~Buffer() {
  if (valid()) {
    Release();
  }
}

VmoPool::~VmoPool() {
  // Clear out the free_buffers_, since the intrusive container
  // will throw an assert if it contains unmanaged pointers on
  // destruction.
  free_buffers_.clear_unsafe();
  for (auto& buffer : buffers_) {
    buffer.vmo.reset();
  }
}

zx_status_t VmoPool::ListableBuffer::PinVmo(const zx::bti& bti,
                                            VmoPool::RequireContig require_contiguous,
                                            VmoPool::RequireLowMem require_low_memory) {
  zx_status_t status;
  status = pinned_buffer.Pin(vmo, bti, ZX_BTI_CONTIGUOUS | ZX_VM_PERM_READ | ZX_VM_PERM_WRITE);
  if (status != ZX_OK) {
    return status;
  }
  if (static_cast<bool>(require_contiguous) && pinned_buffer.region_count() != 1) {
    return ZX_ERR_NO_MEMORY;
  }
  if (static_cast<bool>(require_low_memory) && pinned_buffer.region(0).phys_addr > UINT32_MAX) {
    return ZX_ERR_NO_MEMORY;
  }
  is_pinned = true;
  return ZX_OK;
}

zx_status_t VmoPool::ListableBuffer::MapVmo() {
  zx_status_t status = mapped_buffer.Map(vmo, 0, 0, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE);
  if (status == ZX_OK) {
    is_mapped = true;
  }
  return status;
}

zx_status_t VmoPool::Init(cpp20::span<zx::unowned_vmo> vmos) {
  fbl::AllocChecker ac;
  fbl::Array<ListableBuffer> buffers(new (&ac) ListableBuffer[vmos.size()], vmos.size());
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }
  buffers_ = std::move(buffers);
  free_buffers_.clear_unsafe();

  zx_status_t status = [&]() {
    for (size_t i = 0; i < vmos.size(); ++i) {
      free_buffers_.push_front(&buffers_[i]);
      if (zx_status_t status = vmos[i]->get_size(&buffers_[i].buffer_size); status != ZX_OK) {
        return status;
      }
      if (status = vmos[i]->duplicate(ZX_RIGHT_SAME_RIGHTS, &buffers_[i].vmo); status != ZX_OK) {
        return status;
      }
    }
    return ZX_OK;
  }();
  if (status != ZX_OK) {
    free_buffers_.clear_unsafe();
    buffers_.reset();
  }
  return status;
}

zx_status_t VmoPool::PinVmos(const zx::bti& bti, VmoPool::RequireContig req_contiguous,
                             VmoPool::RequireLowMem req_low_memory) {
  for (auto& buffer : buffers_) {
    zx_status_t status = buffer.PinVmo(bti, req_contiguous, req_low_memory);
    if (status != ZX_OK) {
      free_buffers_.clear_unsafe();
      buffers_.reset();
      return status;
    }
  }
  return ZX_OK;
}

zx_status_t VmoPool::MapVmos() {
  for (auto& buffer : buffers_) {
    zx_status_t status = buffer.MapVmo();
    if (status != ZX_OK) {
      free_buffers_.clear_unsafe();
      buffers_.reset();
      return status;
    }
  }
  return ZX_OK;
}

void VmoPool::Reset() {
  for (auto& buffer : buffers_) {
    if (!buffer.InContainer()) {
      free_buffers_.push_front(&buffer);
    }
  }
}

std::optional<VmoPool::Buffer> VmoPool::LockBufferForWrite() {
  if (free_buffers_.is_empty()) {  // No available buffers!
    return std::nullopt;
  }
  ListableBuffer* buf = free_buffers_.pop_front();
  ZX_DEBUG_ASSERT(buf >= buffers_.data());
  uint32_t buffer_offset = static_cast<uint32_t>(buf - buffers_.begin());
  ZX_DEBUG_ASSERT(buffer_offset < buffers_.size());
  return Buffer(this, buffer_offset);
}

zx_status_t VmoPool::ReleaseBuffer(uint32_t buffer_index) {
  if (buffer_index >= buffers_.size()) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (buffers_[buffer_index].InContainer()) {
    return ZX_ERR_NOT_FOUND;
  }

  free_buffers_.push_front(&buffers_[buffer_index]);
  return ZX_OK;
}
}  // namespace fzl
