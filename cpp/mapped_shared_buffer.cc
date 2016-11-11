// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/cpp/mapped_shared_buffer.h"

#include <magenta/types.h>
#include <mx/process.h>
#include <mx/vmo.h>

#include "apps/media/cpp/fifo_allocator.h"
#include "apps/media/services/media_transport.fidl.h"
#include "lib/ftl/logging.h"

namespace media {

MappedSharedBuffer::MappedSharedBuffer() {}

MappedSharedBuffer::~MappedSharedBuffer() {
  Reset();
}

mx_status_t MappedSharedBuffer::InitNew(uint64_t size) {
  FTL_DCHECK(size > 0);

  mx::vmo vmo;

  mx_status_t status = mx::vmo::create(size, 0, &vmo);
  if (status != NO_ERROR) {
    FTL_LOG(ERROR) << "mx::vmo::create failed, status " << status;
    return status;
  }

  // Allocate physical memory for the buffer.
  status = vmo.op_range(MX_VMO_OP_COMMIT, 0u, size, nullptr, 0u);
  if (status != NO_ERROR) {
    FTL_LOG(ERROR) << "mx::vmo::op_range failed, status " << status;
    return status;
  }

  return InitInternal(std::move(vmo));
}

mx_status_t MappedSharedBuffer::InitFromVmo(mx::vmo vmo) {
  return InitInternal(std::move(vmo));
}

mx_status_t MappedSharedBuffer::InitInternal(mx::vmo vmo) {
  uint64_t size;
  mx_status_t status = vmo.get_size(&size);
  if (status != NO_ERROR) {
    FTL_LOG(ERROR) << "mx::vmo::get_size failed, status " << status;
    return status;
  }

  if (size == 0 || size > MediaPacketConsumer::kMaxBufferLen) {
    FTL_LOG(ERROR) << "mx::vmo::get_size returned invalid size " << size;
    return ERR_OUT_OF_RANGE;
  }

  size_ = size;
  buffer_ptr_.reset();

  // TODO(dalesat): Map only for required operations (read or write).
  uintptr_t mapped_buffer = 0u;
  status =
      mx::process::self().map_vm(vmo, 0u, size, &mapped_buffer,
                                 MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE);
  if (status != NO_ERROR) {
    FTL_LOG(ERROR) << "mx::process::map_vm failed, status " << status;
    return status;
  }

  buffer_ptr_.reset(reinterpret_cast<uint8_t*>(mapped_buffer));

  vmo_ = std::move(vmo);

  OnInit();

  return NO_ERROR;
}

bool MappedSharedBuffer::initialized() const {
  return buffer_ptr_ != nullptr;
}

void MappedSharedBuffer::Reset() {
  size_ = 0;
  vmo_.reset();
  buffer_ptr_.reset();
}

uint64_t MappedSharedBuffer::size() const {
  return size_;
}

mx::vmo MappedSharedBuffer::GetDuplicateVmo() const {
  FTL_DCHECK(initialized());
  mx::vmo vmo;
  // TODO(dalesat): Limit rights depending on usage.
  mx_status_t status = vmo_.duplicate(MX_RIGHT_SAME_RIGHTS, &vmo);
  if (status != NO_ERROR) {
    FTL_LOG(ERROR) << "mx::handle::duplicate failed, status " << status;
  }

  return vmo;
}

bool MappedSharedBuffer::Validate(uint64_t offset, uint64_t size) {
  FTL_DCHECK(buffer_ptr_);
  return offset < size_ && size <= size_ - offset;
}

void* MappedSharedBuffer::PtrFromOffset(uint64_t offset) const {
  FTL_DCHECK(buffer_ptr_);

  if (offset == FifoAllocator::kNullOffset) {
    return nullptr;
  }

  FTL_DCHECK(offset < size_);
  return buffer_ptr_.get() + offset;
}

uint64_t MappedSharedBuffer::OffsetFromPtr(void* ptr) const {
  FTL_DCHECK(buffer_ptr_);
  if (ptr == nullptr) {
    return FifoAllocator::kNullOffset;
  }
  uint64_t offset = reinterpret_cast<uint8_t*>(ptr) - buffer_ptr_.get();
  FTL_DCHECK(offset < size_);
  return offset;
}

void MappedSharedBuffer::OnInit() {}

}  // namespace media
