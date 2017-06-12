// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/lib/transport/mapped_shared_buffer.h"

#include <magenta/types.h>
#include <mx/vmar.h>
#include <mx/vmo.h>

#include "apps/media/lib/transport/fifo_allocator.h"
#include "apps/media/services/media_transport.fidl.h"
#include "lib/ftl/logging.h"

namespace media {

MappedSharedBuffer::MappedSharedBuffer() {}

MappedSharedBuffer::~MappedSharedBuffer() {
  Reset();
}

mx_status_t MappedSharedBuffer::InitNew(uint64_t size, uint32_t map_flags) {
  FTL_DCHECK(size > 0);

  mx::vmo vmo;

  mx_status_t status = mx::vmo::create(size, 0, &vmo);
  if (status != MX_OK) {
    FTL_LOG(ERROR) << "mx::vmo::create failed, status " << status;
    return status;
  }

  // Allocate physical memory for the buffer.
  status = vmo.op_range(MX_VMO_OP_COMMIT, 0u, size, nullptr, 0u);
  if (status != MX_OK) {
    FTL_LOG(ERROR) << "mx::vmo::op_range failed, status " << status;
    return status;
  }

  return InitInternal(std::move(vmo), map_flags);
}

mx_status_t MappedSharedBuffer::InitFromVmo(mx::vmo vmo, uint32_t map_flags) {
  return InitInternal(std::move(vmo), map_flags);
}

mx_status_t MappedSharedBuffer::InitInternal(mx::vmo vmo, uint32_t map_flags) {
  Reset();

  uint64_t size;
  mx_status_t status = vmo.get_size(&size);
  if (status != MX_OK) {
    FTL_LOG(ERROR) << "mx::vmo::get_size failed, status " << status;
    return status;
  }

  if (size == 0 || size > MediaPacketConsumer::kMaxBufferLen) {
    FTL_LOG(ERROR) << "mx::vmo::get_size returned invalid size " << size;
    return MX_ERR_OUT_OF_RANGE;
  }

  size_ = size;

  // TODO(dalesat): Map only for required operations (read or write).
  uintptr_t mapped_buffer = 0u;
  status =
      mx::vmar::root_self().map(0, vmo, 0u, size, map_flags, &mapped_buffer);
  if (status != MX_OK) {
    FTL_LOG(ERROR) << "mx::vmar::map failed, status " << status;
    return status;
  }

  buffer_ptr_ = reinterpret_cast<uint8_t*>(mapped_buffer);

  vmo_ = std::move(vmo);

  OnInit();

  return MX_OK;
}

bool MappedSharedBuffer::initialized() const {
  return buffer_ptr_ != nullptr;
}

void MappedSharedBuffer::Reset() {
  if (buffer_ptr_ != nullptr) {
    FTL_DCHECK(size_ != 0);
    mx_status_t status = mx::vmar::root_self().unmap(
        reinterpret_cast<uintptr_t>(buffer_ptr_), size_);
    FTL_CHECK(status == MX_OK);
    buffer_ptr_ = nullptr;
  }

  size_ = 0;
  vmo_.reset();
}

uint64_t MappedSharedBuffer::size() const {
  return size_;
}

mx::vmo MappedSharedBuffer::GetDuplicateVmo(mx_rights_t rights) const {
  FTL_DCHECK(initialized());
  mx::vmo vmo;
  mx_status_t status = vmo_.duplicate(rights, &vmo);
  if (status != MX_OK) {
    FTL_LOG(ERROR) << "mx::handle::duplicate failed, status " << status;
  }

  return vmo;
}

bool MappedSharedBuffer::Validate(uint64_t offset, uint64_t size) {
  FTL_DCHECK(buffer_ptr_ != nullptr);
  return offset < size_ && size <= size_ - offset;
}

void* MappedSharedBuffer::PtrFromOffset(uint64_t offset) const {
  FTL_DCHECK(buffer_ptr_ != nullptr);

  if (offset == FifoAllocator::kNullOffset) {
    return nullptr;
  }

  FTL_DCHECK(offset < size_);
  return buffer_ptr_ + offset;
}

uint64_t MappedSharedBuffer::OffsetFromPtr(void* ptr) const {
  FTL_DCHECK(buffer_ptr_ != nullptr);
  if (ptr == nullptr) {
    return FifoAllocator::kNullOffset;
  }
  uint64_t offset = reinterpret_cast<uint8_t*>(ptr) - buffer_ptr_;
  FTL_DCHECK(offset < size_);
  return offset;
}

void MappedSharedBuffer::OnInit() {}

}  // namespace media
