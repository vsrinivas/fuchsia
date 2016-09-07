// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mock_platform_buffer.h"
#include <errno.h>

uint32_t MockPlatformBuffer::handle_count_ = 0;

int32_t msd_platform_buffer_alloc(struct msd_platform_buffer** buffer_out, uint64_t size,
                                  uint64_t* size_out, uint32_t* handle_out)
{
    if (size == 0) {
        return DRET(-EINVAL);
    }
    auto buffer = new MockPlatformBuffer(size);
    *buffer_out = buffer;
    *size_out = buffer->size();
    *handle_out = buffer->handle();

    return 0;
}

void msd_platform_buffer_incref(struct msd_platform_buffer* buffer)
{
    MockPlatformBuffer::cast(buffer)->Incref();
}

void msd_platform_buffer_decref(struct msd_platform_buffer* buffer)
{
    MockPlatformBuffer::cast(buffer)->Decref();
}

uint32_t msd_platform_buffer_getref(struct msd_platform_buffer* buffer)
{
    return MockPlatformBuffer::cast(buffer)->Getref();
}

int32_t msd_platform_buffer_get_size(struct msd_platform_buffer* buffer, uint64_t* size_out)
{
    *size_out = MockPlatformBuffer::cast(buffer)->size();
    return 0;
}

int32_t msd_platform_buffer_get_handle(struct msd_platform_buffer* buffer, uint32_t* handle_out)
{
    *handle_out = MockPlatformBuffer::cast(buffer)->handle();
    return 0;
}

int32_t msd_platform_buffer_map_cpu(struct msd_platform_buffer* buffer, void** addr_out)
{
    *addr_out = MockPlatformBuffer::cast(buffer)->virt_addr();
    return 0;
}

int32_t msd_platform_buffer_unmap_cpu(struct msd_platform_buffer* buffer)
{
    return 0; // NoOp
}

int32_t msd_platform_buffer_pin_pages(struct msd_platform_buffer* buffer)
{
    return 0; // NoOp
}

int32_t msd_platform_buffer_unpin_pages(struct msd_platform_buffer* buffer)
{
    return 0; // NoOp
}

int32_t msd_platform_buffer_pinned_page_count(struct msd_platform_buffer* buffer,
                                              uint32_t* num_pages_out)
{
    *num_pages_out = MockPlatformBuffer::cast(buffer)->num_pages();
    return 0;
}

int32_t msd_platform_buffer_map_page_cpu(struct msd_platform_buffer* buffer, uint32_t page_index,
                                         void** addr_out)
{
    if (page_index >= MockPlatformBuffer::cast(buffer)->num_pages())
        return DRET(-EINVAL);
    *addr_out = MockPlatformBuffer::cast(buffer)->virt_addr() + page_index * PAGE_SIZE;
    return 0;
}

int32_t msd_platform_buffer_unmap_page_cpu(struct msd_platform_buffer* buffer, uint32_t page_index)
{
    return 0; // NoOp
}

int32_t msd_platform_buffer_map_page_bus(struct msd_platform_buffer* buffer, uint32_t page_index,
                                         uint64_t* addr_out)
{
    if (page_index >= MockPlatformBuffer::cast(buffer)->num_pages())
        return DRET(-EINVAL);
    // Mock physical base address of 0
    *addr_out = page_index * PAGE_SIZE;
    return 0;
}

int32_t msd_platform_buffer_unmap_page_bus(struct msd_platform_buffer* buffer, uint32_t page_index)
{
    return 0; // NoOp
}
