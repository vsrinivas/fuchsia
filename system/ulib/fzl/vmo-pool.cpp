// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fzl/vmo-pool.h>
#include <lib/zx/vmar.h>
#include <string.h>

namespace fzl {

VmoPool::~VmoPool() {
    // Clear out the free_buffers_, since the intrusive container
    // will throw an assert if it contains unmanaged pointers on
    // destruction.
    free_buffers_.clear_unsafe();
}

zx_status_t VmoPool::Init(const fbl::Vector<zx::vmo>& vmos) {
    return Init(vmos.begin(), vmos.size());
}

zx_status_t VmoPool::Init(const zx::vmo* vmos, size_t num_vmos) {
    fbl::AllocChecker ac;
    fbl::Array<ListableBuffer> buffers(new (&ac) ListableBuffer[num_vmos], num_vmos);
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }
    buffers_ = fbl::move(buffers);

    zx_status_t status;
    for (size_t i = 0; i < num_vmos; ++i) {
        free_buffers_.push_front(&buffers_[i]);
        status = buffers_[i].buffer.Map(vmos[i], 0, 0, ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE);
        if (status != ZX_OK) {
            free_buffers_.clear_unsafe();
            buffers_.reset();
            return status;
        }
    }
    current_buffer_ = kInvalidCurBuffer;
    return ZX_OK;
}

void VmoPool::Reset() {
    current_buffer_ = kInvalidCurBuffer;
    for (size_t i = 0; i < buffers_.size(); ++i) {
        if (!buffers_[i].InContainer()) {
            free_buffers_.push_front(&buffers_[i]);
        }
    }
}

zx_status_t VmoPool::GetNewBuffer(uint32_t* buffer_index) {
    if (HasBufferInProgress()) {
        return ZX_ERR_BAD_STATE;
    }
    if (free_buffers_.is_empty()) { // No available buffers!
        return ZX_ERR_NOT_FOUND;
    }
    ListableBuffer* buf = free_buffers_.pop_front();
    ZX_DEBUG_ASSERT(buf >= &buffers_[0]);
    uint32_t buffer_offset = static_cast<uint32_t>(buf - &buffers_[0]);
    ZX_DEBUG_ASSERT(buffer_offset < buffers_.size());
    current_buffer_ = buffer_offset;
    if (buffer_index != nullptr) {
        *buffer_index = current_buffer_;
    }
    return ZX_OK;
}

zx_status_t VmoPool::BufferCompleted(uint32_t* buffer_index) {
    if (!HasBufferInProgress()) {
        return ZX_ERR_BAD_STATE;
    }
    if (buffer_index != nullptr) {
        *buffer_index = current_buffer_;
    }
    current_buffer_ = kInvalidCurBuffer;
    return ZX_OK;
}

zx_status_t VmoPool::BufferRelease(uint32_t buffer_index) {
    if (buffer_index >= buffers_.size()) {
        return ZX_ERR_INVALID_ARGS;
    }
    if (buffers_[buffer_index].InContainer()) {
        return ZX_ERR_NOT_FOUND;
    }
    // If we are cancelling the in-progress buffer:
    if (current_buffer_ == buffer_index) {
        current_buffer_ = kInvalidCurBuffer;
    }

    free_buffers_.push_front(&buffers_[buffer_index]);
    return ZX_OK;
}

uint64_t VmoPool::CurrentBufferSize() const {
    if (HasBufferInProgress()) {
        return buffers_[current_buffer_].buffer.size();
    }
    return 0;
}
void* VmoPool::CurrentBufferAddress() const {
    if (HasBufferInProgress()) {
        return buffers_[current_buffer_].buffer.start();
    }
    return nullptr;
}
} // namespace fzl
