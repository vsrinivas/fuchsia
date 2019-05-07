// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <blobfs/vmo-buffer.h>

#include <utility>

#include <zircon/status.h>
#include <zircon/assert.h>

namespace blobfs {

VmoBuffer::VmoBuffer(VmoBuffer&& other)
    : space_manager_(std::move(other.space_manager_)),
      mapper_(std::move(other.mapper_)),
      vmoid_(other.vmoid_),
      capacity_(other.capacity_) {
    other.Reset();
}

VmoBuffer& VmoBuffer::operator=(VmoBuffer&& other) {
    if (&other != this) {
        space_manager_ = other.space_manager_;
        mapper_ = std::move(other.mapper_);
        vmoid_ = other.vmoid_;
        capacity_ = other.capacity_;

        other.Reset();
    }
    return *this;
}

VmoBuffer::~VmoBuffer() {
    if (vmoid_ != VMOID_INVALID) {
        space_manager_->DetachVmo(vmoid_);
    }
}

void VmoBuffer::Reset() {
    space_manager_ = nullptr;
    vmoid_ = VMOID_INVALID;
    capacity_ = 0;
}

zx_status_t VmoBuffer::Initialize(SpaceManager* space_manager, size_t blocks, const char* label) {
    ZX_DEBUG_ASSERT(vmoid_ == VMOID_INVALID);
    fzl::OwnedVmoMapper mapper;
    zx_status_t status = mapper.CreateAndMap(blocks * kBlobfsBlockSize, label);
    if (status != ZX_OK) {
        FS_TRACE_ERROR("VmoBuffer: Failed to create vmo %s: %s\n", label,
                       zx_status_get_string(status));
        return status;
    }

    vmoid_t vmoid;
    status = space_manager->AttachVmo(mapper.vmo(), &vmoid);
    if (status != ZX_OK) {
        FS_TRACE_ERROR("VmoBuffer: Failed to attach vmo %s: %s\n", label,
                       zx_status_get_string(status));
        return status;
    }

    space_manager_ = space_manager;
    mapper_ = std::move(mapper);
    vmoid_ = vmoid;
    capacity_ = mapper_.size() / kBlobfsBlockSize;
    return ZX_OK;
}

void* VmoBuffer::MutableData(size_t index) {
    ZX_DEBUG_ASSERT(index < capacity_);
    return reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(mapper_.start()) +
                                   (index * kBlobfsBlockSize));
}

} // namespace blobfs
