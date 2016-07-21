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

#include <errno.h>
#include <limits.h> // PAGE_SIZE
#include <magenta/syscalls-ddk.h>
#include <magma_util/dlog.h>
#include <magma_util/macros.h>
#include <magma_util/refcounted.h>
#include <platform_buffer_abi.h>

class MagentaPlatformBuffer : public PlatformBufferToken, public magma::Refcounted {
public:
    MagentaPlatformBuffer(uint64_t size, void* vaddr, uint64_t paddr)
        : magma::Refcounted("MagentaPlatformBuffer"), size_(size), vaddr_(vaddr),
          handle_(++handle_count_)
    {
        DLOG("new MagentaObject size %lld handle 0x%x", size, handle_);
    }

    uint64_t size() { return size_; }

    uint32_t handle() { return handle_; }

    void* virt_addr() { return vaddr_; }

    static MagentaPlatformBuffer* cast(PlatformBufferToken* buffer)
    {
        return static_cast<MagentaPlatformBuffer*>(buffer);
    }

private:
    static uint32_t handle_count_;

    uint64_t size_;
    void* vaddr_;
    uint32_t handle_;
};

uint32_t MagentaPlatformBuffer::handle_count_{};

//////////////////////////////////////////////////////////////////////////////

int msd_platform_buffer_alloc(struct PlatformBufferToken** buffer_out, uint64_t size,
                              uint64_t* size_out, uint32_t* handle_out)
{
    size = magma::round_up_64(size, PAGE_SIZE);
    if (size == 0)
        return DRET(-EINVAL);

    void* vaddr;
    mx_paddr_t paddr;
    mx_status_t status = mx_alloc_device_memory(size, &paddr, &vaddr);
    if (status < 0) {
        DLOG("failed to allocate device memory size %lld: %d", size, status);
        return DRET(-ENOMEM);
    }

    DLOG("allocated object size %lld vaddr %p paddr %p", size, vaddr, (void*)paddr);
    auto buffer = new MagentaPlatformBuffer(size, vaddr, paddr);
    if (!buffer)
        DRET(-ENOMEM);

    *buffer_out = buffer;
    *size_out = buffer->size();
    *handle_out = buffer->handle();

    return 0;
}

void msd_platform_buffer_incref(struct PlatformBufferToken* buffer)
{
    MagentaPlatformBuffer::cast(buffer)->Incref();
}

void msd_platform_buffer_decref(struct PlatformBufferToken* buffer)
{
    MagentaPlatformBuffer::cast(buffer)->Decref();
}

void msd_platform_buffer_get_size(struct PlatformBufferToken* buffer, uint64_t* size_out)
{
    *size_out = MagentaPlatformBuffer::cast(buffer)->size();
}

void msd_platform_buffer_get_handle(struct PlatformBufferToken* buffer, uint32_t* handle_out)
{
    *handle_out = MagentaPlatformBuffer::cast(buffer)->handle();
}

int msd_platform_buffer_get_backing_store(struct PlatformBufferToken* buffer,
                                          BackingStore* backing_store)
{
    DLOG("TODO: msd_platform_buffer_get_backing_store");
    return 0;
}

int msd_platform_buffer_map(struct PlatformBufferToken* buffer, void** addr_out)
{
    *addr_out = MagentaPlatformBuffer::cast(buffer)->virt_addr();
    return 0;
}

int msd_platform_buffer_unmap(struct PlatformBufferToken* buffer)
{
    // Nothing to do.
    return 0;
}
