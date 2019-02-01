// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ringbuffer.h"
#include "address_space.h"

Ringbuffer::Ringbuffer(std::unique_ptr<MsdIntelBuffer> buffer) : buffer_(std::move(buffer))
{
    size_ = buffer_->platform_buffer()->size();
    DASSERT(magma::is_page_aligned(size_));

    // Starting position is arbitrary; put it near the top to facilitate wrap testing.
    constexpr uint32_t kOffsetFromTop = PAGE_SIZE;
    DASSERT(size_ >= kOffsetFromTop);
    tail_ = size_ - kOffsetFromTop;
    head_ = tail_;
}

bool Ringbuffer::HasSpace(uint32_t bytes)
{
    // Can't fill completely such that tail_ == head_
    int32_t space = head_ - tail_ - 4;
    if (space <= 0)
        space += size_;
    bool ret = static_cast<uint32_t>(space) >= bytes;
    return DRETF(ret, "insufficient space: bytes 0x%x space 0x%x", bytes, space);
}

bool Ringbuffer::Map(std::shared_ptr<AddressSpace> address_space)
{
    DASSERT(!vaddr_);

    gpu_mapping_ = AddressSpace::MapBufferGpu(address_space, buffer_);
    if (!gpu_mapping_)
        return DRETF(false, "failed to pin");

    void* addr;
    if (!buffer_->platform_buffer()->MapCpu(&addr)) {
        gpu_mapping_ = nullptr;
        return DRETF(false, "failed to map");
    }

    vaddr_ = reinterpret_cast<uint32_t*>(addr);
    return true;
}

bool Ringbuffer::Unmap()
{
    DASSERT(vaddr_);

    if (!buffer_->platform_buffer()->UnmapCpu())
        return DRETF(false, "failed to unmap");

    gpu_mapping_.reset();

    return true;
}

bool Ringbuffer::GetGpuAddress(gpu_addr_t* addr_out)
{
    if (!gpu_mapping_)
        return DRETF(false, "not mapped");

    *addr_out = gpu_mapping_->gpu_addr();
    return true;
}
