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

#include "magma_driver.h"
#include <magma_system.h>

#ifdef __linux__
#include <unistd.h>
static ClientId get_client_id() { return static_cast<ClientId>(getpid()); }
#else
static ClientId get_client_id() { return static_cast<ClientId>(1); }
#endif // __linux__

MagmaSystemDevice* MagmaDriver::g_device;

bool magma_system_open(MagmaSystemDevice** pdev, uint32_t device_handle)
{
    if (device_handle != 0xdeadbeef) {
        DLOG("Unexpected device_handle\n");
        return false;
    }

    MagmaSystemDevice* dev = MagmaDriver::GetDevice();
    DASSERT(dev);

    ClientId client_id = get_client_id();
    if (!msd_open(dev->arch(), client_id)) {
        DLOG("msd_open failed");
        return false;
    }

    dev->set_client_id(client_id);

    *pdev = dev;
    return true;
}

void magma_system_close(MagmaSystemDevice* dev)
{
    msd_close(dev->arch(), dev->client_id());

    delete dev;
}

// Returns the device id.  0 is an invalid device id.
uint32_t magma_system_get_device_id(MagmaSystemDevice* dev)
{
    return msd_get_device_id(dev->arch());
}

bool magma_system_create_context(MagmaSystemDevice* dev, int* context_id)
{
    return msd_create_context(dev->arch(), dev->client_id(), context_id);
}

bool magma_system_alloc(MagmaSystemDevice* dev, uint64_t size, uint64_t* size_out,
                        uint32_t* handle_out)
{
    auto buf = dev->AllocateBuffer(size);
    if (!buf)
        return false;

    *size_out = buf->size();
    *handle_out = buf->handle();
    return true;
}

bool magma_system_free(struct MagmaSystemDevice* dev, uint32_t handle)
{
    return dev->FreeBuffer(handle);
}

bool magma_system_set_tiling_mode(struct MagmaSystemDevice* dev, uint32_t handle,
                                  uint32_t tiling_mode)
{
    return msd_set_tiling_mode(dev->arch(), handle, tiling_mode);
}

bool magma_system_map(struct MagmaSystemDevice* dev, uint32_t handle, void** paddr)
{
    auto buf = dev->LookupBuffer(handle);
    if (!buf)
        return false;

    return buf->platform_buffer()->Map(paddr);
}

bool magma_system_unmap(struct MagmaSystemDevice* dev, uint32_t handle, void* addr)
{
    auto buf = dev->LookupBuffer(handle);
    if (!buf)
        return false;

    return buf->platform_buffer()->Unmap();
}

bool magma_system_set_domain(struct MagmaSystemDevice* dev, uint32_t handle, uint32_t read_domains,
                             uint32_t write_domain)
{
    return msd_set_domain(dev->arch(), handle, read_domains, write_domain);
}

bool magma_system_subdata(struct MagmaSystemDevice* dev, uint32_t handle, unsigned long offset,
                          unsigned long size, const void* data)
{
    return msd_subdata(dev->arch(), handle, offset, size, data);
}

bool magma_system_execute_buffer(struct MagmaSystemDevice* dev, struct MagmaExecBuffer* execbuffer)
{
    return msd_execute_buffer(dev->arch(), dev->client_id(), execbuffer);
}

void magma_system_wait_rendering(struct MagmaSystemDevice* dev, uint32_t handle)
{
    return msd_wait_rendering(dev->arch(), handle);
}
