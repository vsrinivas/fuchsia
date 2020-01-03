// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "storage/buffer/vmo_buffer.h"

#include <zircon/assert.h>
#include <zircon/status.h>

#include <utility>

#include <fs/trace.h>

namespace storage {

VmoBuffer::VmoBuffer(VmoBuffer&& other)
    : vmoid_registry_(std::move(other.vmoid_registry_)),
      mapper_(std::move(other.mapper_)),
      vmoid_(other.vmoid_),
      block_size_(other.block_size_),
      capacity_(other.capacity_) {
  other.Reset();
}

VmoBuffer& VmoBuffer::operator=(VmoBuffer&& other) {
  if (&other != this) {
    vmoid_registry_ = other.vmoid_registry_;
    mapper_ = std::move(other.mapper_);
    vmoid_ = other.vmoid_;
    block_size_ = other.block_size_;
    capacity_ = other.capacity_;

    other.Reset();
  }
  return *this;
}

VmoBuffer::~VmoBuffer() {
  if (vmoid_ != BLOCK_VMOID_INVALID) {
    vmoid_registry_->DetachVmo(vmoid_);
  }
}

void VmoBuffer::Reset() {
  vmoid_registry_ = nullptr;
  mapper_.Reset();
  vmoid_ = BLOCK_VMOID_INVALID;
  capacity_ = 0;
}

zx_status_t VmoBuffer::Initialize(VmoidRegistry* vmoid_registry, size_t blocks, uint32_t block_size,
                                  const char* label) {
  ZX_DEBUG_ASSERT(vmoid_ == BLOCK_VMOID_INVALID);
  fzl::OwnedVmoMapper mapper;
  zx_status_t status = mapper.CreateAndMap(blocks * block_size, label);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("VmoBuffer: Failed to create vmo %s: %s\n", label, zx_status_get_string(status));
    return status;
  }

  vmoid_t vmoid;
  status = vmoid_registry->AttachVmo(mapper.vmo(), &vmoid);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("VmoBuffer: Failed to attach vmo %s: %s\n", label, zx_status_get_string(status));
    return status;
  }

  vmoid_registry_ = vmoid_registry;
  mapper_ = std::move(mapper);
  vmoid_ = vmoid;
  block_size_ = block_size;
  capacity_ = mapper_.size() / block_size;
  return ZX_OK;
}

void* VmoBuffer::Data(size_t index) {
  return const_cast<void*>(const_cast<const VmoBuffer*>(this)->Data(index));
}

const void* VmoBuffer::Data(size_t index) const {
  ZX_DEBUG_ASSERT(index < capacity_);
  return reinterpret_cast<const void*>(reinterpret_cast<uintptr_t>(mapper_.start()) +
                                       (index * block_size_));
}

}  // namespace storage
