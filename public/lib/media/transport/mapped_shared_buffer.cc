// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/media/transport/mapped_shared_buffer.h"

#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>
#include <zircon/types.h>

#include "lib/fxl/logging.h"
#include "lib/media/transport/fifo_allocator.h"

namespace media {
namespace {

constexpr uint64_t kMaxBufferLen = 0x3fffffffffffffff;

}  // namespace

MappedSharedBuffer::MappedSharedBuffer() {}

MappedSharedBuffer::~MappedSharedBuffer() { Reset(); }

zx_status_t MappedSharedBuffer::InitNew(uint64_t size, uint32_t map_flags) {
  FXL_DCHECK(size > 0);

  zx::vmo vmo;

  zx_status_t status = zx::vmo::create(size, 0, &vmo);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "zx::vmo::create failed, status " << status;
    return status;
  }

  // Allocate physical memory for the buffer.
  status = vmo.op_range(ZX_VMO_OP_COMMIT, 0u, size, nullptr, 0u);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "zx::vmo::op_range failed, status " << status;
    return status;
  }

  return InitInternal(std::move(vmo), map_flags);
}

zx_status_t MappedSharedBuffer::InitFromVmo(zx::vmo vmo, uint32_t map_flags) {
  return InitInternal(std::move(vmo), map_flags);
}

zx_status_t MappedSharedBuffer::InitInternal(zx::vmo vmo, uint32_t map_flags) {
  Reset();

  uint64_t size;
  zx_status_t status = vmo.get_size(&size);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "zx::vmo::get_size failed, status " << status;
    return status;
  }

  if (size == 0 || size > kMaxBufferLen) {
    FXL_LOG(ERROR) << "zx::vmo::get_size returned invalid size " << size;
    return ZX_ERR_OUT_OF_RANGE;
  }

  size_ = size;

  // TODO(dalesat): Map only for required operations (read or write).
  uintptr_t mapped_buffer = 0u;
  status =
      zx::vmar::root_self().map(0, vmo, 0u, size, map_flags, &mapped_buffer);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "zx::vmar::map failed, status " << status;
    return status;
  }

  buffer_ptr_ = reinterpret_cast<uint8_t*>(mapped_buffer);

  vmo_ = std::move(vmo);

  OnInit();

  return ZX_OK;
}

bool MappedSharedBuffer::initialized() const { return buffer_ptr_ != nullptr; }

void MappedSharedBuffer::Reset() {
  if (buffer_ptr_ != nullptr) {
    FXL_DCHECK(size_ != 0);
    zx_status_t status = zx::vmar::root_self().unmap(
        reinterpret_cast<uintptr_t>(buffer_ptr_), size_);
    FXL_CHECK(status == ZX_OK);
    buffer_ptr_ = nullptr;
  }

  size_ = 0;
  vmo_.reset();
}

uint64_t MappedSharedBuffer::size() const { return size_; }

zx::vmo MappedSharedBuffer::GetDuplicateVmo(zx_rights_t rights) const {
  FXL_DCHECK(initialized());
  zx::vmo vmo;
  zx_status_t status = vmo_.duplicate(rights, &vmo);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "zx::handle::duplicate failed, status " << status;
  }

  return vmo;
}

bool MappedSharedBuffer::Validate(uint64_t offset, uint64_t size) {
  FXL_DCHECK(buffer_ptr_ != nullptr);
  return offset < size_ && size <= size_ - offset;
}

void* MappedSharedBuffer::PtrFromOffset(uint64_t offset) const {
  FXL_DCHECK(buffer_ptr_ != nullptr);

  if (offset == FifoAllocator::kNullOffset) {
    return nullptr;
  }

  FXL_DCHECK(offset < size_);
  return buffer_ptr_ + offset;
}

uint64_t MappedSharedBuffer::OffsetFromPtr(void* ptr) const {
  FXL_DCHECK(buffer_ptr_ != nullptr);
  if (ptr == nullptr) {
    return FifoAllocator::kNullOffset;
  }
  uint64_t offset = reinterpret_cast<uint8_t*>(ptr) - buffer_ptr_;
  FXL_DCHECK(offset < size_);
  return offset;
}

void MappedSharedBuffer::OnInit() {}

}  // namespace media
