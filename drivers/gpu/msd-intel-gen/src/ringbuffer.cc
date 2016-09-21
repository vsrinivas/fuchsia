// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ringbuffer.h"

Ringbuffer::Ringbuffer(std::unique_ptr<MsdIntelBuffer> buffer) : buffer_(std::move(buffer))
{
    size_ = buffer_->platform_buffer()->size();
    DASSERT(magma::is_page_aligned(size_));
}

bool Ringbuffer::HasSpace(uint32_t bytes)
{
    // Can't fill completely such that tail_ == head_
    int32_t space = head_ - tail_ - 4;
    if (space <= 0)
        space += size_;
    bool ret = static_cast<uint32_t>(space) >= bytes;
    if (!ret)
        DLOG("HasSpace: bytes 0x%x space 0x%x", bytes, space);
    return DRETF(ret, "insufficient space");
}

bool Ringbuffer::Map(AddressSpace* address_space)
{
    DASSERT(!vaddr_);

    if (!buffer()->MapGpu(address_space, PAGE_SIZE))
        return DRETF(false, "failed to pin");

    void* addr;
    if (!buffer()->platform_buffer()->MapCpu(&addr)) {
        if (!buffer()->UnmapGpu(address_space))
            DLOG("failed to unpin");
        return DRETF(false, "failed to map");
    }

    vaddr_ = reinterpret_cast<uint32_t*>(addr);
    return true;
}

bool Ringbuffer::Unmap(AddressSpace* address_space)
{
    DASSERT(vaddr_);

    bool ret = true;

    if (!buffer()->UnmapGpu(address_space)) {
        DLOG("failed to unpin");
        ret = false;
    }

    if (!buffer()->platform_buffer()->UnmapCpu()) {
        DLOG("failed to unmap");
        ret = false;
    }

    return DRETF(ret, "error");
}

bool Ringbuffer::GetGpuAddress(AddressSpaceId id, gpu_addr_t* addr_out)
{
    return buffer()->GetGpuAddress(id, addr_out);
}
