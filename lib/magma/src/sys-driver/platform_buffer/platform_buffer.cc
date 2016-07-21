// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <platform_buffer.h>

PlatformBuffer* PlatformBuffer::Create(uint64_t size)
{
    PlatformBufferToken* token;
    uint32_t handle;

    if (msd_platform_buffer_alloc(&token, size, &size, &handle) == 0)
        return new PlatformBuffer(token, size, handle);

    return nullptr;
}

PlatformBuffer* PlatformBuffer::Create(PlatformBufferToken* token)
{
    uint64_t size;
    uint32_t handle;

    msd_platform_buffer_get_size(token, &size);
    msd_platform_buffer_get_handle(token, &handle);

    auto buffer = new PlatformBuffer(token, size, handle);
    if (!buffer) {
        DLOG("Couldn't create PlatformBuffer");
        return nullptr;
    }

    msd_platform_buffer_incref(token);

    return buffer;
}

PlatformBuffer::~PlatformBuffer()
{
    DLOG("PlatformBuffer dtor handle 0x%x", handle_);
    msd_platform_buffer_decref(token_);
}

int PlatformBuffer::Map(void** addr_out) { return msd_platform_buffer_map(token_, addr_out); }

int PlatformBuffer::Unmap() { return msd_platform_buffer_unmap(token_); }

int PlatformBuffer::GetBackingStore(BackingStore* backing_store)
{
    return msd_platform_buffer_get_backing_store(token_, backing_store);
}

PlatformBuffer::PlatformBuffer(PlatformBufferToken* token, uint64_t size, uint32_t handle)
    : token_(token), size_(size), handle_(handle)
{
    DLOG("PlatformBuffer ctor size 0x%llx handle 0x%x", size, handle);
}
