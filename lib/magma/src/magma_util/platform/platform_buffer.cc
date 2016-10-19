// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "platform_buffer.h"
#include "magma_util/dlog.h"
#include "magma_util/macros.h"

namespace magma {

std::unique_ptr<PlatformBuffer> PlatformBuffer::Create(uint64_t size,
                                                       msd_platform_buffer** token_out)
{
    msd_platform_buffer* token;
    uint32_t handle;

    if (msd_platform_buffer_alloc(&token, size, &size, &handle) == 0) {
        auto buffer = new PlatformBuffer(token, size, handle);
        if (!buffer)
            return DRETP(nullptr, "Couldn't allocated PlatformBuffer");
        *token_out = token;
        return std::unique_ptr<PlatformBuffer>(buffer);
    }

    return nullptr;
}

std::unique_ptr<PlatformBuffer> PlatformBuffer::Create(uint64_t size)
{
    msd_platform_buffer* dummy;
    return Create(size, &dummy);
}

std::unique_ptr<PlatformBuffer> PlatformBuffer::Import(uint32_t handle)
{
    msd_platform_buffer* token;
    if (msd_platform_buffer_import(&token, handle) != 0)
        return DRETP(nullptr, "failed to import platform buffer");

    uint64_t size;
    if (msd_platform_buffer_get_size(token, &size))
        return DRETP(nullptr, "failed to get platform buffer size");

    return std::unique_ptr<PlatformBuffer>(new PlatformBuffer(token, size, handle));
}

std::unique_ptr<PlatformBuffer> PlatformBuffer::Create(msd_platform_buffer* token)
{
    uint64_t size;
    uint32_t handle;

    if (msd_platform_buffer_get_size(token, &size) != 0)
        return DRETP(nullptr, "Couldn't get buffer size");

    if (msd_platform_buffer_get_handle(token, &handle) != 0)
        return DRETP(nullptr, "Couldn't get buffer handle");

    auto buffer = new PlatformBuffer(token, size, handle);
    if (!buffer)
        return DRETP(nullptr, "Couldn't create PlatformBuffer");

    msd_platform_buffer_incref(token);

    return std::unique_ptr<PlatformBuffer>(buffer);
}

PlatformBuffer::~PlatformBuffer()
{
    DLOG("PlatformBuffer dtor handle 0x%x", handle_);
    msd_platform_buffer_decref(token_);
}

PlatformBuffer::PlatformBuffer(msd_platform_buffer* token, uint64_t size, uint32_t handle)
    : token_(token), size_(size), handle_(handle)
{
    DLOG("PlatformBuffer ctor size 0x%llx handle 0x%x", size, handle);
}

bool PlatformBuffer::MapCpu(void** addr_out)
{
    return msd_platform_buffer_map_cpu(token_, addr_out) == 0;
}

bool PlatformBuffer::UnmapCpu() { return msd_platform_buffer_unmap_cpu(token_) == 0; }

bool PlatformBuffer::PinPages(uint32_t start_page_index, uint32_t page_count)
{
    return msd_platform_buffer_pin_pages(token_, start_page_index, page_count) == 0;
}

bool PlatformBuffer::UnpinPages(uint32_t start_page_index, uint32_t page_count)
{
    return msd_platform_buffer_unpin_pages(token_, start_page_index, page_count) == 0;
}

bool PlatformBuffer::MapPageCpu(unsigned int page_index, void** addr_out)
{
    return msd_platform_buffer_map_page_cpu(token_, page_index, addr_out) == 0;
}

bool PlatformBuffer::UnmapPageCpu(unsigned int page_index)
{
    return msd_platform_buffer_unmap_page_cpu(token_, page_index) == 0;
}

bool PlatformBuffer::MapPageBus(unsigned int page_index, uint64_t* addr_out)
{
    return msd_platform_buffer_map_page_bus(token_, page_index, addr_out) == 0;
}

bool PlatformBuffer::UnmapPageBus(unsigned int page_index)
{
    return msd_platform_buffer_unmap_page_bus(token_, page_index) == 0;
}

uint32_t PlatformBuffer::GetRefCount() { return msd_platform_buffer_getref(token_); }

} // namespace
